#include <assert.h>
#include <string.h>
#include "err.h"

static void
test_rlk_err_to_string_ok(void) {
    assert(strcmp(rlk_err_to_string(RLK_OK), "Success") == 0);
}

static void
test_rlk_err_to_string_invalid(void) {
    assert(strcmp(rlk_err_to_string(RLK_E_INVALID),
                  "Invalid parameter or configuration") == 0);
}

static void
test_rlk_err_to_string_range(void) {
    assert(strcmp(rlk_err_to_string(RLK_E_RANGE), "Out of range") == 0);
}

static void
test_rlk_err_to_string_unknown(void) {
    assert(strcmp(rlk_err_to_string((rlk_err_t)999), "Unknown error") == 0);
}

int
main(void) {
    test_rlk_err_to_string_ok();
    test_rlk_err_to_string_invalid();
    test_rlk_err_to_string_range();
    test_rlk_err_to_string_unknown();
    return 0;
}
