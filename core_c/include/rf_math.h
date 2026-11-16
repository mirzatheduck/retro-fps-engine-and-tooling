/*
 * rf_math.h - Minimal header-only vector math for the retro FPS core.
 *
 * The core is deliberately independent of raylib so it can be unit tested
 * and run headless with any C99 compiler. main_prototype.c converts these
 * types to raylib's Vector2/Vector3 at the rendering boundary only.
 */
#ifndef RF_MATH_H
#define RF_MATH_H

#include <math.h>

typedef struct RfVec2 { float x, y; } RfVec2;
typedef struct RfVec3 { float x, y, z; } RfVec3;

static inline RfVec2 rf_vec2(float x, float y) { RfVec2 v; v.x = x; v.y = y; return v; }
static inline RfVec3 rf_vec3(float x, float y, float z) { RfVec3 v; v.x = x; v.y = y; v.z = z; return v; }

static inline RfVec2 rf_vec2_add(RfVec2 a, RfVec2 b) { return rf_vec2(a.x + b.x, a.y + b.y); }
static inline RfVec2 rf_vec2_sub(RfVec2 a, RfVec2 b) { return rf_vec2(a.x - b.x, a.y - b.y); }
static inline RfVec2 rf_vec2_scale(RfVec2 a, float s) { return rf_vec2(a.x * s, a.y * s); }
static inline float  rf_vec2_length(RfVec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static inline float  rf_vec2_distance(RfVec2 a, RfVec2 b) { return rf_vec2_length(rf_vec2_sub(a, b)); }
static inline RfVec2 rf_vec2_lerp(RfVec2 a, RfVec2 b, float t) { return rf_vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }

static inline RfVec3 rf_vec3_add(RfVec3 a, RfVec3 b) { return rf_vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline RfVec3 rf_vec3_sub(RfVec3 a, RfVec3 b) { return rf_vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline RfVec3 rf_vec3_scale(RfVec3 a, float s) { return rf_vec3(a.x * s, a.y * s, a.z * s); }
static inline float  rf_vec3_dot(RfVec3 a, RfVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float  rf_vec3_length(RfVec3 a) { return sqrtf(rf_vec3_dot(a, a)); }
static inline float  rf_vec3_length_xz(RfVec3 a) { return sqrtf(a.x * a.x + a.z * a.z); }
static inline float  rf_vec3_distance(RfVec3 a, RfVec3 b) { return rf_vec3_length(rf_vec3_sub(a, b)); }

static inline RfVec3 rf_vec3_normalize(RfVec3 a)
{
    float len = rf_vec3_length(a);
    return (len > 1e-6f) ? rf_vec3_scale(a, 1.0f / len) : rf_vec3(0.0f, 0.0f, 0.0f);
}

static inline float rf_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float rf_lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float rf_minf(float a, float b) { return a < b ? a : b; }
static inline float rf_maxf(float a, float b) { return a > b ? a : b; }

#endif /* RF_MATH_H */
