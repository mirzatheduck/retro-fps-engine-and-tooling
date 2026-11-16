"""Behavior tests for the real procedural mesh and exported contracts."""
import csv
import importlib
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'tooling_python/output/test_artifacts'


class PipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)

    def pipeline(self):
        # Missing implementation is an assertion failure during the initial RED run.
        self.assertTrue((ROOT / 'tooling_python/mesh_and_config_pipeline.py').exists(),
                        'Mesh pipeline has not been implemented')
        return importlib.import_module('tooling_python.mesh_and_config_pipeline')

    def mesh(self):
        p = self.pipeline()
        gen = importlib.import_module('tooling_python.sample_assets.generate_knife_obj')
        path = OUT / 'knife.obj'
        gen.generate_knife_obj(path)
        return p, p.parse_obj(path)

    def test_generator_exact_face_count_and_determinism(self):
        p, mesh = self.mesh()
        self.assertEqual(len(mesh.faces), 14200)
        gen = importlib.import_module('tooling_python.sample_assets.generate_knife_obj')
        gen.generate_knife_obj(OUT / 'knife2.obj')
        self.assertEqual((OUT / 'knife.obj').read_bytes(), (OUT / 'knife2.obj').read_bytes())

    def test_injected_topology_defects(self):
        p, mesh = self.mesh()
        report = p.analyze_topology(mesh)
        self.assertGreater(report['duplicate_vertices'], 0)
        self.assertGreater(report['degenerate_faces'], 0)
        self.assertGreaterEqual(report['non_manifold_edges'], 1)

    def test_clean_removes_degenerate_and_duplicate_geometry(self):
        p, mesh = self.mesh()
        cleaned = p.clean_mesh(mesh)
        report = p.analyze_topology(cleaned)
        self.assertEqual(report['degenerate_faces'], 0)
        self.assertEqual(report['duplicate_vertices'], 0)
        self.assertEqual(len({tuple(sorted(f)) for f in cleaned.faces}), len(cleaned.faces))

    def test_decimation_respects_budget_and_saves_memory(self):
        p, mesh = self.mesh()
        low, grid = p.decimate_mesh(p.clean_mesh(mesh), 480)
        self.assertGreaterEqual(len(low.faces), 432)
        self.assertLessEqual(len(low.faces), 480)
        self.assertGreater(grid, 0)
        self.assertLess(p.estimate_memory(low), p.estimate_memory(mesh))
        self.assertEqual(p.analyze_topology(low)['degenerate_faces'], 0)

    def test_obj_fan_slash_and_negative_indices(self):
        p = self.pipeline()
        path = OUT / 'quad.obj'
        path.write_text('v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf -4/1/1 -3/2/1 -2/3/1 -1/4/1\n')
        self.assertEqual(p.parse_obj(path).faces, [(0, 1, 2), (0, 2, 3)])

    def test_invalid_obj_index_rejected(self):
        p = self.pipeline()
        path = OUT / 'bad.obj'
        path.write_text('v 0 0 0\nf 0 1 1\n')
        with self.assertRaises(ValueError):
            p.parse_obj(path)

    def test_memory_formula(self):
        p = self.pipeline()
        self.assertEqual(p.estimate_memory(p.Mesh([(0, 0, 0)] * 3, [(0, 1, 2)])), 102)

    def test_zero_budget_and_invalid_budget(self):
        p, mesh = self.mesh()
        self.assertEqual(len(p.decimate_mesh(p.clean_mesh(mesh), 0)[0].faces), 0)
        with self.assertRaises(ValueError):
            p.decimate_mesh(mesh, -1)

    def test_exports_round_trip_contract(self):
        p, mesh = self.mesh()
        asset = p.process_asset(OUT / 'knife.obj', 480, OUT / 'low.obj')
        ex = importlib.import_module('tooling_python.engine_exporter')
        ex.export_c99_header([asset], OUT / 'asset_manifest.h')
        ex.export_json([asset], OUT / 'manifest.json')
        ex.export_unreal_datatable([asset], OUT / 'unreal.csv')
        ex.export_unity_scriptable([asset], OUT / 'RetroAssetManifest.cs')
        header = (OUT / 'asset_manifest.h').read_text()
        self.assertIn('#define ASSET_MANIFEST_COUNT 1', header)
        count = int(re.search(r'#define ASSET_RETRO_THROWING_KNIFE_INDEX_COUNT (\d+)', header)[1])
        self.assertEqual(count, asset['tris_after'] * 3)
        data = json.loads((OUT / 'manifest.json').read_text())
        self.assertEqual(data['version'], 1)
        self.assertEqual(data['budget'], 480)
        keys = {'name', 'source_path', 'lowpoly_path', 'tris_before', 'tris_after',
                'verts_before', 'verts_after', 'non_manifold_edges', 'degenerate_faces_removed',
                'duplicate_vertices_merged', 'bytes_before', 'bytes_after', 'memory_saved_pct',
                'snap_grid', 'compat_score', 'bounds_min', 'bounds_max', 'stages'}
        self.assertEqual(set(data['assets'][0]), keys)
        self.assertEqual([s['stage'] for s in data['assets'][0]['stages']],
                         ['parse', 'topology', 'clean', 'decimate', 'memory', 'score', 'export'])
        with (OUT / 'unreal.csv').open(newline='') as stream:
            rows = list(csv.reader(stream))
        self.assertEqual(rows[0][0], '---')
        self.assertEqual(int(rows[1][rows[0].index('tris_after')]), asset['tris_after'])
        self.assertIn('static readonly', (OUT / 'RetroAssetManifest.cs').read_text())
        self.assertEqual(len(p.parse_obj(OUT / 'low.obj').faces), asset['tris_after'])


if __name__ == '__main__':
    unittest.main()
