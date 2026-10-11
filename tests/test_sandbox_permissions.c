/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 샌드박스 권한 어휘 매핑 단위 테스트
 * 수행범위: permission_from_string() — app.json 선언 어휘 → 격리 비트마스크
 * 의존방향: runtime/services/sandbox/sandbox.c, libseccomp, gio, json-glib
 * SOLID: SRP — 권한 매핑 검증만 담당
 *
 * 이 매핑이 틀리면 앱이 능력을 통째로 잃는다. ZYL_PERM_NETWORK 가 없으면
 * unshare(CLONE_NEWNET) 로 네트워크가 끊기고, ZYL_PERM_PHONE 이 없으면
 * ModemManager1 D-Bus 접근이 막힌다.
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../runtime/services/sandbox/sandbox.h"

static int tests_run;
static int tests_pass;

#define RUN_TEST(fn) do { \
    tests_run++; \
    printf("  %-46s ", #fn); \
    fn(); \
    tests_pass++; \
    printf("PASS\n"); \
} while (0)

/* Pull the real implementation in so the static mapping is reachable. */
#include "../runtime/services/sandbox/sandbox.c"

/* The dialer declares "telephony"; before this mapping was fixed the parser
   only knew "phone", so the app silently lost ModemManager access. */
static void test_telephony_grants_modem_access(void)
{
    assert(permission_from_string("telephony") == ZYL_PERM_PHONE);
    assert(permission_from_string("messaging") == ZYL_PERM_PHONE);
}

/* Nine apps declare plain "storage"; the parser only knew "storage.read". */
static void test_storage_vocabulary(void)
{
    uint32_t both = ZYL_PERM_STORAGE_READ | ZYL_PERM_STORAGE_WRITE;
    assert(permission_from_string("storage")      == both);
    assert(permission_from_string("files.read")   == ZYL_PERM_STORAGE_READ);
    assert(permission_from_string("files.write")  == ZYL_PERM_STORAGE_WRITE);
    assert(permission_from_string("files.delete") == ZYL_PERM_STORAGE_WRITE);
}

/* ZYL_PERM_MICROPHONE and ZYL_PERM_NOTIFICATION existed in the enum but no
   string reached them, so those bits could never be set. */
static void test_previously_unreachable_bits(void)
{
    assert(permission_from_string("microphone")        == ZYL_PERM_MICROPHONE);
    assert(permission_from_string("notification")      == ZYL_PERM_NOTIFICATION);
    assert(permission_from_string("notification.read") == ZYL_PERM_NOTIFICATION);
}

static void test_device_and_data_permissions(void)
{
    assert(permission_from_string("camera")    == ZYL_PERM_CAMERA);
    assert(permission_from_string("location")  == ZYL_PERM_LOCATION);
    assert(permission_from_string("bluetooth") == ZYL_PERM_BLUETOOTH);
    assert(permission_from_string("contacts")  == ZYL_PERM_CONTACTS);
    assert(permission_from_string("network")   == ZYL_PERM_NETWORK);
    assert(permission_from_string("system")    == ZYL_PERM_SYSTEM);
}

/* Packages signed before the vocabulary was unified must keep working. */
static void test_legacy_spellings_still_accepted(void)
{
    assert(permission_from_string("phone")          == ZYL_PERM_PHONE);
    assert(permission_from_string("notifications")  == ZYL_PERM_NOTIFICATION);
    assert(permission_from_string("storage.read")   == ZYL_PERM_STORAGE_READ);
    assert(permission_from_string("storage.write")  == ZYL_PERM_STORAGE_WRITE);
    assert(permission_from_string("storage.shared") ==
           (ZYL_PERM_STORAGE_READ | ZYL_PERM_STORAGE_WRITE));
}

/* These gate service calls in the OS layer, not syscalls or D-Bus here.
   Returning 0 is the intended answer, not an oversight. */
static void test_permissions_without_isolation_meaning(void)
{
    const char *none[] = {
        "audio", "wifi", "display", "input", "i18n", "time", "webview",
        "wallpaper", "bookmarks", "auth", "battery", "shell",
        "app.launch", "app.list", "app.manage", NULL
    };
    for (int i = 0; none[i]; i++)
        assert(permission_from_string(none[i]) == 0);
}

static void test_unknown_and_null_are_zero(void)
{
    assert(permission_from_string(NULL) == 0);
    assert(permission_from_string("") == 0);
    assert(permission_from_string("not-a-permission") == 0);
    /* No prefix or substring matching: "cameras" must not grant "camera". */
    assert(permission_from_string("cameras") == 0);
    assert(permission_from_string("cam") == 0);
}

int main(void)
{
    printf("=== Zyl OS Sandbox Permission Mapping Tests ===\n");
    RUN_TEST(test_telephony_grants_modem_access);
    RUN_TEST(test_storage_vocabulary);
    RUN_TEST(test_previously_unreachable_bits);
    RUN_TEST(test_device_and_data_permissions);
    RUN_TEST(test_legacy_spellings_still_accepted);
    RUN_TEST(test_permissions_without_isolation_meaning);
    RUN_TEST(test_unknown_and_null_are_zero);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
