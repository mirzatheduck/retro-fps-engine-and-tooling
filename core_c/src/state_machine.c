/*
 * state_machine.c - Player movement FSM (ground / bunny hop / dash / bullet-time air).
 */
#include "state_machine.h"

#include <string.h>

MovementTuning movement_default_tuning(void)
{
    MovementTuning t;
    t.gravity = 32.0f;
    t.max_ground_speed = 12.0f;
    t.ground_accel = 10.0f;
    t.ground_friction = 6.0f;
    t.stop_speed = 4.0f;
    t.air_accel = 12.0f;
    t.air_wish_cap = 1.2f;
    t.jump_velocity = 10.0f;
    t.bhop_window = 0.12f;
    t.bhop_speed_gain = 1.10f;
    t.bhop_max_speed = 28.0f;
    t.dash_speed = 30.0f;
    t.dash_duration = 0.18f;
    t.dash_cooldown = 0.60f;
    t.bullet_time_gravity_scale = 0.35f;
    t.bullet_time_air_control = 2.5f;
    t.eye_height = 1.6f;
    return t;
}

void motor_init(PlayerMotor *motor, const MovementTuning *tuning)
{
    memset(motor, 0, sizeof(*motor));
    motor->tuning = tuning ? *tuning : movement_default_tuning();
    motor->state = STATE_GROUND_MOVE;
    motor->previous_state = STATE_GROUND_MOVE;
    motor->grounded = true;
    motor->time_scale = 1.0f;
}

const char *player_state_name(PlayerState state)
{
    switch (state) {
        case STATE_GROUND_MOVE:     return "GROUND_MOVE";
        case STATE_BUNNY_HOP:       return "BUNNY_HOP";
        case STATE_DASH:            return "DASH";
        case STATE_BULLET_TIME_AIR: return "BULLET_TIME_AIR";
        case STATE_COUNT:           break;
    }
    return "UNKNOWN";
}

float motor_speed(const PlayerMotor *motor)
{
    return rf_vec3_length_xz(motor->velocity);
}

/* ------------------------------------------------------------------ helpers */

static void set_state(PlayerMotor *m, PlayerState next)
{
    if (m->state == next) return;
    m->previous_state = m->state;
    m->state = next;
    m->state_time = 0.0f;
    m->transitions++;
}

/* World-space wish direction (unit, XZ) and magnitude in [0,1]. */
static RfVec3 wish_direction(const PlayerInput *in, float *magnitude)
{
    RfVec3 forward = rf_vec3(sinf(in->yaw), 0.0f, cosf(in->yaw));
    RfVec3 right = rf_vec3(-cosf(in->yaw), 0.0f, sinf(in->yaw)); /* forward x up */
    RfVec3 wish = rf_vec3_add(rf_vec3_scale(forward, in->move_z), rf_vec3_scale(right, in->move_x));
    float len = rf_vec3_length(wish);
    *magnitude = rf_minf(len, 1.0f);
    return len > 1e-6f ? rf_vec3_scale(wish, 1.0f / len) : rf_vec3(0.0f, 0.0f, 0.0f);
}

/* Quake PM_Accelerate: only adds speed along wishdir up to wish_speed. */
static void accelerate(RfVec3 *vel, RfVec3 wishdir, float wish_speed, float add_cap,
                       float accel, float dt)
{
    float current = vel->x * wishdir.x + vel->z * wishdir.z;
    float add = add_cap - current;
    float accel_speed;
    if (add <= 0.0f) return;
    accel_speed = accel * dt * wish_speed;
    if (accel_speed > add) accel_speed = add;
    vel->x += wishdir.x * accel_speed;
    vel->z += wishdir.z * accel_speed;
}

/* Quake PM_Friction on the horizontal plane. */
static void apply_friction(RfVec3 *vel, float friction, float stop_speed, float dt)
{
    float speed = sqrtf(vel->x * vel->x + vel->z * vel->z);
    float control, drop, new_speed;
    if (speed < 1e-4f) { vel->x = 0.0f; vel->z = 0.0f; return; }
    control = speed < stop_speed ? stop_speed : speed;
    drop = control * friction * dt;
    new_speed = speed - drop;
    if (new_speed < 0.0f) new_speed = 0.0f;
    new_speed /= speed;
    vel->x *= new_speed;
    vel->z *= new_speed;
}

static void start_jump(PlayerMotor *m)
{
    m->velocity.y = m->tuning.jump_velocity;
    m->grounded = false;
    m->jump_buffer = 0.0f;
    set_state(m, STATE_BUNNY_HOP);
}

static void start_dash(PlayerMotor *m, RfVec3 wishdir, float yaw)
{
    if (rf_vec3_length(wishdir) < 1e-6f) wishdir = rf_vec3(sinf(yaw), 0.0f, cosf(yaw));
    m->dash_direction = wishdir;
    m->dash_timer = m->tuning.dash_duration;
    m->dash_cooldown = m->tuning.dash_cooldown;
    set_state(m, STATE_DASH);
}

static bool can_dash(const PlayerMotor *m, const PlayerInput *in)
{
    return in->dash_pressed && m->dash_cooldown <= 0.0f;
}

/* Landing from an airborne state: chain a hop if a jump is buffered. */
static void handle_landing(PlayerMotor *m)
{
    const MovementTuning *t = &m->tuning;
    m->position.y = 0.0f;
    if (m->jump_buffer > 0.0f) {
        float speed;
        m->velocity.x *= t->bhop_speed_gain;
        m->velocity.z *= t->bhop_speed_gain;
        speed = rf_vec3_length_xz(m->velocity);
        if (speed > t->bhop_max_speed) {
            float k = t->bhop_max_speed / speed;
            m->velocity.x *= k;
            m->velocity.z *= k;
        }
        m->bhop_chain++;
        m->velocity.y = t->jump_velocity;
        m->jump_buffer = 0.0f;
        m->grounded = false;
        if (m->state != STATE_BUNNY_HOP) set_state(m, STATE_BUNNY_HOP);
        else m->state_time = 0.0f;
    } else {
        m->velocity.y = 0.0f;
        m->bhop_chain = 0;
        m->grounded = true;
        set_state(m, STATE_GROUND_MOVE);
    }
}

/* -------------------------------------------------------------------- update */

void motor_update(PlayerMotor *m, const PlayerInput *in, float delta_time, float time_scale)
{
    const MovementTuning *t = &m->tuning;
    const float dt = delta_time * time_scale;   /* the only clock the motor uses */
    float wish_mag;
    RfVec3 wishdir = wish_direction(in, &wish_mag);
    float wish_speed = t->max_ground_speed * wish_mag;

    m->time_scale = time_scale;

    /* timers */
    m->jump_buffer = in->jump_pressed ? t->bhop_window : rf_maxf(0.0f, m->jump_buffer - dt);
    m->dash_cooldown = rf_maxf(0.0f, m->dash_cooldown - dt);

    /* input-driven transitions */
    switch (m->state) {
        case STATE_GROUND_MOVE:
            if (can_dash(m, in)) start_dash(m, wishdir, in->yaw);
            else if (in->jump_pressed) start_jump(m);
            break;
        case STATE_BUNNY_HOP:
            if (can_dash(m, in)) start_dash(m, wishdir, in->yaw);
            else if (in->bullet_time_requested) set_state(m, STATE_BULLET_TIME_AIR);
            break;
        case STATE_BULLET_TIME_AIR:
            if (can_dash(m, in)) start_dash(m, wishdir, in->yaw);
            else if (!in->bullet_time_requested) set_state(m, STATE_BUNNY_HOP);
            break;
        case STATE_DASH:
        case STATE_COUNT:
            break;
    }

    /* per-state physics */
    switch (m->state) {
        case STATE_GROUND_MOVE:
            apply_friction(&m->velocity, t->ground_friction, t->stop_speed, dt);
            accelerate(&m->velocity, wishdir, wish_speed, wish_speed, t->ground_accel, dt);
            m->velocity.y = 0.0f;
            break;
        case STATE_BUNNY_HOP:
            accelerate(&m->velocity, wishdir, wish_speed, rf_minf(wish_speed, t->air_wish_cap),
                       t->air_accel, dt);
            m->velocity.y -= t->gravity * dt;
            break;
        case STATE_BULLET_TIME_AIR:
            accelerate(&m->velocity, wishdir, wish_speed, rf_minf(wish_speed, t->air_wish_cap),
                       t->air_accel * t->bullet_time_air_control, dt);
            m->velocity.y -= t->gravity * t->bullet_time_gravity_scale * dt;
            break;
        case STATE_DASH:
            m->velocity = rf_vec3_scale(m->dash_direction, t->dash_speed);
            m->velocity.y = 0.0f;
            m->dash_timer -= dt;
            break;
        case STATE_COUNT:
            break;
    }

    /* integrate */
    m->position = rf_vec3_add(m->position, rf_vec3_scale(m->velocity, dt));

    /* ground plane + post-physics transitions */
    if (m->state == STATE_DASH) {
        if (m->dash_timer <= 0.0f) {
            if (m->position.y <= 0.0f) {
                m->position.y = 0.0f;
                m->grounded = true;
                set_state(m, STATE_GROUND_MOVE);
            } else {
                m->grounded = false;
                set_state(m, STATE_BUNNY_HOP);
            }
        }
    } else if (m->state == STATE_BUNNY_HOP || m->state == STATE_BULLET_TIME_AIR) {
        if (m->position.y <= 0.0f && m->velocity.y <= 0.0f) handle_landing(m);
        else m->grounded = false;
    } else {
        m->position.y = 0.0f;
        m->grounded = true;
    }

    m->state_time += dt;
}
