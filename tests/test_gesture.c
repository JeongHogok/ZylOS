/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 시스템 제스처 판정(compositor/src/input/gesture_detect.c) 단위 테스트
 * 수행범위: 가장자리에서 시작한 터치만 제스처 — 아래(홈, 옆으로 다음 앱),
 *           위(알림판), 좌우 가장자리에서 안쪽으로(뒤로). 화면 안쪽의 가로·세로
 *           움직임은 앱의 것, 임계값 아래는 아무것도 아님
 * 의존방향: zyl_gesture.h, gesture_detect.c (실제 구현을 컴파일 — 복사본이 아니다)
 * SOLID: SRP — 판정 규칙만
 * ────────────────────────────────────────────────────────── */

#include "zyl_gesture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); fn(); tests_pass++; printf("PASS\n"); } while (0)

#define W 1080
#define H 1920

static const struct zyl_config CFG = {
    .swipe_threshold_px = 50, .swipe_from_bottom_px = 40, .swipe_from_top_px = 40,
    .swipe_from_side_px = 24, .statusbar_height_px = 36,
};

static enum gesture_direction swipe(double sx, double sy, double cx, double cy) {
    struct touch_state t;
    memset(&t, 0, sizeof(t));
    t.active = true;
    t.start_x = sx; t.start_y = sy;
    t.current_x = cx; t.current_y = cy;
    return gesture_detect(&t, W, H, &CFG);
}

static void test_bottom_edge_up_is_home_a_short_one_recents(void) {
    assert(swipe(540, H - 10, 540, H - 200) == GESTURE_SWIPE_UP);
    assert(swipe(540, H - 10, 600, H - 300) == GESTURE_SWIPE_UP);     /* a little sideways */
    assert(swipe(540, H - 10, 540, H - 100) == GESTURE_SWIPE_UP_SHORT);
}

static void test_along_the_bottom_is_the_next_app(void) {
    assert(swipe(300, H - 10, 600, H - 20) == GESTURE_SWIPE_RIGHT);
    assert(swipe(600, H - 10, 300, H - 20) == GESTURE_SWIPE_RIGHT);
}

static void test_top_edge_down_is_the_panel(void) {
    assert(swipe(540, 10, 540, 300) == GESTURE_SWIPE_DOWN);
    assert(swipe(540, 10, 900, 30) == GESTURE_NONE);                  /* along the top */
}

static void test_side_edge_inward_is_back(void) {
    assert(swipe(5, 900, 200, 910) == GESTURE_SWIPE_LEFT);            /* from the left */
    assert(swipe(W - 5, 900, W - 200, 910) == GESTURE_SWIPE_LEFT);    /* from the right */
    assert(swipe(5, 900, 5, 1200) == GESTURE_NONE);                   /* down the edge */
}

static void test_inside_the_screen_is_the_apps(void) {
    assert(swipe(300, 900, 800, 900) == GESTURE_NONE);                /* a carousel, not back */
    assert(swipe(800, 900, 300, 900) == GESTURE_NONE);
    assert(swipe(540, 1500, 540, 300) == GESTURE_NONE);               /* a scroll, not home */
    assert(swipe(540, 300, 540, 1500) == GESTURE_NONE);
}

static void test_below_the_threshold_is_nothing(void) {
    assert(swipe(540, H - 10, 540, H - 40) == GESTURE_NONE);         /* 30 px < 50 */
    assert(swipe(5, 900, 40, 900) == GESTURE_NONE);
}

static void test_edge_zones(void) {
    assert(gesture_at_edge(540, H - 1, W, H, &CFG));
    assert(gesture_at_edge(540, 1, W, H, &CFG));
    assert(gesture_at_edge(1, 900, W, H, &CFG));
    assert(gesture_at_edge(W - 1, 900, W, H, &CFG));
    assert(!gesture_at_edge(540, 900, W, H, &CFG));
    assert(!gesture_at_edge(30, 900, W, H, &CFG));                    /* just inside */
}

int main(void) {
    printf("Gesture judgement tests\n");
    RUN_TEST(test_bottom_edge_up_is_home_a_short_one_recents);
    RUN_TEST(test_along_the_bottom_is_the_next_app);
    RUN_TEST(test_top_edge_down_is_the_panel);
    RUN_TEST(test_side_edge_inward_is_back);
    RUN_TEST(test_inside_the_screen_is_the_apps);
    RUN_TEST(test_below_the_threshold_is_nothing);
    RUN_TEST(test_edge_zones);
    printf("\n%d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
