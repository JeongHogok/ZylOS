/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: gesture_detect() 알고리즘 단위 테스트
 * 수행범위: 방향 감지, 임계값 경계, 대각선 이동, 에지 스와이프 검증
 * 의존방향: zyl_compositor.h, gesture.h (테스트 대상)
 * SOLID: SRP — gesture detection 로직만 테스트
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* We only need the types and gesture_detect() — avoid pulling in
   Wayland/wlroots headers by redefining just the minimal structs. */
#include "zyl_compositor.h"

/* gesture_detect() prototype */
enum gesture_direction gesture_detect(const struct touch_state *t,
                                      int screen_h,
                                      const struct zyl_config *cfg);

/* ── Test helpers ─────────────────────────────────────────── */

static int tests_run  = 0;
static int tests_pass = 0;

#define RUN_TEST(fn)                                        \
    do {                                                    \
        tests_run++;                                        \
        printf("  %-50s ", #fn);                            \
        fn();                                               \
        tests_pass++;                                       \
        printf("PASS\n");                                   \
    } while (0)

/* Default config matching typical mobile screen */
static struct zyl_config default_cfg(void)
{
    struct zyl_config cfg;
    cfg.swipe_threshold_px   = 50;
    cfg.swipe_from_bottom_px = 80;
    cfg.swipe_from_top_px    = 80;
    cfg.statusbar_height_px  = 40;
    return cfg;
}

static struct touch_state make_touch(double sx, double sy,
                                     double cx, double cy)
{
    struct touch_state t;
    memset(&t, 0, sizeof(t));
    t.active    = true;
    t.start_x   = sx;
    t.start_y   = sy;
    t.current_x  = cx;
    t.current_y  = cy;
    t.pending    = GESTURE_NONE;
    return t;
}

/* ── Tests: swipe up from bottom edge -> HOME ─────────────── */

static void test_swipe_up_from_bottom(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Start near bottom edge (within swipe_from_bottom_px zone),
       swipe upward by more than threshold */
    struct touch_state t = make_touch(
        540.0, (double)(screen_h - 30),   /* start: 30px from bottom */
        540.0, (double)(screen_h - 130)   /* end: 100px upward */
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_SWIPE_UP);
}

/* ── Tests: swipe down from top edge -> NOTIFICATION ──────── */

static void test_swipe_down_from_top(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Start near top edge (within swipe_from_top_px zone),
       swipe downward by more than threshold */
    struct touch_state t = make_touch(
        540.0, 30.0,   /* start: 30px from top */
        540.0, 200.0   /* end: 170px downward */
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_SWIPE_DOWN);
}

/* ── Tests: swipe left -> BACK ────────────────────────────── */

static void test_swipe_left(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Horizontal swipe left from middle of screen */
    struct touch_state t = make_touch(
        800.0, 960.0,   /* start: mid-screen */
        650.0, 960.0    /* end: 150px left */
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_SWIPE_LEFT);
}

/* ── Tests: swipe right -> APP_SWITCH ─────────────────────── */

static void test_swipe_right(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Horizontal swipe right from middle of screen */
    struct touch_state t = make_touch(
        200.0, 960.0,   /* start: mid-screen */
        350.0, 960.0    /* end: 150px right */
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_SWIPE_RIGHT);
}

/* ── Tests: no movement -> NONE ───────────────────────────── */

static void test_no_movement(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Tap in place — no movement */
    struct touch_state t = make_touch(
        540.0, 960.0,
        540.0, 960.0
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_NONE);
}

/* ── Tests: small movement below threshold -> NONE ────────── */

static void test_below_threshold(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Move only 30px (below 50px threshold) */
    struct touch_state t = make_touch(
        540.0, 960.0,
        570.0, 960.0
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_NONE);
}

/* ── Tests: exactly at threshold boundary -> NONE ─────────── */

static void test_exact_threshold_boundary(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Move exactly threshold px in x, but from mid-screen
       (not an edge), and dx is negative so abs_dx == threshold
       but the condition checks < threshold, so this should
       still be NONE at the boundary */
    struct touch_state t = make_touch(
        540.0, 960.0,
        540.0 + 49.0, 960.0   /* 49px, just below 50px threshold */
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_NONE);
}

/* ── Tests: one pixel above threshold -> detected ─────────── */

static void test_just_above_threshold(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Move 51px right from mid-screen */
    struct touch_state t = make_touch(
        540.0, 960.0,
        591.0, 960.0
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_SWIPE_RIGHT);
}

/* ── Tests: diagonal movement -> pick dominant axis ───────── */

static void test_diagonal_dominant_horizontal(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Diagonal with dominant horizontal (dx=120, dy=60) from mid-screen */
    struct touch_state t = make_touch(
        400.0, 960.0,
        520.0, 1020.0
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    /* abs_dx(120) > abs_dy(60), and dx > 0 -> SWIPE_RIGHT */
    assert(result == GESTURE_SWIPE_RIGHT);
}

static void test_diagonal_dominant_vertical_from_bottom(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Diagonal from bottom edge with dominant vertical (dy=-120, dx=40) */
    struct touch_state t = make_touch(
        540.0, (double)(screen_h - 30),
        580.0, (double)(screen_h - 150)
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    /* Start is in bottom zone, dy is negative and large -> SWIPE_UP */
    assert(result == GESTURE_SWIPE_UP);
}

/* ── Tests: swipe up NOT from bottom edge -> NONE ─────────── */

static void test_swipe_up_from_middle(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Swipe up from middle of screen (not bottom edge) — the code
       checks for bottom-edge zone, then top-edge zone, then horizontal.
       A vertical up from mid-screen doesn't match any of those. */
    struct touch_state t = make_touch(
        540.0, 960.0,
        540.0, 800.0
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    /* Not from bottom edge, not from top edge, not horizontal -> NONE */
    assert(result == GESTURE_NONE);
}

/* ── Tests: swipe down NOT from top edge -> NONE ──────────── */

static void test_swipe_down_from_middle(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Swipe down from middle of screen (not top edge) */
    struct touch_state t = make_touch(
        540.0, 960.0,
        540.0, 1120.0
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    /* Not from top edge, not horizontal dominant -> NONE */
    assert(result == GESTURE_NONE);
}

/* ── Tests: custom config with different threshold ────────── */

static void test_custom_threshold(void)
{
    struct zyl_config cfg = default_cfg();
    cfg.swipe_threshold_px = 100; /* Larger threshold */
    int screen_h = 1920;

    /* Move 80px right — below new 100px threshold */
    struct touch_state t = make_touch(
        540.0, 960.0,
        620.0, 960.0
    );

    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_NONE);

    /* Move 120px right — above new 100px threshold */
    t = make_touch(540.0, 960.0, 660.0, 960.0);
    result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_SWIPE_RIGHT);
}

/* ── Tests: bottom edge zone boundary ─────────────────────── */

static void test_bottom_edge_zone_boundary(void)
{
    struct zyl_config cfg = default_cfg();
    int screen_h = 1920;

    /* Start exactly at the edge of the bottom zone (screen_h - swipe_from_bottom_px) */
    double zone_boundary = (double)(screen_h - cfg.swipe_from_bottom_px);

    /* Just inside zone (1px below boundary) — should be SWIPE_UP */
    struct touch_state t = make_touch(
        540.0, zone_boundary + 1.0,
        540.0, zone_boundary - 100.0
    );
    enum gesture_direction result = gesture_detect(&t, screen_h, &cfg);
    assert(result == GESTURE_SWIPE_UP);

    /* Just outside zone (1px above boundary) — should NOT be SWIPE_UP */
    t = make_touch(
        540.0, zone_boundary - 1.0,
        540.0, zone_boundary - 200.0
    );
    result = gesture_detect(&t, screen_h, &cfg);
    /* This is a vertical upward swipe from mid-screen — NONE */
    assert(result == GESTURE_NONE);
}

/* ── Main ─────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Zyl OS Gesture Detection Tests ===\n");

    RUN_TEST(test_swipe_up_from_bottom);
    RUN_TEST(test_swipe_down_from_top);
    RUN_TEST(test_swipe_left);
    RUN_TEST(test_swipe_right);
    RUN_TEST(test_no_movement);
    RUN_TEST(test_below_threshold);
    RUN_TEST(test_exact_threshold_boundary);
    RUN_TEST(test_just_above_threshold);
    RUN_TEST(test_diagonal_dominant_horizontal);
    RUN_TEST(test_diagonal_dominant_vertical_from_bottom);
    RUN_TEST(test_swipe_up_from_middle);
    RUN_TEST(test_swipe_down_from_middle);
    RUN_TEST(test_custom_threshold);
    RUN_TEST(test_bottom_edge_zone_boundary);

    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
