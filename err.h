#ifndef RLK_ERR_H
#define RLK_ERR_H

/*
 * RLK error codes.
 *
 * Functions returning rlk_err_t use RLK_SUCCESS (0) on success and a
 * non-zero enum value on failure.
 */
typedef enum {
    RLK_SUCCESS,
    RLK_E_INVALID_PARAM,
    RLK_E_OUT_OF_MEM,
} rlk_err_t;

/* Return a static string describing the error code. */
static inline const char *
rlk_err_to_string(rlk_err_t err) {
    switch (err) {
    case RLK_SUCCESS:         return "Success";
    case RLK_E_INVALID_PARAM: return "Invalid parameter";
    case RLK_E_OUT_OF_MEM:    return "Out of memory";
    default:                  return "Unknown error";
    }
}

#endif /* RLK_ERR_H */
