/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 셸 정책(runtime/wam/src/shell) 단위 테스트
 * 수행범위: 첫 설정 중·잠금 중에 앞에 올 수 있는 앱, 홈·최근 앱·닫기,
 *           잠금 해제와 설정 완료는 정해진 앱만, 화면 켜짐
 * 의존방향: shell.c, glib
 * SOLID: SRP — apps/system/shell.js 와 같은 규칙인지 검증
 *        (tests/test_services.js 의 "ZylShell" 과 같은 시나리오)
 * ────────────────────────────────────────────────────────── */

#include "../runtime/wam/src/shell/shell.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); fn(); tests_pass++; printf("PASS\n"); } while (0)

#define HOME  ZYL_SHELL_HOME
#define LOCK  ZYL_SHELL_LOCK
#define SETUP ZYL_SHELL_SETUP

static gboolean same(const char *a, const char *b) {
    return (!a && !b) || (a && b && strcmp(a, b) == 0);
}
#define EQ(got, want) do { const char *g_ = (got); if (!same(g_, (want))) { \
    printf("\n    %s: want %s got %s\n", #got, (want) ? (want) : "NULL", g_ ? g_ : "NULL"); assert(0); } } while (0)

/* Show what the policy said, as the WAM does */
static void show(ZylShell *s, const char *app) { if (app) zyl_shell_shown(s, app); }

static void test_setup_holds_the_device(void) {
    ZylShell s;
    EQ(zyl_shell_boot(&s, FALSE), SETUP);
    show(&s, SETUP);
    assert(zyl_shell_may_launch(&s, SETUP));
    assert(zyl_shell_may_launch(&s, "com.zylos.phone"));       /* an emergency call */
    assert(!zyl_shell_may_launch(&s, HOME));
    assert(!zyl_shell_may_launch(&s, LOCK));
    assert(!zyl_shell_may_launch(&s, "com.zylos.settings"));
    assert(!zyl_shell_may_launch(&s, NULL));
    EQ(zyl_shell_home(&s), NULL);
    assert(!zyl_shell_navigable(&s));
    assert(!zyl_shell_may_close(&s, SETUP));
    EQ(zyl_shell_lock(&s), NULL);                              /* setup never locks */
    EQ(zyl_shell_unlock(&s, LOCK, NULL), NULL);
    show(&s, HOME);
    EQ(zyl_shell_screen_on(&s), SETUP);                         /* back to setup */
}

static void test_only_setup_finishes_setup(void) {
    ZylShell s;
    show(&s, zyl_shell_boot(&s, FALSE));
    EQ(zyl_shell_setup_done(&s, "com.example.app"), NULL);
    EQ(zyl_shell_setup_done(&s, LOCK), NULL);
    EQ(zyl_shell_setup_done(&s, SETUP), HOME);
    show(&s, HOME);
    assert(zyl_shell_navigable(&s));                           /* just set up: not locked */
    assert(zyl_shell_may_launch(&s, "com.zylos.settings"));
    assert(!zyl_shell_may_launch(&s, SETUP));                  /* not again */
    EQ(zyl_shell_setup_done(&s, SETUP), NULL);
}

static void test_locked_shows_only_the_lock_screen(void) {
    ZylShell s;
    EQ(zyl_shell_boot(&s, TRUE), LOCK);
    show(&s, LOCK);
    assert(zyl_shell_may_launch(&s, LOCK));
    assert(zyl_shell_may_launch(&s, "com.zylos.camera"));      /* the shortcut */
    assert(zyl_shell_may_launch(&s, "com.zylos.alerts"));
    assert(!zyl_shell_may_launch(&s, HOME));
    assert(!zyl_shell_may_launch(&s, "com.zylos.gallery"));    /* not from the camera either */
    assert(!zyl_shell_may_launch(&s, SETUP));
    assert(!zyl_shell_navigable(&s));
    assert(!zyl_shell_may_close(&s, LOCK));
    EQ(zyl_shell_home(&s), NULL);                              /* already there */
    show(&s, "com.zylos.camera");
    EQ(zyl_shell_home(&s), LOCK);                              /* leaving it: the lock screen */
    EQ(zyl_shell_screen_on(&s), NULL);                         /* a call/alert/camera stays */
}

static void test_only_the_lock_screen_unlocks(void) {
    ZylShell s;
    show(&s, zyl_shell_boot(&s, TRUE));
    EQ(zyl_shell_unlock(&s, "com.zylos.camera", NULL), NULL);
    EQ(zyl_shell_unlock(&s, "com.example.app", NULL), NULL);
    EQ(zyl_shell_unlock(&s, NULL, NULL), NULL);
    assert(!zyl_shell_navigable(&s));
    EQ(zyl_shell_unlock(&s, LOCK, "com.zylos.messages"), "com.zylos.messages");
    assert(zyl_shell_navigable(&s));
    EQ(zyl_shell_unlock(&s, LOCK, NULL), NULL);                /* not locked */
}

static void test_unlock_opens_home_unless_an_app_may_open(void) {
    ZylShell s;
    show(&s, zyl_shell_boot(&s, TRUE));
    EQ(zyl_shell_unlock(&s, LOCK, NULL), HOME);
    show(&s, zyl_shell_lock(&s));
    EQ(zyl_shell_unlock(&s, LOCK, ""), HOME);
    show(&s, zyl_shell_lock(&s));
    EQ(zyl_shell_unlock(&s, LOCK, SETUP), HOME);               /* never setup again */
    show(&s, zyl_shell_lock(&s));
    EQ(zyl_shell_unlock(&s, LOCK, LOCK), HOME);
}

static void test_unlocked_home_close_and_lock(void) {
    ZylShell s;
    show(&s, zyl_shell_boot(&s, TRUE));
    show(&s, zyl_shell_unlock(&s, LOCK, NULL));
    EQ(zyl_shell_home(&s), NULL);                              /* already home */
    show(&s, "com.zylos.notes");
    EQ(zyl_shell_home(&s), HOME);
    assert(!zyl_shell_may_launch(&s, LOCK));                   /* only by locking */
    assert(!zyl_shell_may_close(&s, HOME));
    assert(zyl_shell_may_close(&s, LOCK));
    assert(zyl_shell_may_close(&s, SETUP));
    assert(zyl_shell_may_close(&s, "com.zylos.notes"));
    EQ(zyl_shell_screen_on(&s), NULL);                         /* unlocked: nothing to cover */
    EQ(zyl_shell_lock(&s), LOCK);
    assert(!zyl_shell_navigable(&s));
    EQ(zyl_shell_screen_on(&s), LOCK);                         /* not shown yet: cover it */
    show(&s, LOCK);
    EQ(zyl_shell_lock(&s), NULL);                              /* already in front */
    EQ(zyl_shell_screen_on(&s), NULL);
}

/* The system UI's surface: only it asks; the whole screen only when the user
   may move between apps, a banner not over the lock screen or setup */
static void test_the_panel_follows_the_device_state(void) {
    ZylShell s;
    const char *UI = ZYL_SHELL_SYSTEM_UI;
    show(&s, zyl_shell_boot(&s, FALSE));
    assert(zyl_shell_panel_mode(&s, "com.example.app", "full") == ZYL_PANEL_NONE);
    assert(zyl_shell_panel_mode(&s, SETUP, "bar") == ZYL_PANEL_NONE);
    assert(zyl_shell_panel_mode(&s, UI, "full") == ZYL_PANEL_BAR);     /* setup */
    assert(zyl_shell_panel_mode(&s, UI, "banner") == ZYL_PANEL_BAR);
    show(&s, zyl_shell_setup_done(&s, SETUP));
    assert(zyl_shell_panel_mode(&s, UI, "full") == ZYL_PANEL_FULL);
    assert(zyl_shell_panel_mode(&s, UI, "banner") == ZYL_PANEL_BANNER);
    assert(zyl_shell_panel_mode(&s, UI, "bar") == ZYL_PANEL_BAR);
    assert(zyl_shell_panel_mode(&s, UI, "anything") == ZYL_PANEL_BAR);
    show(&s, zyl_shell_lock(&s));
    assert(zyl_shell_panel_mode(&s, UI, "full") == ZYL_PANEL_BAR);     /* locked */
    assert(zyl_shell_panel_mode(&s, UI, "banner") == ZYL_PANEL_BAR);
    assert(!zyl_shell_may_launch(&s, UI) && !zyl_shell_may_close(&s, UI));   /* a surface */
    show(&s, zyl_shell_unlock(&s, LOCK, NULL));
    assert(!zyl_shell_may_launch(&s, UI) && !zyl_shell_may_close(&s, UI));
}

int main(void) {
    printf("Shell policy tests\n");
    RUN_TEST(test_setup_holds_the_device);
    RUN_TEST(test_only_setup_finishes_setup);
    RUN_TEST(test_locked_shows_only_the_lock_screen);
    RUN_TEST(test_only_the_lock_screen_unlocks);
    RUN_TEST(test_unlock_opens_home_unless_an_app_may_open);
    RUN_TEST(test_unlocked_home_close_and_lock);
    RUN_TEST(test_the_panel_follows_the_device_state);
    printf("\n%d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
