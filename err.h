#ifndef RLK_ERR_H
#define RLK_ERR_H

typedef enum {
    RLK_OK,
    RLK_E_INVALID,      /* Invalid parameter or configuration */
    RLK_E_NOTFOUND,     /* No result (e.g. no path found) */
} rlk_err_t;

/* Static string describing the error code. */
static inline const char *
rlk_err_to_string(rlk_err_t err) {
    switch (err) {
    case RLK_OK:         return "Success";
    case RLK_E_INVALID:  return "Invalid parameter or configuration";
    case RLK_E_NOTFOUND: return "Not found";
    default:             return "Unknown error";
    }
}

#endif /* RLK_ERR_H */
