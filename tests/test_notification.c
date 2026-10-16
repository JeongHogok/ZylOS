/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 알림 서비스(ZylNotificationService) 단위 테스트
 * 수행범위: post/cancel(자기 앱 것만)/clear_all/채널 필터링, 액션 누르기(알림이 준
 *           액션만, 누르면 끝난 알림) 기능 검증
 * 의존방향: notification.h (테스트 대상)
 * SOLID: SRP — 알림 서비스 로직만 테스트
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "notification.h"

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

/* ── Test: post notification returns valid ID ─────────────── */

static void test_post_returns_valid_id(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();
    assert(svc != NULL);

    uint64_t id = zyl_notification_post(svc,
        "com.zylos.test", "general", "Test Title", "Test Body",
        "icon.png", ZYL_NOTIFICATION_PRIORITY_DEFAULT);

    assert(id > 0);

    zyl_notification_service_destroy(svc);
}

/* ── Test: sequential post returns incrementing IDs ───────── */

static void test_post_sequential_ids(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    uint64_t id1 = zyl_notification_post(svc,
        "com.zylos.test", "general", "First", "Body1",
        NULL, ZYL_NOTIFICATION_PRIORITY_LOW);

    uint64_t id2 = zyl_notification_post(svc,
        "com.zylos.test", "general", "Second", "Body2",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);

    assert(id1 > 0);
    assert(id2 > 0);
    assert(id2 > id1);

    zyl_notification_service_destroy(svc);
}

/* ── Test: post with NULL required fields returns 0 ───────── */

static void test_post_null_app_id_returns_zero(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    uint64_t id = zyl_notification_post(svc,
        NULL, "general", "Title", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);

    assert(id == 0);

    zyl_notification_service_destroy(svc);
}

static void test_post_null_title_returns_zero(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    uint64_t id = zyl_notification_post(svc,
        "com.zylos.test", "general", NULL, "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);

    assert(id == 0);

    zyl_notification_service_destroy(svc);
}

/* ── Test: get_active returns posted notification ─────────── */

static void test_get_active_after_post(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    zyl_notification_post(svc,
        "com.zylos.test", "general", "Hello", "World",
        "icon.png", ZYL_NOTIFICATION_PRIORITY_DEFAULT);

    ZylNotification *list = NULL;
    int count = 0;
    zyl_notification_get_active(svc, &list, &count);

    assert(count == 1);
    assert(list != NULL);
    assert(strcmp(list[0].title, "Hello") == 0);
    assert(strcmp(list[0].body, "World") == 0);

    /* Cleanup returned list */
    for (int i = 0; i < count; i++)
        zyl_notification_free(&list[i]);
    g_free(list);

    zyl_notification_service_destroy(svc);
}

/* ── Test: cancel notification removes it ─────────────────── */

static void test_cancel_removes_notification(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    uint64_t id = zyl_notification_post(svc,
        "com.zylos.test", "general", "To Cancel", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);
    assert(id > 0);

    /* The app that posted it cancels it */
    zyl_notification_cancel(svc, id, "com.zylos.test");

    /* Verify it's gone */
    ZylNotification *list = NULL;
    int count = 0;
    zyl_notification_get_active(svc, &list, &count);

    assert(count == 0);
    assert(list == NULL);

    zyl_notification_service_destroy(svc);
}

/* ── Test: cancel non-existent ID is a no-op ──────────────── */

static void test_cancel_nonexistent_id(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    /* Post one notification */
    uint64_t id = zyl_notification_post(svc,
        "com.zylos.test", "general", "Keep Me", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);

    /* Cancel with wrong ID */
    zyl_notification_cancel(svc, id + 999, "");

    /* Original should still be there */
    ZylNotification *list = NULL;
    int count = 0;
    zyl_notification_get_active(svc, &list, &count);

    assert(count == 1);

    for (int i = 0; i < count; i++)
        zyl_notification_free(&list[i]);
    g_free(list);

    zyl_notification_service_destroy(svc);
}

/* ── Test: clear_all removes all non-persistent ───────────── */

static void test_clear_all_removes_non_persistent(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    /* Post two regular notifications */
    zyl_notification_post(svc,
        "com.zylos.test", "general", "Regular1", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);
    zyl_notification_post(svc,
        "com.zylos.test", "general", "Regular2", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_LOW);

    /* Post one URGENT notification (persistent) */
    zyl_notification_post(svc,
        "com.zylos.test", "general", "Urgent", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_URGENT);

    /* Clear all non-persistent */
    zyl_notification_clear_all(svc);

    /* Only the persistent (URGENT) notification should remain */
    ZylNotification *list = NULL;
    int count = 0;
    zyl_notification_get_active(svc, &list, &count);

    assert(count == 1);
    assert(strcmp(list[0].title, "Urgent") == 0);

    for (int i = 0; i < count; i++)
        zyl_notification_free(&list[i]);
    g_free(list);

    zyl_notification_service_destroy(svc);
}

/* ── Test: a removed app's notifications all go, persistent too ── */

static void test_forget_app_removes_only_that_app(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();
    zyl_notification_post(svc, "com.example.gone", "general", "A", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);
    zyl_notification_post(svc, "com.example.gone", "general", "Urgent", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_URGENT);
    zyl_notification_post(svc, "com.zylos.test", "general", "Kept", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);

    zyl_notification_forget_app(svc, "com.example.gone");

    ZylNotification *list = NULL;
    int count = 0;
    zyl_notification_get_active(svc, &list, &count);
    assert(count == 1);
    assert(strcmp(list[0].title, "Kept") == 0);
    for (int i = 0; i < count; i++)
        zyl_notification_free(&list[i]);
    g_free(list);
    zyl_notification_forget_app(svc, NULL);
    zyl_notification_forget_app(NULL, "x");
    zyl_notification_service_destroy(svc);
}

/* ── Test: clear_all on empty is a no-op ──────────────────── */

static void test_clear_all_empty(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    /* Clear on empty service — should not crash */
    zyl_notification_clear_all(svc);

    ZylNotification *list = NULL;
    int count = 0;
    zyl_notification_get_active(svc, &list, &count);

    assert(count == 0);
    assert(list == NULL);

    zyl_notification_service_destroy(svc);
}

/* ── Test: channel disabled drops notification ────────────── */

static void test_channel_disabled_drops_notification(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    /* Register a channel and disable it */
    zyl_notification_channel_register(svc, "muted", "Muted Channel", 2);
    zyl_notification_channel_set_enabled(svc, "muted", false);

    /* Try to post on the disabled channel */
    uint64_t id = zyl_notification_post(svc,
        "com.zylos.test", "muted", "Should Drop", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);

    /* Should return 0 (rejected) */
    assert(id == 0);

    /* Verify nothing stored */
    ZylNotification *list = NULL;
    int count = 0;
    zyl_notification_get_active(svc, &list, &count);
    assert(count == 0);

    zyl_notification_service_destroy(svc);
}

/* ── Test: channel importance 0 silently drops ────────────── */

static void test_channel_importance_zero_drops(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    /* Register a channel with importance 0 */
    zyl_notification_channel_register(svc, "silent", "Silent Channel", 0);

    uint64_t id = zyl_notification_post(svc,
        "com.zylos.test", "silent", "Should Drop", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);

    assert(id == 0);

    zyl_notification_service_destroy(svc);
}

/* ── Test: channel enabled allows notification ────────────── */

static void test_channel_enabled_allows_notification(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    /* Register an enabled channel */
    zyl_notification_channel_register(svc, "alerts", "Alerts", 2);

    uint64_t id = zyl_notification_post(svc,
        "com.zylos.test", "alerts", "Alert", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_HIGH);

    assert(id > 0);

    zyl_notification_service_destroy(svc);
}

/* ── Test: re-enable channel allows posting again ─────────── */

static void test_channel_re_enable(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    zyl_notification_channel_register(svc, "toggle", "Toggle Channel", 2);

    /* Disable */
    zyl_notification_channel_set_enabled(svc, "toggle", false);
    uint64_t id1 = zyl_notification_post(svc,
        "com.zylos.test", "toggle", "Blocked", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);
    assert(id1 == 0);

    /* Re-enable */
    zyl_notification_channel_set_enabled(svc, "toggle", true);
    uint64_t id2 = zyl_notification_post(svc,
        "com.zylos.test", "toggle", "Allowed", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);
    assert(id2 > 0);

    zyl_notification_service_destroy(svc);
}

/* ── Test: unknown channel allows posting (no filter) ─────── */

static void test_unknown_channel_allows_posting(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();

    /* Post on a channel that was never registered — should pass through */
    uint64_t id = zyl_notification_post(svc,
        "com.zylos.test", "nonexistent", "No Channel", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);

    assert(id > 0);

    zyl_notification_service_destroy(svc);
}

/* ── Test: an app cancels its own only; "" (a system app) any app's ── */

static void test_cancel_only_the_apps_own(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();
    uint64_t mine = zyl_notification_post(svc, "com.example.chat", "general", "Mine", "b",
                                          NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);
    uint64_t theirs = zyl_notification_post(svc, "com.zylos.messages", "general", "Mom", "code 1234",
                                            NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);
    ZylNotification *list = NULL;
    int count = 0;

    zyl_notification_cancel(svc, theirs, "com.example.chat");     /* not its to take away */
    zyl_notification_get_active(svc, &list, &count);
    assert(count == 2);
    for (int i = 0; i < count; i++) zyl_notification_free(&list[i]);
    g_free(list);

    zyl_notification_cancel(svc, mine, "com.example.chat");
    zyl_notification_get_active(svc, &list, &count);
    assert(count == 1 && list[0].id == theirs);
    zyl_notification_free(&list[0]);
    g_free(list);

    zyl_notification_cancel(svc, theirs, "");                     /* the status bar's */
    zyl_notification_get_active(svc, &list, &count);
    assert(count == 0 && list == NULL);

    zyl_notification_service_destroy(svc);
}

/* The user pressed an action: only one the notification offered, and then it is done */
static void test_invoke_action_only_offered_then_done(void)
{
    ZylNotificationService *svc = zyl_notification_service_create();
    ZylNotifAction acts[] = { { "Reply", "reply" }, { "Mark read", "read" } };
    uint64_t id = zyl_notification_post_with_actions(svc, "com.example.chat", "general", "Mom", "hi",
                                                     NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT, acts, 2);
    ZylNotification *list = NULL;
    int count = 0;
    zyl_notification_get_active(svc, &list, &count);
    assert(count == 1 && list[0].num_actions == 2 && strcmp(list[0].actions[1].action_id, "read") == 0);
    zyl_notification_free(&list[0]);
    g_free(list);

    assert(!zyl_notification_invoke_action(svc, id, "delete-all"));   /* not offered */
    assert(!zyl_notification_invoke_action(svc, id + 99, "reply"));   /* no such notification */
    assert(!zyl_notification_invoke_action(NULL, id, "reply"));
    assert(zyl_notification_invoke_action(svc, id, "reply"));
    zyl_notification_get_active(svc, &list, &count);
    assert(count == 0);                                               /* answered: gone */
    assert(!zyl_notification_invoke_action(svc, id, "reply"));        /* once */
    zyl_notification_service_destroy(svc);
}

/* ── Test: NULL service pointer handling ──────────────────── */

static void test_null_service_safety(void)
{
    /* These should not crash */
    zyl_notification_cancel(NULL, 1, NULL);
    zyl_notification_clear_all(NULL);

    ZylNotification *list = NULL;
    int count = 99;
    zyl_notification_get_active(NULL, &list, &count);
    assert(count == 0);
    assert(list == NULL);

    uint64_t id = zyl_notification_post(NULL,
        "com.zylos.test", "ch", "Title", "Body",
        NULL, ZYL_NOTIFICATION_PRIORITY_DEFAULT);
    assert(id == 0);
}

/* ── Main ─────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Zyl OS Notification Service Tests ===\n");

    RUN_TEST(test_post_returns_valid_id);
    RUN_TEST(test_post_sequential_ids);
    RUN_TEST(test_post_null_app_id_returns_zero);
    RUN_TEST(test_post_null_title_returns_zero);
    RUN_TEST(test_get_active_after_post);
    RUN_TEST(test_cancel_removes_notification);
    RUN_TEST(test_cancel_nonexistent_id);
    RUN_TEST(test_cancel_only_the_apps_own);
    RUN_TEST(test_invoke_action_only_offered_then_done);
    RUN_TEST(test_clear_all_removes_non_persistent);
    RUN_TEST(test_forget_app_removes_only_that_app);
    RUN_TEST(test_clear_all_empty);
    RUN_TEST(test_channel_disabled_drops_notification);
    RUN_TEST(test_channel_importance_zero_drops);
    RUN_TEST(test_channel_enabled_allows_notification);
    RUN_TEST(test_channel_re_enable);
    RUN_TEST(test_unknown_channel_allows_posting);
    RUN_TEST(test_null_service_safety);

    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
