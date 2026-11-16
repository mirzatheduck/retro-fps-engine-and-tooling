"""Deterministic subdivided knife: blade, guard, grip and toroidal pommel.

Base surfaces contain 14,196 triangles. Inject exactly three duplicate vertices,
three zero-area faces (two repeated-index, one using a duplicate position), and
one nondegenerate face sharing the internal edge (0, 49) with two blade faces.
The four injected faces count toward the 14,200 total.
"""
from math import cos, sin, pi
from pathlib import Path


def generate_knife_obj(path=None):
    path = Path(path) if path is not None else Path(__file__).with_name('retro_throwing_knife.obj')
    vertices, faces = [], []

    def surface(rows, columns, point, wrap_rows=False):
        start = len(vertices)
        for i in range(rows if wrap_rows else rows + 1):
            for j in range(columns):
                vertices.append(point(i / rows, 2 * pi * j / columns))
        for i in range(rows):
            for j in range(columns):
                a = start + i * columns + j
                b = start + i * columns + (j + 1) % columns
                c = start + ((i + 1) % rows if wrap_rows else i + 1) * columns + (j + 1) % columns
                d = start + ((i + 1) % rows if wrap_rows else i + 1) * columns + j
                faces.extend(((a, b, c), (a, c, d)))
        return start

    # Lenticular blade section; tapered tip retains a tiny finite section.
    surface(80, 48, lambda t, a: (
        (0.66 * (1 - t) ** 0.65 + 0.008) * cos(a),
        0.7 + 5.3 * t,
        (0.12 * (1 - t) + 0.006) * sin(a)))
    # Grooved grip, with sufficient radial subdivisions for visible fluting.
    surface(48, 40, lambda t, a: (
        (0.26 + 0.018 * cos(t * 16 * pi)) * cos(a),
        -2.8 + 3.15 * t,
        (0.22 + 0.015 * cos(t * 16 * pi)) * sin(a)))
    guard = surface(20, 32, lambda t, a: (
        (0.95 - 0.10 * cos(t * 2 * pi)) * cos(a),
        0.30 + 0.4 * t,
        0.19 * sin(a)))
    # Seal one guard end with a subdivided disk (32 triangles).
    center = len(vertices)
    vertices.append((0.0, 0.30, 0.0))
    faces.extend((center, guard + (j + 1) % 32, guard + j) for j in range(32))
    # Ring lies in XY; tube extends in Z.
    surface(31, 22, lambda t, a: (
        (0.38 + 0.115 * cos(a)) * cos(2 * pi * t),
        -3.12 + (0.38 + 0.115 * cos(a)) * sin(2 * pi * t),
        0.115 * sin(a)), wrap_rows=True)
    assert len(faces) == 14196
    duplicate = len(vertices)
    vertices.extend((vertices[0], vertices[1], vertices[49]))
    faces.extend(((0, 0, 1), (1, 1, 49), (0, duplicate, 49), (0, 49, 100)))
    assert len(faces) == 14200
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write('# Procedural retro throwing knife; 14196 surface + 4 injected triangles\n')
        stream.write('# Defects: 3 exact duplicate vertices; 3 zero-area faces; extra face on edge (1,50).\n')
        for x, y, z in vertices:
            stream.write(f'v {x:.12f} {y:.12f} {z:.12f}\n')
        for a, b, c in faces:
            stream.write(f'f {a + 1} {b + 1} {c + 1}\n')
    return path


if __name__ == '__main__':
    print(generate_knife_obj())
