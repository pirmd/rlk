#ifndef RLK_ERR_H
#define RLK_ERR_H

typedef enum {
    RLK_OK,
    RLK_E_INVALID,      /* Invalid parameter or configuration */
    RLK_E_RANGE,        /* Coordinate/index out of bounds */
} rlk_err_t;

/* Static string describing the error code. */
static inline const char *
rlk_err_to_string(rlk_err_t err) {
    switch (err) {
    case RLK_OK:        return "Success";
    case RLK_E_INVALID: return "Invalid parameter or configuration";
    case RLK_E_RANGE:   return "Out of range";
    default:            return "Unknown error";
    }
}

#endif /* RLK_ERR_H */
