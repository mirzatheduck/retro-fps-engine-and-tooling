/*
 * state_machine.h - Player movement finite state machine for a fast retro FPS.
 *
 * States
 *   STATE_GROUND_MOVE      grounded, Quake-style acceleration + friction
 *   STATE_BUNNY_HOP        airborne after a jump; landing with jump buffered
 *                          re-launches instantly (no friction frame) and
 *                          multiplies horizontal speed by bhop_speed_gain
 *   STATE_DASH             short burst at constant dash_speed, gravity off
 *   STATE_BULLET_TIME_AIR  airborne slow-motion: reduced gravity and higher
 *                          air control, entered while airborne on request
 *
 * Every velocity/acceleration/friction/position integration uses
 *   scaled_dt = delta_time * time_scale
 * so the combo system's bullet time (time_scale = 0.25f) slows the player
 * simulation without touching the movement code.
 *
 * Coordinate system: right-handed, +Y up (raylib convention). yaw = 0 faces
 * +Z; "right" is forward x up.
 */
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdbool.h>

#include "rf_math.h"

typedef enum PlayerState {
    STATE_GROUND_MOVE = 0,
    STATE_BUNNY_HOP,
    STATE_DASH,
    STATE_BULLET_TIME_AIR,
    STATE_COUNT
} PlayerState;

typedef struct PlayerInput {
    float move_x;               /* -1 left .. +1 right (local) */
    float move_z;               /* -1 back .. +1 forward (local) */
    float yaw;                  /* radians, 0 faces +Z */
    bool  jump_pressed;         /* held or pressed this frame */
    bool  dash_pressed;
    bool  bullet_time_requested;
} PlayerInput;

typedef struct MovementTuning {
    float gravity;              /* units/s^2 */
    float max_ground_speed;     /* wish speed on ground */
    float ground_accel;         /* Quake accel factor */
    float ground_friction;      /* Quake friction factor */
    float stop_speed;           /* friction control floor */
    float air_accel;
    float air_wish_cap;         /* Quake air wish-speed cap (strafe gain) */
    float jump_velocity;
    float bhop_window;          /* seconds a jump stays buffered */
    float bhop_speed_gain;      /* horizontal multiplier per chained hop */
    float bhop_max_speed;
    float dash_speed;
    float dash_duration;
    float dash_cooldown;        /* measured from dash start */
    float bullet_time_gravity_scale;
    float bullet_time_air_control;  /* multiplier on air_accel */
    float eye_height;
} MovementTuning;

typedef struct PlayerMotor {
    RfVec3 position;            /* feet position, ground plane is y = 0 */
    RfVec3 velocity;
    RfVec3 dash_direction;
    PlayerState state;
    PlayerState previous_state;
    float state_time;           /* scaled seconds spent in current state */
    float dash_timer;
    float dash_cooldown;
    float jump_buffer;
    float time_scale;           /* last time scale applied */
    int   bhop_chain;
    int   transitions;          /* total state changes (telemetry) */
    bool  grounded;
    MovementTuning tuning;
} PlayerMotor;

MovementTuning movement_default_tuning(void);

/* tuning may be NULL for defaults. */
void motor_init(PlayerMotor *motor, const MovementTuning *tuning);

/* Advances the FSM and physics by delta_time * time_scale seconds. */
void motor_update(PlayerMotor *motor, const PlayerInput *input, float delta_time, float time_scale);

const char *player_state_name(PlayerState state);

/* Horizontal speed, units/s. */
float motor_speed(const PlayerMotor *motor);

#endif /* STATE_MACHINE_H */
