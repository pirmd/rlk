#ifndef RL_MATH_H
#define RL_MATH_H

#include <stdint.h>

/*
 * Tiny math helpers, allocation-free and <math.h>-free (keeps the rlk core
 * free of a -lm link dependency). Standard C99 only.
 */

/* floorf without <math.h>. Correct for negatives (truncates toward -inf). */
static inline float
rlk_floorf(float v) {
    int32_t i = (int32_t)v;
    return (float)((v < 0.0f && (float)i != v) ? i - 1 : i);
}

/* Clamp v to [lo, hi]. lo <= hi must hold. */
static inline float
rlk_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

#endif /* RL_MATH_H */
