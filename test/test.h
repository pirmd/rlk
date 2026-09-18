#ifndef RLK_TEST_H
#define RLK_TEST_H

#include <stdio.h>
#include <stdlib.h>

/* Minimalist assert-based testing, mirroring pxl's test.h. */
#define ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

#endif /* RLK_TEST_H */
