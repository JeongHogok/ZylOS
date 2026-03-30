/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 알림 서비스(ZylNotificationService) 단위 테스트
 * 수행범위: post/cancel/clear_all/채널 필터링 기능 검증
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

    /* Cancel the notification */
    zyl_notification_cancel(svc, id);

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
    zyl_notification_cancel(svc, id + 999);

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

/* ── Test: NULL service pointer handling ──────────────────── */

static void test_null_service_safety(void)
{
    /* These should not crash */
    zyl_notification_cancel(NULL, 1);
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
    RUN_TEST(test_clear_all_removes_non_persistent);
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
