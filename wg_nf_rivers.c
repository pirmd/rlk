#include "wg_nf_rivers.h"

#include <assert.h>
#include <math.h>

/* Water half-width for a cell draining `acc` cells: ~ sqrt(acc / acc_min) * width_min, capped. */
static inline float
river_width(const rlk_wg_nf_rivers_t *r, uint32_t acc) {
    float w = r->width_min * sqrtf((float)acc / (float)r->acc_min);
    return w < r->width_max ? w : r->width_max;
}

void
rlk_wg_nf_rivers_init(rlk_wg_nf_rivers_t *r) {
    assert(r);
    assert(r->drain && r->elev);
    assert(rlk_lattice_same(&r->drain->lattice, &r->elev->lattice));
    if (r->acc_min == 0)
        r->acc_min = 1;
    if (r->width_min <= 0.0f)
        r->width_min = 1.0f;
    if (r->width_max < r->width_min)
        r->width_max = r->width_min;
    if (r->valley_mult < 1.0f)
        r->valley_mult = 1.0f;
    /* Meanders and creeks are opt-in: disable if amplitude is zero */
    if (r->wig_tiles <= 0.0f) {
        r->f_wig_x = RLK_WG_NF_RIVERS_NONE;
        r->f_wig_y = RLK_WG_NF_RIVERS_NONE;
    }
    if (r->creek_width <= 0.0f)
        r->f_creek = RLK_WG_NF_RIVERS_NONE;
}

void
rlk_wg_nf_rivers_stage(rlk_wg_nf_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_nf_rivers_t *r = (const rlk_wg_nf_rivers_t *)data;
    const rlk_wg_nf_t *wg = ctx->wg;

    const int w = r->drain->lattice.w;
    const int h = r->drain->lattice.h;
    const float cell = (float)r->elev->lattice.cellsize;

    /* Tile centre in world coords, displaced by optional meanders */
    float px = (float)ctx->wx + 0.5f;
    float py = (float)ctx->wy + 0.5f;
    if (r->f_wig_x != RLK_WG_NF_RIVERS_NONE && r->f_wig_y != RLK_WG_NF_RIVERS_NONE) {
        px += (ctx->field[r->f_wig_x] - 0.5f) * 2.0f * r->wig_tiles;
        py += (ctx->field[r->f_wig_y] - 0.5f) * 2.0f * r->wig_tiles;
    }

    /* Grid coords of the tile centre */
    const float gx = (px - (float)r->elev->lattice.origin_x) / cell;
    const float gy = (py - (float)r->elev->lattice.origin_y) / cell;

    float mask = 0.0f;
    float carve = 0.0f;

    if (gx >= 0.0f && gx < (float)w && gy >= 0.0f && gy < (float)h) {
        const int ci = (int)gx;
        const int cj = (int)gy;

        /* Search radius: enough cells to cover the widest valley */
        const int k = (int)(r->valley_mult * r->width_max / cell) + 3;
        const int i0 = ci - k < 0 ? 0 : ci - k;
        const int i1 = ci + k >= w ? w - 1 : ci + k;
        const int j0 = cj - k < 0 ? 0 : cj - k;
        const int j1 = cj + k >= h ? h - 1 : cj + k;

        for (int j = j0; j <= j1; j++) {
            for (int i = i0; i <= i1; i++) {
                const size_t c = (size_t)j * (size_t)w + (size_t)i;
                const uint32_t acc = r->drain->acc[c];
                if (acc < r->acc_min)
                    continue;
                if (r->drain->dir[c] == RLK_DRAIN_OUTLET)
                    continue;

                /* Skip underwater cells */
                const float e = r->elev->v[c];
                if (e < r->sea)
                    continue;

                /* Segment from this cell's centre to its downstream cell's centre */
                int dx, dy;
                rlk_drain_step(r->drain->dir[c], &dx, &dy);
                const int ni = i + dx;
                const int nj = j + dy;
                if (ni < 0 || ni >= w || nj < 0 || nj >= h)
                    continue;
                const size_t nc = (size_t)nj * (size_t)w + (size_t)ni;

                const float ax = (float)r->elev->lattice.origin_x + ((float)i + 0.5f) * cell;
                const float ay = (float)r->elev->lattice.origin_y + ((float)j + 0.5f) * cell;
                const float vx = (float)dx * cell;
                const float vy = (float)dy * cell;

                /* Project (px,py) onto the segment */
                const float ex = px - ax;
                const float ey = py - ay;
                const float vv = vx * vx + vy * vy;
                float t = vv > 0.0f ? (ex * vx + ey * vy) / vv : 0.0f;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

                const float ox = ax + t * vx - px;
                const float oy = ay + t * vy - py;
                const float dist = sqrtf(ox * ox + oy * oy);

                /* Interpolated width along the segment */
                const float w0 = river_width(r, acc);
                const float w1 = river_width(r, r->drain->acc[nc]);
                const float wd = w0 + (w1 - w0) * t;
                if (wd <= 1e-3f)
                    continue;

                const float m = 1.0f - dist / wd;
                if (m > mask)
                    mask = m > 0.0f ? m : 0.0f;

                const float vall = 1.0f - dist / (r->valley_mult * wd);
                if (vall > 0.0f) {
                    const float cv = r->carve * vall * vall;
                    if (cv > carve)
                        carve = cv;
                }
            }
        }
    }

    /* Creeks: noise level-line streams on top of the drainage network */
    if (r->f_creek != RLK_WG_NF_RIVERS_NONE && r->creek_width > 0.0f) {
        const float cv = rlk_wg_nf_sample_field(wg, r->f_creek, px, py);
        const float cx = rlk_wg_nf_sample_field(wg, r->f_creek, px + 1.0f, py) - cv;
        const float cy = rlk_wg_nf_sample_field(wg, r->f_creek, px, py + 1.0f) - cv;
        float g = sqrtf(cx * cx + cy * cy);
        if (g < 0.004f) g = 0.004f;
        const float dist = fabsf(cv - 0.5f) / g;
        const float m = 1.0f - dist / r->creek_width;
        if (m > mask)
            mask = m > 0.0f ? m : 0.0f;
        const float vall = 1.0f - dist / (r->valley_mult * r->creek_width);
        if (vall > 0.0f) {
            const float cc = r->creek_carve * vall * vall;
            if (cc > carve)
                carve = cc;
        }
    }

    /* Rivers do not flow under the sea: zero the mask where the coarse
       elevation is below sea level. Uses the original tile position (not the
       wiggle-displaced one). Lake overlay below is exempt (inland depressions). */
    {
        const float e_coarse = rlk_grid_sample(r->elev, (float)ctx->wx + 0.5f, (float)ctx->wy + 0.5f);
        if (e_coarse < r->sea) {
            mask = 0.0f;
            carve = 0.0f;
        }
    }

    /* Lake overlay: bilinear sample of the lake-depth grid.
       Uses the original tile position (not the wiggle-displaced one):
       a lake is a property of the terrain, not of the meander. */
    if (r->lake_depth && r->lake_min_depth > 0.0f) {
        const float d = rlk_grid_sample(r->lake_depth, (float)ctx->wx + 0.5f, (float)ctx->wy + 0.5f);
        if (d >= r->lake_min_depth) {
            /* Smooth threshold */
            const float lm = (d - 0.5f * r->lake_min_depth) / (0.5f * r->lake_min_depth);
            const float lc = lm < 0.0f ? 0.0f : (lm > 1.0f ? 1.0f : lm);
            if (lc > mask)
                mask = lc;
        }
    }

    /* Write outputs */
    if (r->slot_mask != RLK_WG_NF_RIVERS_NONE) {
        const int idx = wg->nsampled + r->slot_mask;
        assert(idx >= 0 && idx < RLK_WG_NF_MAX_FIELDS);
        ctx->field[idx] = mask < 0.0f ? 0.0f : (mask > 1.0f ? 1.0f : mask);
    }
    if (r->slot_carve != RLK_WG_NF_RIVERS_NONE) {
        const int idx = wg->nsampled + r->slot_carve;
        assert(idx >= 0 && idx < RLK_WG_NF_MAX_FIELDS);
        ctx->field[idx] = carve;
    }
}

rlk_err_t
rlk_wg_nf_rivers_add(rlk_wg_nf_t *wg, rlk_wg_nf_rivers_t *r) {
    assert(wg && r);
    rlk_wg_nf_rivers_init(r);
    /* Validate slots against the declared derived field count */
    if (r->slot_mask != RLK_WG_NF_RIVERS_NONE && r->slot_mask >= wg->nderived)
        return RLK_E_INVALID;
    if (r->slot_carve != RLK_WG_NF_RIVERS_NONE && r->slot_carve >= wg->nderived)
        return RLK_E_INVALID;
    return rlk_wg_nf_add_stage(wg, rlk_wg_nf_rivers_stage, r);
}
