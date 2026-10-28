#include <assert.h>
#include "geom.h"

/* rlk_rect_contains tests ----------------------------------------------------- */

static void
test_rlk_rect_contains_inside(void) {
	rlk_rect_t r = { 2, 3, 4, 5 };
	assert(rlk_rect_contains(&r, 2, 3));    /* top-left corner */
	assert(rlk_rect_contains(&r, 5, 7));    /* last tile inside */
	assert(!rlk_rect_contains(&r, 6, 3));   /* x + w is exclusive */
	assert(!rlk_rect_contains(&r, 2, 8));   /* y + h is exclusive */
	assert(!rlk_rect_contains(&r, 1, 3));
	assert(!rlk_rect_contains(&r, 2, 2));
	assert(!rlk_rect_contains(&r, -2, -3)); /* negative coords */
}

static void
test_rlk_rect_contains_negative_origin(void) {
	rlk_rect_t r = { -4, -2, 3, 3 };
	assert(rlk_rect_contains(&r, -4, -2));
	assert(rlk_rect_contains(&r, -2, 0));
	assert(!rlk_rect_contains(&r, -1, 0));
	assert(!rlk_rect_contains(&r, -4, 1));
}

/* rlk_rect_intersect tests ---------------------------------------------------- */

static void
test_rlk_rect_intersect_overlap(void) {
	rlk_rect_t a = { 0, 0, 4, 4 }, b = { 2, 2, 4, 4 }, out;
	assert(rlk_rect_intersect(&out, &a, &b) == 1);
	assert(out.x == 2 && out.y == 2 && out.w == 2 && out.h == 2);
}

static void
test_rlk_rect_intersect_contained(void) {
	rlk_rect_t a = { 0, 0, 8, 8 }, b = { 2, 3, 2, 2 }, out;
	assert(rlk_rect_intersect(&out, &a, &b) == 1);
	assert(out.x == 2 && out.y == 3 && out.w == 2 && out.h == 2);
}

static void
test_rlk_rect_intersect_empty(void) {
	rlk_rect_t a = { 0, 0, 2, 2 }, b = { 4, 4, 2, 2 }, out = { 9, 9, 9, 9 };
	assert(rlk_rect_intersect(&out, &a, &b) == 0);
	assert(out.x == 9 && out.y == 9); /* untouched on empty intersection */
	/* touching edges share no tile */
	rlk_rect_t c = { 2, 0, 2, 2 };
	assert(rlk_rect_intersect(&out, &a, &c) == 0);
}

/* rlk_lattice_index tests ----------------------------------------------------- */

static void
test_rlk_lattice_index_inside(void) {
	rlk_lattice_t l = { .w = 4, .h = 3, .cellsize = 2,
	                    .origin_x = 10, .origin_y = 20 };
	assert(rlk_lattice_index(&l, 10, 20) == 0);         /* origin cell */
	assert(rlk_lattice_index(&l, 11, 21) == 0);         /* same cell */
	assert(rlk_lattice_index(&l, 12, 20) == 1);         /* next cell E */
	assert(rlk_lattice_index(&l, 10, 22) == 4);         /* next row */
	assert(rlk_lattice_index(&l, 17, 25) == 2 * 4 + 3); /* last cell */
}

static void
test_rlk_lattice_index_outside(void) {
	rlk_lattice_t l = { .w = 4, .h = 3, .cellsize = 2,
	                    .origin_x = 10, .origin_y = 20 };
	assert(rlk_lattice_index(&l, 9, 20) == -1);   /* before origin */
	assert(rlk_lattice_index(&l, 10, 19) == -1);
	assert(rlk_lattice_index(&l, 18, 20) == -1);  /* past origin + w*cellsize */
	assert(rlk_lattice_index(&l, 10, 26) == -1);  /* past origin + h*cellsize */
	assert(rlk_lattice_index(&l, -10, -20) == -1); /* negative coords */
}

static void
test_rlk_lattice_index_cellsize1(void) {
	rlk_lattice_t l = { .w = 3, .h = 2, .cellsize = 1,
	                    .origin_x = 0, .origin_y = 0 };
	assert(rlk_lattice_index(&l, 0, 0) == 0);
	assert(rlk_lattice_index(&l, 2, 1) == 1 * 3 + 2);
	assert(rlk_lattice_index(&l, 3, 0) == -1);
}

/* rlk_lattice_same tests ------------------------------------------------------ */

static void
test_rlk_lattice_same_equal(void) {
	rlk_lattice_t a = { .w = 4, .h = 3, .cellsize = 2,
	                    .origin_x = 10, .origin_y = 20 };
	rlk_lattice_t b = a;
	assert(rlk_lattice_same(&a, &b) == 1);
}

static void
test_rlk_lattice_same_different(void) {
	rlk_lattice_t a = { .w = 4, .h = 3, .cellsize = 2,
	                    .origin_x = 10, .origin_y = 20 };
	rlk_lattice_t b = a;
	b.w = 5;
	assert(rlk_lattice_same(&a, &b) == 0);
	b = a;
	b.cellsize = 1;
	assert(rlk_lattice_same(&a, &b) == 0);
	b = a;
	b.origin_x = 11;
	assert(rlk_lattice_same(&a, &b) == 0);
	b = a;
	b.origin_y = 21;
	assert(rlk_lattice_same(&a, &b) == 0);
}

/* Main ------------------------------------------------------------------------ */

int
main(void) {
	test_rlk_rect_contains_inside();
	test_rlk_rect_contains_negative_origin();
	test_rlk_rect_intersect_overlap();
	test_rlk_rect_intersect_contained();
	test_rlk_rect_intersect_empty();
	test_rlk_lattice_index_inside();
	test_rlk_lattice_index_outside();
	test_rlk_lattice_index_cellsize1();
	test_rlk_lattice_same_equal();
	test_rlk_lattice_same_different();
	return 0;
}
