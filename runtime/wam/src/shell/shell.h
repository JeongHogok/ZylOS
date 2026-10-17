/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 셸 정책 — 지금 어느 앱이 앞에 올 수 있는가 (OS 의 판단)
 * 수행범위: 첫 설정(OOBE)과 잠금 화면, 그리고 그것들이 허락하는 것: 설정 중에는
 *           설정만, 잠금 중에는 잠금 화면만. 전화·긴급 경보·잠금 화면의 카메라
 *           바로가기는 둘 위에 뜰 수 있다. 홈·뒤로 나가기·최근 앱·앱 전환은
 *           여기서 따라 나온다. 잠금 해제는 잠금 화면만, 설정 완료는 설정만 —
 *           보낸 앱은 OS 가 말한다. 홈은 닫히지 않는다.
 *           /data 를 열지 못한 부팅(복구)에는 복구 화면만 — 그 밖에서는 복구 화면을
 *           띄우지 않는다(사용자 데이터를 지우는 화면이다).
 *           같은 규칙: apps/system/shell.js
 * 의존방향: glib
 * SOLID: SRP — 판단만. 띄우기·창 관리는 WAM(wam.c)
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WAM_SHELL_H
#define ZYL_WAM_SHELL_H

#include <glib.h>

#define ZYL_SHELL_HOME  "com.zylos.home"
#define ZYL_SHELL_LOCK  "com.zylos.lockscreen"
#define ZYL_SHELL_SETUP "com.zylos.oobe"
#define ZYL_SHELL_RECOVERY "com.zylos.recovery"
/* The surface over every app: status bar, notification panel, recents */
#define ZYL_SHELL_SYSTEM_UI "com.zylos.statusbar"
/* Its bar: app windows start below it (compositor), its input region when
   folded (WAM); and the banner a new notification opens. The CSS that draws
   them (apps/statusbar, the emulator's frame) must agree — verify-all checks */
#define ZYL_SHELL_BAR_PX    36
#define ZYL_SHELL_BANNER_PX 136

typedef enum { ZYL_PANEL_NONE, ZYL_PANEL_BAR, ZYL_PANEL_BANNER, ZYL_PANEL_FULL } ZylPanelMode;

typedef struct {
    gboolean recovery;  /* /data could not be mounted: only the recovery screen */
    gboolean setup;     /* first setup is not done */
    gboolean locked;
    char     top[256];  /* the app in front, as the WAM last showed it */
} ZylShell;

/* The device started: setup until it is done, then the lock screen */
const char *zyl_shell_boot(ZylShell *s, gboolean setup_done);

/* The user's data could not be mounted (runtime/lib/recovery.h): the recovery
   screen, and nothing else for this boot */
const char *zyl_shell_recover(ZylShell *s);

/* May this app come to the front now? (an app, a notification, an intent) */
gboolean zyl_shell_may_launch(const ZylShell *s, const char *app_id);

/* Home, and where an app the user backs out of goes; NULL: stay */
const char *zyl_shell_home(const ZylShell *s);

/* May the user move between apps — recents, switching, the quick panel? */
gboolean zyl_shell_navigable(const ZylShell *s);

/* Home never; the lock screen and setup not while they hold the device */
gboolean zyl_shell_may_close(const ZylShell *s, const char *app_id);

/* Lock now (the user, or the screen went off). Setup never locks. NULL: stay */
const char *zyl_shell_lock(ZylShell *s);

/* The screen came on: what holds the device, unless a call or alert is in front */
const char *zyl_shell_screen_on(const ZylShell *s);

/* The lock screen says the user got in. Returns the app to show (app_id when
   it may be opened, else home), NULL when refused. The result may be app_id. */
const char *zyl_shell_unlock(ZylShell *s, const char *source, const char *app_id);

/* Setup says it is finished: home, NULL when refused */
const char *zyl_shell_setup_done(ZylShell *s, const char *source);

/* The system UI asks for "bar", "banner" (a new notification) or "full" (the
   panel, recents): what it gets — full only when navigable, banner not while
   locked or in setup. ZYL_PANEL_NONE when the sender is not the system UI */
ZylPanelMode zyl_shell_panel_mode(const ZylShell *s, const char *source, const char *mode);

/* The WAM brought this app to the front */
void zyl_shell_shown(ZylShell *s, const char *app_id);

#endif /* ZYL_WAM_SHELL_H */
