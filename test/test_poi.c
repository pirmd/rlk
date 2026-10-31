#include <assert.h>
#include <stdint.h>

#include "poi.h"

/* Game-defined POI types (the library defines none). */
#define POI_A 1u
#define POI_B 2u

static rlk_poi_t g_buf[8];
static rlk_poi_list_t g_list;

static void
fixture(void) {
    g_list = (rlk_poi_list_t){ g_buf, 0, 8 };
}

/* push: append POI, n increments; bbox contains the anchor. */

static void
test_push_basic(void) {
    fixture();
    rlk_poi_t p = {
        .x = 10, .y = 20,
        .bbox = { 2, 12, 16, 16 },
        .type = POI_A,
        .seed = 12345ULL,
        .gen = 0,
        .level = 0,
    };
    assert(rlk_poi_push(&g_list, &p) == RLK_OK);
    assert(g_list.n == 1);
    assert(g_buf[0].x == 10);
    assert(g_buf[0].y == 20);
    assert(g_buf[0].bbox.x == 2 && g_buf[0].bbox.y == 12);
    assert(g_buf[0].bbox.w == 16 && g_buf[0].bbox.h == 16);
    assert(g_buf[0].type == POI_A);
    assert(g_buf[0].seed == 12345ULL);
    assert(g_buf[0].bbox.x <= g_buf[0].x && g_buf[0].x < g_buf[0].bbox.x + g_buf[0].bbox.w);
    assert(g_buf[0].bbox.y <= g_buf[0].y && g_buf[0].y < g_buf[0].bbox.y + g_buf[0].bbox.h);
}

/* push: full list returns RLK_E_INVALID. */

static void
test_push_full(void) {
    fixture();
    rlk_poi_t p = { .x = 0, .y = 0, .type = POI_A };
    for (int i = 0; i < 8; i++)
        assert(rlk_poi_push(&g_list, &p) == RLK_OK);
    assert(g_list.n == 8);
    assert(rlk_poi_push(&g_list, &p) == RLK_E_INVALID);
    assert(g_list.n == 8);
}

/* find: nearest POI of a given type within radius. */

static void
test_find(void) {
    fixture();
    rlk_poi_t a = { .x = 10, .y = 10, .type = POI_A };
    rlk_poi_t b = { .x = 12, .y = 10, .type = POI_A };
    rlk_poi_t c = { .x = 11, .y = 10, .type = POI_B };
    assert(rlk_poi_push(&g_list, &a) == RLK_OK);
    assert(rlk_poi_push(&g_list, &b) == RLK_OK);
    assert(rlk_poi_push(&g_list, &c) == RLK_OK);

    const rlk_poi_t *f = rlk_poi_find(&g_list, 12, 10, 4, POI_A);
    assert(f != NULL && f->x == 12);
    f = rlk_poi_find(&g_list, 10, 10, 0, POI_A);
    assert(f != NULL && f->x == 10);
    /* type filter */
    f = rlk_poi_find(&g_list, 11, 10, 4, POI_B);
    assert(f != NULL && f->x == 11);
    /* out of radius */
    assert(rlk_poi_find(&g_list, 0, 0, 4, POI_A) == NULL);
    /* no such type */
    assert(rlk_poi_find(&g_list, 11, 10, 100, 99u) == NULL);
}

/* poi_at: first POI (emission order) whose bbox contains the point. */

static void
test_poi_at(void) {
    fixture();
    rlk_poi_t a = { .x = 10, .y = 10, .bbox = { 5, 5, 10, 10 }, .type = POI_A };
    rlk_poi_t b = { .x = 12, .y = 12, .bbox = { 8, 8, 10, 10 }, .type = POI_B };
    rlk_poi_push(&g_list, &a);
    rlk_poi_push(&g_list, &b);
    /* Outside all bboxes. */
    assert(rlk_poi_at(&g_list, 0, 0) == NULL);
    /* Inside a only. */
    assert(rlk_poi_at(&g_list, 6, 6) == &g_list.pois[0]);
    /* Inside both (overlap): first in emission order wins. */
    assert(rlk_poi_at(&g_list, 10, 10) == &g_list.pois[0]);
    /* Inside b only. */
    assert(rlk_poi_at(&g_list, 17, 17) == &g_list.pois[1]);
    /* Empty list. */
    fixture();
    assert(rlk_poi_at(&g_list, 10, 10) == NULL);
}

/* seed: deterministic, decorrelated by seed/type/position/salt. */

static void
test_seed(void) {
    uint64_t s1 = rlk_poi_seed(42ULL, 3, 7, POI_A, 0xCAFEULL);
    uint64_t s2 = rlk_poi_seed(42ULL, 3, 7, POI_A, 0xCAFEULL);
    assert(s1 == s2);
    assert(s1 != rlk_poi_seed(42ULL, 3, 7, POI_B, 0xCAFEULL));
    assert(s1 != rlk_poi_seed(42ULL, 3, 8, POI_A, 0xCAFEULL));
    assert(s1 != rlk_poi_seed(42ULL, 3, 7, POI_A, 0xD0D0ULL));
    assert(s1 != rlk_poi_seed(43ULL, 3, 7, POI_A, 0xCAFEULL));
}

/* Main */

int
main(void) {
    test_push_basic();
    test_push_full();
    test_find();
    test_poi_at();
    test_seed();
    return 0;
}
