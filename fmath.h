#ifndef RLK_FMATH_H
#define RLK_FMATH_H

#include <assert.h>
#include <math.h>

/* Clamp v between lo and hi (requires lo <= hi). */
static inline float
rlk_clampf(float v, float lo, float hi) {
    assert(lo <= hi);
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Linear interpolation between a and b at t. Unclamped; t may be outside [0,1]. */
static inline float
rlk_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/* Smoothstep interpolation weight for t in [0, 1]. Output in [0, 1]. */
static inline float
rlk_smoothstep(float t) {
    assert(t >= 0.0f && t <= 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* 2D Euclidean distance. */
static inline float
rlk_dist2f(float x, float y) {
    return sqrtf(x * x + y * y);
}

#endif /* RLK_FMATH_H */
