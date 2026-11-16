/*
 * main_prototype.c - Retro FPS core prototype.
 *
 * Two builds from one file:
 *   default         raylib window, 60 FPS loop:
 *                   InitWindow -> [UpdatePhysics -> BeginDrawing .. EndDrawing] -> CloseWindow
 *   -DHEADLESS_TEST no raylib at all; runs the SAME UpdatePhysics() for 60
 *                   frames with a scripted input bot and prints per-frame
 *                   physics/state telemetry. Exit code != 0 if an invariant
 *                   breaks, so it doubles as an integration test (ctest).
 *
 * All runtime memory comes from one static 1024 KB MemoryArena: targets and
 * the knife trail are allocated once at init; per-frame scratch (projected
 * target positions) uses arena_mark()/arena_rewind(). No malloc/free in the loop.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "memory_arena.h"
#include "state_machine.h"
#include "combat_combo.h"
#include "asset_manifest.h"

#define SCREEN_WIDTH     1280
#define SCREEN_HEIGHT    720
#define CAMERA_FOVY_DEG  75.0f
#define ARENA_CAPACITY   ARENA_KB(1024)
#define TARGET_COUNT     6
#define TRAIL_CAPACITY   48
#define FIXED_DT         (1.0f / 60.0f)
#define HEADLESS_FRAMES  60
#define RF_PI            3.14159265358979f

/* ---------------------------------------------------------------- game state */

typedef struct FrameCommands {
    PlayerInput move;
    bool throw_pressed;
    bool recall_pressed;
} FrameCommands;

typedef struct GameState {
    PlayerMotor  motor;
    ComboSystem  combo;
    DualReticle  reticle;
    CombatTarget *targets;      /* arena */
    int          target_count;
    RfVec3       *trail;        /* arena ring buffer */
    int          trail_head;
    int          trail_len;
    float        yaw;
    float        pitch;
    long         frame;
    int          respawns;
    int          hits_this_frame;
    size_t       frame_scratch_peak;
} GameState;

static unsigned char g_arena_memory[ARENA_CAPACITY];
static MemoryArena   g_arena;

static RfVec3 EyePosition(const GameState *gs)
{
    RfVec3 p = gs->motor.position;
    p.y += gs->motor.tuning.eye_height;
    return p;
}

static RfVec3 LookDirection(float yaw, float pitch)
{
    return rf_vec3(cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw));
}

/* Pinhole projection matching raylib's perspective camera (vertical fovy). */
static bool ProjectPoint(RfVec3 eye, float yaw, float pitch, RfVec3 p, RfVec2 *out)
{
    RfVec3 f = LookDirection(yaw, pitch);
    RfVec3 r = rf_vec3_normalize(rf_vec3(-f.z, 0.0f, f.x));  /* f x up */
    RfVec3 u = rf_vec3(r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x);
    RfVec3 d = rf_vec3_sub(p, eye);
    float zc = rf_vec3_dot(d, f);
    float tan_half = tanf(CAMERA_FOVY_DEG * 0.5f * RF_PI / 180.0f);
    float aspect = (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT;
    if (zc < 0.1f) return false;
    out->x = SCREEN_WIDTH * 0.5f + (rf_vec3_dot(d, r) / (zc * tan_half * aspect)) * SCREEN_WIDTH * 0.5f;
    out->y = SCREEN_HEIGHT * 0.5f - (rf_vec3_dot(d, u) / (zc * tan_half)) * SCREEN_HEIGHT * 0.5f;
    return true;
}

static void SpawnTargets(GameState *gs)
{
    int i;
    float base_z = gs->motor.position.z + 6.0f;
    for (i = 0; i < gs->target_count; ++i) {
        CombatTarget *t = &gs->targets[i];
        t->id = gs->respawns * 100 + i;
        t->position = rf_vec3((i % 2 == 0) ? -1.2f : 1.2f, 1.6f + 0.3f * (float)(i % 3), base_z + 3.0f * (float)i);
        t->radius = 0.6f;
        t->alive = true;
    }
    gs->respawns++;
}

static bool GameInit(GameState *gs)
{
    memset(gs, 0, sizeof(*gs));
    arena_init(&g_arena, g_arena_memory, sizeof(g_arena_memory));

    motor_init(&gs->motor, NULL);
    combo_init(&gs->combo);
    reticle_init(&gs->reticle, rf_vec2(SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f));

    gs->target_count = TARGET_COUNT;
    gs->targets = ARENA_PUSH_ARRAY(&g_arena, CombatTarget, TARGET_COUNT);
    gs->trail = ARENA_PUSH_ARRAY(&g_arena, RfVec3, TRAIL_CAPACITY);
    if (!gs->targets || !gs->trail) return false;
    SpawnTargets(gs);
    return true;
}

/* One simulation step. real_dt is the unscaled frame time; the combo system
 * owns time_scale and every simulated subsystem multiplies by it. */
static void UpdatePhysics(GameState *gs, const FrameCommands *cmd, float real_dt)
{
    ArenaMarker scratch = arena_mark(&g_arena);
    RfVec2 *screens = ARENA_PUSH_ARRAY(&g_arena, RfVec2, gs->target_count);
    int *screen_to_target = ARENA_PUSH_ARRAY(&g_arena, int, gs->target_count);
    int i, visible = 0, alive = 0;
    RfVec3 eye = EyePosition(gs);
    PlayerInput move = cmd->move;

    /* 1. dual reticle over the targets currently on screen */
    for (i = 0; i < gs->target_count; ++i) {
        RfVec2 s;
        if (!gs->targets[i].alive) continue;
        alive++;
        if (ProjectPoint(eye, gs->yaw, gs->pitch, gs->targets[i].position, &s)) {
            screens[visible] = s;
            screen_to_target[visible] = i;
            visible++;
        }
    }
    reticle_update(&gs->reticle, rf_vec2(SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f), screens, visible, real_dt);

    /* 2. combo clock (real time) decides the world time scale */
    combo_update(&gs->combo, real_dt);

    /* 3. movement FSM on delta_time * time_scale; combo slow-mo pulls an
     *    airborne player into STATE_BULLET_TIME_AIR */
    move.yaw = gs->yaw;
    move.bullet_time_requested = move.bullet_time_requested || gs->combo.time_scale < 1.0f;
    motor_update(&gs->motor, &move, real_dt, gs->combo.time_scale);
    eye = EyePosition(gs);
    combo_set_owner_position(&gs->combo, eye);

    /* 4. knife: assisted throw toward the secondary reticle's target */
    if (cmd->throw_pressed) {
        RfVec3 aim_point = rf_vec3_add(eye, rf_vec3_scale(LookDirection(gs->yaw, gs->pitch), 30.0f));
        if (gs->reticle.secondary.target_id >= 0)
            aim_point = gs->targets[screen_to_target[gs->reticle.secondary.target_id]].position;
        combo_throw_knife(&gs->combo, eye, aim_point);
    }
    if (cmd->recall_pressed) combo_recall_knife(&gs->combo, eye);
    gs->hits_this_frame = combo_update_knife(&gs->combo, gs->targets, gs->target_count, real_dt);

    if (gs->combo.knife_recall_state != KNIFE_HELD) {
        gs->trail[gs->trail_head] = gs->combo.knife.position;
        gs->trail_head = (gs->trail_head + 1) % TRAIL_CAPACITY;
        if (gs->trail_len < TRAIL_CAPACITY) gs->trail_len++;
    } else {
        gs->trail_len = 0;
    }

    if (alive == 0 && gs->combo.knife_recall_state == KNIFE_HELD) SpawnTargets(gs);

    if (arena_used(&g_arena) - scratch.offset > gs->frame_scratch_peak)
        gs->frame_scratch_peak = arena_used(&g_arena) - scratch.offset;
    arena_rewind(&g_arena, scratch);   /* per-frame scratch released, no free() */
    gs->frame++;
}

/* Validates the tooling -> runtime contract of the generated header. */
static int ValidateAssetManifest(void)
{
    int a, i, bad = 0;
    for (a = 0; a < ASSET_MANIFEST_COUNT; ++a) {
        const AssetManifestEntry *e = &ASSET_MANIFEST[a];
        if (e->index_count != e->tris_after * 3) bad++;
        if (e->vertex_count != e->verts_after) bad++;
        for (i = 0; i < e->index_count; ++i)
            if ((int)e->indices[i] >= e->vertex_count) { bad++; break; }
    }
    return bad;
}

#ifdef HEADLESS_TEST
/* ======================================================================
 * HEADLESS TEST BUILD - no raylib, console telemetry only
 * ====================================================================== */

/* Scripted input bot: run, jump-chain, air dash, keep throwing/recalling. */
static FrameCommands ScriptedInput(const GameState *gs, long f)
{
    FrameCommands c;
    memset(&c, 0, sizeof(c));
    c.move.move_z = 1.0f;
    c.move.jump_pressed = (f >= 12 && f < 48);
    c.move.dash_pressed = (f == 50);
    c.move.move_x = (f >= 50 && f < 54) ? 0.6f : 0.0f;
    c.throw_pressed = gs->combo.knife_recall_state == KNIFE_HELD && gs->reticle.secondary.target_id >= 0;
    c.recall_pressed = gs->combo.knife_recall_state == KNIFE_STUCK;
    return c;
}

int main(void)
{
    static GameState gs;
    int visited[STATE_COUNT] = {0};
    int failures = 0, i, a, manifest_errors;
    float max_speed = 0.0f;

    if (!GameInit(&gs)) { printf("arena init failed\n"); return 1; }

    printf("=== RETRO FPS CORE // HEADLESS_TEST (C99, no raylib) ===\n");
    for (a = 0; a < ASSET_MANIFEST_COUNT; ++a) {
        const AssetManifestEntry *e = &ASSET_MANIFEST[a];
        printf("asset[%d] %s: %d -> %d tris, %d -> %d verts, %u -> %u bytes, compat %.1f, snap %.4f\n",
               a, e->name, e->tris_before, e->tris_after, e->verts_before, e->verts_after,
               e->bytes_before, e->bytes_after, e->compat_score, e->snap_grid);
    }
    manifest_errors = ValidateAssetManifest();
    printf("asset_manifest.h contract check: %s\n\n", manifest_errors ? "FAILED" : "OK");
    failures += manifest_errors;

    printf("%-5s %-16s %7s %6s %7s %7s %6s %-16s %-10s %5s %9s\n",
           "frame", "state", "pos.z", "pos.y", "speed", "vel.y", "tscale", "combo", "knife", "lock", "arena");
    for (i = 0; i < HEADLESS_FRAMES; ++i) {
        FrameCommands cmd = ScriptedInput(&gs, gs.frame);
        float speed;
        UpdatePhysics(&gs, &cmd, FIXED_DT);
        speed = motor_speed(&gs.motor);
        if (speed > max_speed) max_speed = speed;
        visited[gs.motor.state]++;
        printf("%5ld %-16s %7.3f %6.3f %7.3f %7.2f %5.2fx x%-2d %-12s %-10s %4.0f%% %6.1f KB%s\n",
               gs.frame, player_state_name(gs.motor.state), gs.motor.position.z, gs.motor.position.y,
               speed, gs.motor.velocity.y, gs.combo.time_scale, gs.combo.combo_count,
               combo_rank_name(&gs.combo)[0] ? combo_rank_name(&gs.combo) : "-",
               knife_state_name(gs.combo.knife_recall_state),
               gs.reticle.secondary.lock_progress * 100.0f, (double)arena_used(&g_arena) / 1024.0,
               gs.hits_this_frame ? "  << HIT" : "");
        if (gs.motor.position.y < -1e-4f) { printf("  invariant: below ground\n"); failures++; }
        if (gs.combo.time_scale < COMBO_SLOWMO_TIME_SCALE - 1e-4f || gs.combo.time_scale > 1.0f + 1e-4f) {
            printf("  invariant: time_scale out of range\n"); failures++;
        }
        if (speed > gs.motor.tuning.dash_speed + 0.01f) { printf("  invariant: speed cap\n"); failures++; }
    }

    printf("\n--- summary (%d frames @ 60 FPS fixed step) ---\n", HEADLESS_FRAMES);
    for (i = 0; i < STATE_COUNT; ++i)
        printf("%-16s %3d frames\n", player_state_name((PlayerState)i), visited[i]);
    printf("state transitions : %d\n", gs.motor.transitions);
    printf("max speed         : %.3f u/s\n", max_speed);
    printf("combo             : best x%d, total hits %d, score %ld, rank now '%s'\n",
           gs.combo.best_combo, gs.combo.total_hits, gs.combo.score, combo_rank_name(&gs.combo));
    printf("arena             : %.1f KB used / %.0f KB, peak %.1f KB, frame scratch peak %u B, failed allocs %u\n",
           (double)arena_used(&g_arena) / 1024.0, (double)ARENA_CAPACITY / 1024.0,
           (double)g_arena.peak / 1024.0, (unsigned)gs.frame_scratch_peak, (unsigned)g_arena.failed_allocs);

    if (g_arena.failed_allocs != 0) failures++;
    if (gs.frame != HEADLESS_FRAMES) failures++;
    for (i = 0; i < STATE_COUNT; ++i)
        if (visited[i] == 0) { printf("  expected state %s to be visited\n", player_state_name((PlayerState)i)); failures++; }
    if (gs.combo.best_combo < COMBO_BULLET_TIME_THRESHOLD) { printf("  expected combo bullet time\n"); failures++; }
    if (gs.combo.total_hits == 0) { printf("  expected at least one knife hit\n"); failures++; }

    printf("\nRESULT: %s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

#else
/* ======================================================================
 * RAYLIB BUILD - 60 FPS window, wireframe corridor, dual reticle HUD
 * ====================================================================== */
#include "raylib.h"
#include "raymath.h"

#define HUD_GREEN  CLITERAL(Color){ 51, 255, 102, 255 }
#define HUD_AMBER  CLITERAL(Color){ 255, 176, 0, 255 }
#define HUD_DIM    CLITERAL(Color){ 20, 90, 40, 255 }
#define CRT_BG     CLITERAL(Color){ 6, 12, 8, 255 }

static Vector3 ToRl3(RfVec3 v) { Vector3 r; r.x = v.x; r.y = v.y; r.z = v.z; return r; }

static Model BuildKnifeModel(void)
{
    const AssetManifestEntry *e = &ASSET_MANIFEST[0];
    Mesh mesh = { 0 };
    mesh.vertexCount = e->vertex_count;
    mesh.triangleCount = e->index_count / 3;
    mesh.vertices = (float *)MemAlloc((unsigned int)(e->vertex_count * 3 * (int)sizeof(float)));
    mesh.indices = (unsigned short *)MemAlloc((unsigned int)(e->index_count * (int)sizeof(unsigned short)));
    memcpy(mesh.vertices, e->vertices, (size_t)e->vertex_count * 3 * sizeof(float));
    memcpy(mesh.indices, e->indices, (size_t)e->index_count * sizeof(unsigned short));
    UploadMesh(&mesh, false);
    {
        /* source knife is ~9.5 units long along +Y: scale to ~0.9 m and lay it along +Z */
        Model model = LoadModelFromMesh(mesh);
        model.transform = MatrixMultiply(MatrixScale(0.1f, 0.1f, 0.1f), MatrixRotateX(RF_PI * 0.5f));
        return model;
    }
}

static void DrawCorridor(float player_z)
{
    int i;
    float start = floorf(player_z / 4.0f) * 4.0f - 8.0f;
    for (i = 0; i < 24; ++i) {
        float z = start + 4.0f * (float)i;
        DrawLine3D((Vector3){ -4.0f, 0.0f, z }, (Vector3){ 4.0f, 0.0f, z }, HUD_DIM);
        DrawLine3D((Vector3){ -4.0f, 4.0f, z }, (Vector3){ 4.0f, 4.0f, z }, HUD_DIM);
        DrawLine3D((Vector3){ -4.0f, 0.0f, z }, (Vector3){ -4.0f, 4.0f, z }, HUD_DIM);
        DrawLine3D((Vector3){ 4.0f, 0.0f, z }, (Vector3){ 4.0f, 4.0f, z }, HUD_DIM);
    }
    for (i = -2; i <= 2; ++i) {
        float x = 2.0f * (float)i;
        DrawLine3D((Vector3){ x, 0.0f, start }, (Vector3){ x, 0.0f, start + 96.0f }, HUD_DIM);
        DrawLine3D((Vector3){ x, 4.0f, start }, (Vector3){ x, 4.0f, start + 96.0f }, HUD_DIM);
    }
}

static void DrawDualReticle(const DualReticle *r)
{
    Vector2 p = { r->primary.screen_pos.x, r->primary.screen_pos.y };
    Vector2 s = { r->secondary.screen_pos.x, r->secondary.screen_pos.y };
    Color sec = r->secondary.locked ? HUD_GREEN : Fade(HUD_GREEN, 0.6f);

    DrawCircleLinesV(p, 14.0f, HUD_AMBER);
    DrawLineEx((Vector2){ p.x - 22, p.y }, (Vector2){ p.x - 8, p.y }, 2.0f, HUD_AMBER);
    DrawLineEx((Vector2){ p.x + 8, p.y }, (Vector2){ p.x + 22, p.y }, 2.0f, HUD_AMBER);
    DrawLineEx((Vector2){ p.x, p.y - 22 }, (Vector2){ p.x, p.y - 8 }, 2.0f, HUD_AMBER);
    DrawLineEx((Vector2){ p.x, p.y + 8 }, (Vector2){ p.x, p.y + 22 }, 2.0f, HUD_AMBER);

    if (r->secondary.target_id >= 0) {
        DrawLineEx(p, s, 1.0f, Fade(HUD_GREEN, 0.5f));
        DrawRing(s, 18.0f, 21.0f, 0.0f, 360.0f * r->secondary.lock_progress, 36, sec);
        DrawRectangleLines((int)s.x - 10, (int)s.y - 10, 20, 20, sec);
        if (r->secondary.locked) DrawText("LOCK", (int)s.x + 24, (int)s.y - 8, 16, HUD_GREEN);
    }
}

static void DrawHud(const GameState *gs)
{
    const ComboSystem *c = &gs->combo;
    char state_line[64];
    float used_kb = (float)g_arena.peak / 1024.0f;
    float cap_kb = (float)ARENA_CAPACITY / 1024.0f;
    const AssetManifestEntry *e = &ASSET_MANIFEST[0];

    snprintf(state_line, sizeof(state_line), "STATE: %s", player_state_name(gs->motor.state));
    DrawRectangle(12, 12, 420, 168, Fade(BLACK, 0.55f));
    DrawRectangleLines(12, 12, 420, 168, HUD_DIM);
    DrawText(state_line, 24, 22, 20, HUD_GREEN);
    DrawText(TextFormat("TIME SCALE: %.2fx", c->time_scale), 24, 48, 20, c->time_scale < 1.0f ? HUD_AMBER : HUD_GREEN);
    DrawText(TextFormat("COMBO: x%d %s", c->combo_count, combo_rank_name(c)), 24, 74, 20, HUD_AMBER);
    DrawText(TextFormat("SPEED: %5.1f u/s  BHOP x%d", motor_speed(&gs->motor), gs->motor.bhop_chain), 24, 100, 18, HUD_GREEN);
    DrawText(TextFormat("KNIFE: %s", knife_state_name(c->knife_recall_state)), 24, 124, 18, HUD_GREEN);
    DrawText(TextFormat("ARENA: %.1f KB / %.0f KB", used_kb, cap_kb), 24, 148, 16, HUD_GREEN);
    DrawRectangleLines(236, 148, 184, 16, HUD_DIM);
    DrawRectangle(238, 150, (int)(180.0f * used_kb / cap_kb) + 1, 12, HUD_GREEN);

    DrawText(TextFormat("ASSET %s  %d -> %d tris  compat %.0f", e->name, e->tris_before, e->tris_after, e->compat_score),
             12, SCREEN_HEIGHT - 28, 16, HUD_DIM);
    DrawText("WASD move  SPACE jump/bhop  SHIFT dash  LMB throw  RMB recall  E bullet time",
             12, SCREEN_HEIGHT - 50, 16, HUD_DIM);
    DrawFPS(SCREEN_WIDTH - 96, 16);
    DrawDualReticle(&gs->reticle);
}

int main(void)
{
    static GameState gs;
    Model knife;
    Camera3D camera = { 0 };
    const float sensitivity = 0.0025f;

    if (!GameInit(&gs) || ValidateAssetManifest() != 0) return 1;

    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Retro FPS Core // C99 + raylib");
    SetTargetFPS(60);
    DisableCursor();
    knife = BuildKnifeModel();

    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = CAMERA_FOVY_DEG;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        FrameCommands cmd;
        Vector2 md = GetMouseDelta();
        RfVec3 eye;
        int i;

        /* input */
        gs.yaw -= md.x * sensitivity;
        gs.pitch = rf_clampf(gs.pitch - md.y * sensitivity, -1.3f, 1.3f);
        memset(&cmd, 0, sizeof(cmd));
        cmd.move.move_x = (float)(IsKeyDown(KEY_D) - IsKeyDown(KEY_A));
        cmd.move.move_z = (float)(IsKeyDown(KEY_W) - IsKeyDown(KEY_S));
        cmd.move.jump_pressed = IsKeyDown(KEY_SPACE);
        cmd.move.dash_pressed = IsKeyPressed(KEY_LEFT_SHIFT);
        cmd.move.bullet_time_requested = IsKeyDown(KEY_E);
        cmd.throw_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        cmd.recall_pressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);

        UpdatePhysics(&gs, &cmd, rf_minf(GetFrameTime(), 0.05f));

        eye = EyePosition(&gs);
        camera.position = ToRl3(eye);
        camera.target = ToRl3(rf_vec3_add(eye, LookDirection(gs.yaw, gs.pitch)));

        BeginDrawing();
            ClearBackground(CRT_BG);
            BeginMode3D(camera);
                DrawCorridor(gs.motor.position.z);
                for (i = 0; i < gs.target_count; ++i)
                    if (gs.targets[i].alive)
                        DrawSphereWires(ToRl3(gs.targets[i].position), gs.targets[i].radius, 6, 8, HUD_AMBER);
                for (i = 1; i < gs.trail_len; ++i) {
                    int a = (gs.trail_head - i + TRAIL_CAPACITY) % TRAIL_CAPACITY;
                    int b = (gs.trail_head - i - 1 + TRAIL_CAPACITY) % TRAIL_CAPACITY;
                    DrawLine3D(ToRl3(gs.trail[a]), ToRl3(gs.trail[b]), Fade(HUD_AMBER, 0.6f));
                }
                if (gs.combo.knife_recall_state != KNIFE_HELD) {
                    RfVec3 v = gs.combo.knife.velocity;
                    float angle = atan2f(v.x, v.z) * 180.0f / RF_PI;
                    DrawModelWiresEx(knife, ToRl3(gs.combo.knife.position), (Vector3){ 0.0f, 1.0f, 0.0f },
                                     angle, (Vector3){ 1.0f, 1.0f, 1.0f }, HUD_GREEN);
                }
            EndMode3D();
            DrawHud(&gs);
        EndDrawing();
    }

    UnloadModel(knife);
    CloseWindow();
    return 0;
}
#endif /* HEADLESS_TEST */
