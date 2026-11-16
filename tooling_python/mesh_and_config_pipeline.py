"""OBJ topology cleanup and deterministic retro vertex-clustering pipeline."""
import argparse
from collections import Counter
from dataclasses import dataclass
import math
from pathlib import Path
import sys

if __package__ in (None, ''):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tooling_python import engine_exporter

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = 'tooling_python/sample_assets/retro_throwing_knife.obj'


@dataclass
class Mesh:
    vertices: list
    faces: list


def parse_obj(path):
    """Read positions and fan-triangulate faces; OBJ negative indices are relative."""
    vertices, faces = [], []
    for number, raw in enumerate(Path(path).read_text(encoding='utf-8').splitlines(), 1):
        fields = raw.partition('#')[0].split()
        if not fields:
            continue
        try:
            if fields[0] == 'v':
                if len(fields) < 4:
                    raise ValueError('vertex requires xyz')
                point = tuple(float(value) for value in fields[1:4])
                if not all(math.isfinite(value) for value in point):
                    raise ValueError('nonfinite vertex')
                vertices.append(point)
            elif fields[0] == 'f':
                if len(fields) < 4:
                    raise ValueError('face requires at least three indices')
                polygon = []
                for field in fields[1:]:
                    index = int(field.split('/')[0])
                    if index == 0:
                        raise ValueError('OBJ indices are 1-based')
                    index = index - 1 if index > 0 else len(vertices) + index
                    if not 0 <= index < len(vertices):
                        raise ValueError('vertex index out of range')
                    polygon.append(index)
                faces.extend((polygon[0], polygon[i], polygon[i + 1]) for i in range(1, len(polygon) - 1))
        except ValueError as exc:
            raise ValueError(f'{path}:{number}: {exc}') from exc
    return Mesh(vertices, faces)


def is_degenerate(vertices, face):
    if len(set(face)) < 3:
        return True
    a, b, c = (vertices[i] for i in face)
    u = tuple(b[i] - a[i] for i in range(3))
    v = tuple(c[i] - a[i] for i in range(3))
    cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
    return sum(value * value for value in cross) == 0.0


def analyze_topology(mesh):
    """Count edge incidence on valid faces, excluding zero-area faces from edges."""
    edges = Counter()
    degenerate = 0
    for face in mesh.faces:
        if is_degenerate(mesh.vertices, face):
            degenerate += 1
            continue
        a, b, c = face
        edges.update(tuple(sorted(edge)) for edge in ((a, b), (b, c), (c, a)))
    return {'duplicate_vertices': len(mesh.vertices) - len(set(mesh.vertices)),
            'degenerate_faces': degenerate,
            'non_manifold_edges': sum(count > 2 for count in edges.values()),
            'boundary_edges': sum(count == 1 for count in edges.values())}


def _remap_faces(vertices, faces, mapping):
    result, seen = [], set()
    for face in faces:
        mapped = tuple(mapping[i] for i in face)
        key = tuple(sorted(mapped))
        if key not in seen and not is_degenerate(vertices, mapped):
            seen.add(key)
            result.append(mapped)
    return result


def _compact(mesh):
    used = sorted({i for face in mesh.faces for i in face})
    mapping = {old: new for new, old in enumerate(used)}
    return Mesh([mesh.vertices[i] for i in used],
                [tuple(mapping[i] for i in face) for face in mesh.faces])


def clean_mesh(mesh):
    vertices, lookup, mapping = [], {}, []
    for vertex in mesh.vertices:
        if vertex not in lookup:
            lookup[vertex] = len(vertices)
            vertices.append(vertex)
        mapping.append(lookup[vertex])
    return _compact(Mesh(vertices, _remap_faces(vertices, mesh.faces, mapping)))


def bounds(mesh):
    if not mesh.vertices:
        return [0.0] * 3, [0.0] * 3
    return ([min(v[i] for v in mesh.vertices) for i in range(3)],
            [max(v[i] for v in mesh.vertices) for i in range(3)])


def cluster_mesh(mesh, grid):
    """Uniform origin-anchored cells; representatives are actual cluster averages."""
    if grid <= 0:
        raise ValueError('grid must be positive')
    clusters, mapping, sums, counts = {}, [], [], []
    for vertex in mesh.vertices:
        cell = tuple(math.floor(value / grid) for value in vertex)
        index = clusters.get(cell)
        if index is None:
            index = len(sums)
            clusters[cell] = index
            sums.append([0.0, 0.0, 0.0])
            counts.append(0)
        mapping.append(index)
        counts[index] += 1
        for axis in range(3):
            sums[index][axis] += vertex[axis]
    vertices = [tuple(value / count for value in total) for total, count in zip(sums, counts)]
    return _compact(Mesh(vertices, _remap_faces(vertices, mesh.faces, mapping)))


def decimate_mesh(mesh, budget):
    """Log sweep plus local bisection, retaining the best observed count <= budget.

    Grid counts can be nonmonotonic: this is a deterministic bounded search, not
    a claim of a global optimum over all positive real cell sizes. Stop on exact
    budget; otherwise return the closest feasible candidate encountered.
    """
    if budget < 0:
        raise ValueError('budget must be nonnegative')
    if len(mesh.faces) <= budget:
        return mesh, 0.0
    lo_bound, hi_bound = bounds(mesh)
    span = max(b - a for a, b in zip(lo_bound, hi_bound))
    if not span or budget == 0:
        return Mesh([], []), span * 2 or 1.0
    best, best_grid = Mesh([], []), span * 2
    samples = []

    def evaluate(grid):
        nonlocal best, best_grid
        candidate = cluster_mesh(mesh, grid)
        count = len(candidate.faces)
        if count <= budget and count > len(best.faces):
            best, best_grid = candidate, grid
        samples.append((grid, count))
        return count

    # Sweep covers fine detail through coarser-than-bounds clusters.
    for i in range(73):
        grid = span * 2 ** (-10 + i / 6)
        if evaluate(grid) == budget:
            return best, best_grid
    brackets = [(a[0], b[0]) for a, b in zip(samples, samples[1:])
                if (a[1] > budget) != (b[1] > budget)]
    for low, high in brackets:
        for _ in range(30):
            mid = (low + high) / 2
            count = evaluate(mid)
            if count == budget:
                return best, best_grid
            if count > budget:
                low = mid
            else:
                high = mid
    # Probe around the best grid to handle local count reversals.
    center = best_grid
    for i in range(81):
        if evaluate(center * (0.96 + i * 0.001)) == budget:
            break
    return best, best_grid


def estimate_memory(mesh):
    return len(mesh.vertices) * 32 + len(mesh.faces) * 3 * (2 if len(mesh.vertices) < 65536 else 4)


def compatibility_score(mesh, budget):
    """100 minus: min(40, 40*budget excess/max(1,budget)), min(30,2*NM
    edges), min(20,5*degenerate faces), 20 if >65535 vertices, and 10 if
    smallest/largest bounding extent <0.01 (or empty). Clamp to [0,100].
    Boundary edges are permitted for intentionally open retro surfaces.
    """
    topo = analyze_topology(mesh)
    minimum, maximum = bounds(mesh)
    extents = [b - a for a, b in zip(minimum, maximum)]
    thin = max(extents) == 0 or min(extents) / max(extents) < 0.01
    penalty = (min(40, 40 * max(0, len(mesh.faces) - budget) / max(1, budget))
               + min(30, 2 * topo['non_manifold_edges'])
               + min(20, 5 * topo['degenerate_faces'])
               + (20 if len(mesh.vertices) > 65535 else 0) + (10 if thin else 0))
    return float(max(0, 100 - penalty))


def process_asset(source, budget, lowpoly_path, log=None):
    stages = []

    def stage(name, message):
        stages.append({'stage': name, 'message': message})
        if log:
            log(f'{name}: {message}')

    source, lowpoly_path = Path(source), Path(lowpoly_path)
    mesh = parse_obj(source)
    stage('parse', f'{len(mesh.faces)} triangles, {len(mesh.vertices)} vertices from {source.as_posix()}')
    topo = analyze_topology(mesh)
    stage('topology', ', '.join(f'{key}={value}' for key, value in topo.items()))
    cleaned = clean_mesh(mesh)
    stage('clean', f'{len(cleaned.faces)} triangles remain; {topo["degenerate_faces"]} degenerate faces removed; '
          f'{topo["duplicate_vertices"]} duplicate vertices merged; '
          f'{len(mesh.faces) - len(cleaned.faces) - topo["degenerate_faces"]} duplicate faces removed')
    low, grid = decimate_mesh(cleaned, budget)
    stage('decimate', f'{len(low.faces)}/{budget} triangles; {len(low.vertices)} vertices; grid={grid:.12g}')
    before, after = estimate_memory(mesh), estimate_memory(low)
    saved = 100 * (before - after) / before if before else 0.0
    stage('memory', f'{before} -> {after} bytes; saved {saved:.6f}%')
    score = compatibility_score(low, budget)
    stage('score', f'{score:.6f}/100; remaining non-manifold edges={analyze_topology(low)["non_manifold_edges"]}')
    minimum, maximum = bounds(low)
    engine_exporter.export_obj(low, lowpoly_path)
    stage('export', f'low-poly OBJ -> {lowpoly_path.as_posix()}')
    return dict(name='retro_throwing_knife', source_path=source.as_posix(), lowpoly_path=lowpoly_path.as_posix(),
                tris_before=len(mesh.faces), tris_after=len(low.faces), verts_before=len(mesh.vertices),
                verts_after=len(low.vertices), non_manifold_edges=topo['non_manifold_edges'],
                degenerate_faces_removed=topo['degenerate_faces'], duplicate_vertices_merged=topo['duplicate_vertices'],
                bytes_before=before, bytes_after=after, memory_saved_pct=saved, snap_grid=grid,
                compat_score=score, bounds_min=minimum, bounds_max=maximum, stages=stages,
                vertices=low.vertices, faces=low.faces, budget=budget)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', default=DEFAULT_INPUT)
    parser.add_argument('--budget', type=int, default=480)
    parser.add_argument('--header-out', default='core_c/include/asset_manifest.h')
    parser.add_argument('--json-out', default='tooling_python/output/asset_manifest.json')
    parser.add_argument('--lowpoly-out', default='tooling_python/output/retro_throwing_knife_lowpoly.obj')
    parser.add_argument('--targets', default='c99,unreal,unity')
    args = parser.parse_args(argv)
    targets = {value.strip() for value in args.targets.split(',') if value.strip()}
    if args.budget < 0 or not targets <= {'c99', 'unreal', 'unity'}:
        parser.error('budget must be nonnegative; targets must be c99,unreal,unity')
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except (AttributeError, OSError, ValueError):
        pass
    from rich import box
    from rich.console import Console
    from rich.panel import Panel
    from rich.table import Table
    console = Console()
    if not Path(args.input).exists() and Path(args.input) == Path(DEFAULT_INPUT):
        from tooling_python.sample_assets.generate_knife_obj import generate_knife_obj
        generate_knife_obj(args.input)
    asset = process_asset(args.input, args.budget, args.lowpoly_out,
                          log=lambda message: console.print(message, markup=False, highlight=False))
    assets = [asset]

    def export_stage(message):
        asset['stages'].append({'stage': 'export', 'message': message})
        console.print(f'export: {message}', markup=False, highlight=False)

    directory = Path(args.json_out).parent
    if 'c99' in targets:
        engine_exporter.export_c99_header(assets, args.header_out)
        export_stage(f'C99 struct array ASSET_MANIFEST[{len(assets)}] ({asset["tris_after"] * 3} indices) '
                     f'-> {Path(args.header_out).as_posix()}')
    if 'unreal' in targets:
        engine_exporter.export_unreal_datatable(assets, directory / 'unreal_asset_table.csv')
        export_stage(f'Unreal DataTable CSV -> {(directory / "unreal_asset_table.csv").as_posix()}')
    if 'unity' in targets:
        engine_exporter.export_unity_scriptable(assets, directory / 'RetroAssetManifest.cs')
        export_stage(f'Unity C# manifest -> {(directory / "RetroAssetManifest.cs").as_posix()}')
    export_stage(f'JSON manifest -> {Path(args.json_out).as_posix()}')
    engine_exporter.export_json(assets, args.json_out)
    table = Table(box=box.ASCII2, expand=False)
    for title in ('Asset', 'Tris Before', 'Tris After', 'Verts Before', 'Verts After', 'Memory Saved', 'Engine Compat Score'):
        table.add_column(title, no_wrap=(title == 'Asset'))
    table.add_row(asset['name'], str(asset['tris_before']), str(asset['tris_after']),
                  str(asset['verts_before']), str(asset['verts_after']),
                  f'{asset["memory_saved_pct"]:.2f}%', f'{asset["compat_score"]:.2f}')
    console.print(table)
    console.print(Panel(asset['stages'][1]['message'], title='Source topology', box=box.ASCII2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
