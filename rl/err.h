#ifndef RL_ERR_H
#define RL_ERR_H

/*
 * rlk error codes. Every init/generate/process entry point returns rlk_err_t
 * (not bool): multiple failure causes are distinguished. See docs/ALLOCATION.md.
 */
typedef enum {
    RLK_OK             =  0,
    RLK_E_BUFSIZE      = -1,   /* provided buffer smaller than _required_size */
    RLK_E_CONFIG       = -2,   /* invalid configuration */
    RLK_E_CAPACITY     = -3,   /* output capacity insufficient (Pattern B) */
    RLK_E_SCRATCH      = -4,   /* scratch buffer too small (Pattern C) */
    RLK_E_RANGE        = -5,   /* coordinate/index out of bounds */
} rlk_err_t;

/* Static string describing the error code. */
static inline const char *
rlk_err_to_string(rlk_err_t err) {
    switch (err) {
    case RLK_OK:        return "Success";
    case RLK_E_BUFSIZE: return "Buffer too small";
    case RLK_E_CONFIG:  return "Invalid configuration";
    case RLK_E_CAPACITY:return "Output capacity insufficient";
    case RLK_E_SCRATCH: return "Scratch buffer too small";
    case RLK_E_RANGE:   return "Out of range";
    default:            return "Unknown error";
    }
}

#endif /* RL_ERR_H */
