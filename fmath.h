#ifndef RLK_FMATH_H
#define RLK_FMATH_H

/* Small float helpers: clamp, lerp, smoothstep, distances. */

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

/* Euclidean distance between two 2D points. */
static inline float
rlk_dist_euclideanf(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrtf(dx * dx + dy * dy);
}

/* Manhattan distance between two integer points. */
static inline int
rlk_dist_manhattan(int x1, int y1, int x2, int y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
}

#endif /* RLK_FMATH_H */
