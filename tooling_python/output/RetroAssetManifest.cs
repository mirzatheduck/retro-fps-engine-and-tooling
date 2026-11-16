// AUTO-GENERATED - DO NOT EDIT BY HAND
using System;

[Serializable]
public struct RetroAssetStage {
    public string stage;
    public string message;
}

[Serializable]
public struct RetroAssetEntry {
    public string name;
    public string source_path;
    public string lowpoly_path;
    public int tris_before;
    public int tris_after;
    public int verts_before;
    public int verts_after;
    public int non_manifold_edges;
    public int degenerate_faces_removed;
    public int duplicate_vertices_merged;
    public uint bytes_before;
    public uint bytes_after;
    public float memory_saved_pct;
    public float snap_grid;
    public float compat_score;
    public float[] bounds_min;
    public float[] bounds_max;
    public RetroAssetStage[] stages;
}

public static class RetroAssetManifest {
    public const int Version = 1;
    public static readonly RetroAssetEntry[] Assets = new RetroAssetEntry[] {
        new RetroAssetEntry {
            name = "retro_throwing_knife",
            source_path = "tooling_python/sample_assets/retro_throwing_knife.obj",
            lowpoly_path = "tooling_python/output/retro_throwing_knife_lowpoly.obj",
            tris_before = 14200,
            tris_after = 480,
            verts_before = 7206,
            verts_after = 239,
            non_manifold_edges = 1,
            degenerate_faces_removed = 3,
            duplicate_vertices_merged = 3,
            bytes_before = 315792u,
            bytes_after = 10528u,
            memory_saved_pct = 96.666160f,
            snap_grid = 0.289693f,
            compat_score = 86.000000f,
            bounds_min = new float[] { -0.966880f, -3.552671f, -0.181036f },
            bounds_max = new float[] { 0.966880f, 5.900625f, 0.171984f },
            stages = new RetroAssetStage[] { new RetroAssetStage { stage = "parse", message = "14200 triangles, 7206 vertices from tooling_python/sample_assets/retro_throwing_knife.obj" }, new RetroAssetStage { stage = "topology", message = "duplicate_vertices=3, degenerate_faces=3, non_manifold_edges=1, boundary_edges=210" }, new RetroAssetStage { stage = "clean", message = "14197 triangles remain; 3 degenerate faces removed; 3 duplicate vertices merged; 0 duplicate faces removed" }, new RetroAssetStage { stage = "decimate", message = "480/480 triangles; 239 vertices; grid=0.289693168574" }, new RetroAssetStage { stage = "memory", message = "315792 -> 10528 bytes; saved 96.666160%" }, new RetroAssetStage { stage = "score", message = "86.000000/100; remaining non-manifold edges=7" }, new RetroAssetStage { stage = "export", message = "low-poly OBJ -> tooling_python/output/retro_throwing_knife_lowpoly.obj" }, new RetroAssetStage { stage = "export", message = "C99 struct array ASSET_MANIFEST[1] (1440 indices) -> core_c/include/asset_manifest.h" }, new RetroAssetStage { stage = "export", message = "Unreal DataTable CSV -> tooling_python/output/unreal_asset_table.csv" } },
        },
    };
}
