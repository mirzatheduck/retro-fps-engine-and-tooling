/*
 * combat_combo.c - Dual reticle, knife throw/recall combo chain, bullet time.
 */
#include "combat_combo.h"

#include <string.h>

#define KNIFE_HIT_RADIUS      0.15f
#define KNIFE_CATCH_DISTANCE  0.5f
#define PRIMARY_SNAP_RADIUS   12.0f

/* ----------------------------------------------------------- dual reticle */

void reticle_init(DualReticle *r, RfVec2 screen_center)
{
    memset(r, 0, sizeof(*r));
    r->primary.screen_pos = screen_center;
    r->primary.target_id = -1;
    r->secondary.screen_pos = screen_center;
    r->secondary.target_id = -1;
    r->lock_radius = 160.0f;
    r->seek_speed = 12.0f;
    r->lock_time = 0.35f;
}

static int nearest_target(RfVec2 from, const RfVec2 *targets, int count, float max_dist)
{
    int i, best = -1;
    float best_d = max_dist;
    for (i = 0; i < count; ++i) {
        float d = rf_vec2_distance(from, targets[i]);
        if (d <= best_d) { best_d = d; best = i; }
    }
    return best;
}

void reticle_update(DualReticle *r, RfVec2 aim, const RfVec2 *targets, int count, float real_dt)
{
    int seek;
    RfVec2 goal;
    /* frame-rate independent exponential ease: 1 - e^(-k*dt) */
    float ease = 1.0f - expf(-r->seek_speed * real_dt);

    /* primary follows the crosshair exactly; it "locks" only when on top of a target */
    r->primary.screen_pos = aim;
    r->primary.target_id = nearest_target(aim, targets, count, PRIMARY_SNAP_RADIUS);
    r->primary.locked = r->primary.target_id >= 0;
    r->primary.lock_progress = r->primary.locked ? 1.0f : 0.0f;

    /* secondary seeks the nearest target around the primary */
    seek = nearest_target(aim, targets, count, r->lock_radius);
    if (seek != r->secondary.target_id) {
        r->secondary.target_id = seek;
        r->secondary.lock_progress = 0.0f;
        r->secondary.locked = false;
    }

    if (seek >= 0) {
        goal = targets[seek];
        r->secondary.lock_progress = rf_minf(1.0f, r->secondary.lock_progress + real_dt / r->lock_time);
    } else {
        goal = aim;
        r->secondary.lock_progress = rf_maxf(0.0f, r->secondary.lock_progress - real_dt / r->lock_time);
    }
    r->secondary.screen_pos = rf_vec2_lerp(r->secondary.screen_pos, goal, ease);
    r->secondary.locked = seek >= 0 && r->secondary.lock_progress >= 1.0f;
}

/* ------------------------------------------------------------------ combo */

void combo_init(ComboSystem *c)
{
    memset(c, 0, sizeof(*c));
    c->combo_multiplier = 1.0f;
    c->combo_window = COMBO_WINDOW_SECONDS;
    c->knife_recall_state = KNIFE_HELD;
    c->knife.stuck_target = -1;
    c->knife_speed = 60.0f;
    c->knife_recall_speed = 90.0f;
    c->knife_max_range = 40.0f;
    c->time_scale = 1.0f;
    c->bullet_time_duration = COMBO_BULLET_TIME_SECONDS;
}

void combo_register_hit(ComboSystem *c)
{
    c->combo_count++;
    c->total_hits++;
    c->combo_multiplier = (float)(c->combo_count < COMBO_MAX_MULTIPLIER ? c->combo_count : COMBO_MAX_MULTIPLIER);
    c->combo_timer = c->combo_window;
    if (c->combo_count > c->best_combo) c->best_combo = c->combo_count;
    c->score += (long)(100.0f * c->combo_multiplier);

    if (c->combo_count >= COMBO_BULLET_TIME_THRESHOLD) {
        c->time_scale = COMBO_SLOWMO_TIME_SCALE;
        c->bullet_time_remaining = c->bullet_time_duration;
    }
}

void combo_update(ComboSystem *c, float real_dt)
{
    /* combo window runs on real time */
    if (c->combo_count > 0) {
        c->combo_timer -= real_dt;
        if (c->combo_timer <= 0.0f) {
            c->combo_timer = 0.0f;
            c->combo_count = 0;
            c->combo_multiplier = 1.0f;
        }
    }

    /* bullet time: hold 0.25x, then ease back to 1.0x */
    if (c->bullet_time_remaining > 0.0f) {
        c->bullet_time_remaining -= real_dt;
        c->time_scale = COMBO_SLOWMO_TIME_SCALE;
        if (c->bullet_time_remaining < 0.0f) c->bullet_time_remaining = 0.0f;
    } else if (c->time_scale < 1.0f) {
        float ease = 1.0f - expf(-COMBO_TIME_SCALE_RECOVERY * real_dt);
        c->time_scale = rf_lerpf(c->time_scale, 1.0f, ease);
        if (1.0f - c->time_scale < 1e-4f) c->time_scale = 1.0f;
    }
}

const char *combo_rank_name(const ComboSystem *c)
{
    if (c->combo_count >= COMBO_MAX_MULTIPLIER) return "ULTRA COMBO";
    if (c->combo_count >= 5) return "GREAT COMBO";
    if (c->combo_count >= COMBO_BULLET_TIME_THRESHOLD) return "NICE COMBO";
    if (c->combo_count >= 1) return "COMBO";
    return "";
}

/* ------------------------------------------------------------------ knife */

const char *knife_state_name(KnifeRecallState state)
{
    switch (state) {
        case KNIFE_HELD:      return "HELD";
        case KNIFE_IN_FLIGHT: return "IN_FLIGHT";
        case KNIFE_STUCK:     return "STUCK";
        case KNIFE_RECALLING: return "RECALLING";
    }
    return "UNKNOWN";
}

bool combo_throw_knife(ComboSystem *c, RfVec3 origin, RfVec3 target)
{
    RfVec3 dir;
    if (c->knife_recall_state != KNIFE_HELD) return false;
    dir = rf_vec3_normalize(rf_vec3_sub(target, origin));
    if (rf_vec3_length(dir) < 0.5f) return false;
    c->knife.position = origin;
    c->knife.velocity = rf_vec3_scale(dir, c->knife_speed);
    c->knife.flight_time = 0.0f;
    c->knife.distance_travelled = 0.0f;
    c->knife.stuck_target = -1;
    c->owner_position = origin;
    c->knife_recall_state = KNIFE_IN_FLIGHT;
    return true;
}

bool combo_recall_knife(ComboSystem *c, RfVec3 owner_position)
{
    if (c->knife_recall_state != KNIFE_STUCK && c->knife_recall_state != KNIFE_IN_FLIGHT) return false;
    c->owner_position = owner_position;
    c->knife.stuck_target = -1;
    c->knife_recall_state = KNIFE_RECALLING;
    return true;
}

void combo_set_owner_position(ComboSystem *c, RfVec3 owner_position)
{
    c->owner_position = owner_position;
}

/* Closest distance from point p to segment ab. */
static float segment_point_distance(RfVec3 a, RfVec3 b, RfVec3 p)
{
    RfVec3 ab = rf_vec3_sub(b, a);
    float len2 = rf_vec3_dot(ab, ab);
    float t = len2 > 1e-8f ? rf_clampf(rf_vec3_dot(rf_vec3_sub(p, a), ab) / len2, 0.0f, 1.0f) : 0.0f;
    return rf_vec3_distance(rf_vec3_add(a, rf_vec3_scale(ab, t)), p);
}

/* Swept hit test along this frame's segment; returns the index hit or -1. */
static int sweep_targets(RfVec3 from, RfVec3 to, CombatTarget *targets, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        if (!targets[i].alive) continue;
        if (segment_point_distance(from, to, targets[i].position) <= targets[i].radius + KNIFE_HIT_RADIUS)
            return i;
    }
    return -1;
}

int combo_update_knife(ComboSystem *c, CombatTarget *targets, int count, float real_dt)
{
    const float dt = real_dt * c->time_scale;
    RfVec3 from = c->knife.position, to;
    int hit, hits = 0;

    switch (c->knife_recall_state) {
        case KNIFE_HELD:
        case KNIFE_STUCK:
            return 0;

        case KNIFE_IN_FLIGHT:
            to = rf_vec3_add(from, rf_vec3_scale(c->knife.velocity, dt));
            c->knife.flight_time += dt;
            c->knife.distance_travelled += rf_vec3_distance(from, to);
            hit = sweep_targets(from, to, targets, count);
            if (hit >= 0) {
                targets[hit].alive = false;
                c->knife.position = targets[hit].position;
                c->knife.velocity = rf_vec3(0.0f, 0.0f, 0.0f);
                c->knife.stuck_target = targets[hit].id;
                c->knife_recall_state = KNIFE_STUCK;
                combo_register_hit(c);
                return 1;
            }
            c->knife.position = to;
            if (c->knife.distance_travelled >= c->knife_max_range) combo_recall_knife(c, c->owner_position);
            return 0;

        case KNIFE_RECALLING: {
            RfVec3 to_owner = rf_vec3_sub(c->owner_position, from);
            float dist = rf_vec3_length(to_owner);
            float step = c->knife_recall_speed * dt;
            if (dist <= KNIFE_CATCH_DISTANCE || step >= dist) {
                c->knife.position = c->owner_position;
                c->knife.velocity = rf_vec3(0.0f, 0.0f, 0.0f);
                c->knife_recall_state = KNIFE_HELD;
                return 0;
            }
            c->knife.velocity = rf_vec3_scale(to_owner, c->knife_recall_speed / dist);
            to = rf_vec3_add(from, rf_vec3_scale(c->knife.velocity, dt));
            /* the returning blade can cut through targets on its way back */
            while ((hit = sweep_targets(from, to, targets, count)) >= 0) {
                targets[hit].alive = false;
                combo_register_hit(c);
                hits++;
            }
            c->knife.position = to;
            return hits;
        }
    }
    return 0;
}
