#ifndef RLK_ENTITY_H
#define RLK_ENTITY_H

/* Generic entities: a fixed-capacity, caller-owned list of positioned
 * records. The index in the list is the stable entity id (never reordered
 * nor compacted). No behaviour, no allocation: the game drives movement
 * through the tilemap (see rlk_tilemap_get for bounds). */

#include <assert.h>
#include <stddef.h>

#include "tilemap.h"

typedef struct {
    int x, y;   /* world position, in tiles */
} rlk_entity_t;

typedef struct {
    rlk_entity_t *ents;
    int n, cap;
} rlk_entity_list_t;

/* Append an entity at (x, y). Returns the id of the new entity, or -1 if
 * the list is full. */
static inline int
rlk_entity_push(rlk_entity_list_t *l, int x, int y) {
    assert(l);
    if (l->n >= l->cap)
        return -1;
    l->ents[(size_t)l->n] = (rlk_entity_t){ .x = x, .y = y };
    return l->n++;
}

/* Entity by id, NULL if out of range. */
static inline rlk_entity_t *
rlk_entity_get(rlk_entity_list_t *l, int id) {
    assert(l);
    if (id < 0 || id >= l->n)
        return NULL;
    return &l->ents[(size_t)id];
}

/* Move entity id by (dx, dy). Atomic all-or-nothing: returns 1 if moved,
 * 0 if the target tile is outside the tilemap (blocked, position kept). */
static inline int
rlk_entity_move(const rlk_tilemap_t *tm, rlk_entity_list_t *l, int id,
                int dx, int dy) {
    assert(tm && l);
    rlk_entity_t *e = rlk_entity_get(l, id);
    assert(e);
    if (!rlk_tilemap_get(tm, e->x + dx, e->y + dy))
        return 0;
    e->x += dx;
    e->y += dy;
    return 1;
}

#endif /* RLK_ENTITY_H */
