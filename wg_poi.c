#include "wg_poi.h"

#include <assert.h>
#include <stddef.h>

#include "fmath.h"

void
rlk_wg_poi_list_init(rlk_wg_poi_list_t *list) {
    assert(list);
    list->count = 0;
}

rlk_err_t
rlk_wg_poi_add(rlk_wg_poi_list_t *list, const rlk_wg_poi_t *poi) {
    assert(list && poi);
    if (list->count >= RLK_WG_POI_MAX)
        return RLK_E_INVALID;
    list->items[(size_t)list->count] = *poi;
    list->count++;
    return RLK_OK;
}

const rlk_wg_poi_t *
rlk_wg_poi_find(const rlk_wg_poi_list_t *list,
                int wx, int wy, int radius, rlk_wg_poi_kind_t kind) {
    assert(list);
    assert(radius >= 0);

    const rlk_wg_poi_t *best = NULL;
    int best_d2 = radius * radius + 1;

    for (int i = 0; i < list->count; i++) {
        const rlk_wg_poi_t *p = &list->items[(size_t)i];
        if (p->kind != kind)
            continue;
        int dx = p->wx - wx;
        int dy = p->wy - wy;
        int d2 = dx * dx + dy * dy;
        if (d2 <= radius * radius && d2 < best_d2) {
            best = p;
            best_d2 = d2;
        }
    }
    return best;
}

int
rlk_wg_poi_count(const rlk_wg_poi_list_t *list, rlk_wg_poi_kind_t kind) {
    assert(list);
    int n = 0;
    for (int i = 0; i < list->count; i++) {
        if (list->items[(size_t)i].kind == kind)
            n++;
    }
    return n;
}
