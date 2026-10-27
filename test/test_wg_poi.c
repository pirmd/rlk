#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "wg_poi.h"

/* list_init: count = 0. */
static void
test_list_init(void) {
    rlk_wg_poi_list_t list;
    memset(&list, 0xFF, sizeof(list));
    rlk_wg_poi_list_init(&list);
    assert(list.count == 0);
}

/* push: append POI, count increments. */
static void
test_push_basic(void) {
    rlk_wg_poi_list_t list;
    rlk_wg_poi_list_init(&list);

    rlk_wg_poi_t poi = {
        .wx = 10, .wy = 20,
        .kind = RLK_WG_POI_CAVE,
        .seed = 12345ULL,
        .size = 48,
        .flags = RLK_WG_POI_NEAR_RIVER,
    };
    assert(rlk_wg_poi_push(&list, &poi) == RLK_OK);
    assert(list.count == 1);
    assert(list.items[0].wx == 10);
    assert(list.items[0].wy == 20);
    assert(list.items[0].kind == RLK_WG_POI_CAVE);
    assert(list.items[0].seed == 12345ULL);
    assert(list.items[0].size == 48);
    assert(list.items[0].flags == RLK_WG_POI_NEAR_RIVER);
}

/* push: full list returns RLK_E_INVALID. */
static void
test_push_full(void) {
    rlk_wg_poi_list_t list;
    rlk_wg_poi_list_init(&list);

    rlk_wg_poi_t poi = { .wx = 0, .wy = 0, .kind = RLK_WG_POI_CAVE };
    for (int i = 0; i < RLK_WG_POI_MAX; i++)
        assert(rlk_wg_poi_push(&list, &poi) == RLK_OK);
    assert(list.count == RLK_WG_POI_MAX);
    assert(rlk_wg_poi_push(&list, &poi) == RLK_E_INVALID);
}

/* find: nearest POI of given kind within radius. */
static void
test_find_nearest(void) {
    rlk_wg_poi_list_t list;
    rlk_wg_poi_list_init(&list);

    rlk_wg_poi_t p1 = { .wx = 10, .wy = 10, .kind = RLK_WG_POI_CAVE };
    rlk_wg_poi_t p2 = { .wx = 15, .wy = 15, .kind = RLK_WG_POI_CAVE };
    rlk_wg_poi_t p3 = { .wx = 12, .wy = 12, .kind = RLK_WG_POI_DUNGEON };
    rlk_wg_poi_push(&list, &p1);
    rlk_wg_poi_push(&list, &p2);
    rlk_wg_poi_push(&list, &p3);

    /* Near (11, 11): closest CAVE is p1 at distance sqrt(2). */
    const rlk_wg_poi_t *found = rlk_wg_poi_find(&list, 11, 11, 10, RLK_WG_POI_CAVE);
    assert(found);
    assert(found->wx == 10 && found->wy == 10);

    /* Near (14, 14): closest CAVE is p2 at distance sqrt(2). */
    found = rlk_wg_poi_find(&list, 14, 14, 10, RLK_WG_POI_CAVE);
    assert(found);
    assert(found->wx == 15 && found->wy == 15);

    /* DUNGEON near (12, 12): finds p3. */
    found = rlk_wg_poi_find(&list, 12, 12, 5, RLK_WG_POI_DUNGEON);
    assert(found);
    assert(found->wx == 12 && found->wy == 12);
}

/* find: no POI within radius returns NULL. */
static void
test_find_none(void) {
    rlk_wg_poi_list_t list;
    rlk_wg_poi_list_init(&list);

    rlk_wg_poi_t p = { .wx = 100, .wy = 100, .kind = RLK_WG_POI_CAVE };
    rlk_wg_poi_push(&list, &p);

    assert(rlk_wg_poi_find(&list, 0, 0, 10, RLK_WG_POI_CAVE) == NULL);
    assert(rlk_wg_poi_find(&list, 100, 100, 0, RLK_WG_POI_DUNGEON) == NULL);
}

/* count: count POIs by kind. */
static void
test_count_by_kind(void) {
    rlk_wg_poi_list_t list;
    rlk_wg_poi_list_init(&list);

    rlk_wg_poi_t cave = { .wx = 0, .wy = 0, .kind = RLK_WG_POI_CAVE };
    rlk_wg_poi_t dunj = { .wx = 1, .wy = 1, .kind = RLK_WG_POI_DUNGEON };
    rlk_wg_poi_push(&list, &cave);
    rlk_wg_poi_push(&list, &dunj);
    rlk_wg_poi_push(&list, &cave);

    assert(rlk_wg_poi_count(&list, RLK_WG_POI_CAVE) == 2);
    assert(rlk_wg_poi_count(&list, RLK_WG_POI_DUNGEON) == 1);
    assert(rlk_wg_poi_count(&list, RLK_WG_POI_VILLAGE) == 0);
}

/* seed: deterministic for same inputs. */
static void
test_seed_deterministic(void) {
    uint64_t s1 = rlk_wg_poi_seed(42ULL, 10, 20, RLK_WG_POI_CAVE, 0xCAFE);
    uint64_t s2 = rlk_wg_poi_seed(42ULL, 10, 20, RLK_WG_POI_CAVE, 0xCAFE);
    assert(s1 == s2);

    /* Different position -> different seed. */
    uint64_t s3 = rlk_wg_poi_seed(42ULL, 11, 20, RLK_WG_POI_CAVE, 0xCAFE);
    assert(s1 != s3);

    /* Different kind -> different seed. */
    uint64_t s4 = rlk_wg_poi_seed(42ULL, 10, 20, RLK_WG_POI_DUNGEON, 0xCAFE);
    assert(s1 != s4);
}

/* has_flag: check context flags. */
static void
test_has_flag(void) {
    rlk_wg_poi_t poi = {
        .wx = 0, .wy = 0,
        .kind = RLK_WG_POI_CAVE,
        .flags = RLK_WG_POI_NEAR_RIVER | RLK_WG_POI_ON_HILL,
    };
    assert(rlk_wg_poi_has_flag(&poi, RLK_WG_POI_NEAR_RIVER));
    assert(rlk_wg_poi_has_flag(&poi, RLK_WG_POI_ON_HILL));
    assert(!rlk_wg_poi_has_flag(&poi, RLK_WG_POI_IN_FOREST));
    assert(!rlk_wg_poi_has_flag(&poi, RLK_WG_POI_COASTAL));
}

int
main(void) {
    test_list_init();
    test_push_basic();
    test_push_full();
    test_find_nearest();
    test_find_none();
    test_count_by_kind();
    test_seed_deterministic();
    test_has_flag();
    return 0;
}
