/*
 * test_core.c - Unit tests for the retro FPS core (arena, FSM, combo).
 * Pure C99, no external framework, no raylib. Returns non-zero on failure.
 */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "memory_arena.h"
#include "state_machine.h"
#include "combat_combo.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabsf((float)(a) - (float)(b)) <= (eps))

static const float DT = 1.0f / 60.0f;

/* ------------------------------------------------------------------ arena */
static void test_arena(void)
{
    static unsigned char backing[1024];
    MemoryArena arena;
    void *a, *b, *c;
    ArenaMarker mark;

    printf("[arena]\n");
    arena_init(&arena, backing, sizeof(backing));
    CHECK(arena_used(&arena) == 0);
    CHECK(arena_remaining(&arena) == sizeof(backing));

    a = arena_alloc(&arena, 10);
    b = arena_alloc(&arena, 10);
    CHECK(a != NULL && b != NULL);
    CHECK(((uintptr_t)a % ARENA_DEFAULT_ALIGNMENT) == 0);
    CHECK(((uintptr_t)b % ARENA_DEFAULT_ALIGNMENT) == 0);
    CHECK((unsigned char *)b > (unsigned char *)a);
    CHECK(arena.alloc_count == 2);

    /* allocations are zeroed */
    CHECK(((unsigned char *)a)[0] == 0 && ((unsigned char *)a)[9] == 0);

    /* out of memory returns NULL and does not corrupt the arena */
    c = arena_alloc(&arena, 4096);
    CHECK(c == NULL);
    CHECK(arena.failed_allocs == 1);

    /* marker / rewind for per-frame scratch */
    mark = arena_mark(&arena);
    CHECK(arena_alloc(&arena, 100) != NULL);
    arena_rewind(&arena, mark);
    CHECK(arena_used(&arena) == mark.offset);

    /* peak survives reset, offset does not */
    CHECK(arena.peak >= 132);
    arena_reset(&arena);
    CHECK(arena_used(&arena) == 0);
    CHECK(arena.peak >= 132);
    CHECK(arena_alloc(&arena, 10) == a); /* memory is reused from the start */
}

/* -------------------------------------------------------------------- fsm */
static PlayerInput no_input(void)
{
    PlayerInput in;
    memset(&in, 0, sizeof(in));
    return in;
}

static void test_fsm_ground_accel_and_friction(void)
{
    PlayerMotor m;
    PlayerInput in = no_input();
    int i;
    float speed_moving;

    printf("[fsm ground]\n");
    motor_init(&m, NULL);
    CHECK(m.state == STATE_GROUND_MOVE);
    CHECK(m.grounded);

    in.move_z = 1.0f; /* forward, yaw 0 => +Z */
    for (i = 0; i < 60; ++i) motor_update(&m, &in, DT, 1.0f);
    speed_moving = rf_vec3_length_xz(m.velocity);
    CHECK(speed_moving > 0.9f * m.tuning.max_ground_speed);
    CHECK(speed_moving <= m.tuning.max_ground_speed + 0.01f);
    CHECK(m.position.z > 1.0f);
    CHECK(m.state == STATE_GROUND_MOVE);

    in = no_input();
    for (i = 0; i < 60; ++i) motor_update(&m, &in, DT, 1.0f);
    CHECK(rf_vec3_length_xz(m.velocity) < 0.05f * speed_moving); /* friction stops us */
}

static void test_fsm_time_scale_slows_integration(void)
{
    PlayerMotor a, b;
    PlayerInput in = no_input();
    int i;

    printf("[fsm time_scale]\n");
    motor_init(&a, NULL);
    motor_init(&b, NULL);
    in.move_z = 1.0f;
    for (i = 0; i < 10; ++i) {
        motor_update(&a, &in, DT, 1.0f);
        motor_update(&b, &in, DT, 0.25f);
    }
    /* b integrated a quarter of the simulated time: it must be clearly behind */
    CHECK(b.position.z < a.position.z * 0.5f);
    CHECK_NEAR(b.time_scale, 0.25f, 1e-6f);
    CHECK_NEAR(b.state_time, 10.0f * DT * 0.25f, 1e-4f);
}

static void test_fsm_bunny_hop_chain_gains_speed(void)
{
    PlayerMotor m;
    PlayerInput in = no_input();
    int i, hops = 0;
    float speed_before;

    printf("[fsm bunny hop]\n");
    motor_init(&m, NULL);
    in.move_z = 1.0f;
    for (i = 0; i < 90; ++i) motor_update(&m, &in, DT, 1.0f);
    speed_before = rf_vec3_length_xz(m.velocity);

    /* hold jump: every landing inside the window re-launches with a speed gain */
    in.jump_pressed = true;
    motor_update(&m, &in, DT, 1.0f);
    CHECK(m.state == STATE_BUNNY_HOP);
    CHECK(!m.grounded);
    CHECK(m.velocity.y > 0.0f);

    for (i = 0; i < 240; ++i) {
        PlayerState before = m.state;
        int chain_before = m.bhop_chain;
        motor_update(&m, &in, DT, 1.0f);
        if (m.bhop_chain > chain_before) hops++;
        (void)before;
    }
    CHECK(hops >= 2);
    CHECK(m.bhop_chain >= 2);
    CHECK(rf_vec3_length_xz(m.velocity) > speed_before * 1.15f);
    CHECK(rf_vec3_length_xz(m.velocity) <= m.tuning.bhop_max_speed + 0.01f);

    /* release jump: next landing returns to ground move and resets the chain */
    in.jump_pressed = false;
    for (i = 0; i < 120; ++i) motor_update(&m, &in, DT, 1.0f);
    CHECK(m.state == STATE_GROUND_MOVE);
    CHECK(m.bhop_chain == 0);
}

static void test_fsm_dash(void)
{
    PlayerMotor m;
    PlayerInput in = no_input();
    int i;

    printf("[fsm dash]\n");
    motor_init(&m, NULL);
    in.move_x = 1.0f; /* strafe right; right-handed Y-up facing +Z => right is -X */
    in.dash_pressed = true;
    motor_update(&m, &in, DT, 1.0f);
    CHECK(m.state == STATE_DASH);
    CHECK_NEAR(rf_vec3_length_xz(m.velocity), m.tuning.dash_speed, 0.01f);
    CHECK(m.velocity.x < 0.0f);
    CHECK(fabsf(m.velocity.z) < 1e-3f);

    in.dash_pressed = false;
    for (i = 0; i < 30; ++i) motor_update(&m, &in, DT, 1.0f);
    CHECK(m.state == STATE_GROUND_MOVE);
    CHECK(m.dash_cooldown > 0.0f);

    /* cooldown blocks an immediate second dash */
    in.dash_pressed = true;
    motor_update(&m, &in, DT, 1.0f);
    CHECK(m.state == STATE_GROUND_MOVE);
}

static void test_fsm_bullet_time_air(void)
{
    PlayerMotor m;
    PlayerInput in = no_input();
    int i;
    float vy_normal, vy_slow;

    printf("[fsm bullet time air]\n");
    motor_init(&m, NULL);
    in.jump_pressed = true;
    motor_update(&m, &in, DT, 1.0f);
    in.jump_pressed = false;
    CHECK(m.state == STATE_BUNNY_HOP);

    in.bullet_time_requested = true;
    motor_update(&m, &in, DT, 1.0f);
    CHECK(m.state == STATE_BULLET_TIME_AIR);

    /* reduced gravity while in bullet-time air state */
    vy_slow = m.velocity.y;
    motor_update(&m, &in, DT, 1.0f);
    vy_slow -= m.velocity.y;
    {
        PlayerMotor ref;
        PlayerInput jump = no_input();
        motor_init(&ref, NULL);
        jump.jump_pressed = true;
        motor_update(&ref, &jump, DT, 1.0f);
        jump.jump_pressed = false;
        vy_normal = ref.velocity.y;
        motor_update(&ref, &jump, DT, 1.0f);
        vy_normal -= ref.velocity.y;
    }
    CHECK(vy_slow < vy_normal * 0.5f);

    for (i = 0; i < 600 && !m.grounded; ++i) motor_update(&m, &in, DT, 1.0f);
    CHECK(m.grounded);
    CHECK(m.state == STATE_GROUND_MOVE);
}

static void test_state_names(void)
{
    printf("[fsm names]\n");
    CHECK(strcmp(player_state_name(STATE_GROUND_MOVE), "GROUND_MOVE") == 0);
    CHECK(strcmp(player_state_name(STATE_BUNNY_HOP), "BUNNY_HOP") == 0);
    CHECK(strcmp(player_state_name(STATE_DASH), "DASH") == 0);
    CHECK(strcmp(player_state_name(STATE_BULLET_TIME_AIR), "BULLET_TIME_AIR") == 0);
}

/* ------------------------------------------------------------------ combo */
static void test_dual_reticle_lock(void)
{
    DualReticle r;
    RfVec2 targets[2];
    int i;

    printf("[dual reticle]\n");
    reticle_init(&r, rf_vec2(640.0f, 360.0f));
    targets[0] = rf_vec2(700.0f, 330.0f); /* near the crosshair */
    targets[1] = rf_vec2(100.0f, 100.0f); /* far away */

    for (i = 0; i < 60; ++i) reticle_update(&r, rf_vec2(640.0f, 360.0f), targets, 2, DT);
    CHECK(r.primary.locked == false);
    CHECK_NEAR(r.primary.screen_pos.x, 640.0f, 0.01f);
    CHECK(r.secondary.target_id == 0);
    CHECK(r.secondary.locked);
    CHECK_NEAR(r.secondary.lock_progress, 1.0f, 1e-4f);
    CHECK(rf_vec2_distance(r.secondary.screen_pos, targets[0]) < 2.0f);

    /* no target in range => secondary drifts back to primary and unlocks */
    for (i = 0; i < 60; ++i) reticle_update(&r, rf_vec2(640.0f, 360.0f), targets + 1, 1, DT);
    CHECK(r.secondary.target_id == -1);
    CHECK(!r.secondary.locked);
    CHECK(rf_vec2_distance(r.secondary.screen_pos, r.primary.screen_pos) < 2.0f);
}

static void test_combo_chain_and_bullet_time(void)
{
    ComboSystem c;
    int i;

    printf("[combo]\n");
    combo_init(&c);
    CHECK(c.combo_count == 0);
    CHECK_NEAR(c.combo_multiplier, 1.0f, 1e-6f);
    CHECK_NEAR(c.time_scale, 1.0f, 1e-6f);
    CHECK(c.knife_recall_state == KNIFE_HELD);

    combo_register_hit(&c);
    combo_register_hit(&c);
    CHECK(c.combo_count == 2);
    CHECK_NEAR(c.time_scale, 1.0f, 1e-6f); /* below threshold */

    combo_register_hit(&c); /* threshold reached */
    CHECK(c.combo_count == COMBO_BULLET_TIME_THRESHOLD);
    CHECK_NEAR(c.time_scale, COMBO_SLOWMO_TIME_SCALE, 1e-6f);
    CHECK_NEAR(COMBO_SLOWMO_TIME_SCALE, 0.25f, 1e-6f);
    CHECK(c.bullet_time_remaining > 0.0f);

    for (i = 0; i < 5; ++i) combo_register_hit(&c);
    CHECK(c.combo_count == 8);
    CHECK_NEAR(c.combo_multiplier, 8.0f, 1e-6f);
    CHECK(strcmp(combo_rank_name(&c), "ULTRA COMBO") == 0);
    combo_register_hit(&c);
    CHECK_NEAR(c.combo_multiplier, (float)COMBO_MAX_MULTIPLIER, 1e-6f); /* capped */
    CHECK(c.best_combo == 9);

    /* bullet time ends in real time and time scale eases back to 1 */
    for (i = 0; i < 240; ++i) combo_update(&c, DT);
    CHECK(c.bullet_time_remaining <= 0.0f);
    CHECK_NEAR(c.time_scale, 1.0f, 1e-3f);

    /* combo window expires */
    CHECK(c.combo_count == 0);
    CHECK_NEAR(c.combo_multiplier, 1.0f, 1e-6f);
    CHECK(c.best_combo == 9);
}

static void test_knife_throw_hit_recall(void)
{
    ComboSystem c;
    CombatTarget t[1];
    int i, hit_frame = -1;

    printf("[knife]\n");
    combo_init(&c);
    t[0].id = 7;
    t[0].position = rf_vec3(0.0f, 1.5f, 12.0f);
    t[0].radius = 0.6f;
    t[0].alive = true;

    CHECK(combo_throw_knife(&c, rf_vec3(0.0f, 1.5f, 0.0f), t[0].position));
    CHECK(c.knife_recall_state == KNIFE_IN_FLIGHT);
    CHECK(!combo_throw_knife(&c, rf_vec3(0.0f, 1.5f, 0.0f), t[0].position)); /* only one knife */

    for (i = 0; i < 120; ++i) {
        int hits = combo_update_knife(&c, t, 1, DT);
        if (hits > 0 && hit_frame < 0) hit_frame = i;
    }
    CHECK(hit_frame >= 0);
    CHECK(c.knife_recall_state == KNIFE_STUCK);
    CHECK(c.knife.stuck_target == 7);
    CHECK(!t[0].alive);
    CHECK(c.combo_count == 1);

    CHECK(combo_recall_knife(&c, rf_vec3(0.0f, 1.5f, 0.0f)));
    CHECK(c.knife_recall_state == KNIFE_RECALLING);
    for (i = 0; i < 240 && c.knife_recall_state != KNIFE_HELD; ++i) {
        combo_set_owner_position(&c, rf_vec3(0.0f, 1.5f, 0.0f));
        combo_update_knife(&c, t, 1, DT);
    }
    CHECK(c.knife_recall_state == KNIFE_HELD);
    CHECK(strcmp(knife_state_name(KNIFE_RECALLING), "RECALLING") == 0);
}

int main(void)
{
    test_arena();
    test_fsm_ground_accel_and_friction();
    test_fsm_time_scale_slows_integration();
    test_fsm_bunny_hop_chain_gains_speed();
    test_fsm_dash();
    test_fsm_bullet_time_air();
    test_state_names();
    test_dual_reticle_lock();
    test_combo_chain_and_bullet_time();
    test_knife_throw_hit_recall();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
