/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Policy
 *
 * 역할: 셸 정책 구현 — 설정·잠금 상태와 앞에 올 수 있는 앱
 * 수행범위: shell.h 의 규칙. 상태 기계만 있고 창·D-Bus 는 모른다
 * 의존방향: shell.h → glib
 * SOLID: SRP — apps/system/shell.js 와 같은 규칙 (tests/test_shell.c,
 *        tests/test_services.js 가 같은 시나리오로 검증)
 * ────────────────────────────────────────────────────────── */

#include "shell.h"

#include <string.h>

/* Shown over the lock screen and setup: an incoming or emergency call, an
   emergency alert, the lock screen's camera shortcut */
static const char *const OVER_LOCK[] = {
    "com.zylos.phone", "com.zylos.alerts", "com.zylos.camera", NULL
};

static gboolean is(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

static gboolean over_lock(const char *app_id) {
    for (int i = 0; OVER_LOCK[i]; i++)
        if (is(OVER_LOCK[i], app_id)) return TRUE;
    return FALSE;
}

/* What to bring to the front, or NULL when it is already there */
static const char *unless(const ZylShell *s, const char *app_id) {
    return is(s->top, app_id) ? NULL : app_id;
}

const char *zyl_shell_boot(ZylShell *s, gboolean setup_done) {
    s->setup = !setup_done;
    s->locked = setup_done ? TRUE : FALSE;
    s->top[0] = '\0';
    return s->setup ? ZYL_SHELL_SETUP : ZYL_SHELL_LOCK;
}

gboolean zyl_shell_may_launch(const ZylShell *s, const char *app_id) {
    if (!app_id || is(app_id, ZYL_SHELL_SYSTEM_UI)) return FALSE;   /* a surface, never in front */
    if (s->setup) return is(app_id, ZYL_SHELL_SETUP) || over_lock(app_id);
    if (s->locked) return is(app_id, ZYL_SHELL_LOCK) || over_lock(app_id);
    return !is(app_id, ZYL_SHELL_LOCK) && !is(app_id, ZYL_SHELL_SETUP);
}

const char *zyl_shell_home(const ZylShell *s) {
    if (s->setup) return NULL;
    return unless(s, s->locked ? ZYL_SHELL_LOCK : ZYL_SHELL_HOME);
}

gboolean zyl_shell_navigable(const ZylShell *s) { return !s->setup && !s->locked; }

gboolean zyl_shell_may_close(const ZylShell *s, const char *app_id) {
    if (is(app_id, ZYL_SHELL_HOME) || is(app_id, ZYL_SHELL_SYSTEM_UI)) return FALSE;
    if (is(app_id, ZYL_SHELL_LOCK)) return !s->locked;
    if (is(app_id, ZYL_SHELL_SETUP)) return !s->setup;
    return TRUE;
}

const char *zyl_shell_lock(ZylShell *s) {
    if (s->setup) return NULL;
    s->locked = TRUE;
    return unless(s, ZYL_SHELL_LOCK);
}

const char *zyl_shell_screen_on(const ZylShell *s) {
    if (over_lock(s->top)) return NULL;
    if (s->setup) return unless(s, ZYL_SHELL_SETUP);
    return s->locked ? unless(s, ZYL_SHELL_LOCK) : NULL;
}

const char *zyl_shell_unlock(ZylShell *s, const char *source, const char *app_id) {
    if (s->setup || !s->locked || !is(source, ZYL_SHELL_LOCK)) return NULL;
    s->locked = FALSE;
    return app_id && app_id[0] && zyl_shell_may_launch(s, app_id) ? app_id : ZYL_SHELL_HOME;
}

const char *zyl_shell_setup_done(ZylShell *s, const char *source) {
    if (!s->setup || !is(source, ZYL_SHELL_SETUP)) return NULL;
    s->setup = FALSE;
    s->locked = FALSE;
    return ZYL_SHELL_HOME;
}

ZylPanelMode zyl_shell_panel_mode(const ZylShell *s, const char *source, const char *mode) {
    if (!is(source, ZYL_SHELL_SYSTEM_UI)) return ZYL_PANEL_NONE;
    if (is(mode, "full")) return zyl_shell_navigable(s) ? ZYL_PANEL_FULL : ZYL_PANEL_BAR;
    if (is(mode, "banner")) return (s->setup || s->locked) ? ZYL_PANEL_BAR : ZYL_PANEL_BANNER;
    return ZYL_PANEL_BAR;
}

void zyl_shell_shown(ZylShell *s, const char *app_id) {
    g_strlcpy(s->top, app_id ? app_id : "", sizeof(s->top));
}
