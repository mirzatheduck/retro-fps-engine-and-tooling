/*
 * combat_combo.h - Knife-throw combo chain, dual reticle lock-on and
 *                  combo-triggered bullet time.
 *
 * Dual reticle
 *   primary   : the player's aim point, follows the crosshair 1:1
 *   secondary : an assist reticle that seeks the nearest target (screen
 *               space) inside lock_radius around the primary, eases onto it
 *               and reports a lock when lock_progress reaches 1
 *
 * Knife (knife_recall_state)
 *   KNIFE_HELD -> combo_throw_knife -> KNIFE_IN_FLIGHT
 *   KNIFE_IN_FLIGHT -> hits target -> KNIFE_STUCK (combo hit registered)
 *   KNIFE_IN_FLIGHT -> max range   -> KNIFE_RECALLING (auto recall)
 *   KNIFE_STUCK -> combo_recall_knife -> KNIFE_RECALLING -> back at owner -> KNIFE_HELD
 *   Knife flight uses the SCALED clock, so it visibly slows in bullet time.
 *
 * Combo
 *   every hit: combo_count++, combo_multiplier = min(combo_count, 8),
 *   combo_timer refilled to combo_window (REAL time, so slow-mo does not
 *   extend the window). At COMBO_BULLET_TIME_THRESHOLD hits and above, each
 *   hit (re)starts bullet time: time_scale = COMBO_SLOWMO_TIME_SCALE (0.25f)
 *   for bullet_time_duration real seconds, then time_scale eases back to 1.
 */
#ifndef COMBAT_COMBO_H
#define COMBAT_COMBO_H

#include <stdbool.h>

#include "rf_math.h"

#define COMBO_SLOWMO_TIME_SCALE      0.25f
#define COMBO_BULLET_TIME_THRESHOLD  3
#define COMBO_MAX_MULTIPLIER         8
#define COMBO_WINDOW_SECONDS         2.5f
#define COMBO_BULLET_TIME_SECONDS    1.5f
#define COMBO_TIME_SCALE_RECOVERY    6.0f   /* 1/s ease back to 1.0 */

typedef enum KnifeRecallState {
    KNIFE_HELD = 0,
    KNIFE_IN_FLIGHT,
    KNIFE_STUCK,
    KNIFE_RECALLING
} KnifeRecallState;

typedef struct Reticle {
    RfVec2 screen_pos;
    float  lock_progress;   /* 0..1 */
    bool   locked;
    int    target_id;       /* index into the target list, -1 = none */
} Reticle;

typedef struct DualReticle {
    Reticle primary;
    Reticle secondary;
    float lock_radius;      /* px around primary to acquire a target */
    float seek_speed;       /* 1/s, secondary ease toward its goal */
    float lock_time;        /* seconds of continuous tracking to lock */
} DualReticle;

typedef struct CombatTarget {
    int    id;
    RfVec3 position;
    float  radius;
    bool   alive;
} CombatTarget;

typedef struct Knife {
    RfVec3 position;
    RfVec3 velocity;
    float  flight_time;
    float  distance_travelled;
    int    stuck_target;    /* target id, -1 = none */
} Knife;

typedef struct ComboSystem {
    int   combo_count;
    float combo_multiplier;
    float combo_timer;              /* real seconds left in the chain */
    float combo_window;
    int   best_combo;
    int   total_hits;
    long  score;

    KnifeRecallState knife_recall_state;
    Knife  knife;
    RfVec3 owner_position;          /* knife returns here */
    float  knife_speed;
    float  knife_recall_speed;
    float  knife_max_range;

    float time_scale;               /* world clock multiplier */
    float bullet_time_remaining;    /* real seconds */
    float bullet_time_duration;
} ComboSystem;

/* Dual reticle */
void reticle_init(DualReticle *reticle, RfVec2 screen_center);
void reticle_update(DualReticle *reticle, RfVec2 aim, const RfVec2 *target_screens,
                    int target_count, float real_dt);

/* Combo chain + bullet time (real_dt = unscaled frame time) */
void  combo_init(ComboSystem *combo);
void  combo_register_hit(ComboSystem *combo);
void  combo_update(ComboSystem *combo, float real_dt);
const char *combo_rank_name(const ComboSystem *combo);

/* Knife */
bool combo_throw_knife(ComboSystem *combo, RfVec3 origin, RfVec3 target);
bool combo_recall_knife(ComboSystem *combo, RfVec3 owner_position);
void combo_set_owner_position(ComboSystem *combo, RfVec3 owner_position);
/* Advances the knife with real_dt * time_scale; returns hits this frame. */
int  combo_update_knife(ComboSystem *combo, CombatTarget *targets, int target_count, float real_dt);
const char *knife_state_name(KnifeRecallState state);

#endif /* COMBAT_COMBO_H */
