#include <assert.h>
#include <stdio.h>

#include "entity.h"

#define CAP 4
#define MAP_W 8
#define MAP_H 8

int
main(void) {
    rlk_entity_t ents[CAP];
    rlk_entity_list_t l = { .ents = ents, .n = 0, .cap = CAP };

    rlk_tile_t tiles[MAP_W * MAP_H];
    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP_W, MAP_H, tiles, sizeof(tiles));

    /* push until full */
    assert(rlk_entity_push(&l, 3, 4) == 0);
    assert(rlk_entity_push(&l, 0, 0) == 1);
    assert(rlk_entity_push(&l, MAP_W - 1, MAP_H - 1) == 2);
    assert(rlk_entity_push(&l, 1, 1) == 3);
    assert(rlk_entity_push(&l, 2, 2) == -1);   /* full */
    assert(l.n == CAP);

    /* get out of range */
    assert(rlk_entity_get(&l, -1) == NULL);
    assert(rlk_entity_get(&l, CAP) == NULL);
    assert(rlk_entity_get(&l, 0)->x == 3 && rlk_entity_get(&l, 0)->y == 4);

    /* move inside: ok */
    assert(rlk_entity_move(&tm, &l, 0, 1, 0) == 1);
    assert(rlk_entity_get(&l, 0)->x == 4 && rlk_entity_get(&l, 0)->y == 4);

    /* move outside the tilemap: blocked, position unchanged */
    assert(rlk_entity_move(&tm, &l, 1, -1, 0) == 0);   /* (0,0) -> (-1,0) */
    assert(rlk_entity_get(&l, 1)->x == 0 && rlk_entity_get(&l, 1)->y == 0);
    assert(rlk_entity_move(&tm, &l, 2, 1, 0) == 0);    /* corner -> outside */
    assert(rlk_entity_get(&l, 2)->x == MAP_W - 1);

    printf("test_entity OK\n");
    return 0;
}
