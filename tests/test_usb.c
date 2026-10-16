/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: USB 가젯 모드 서비스(runtime/services/usb) 단위 테스트
 * 수행범위: 모드 전환이 configs/c.1 의 함수 링크만 바꿈(다른 항목은 그대로),
 *           충전 모드는 UDC 를 실제로 떼어냄(빈 쓰기는 configfs 에 닿지 않음),
 *           UDC 바인딩 실패·UDC 없음은 실패로 알리고 모드를 바꾸지 않음,
 *           모르는 모드 이름·범위 밖 값 거부, 연결 상태는 UDC state 로 판정
 * 의존방향: runtime/services/usb/usb.c (ZYL_TEST, USB_GADGET_BASE·USB_STATE_PATH
 *           교체), gio
 * SOLID: SRP — USB 모드 전환 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-usb-test"
#define USB_GADGET_BASE ROOT_ "/gadget/g1"
#define USB_STATE_PATH ROOT_ "/udc"
#include <assert.h>
#include "../runtime/services/usb/usb.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)
static ZylUsbService *svc;

#define G_ USB_GADGET_BASE
#define CFG_ G_ "/configs/c.1"

static void put(const char *path, const char *text) {
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
}

static char *slurp(const char *path) {
    char *s = NULL;
    g_assert_true(g_file_get_contents(path, &s, NULL, NULL));
    return s;
}

static bool is_link(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

/* What configfs and the UDC class look like on a board with one controller */
static void reset(void) {
    g_assert_true(system("rm -rf " ROOT_) == 0);
    g_assert_true(system("mkdir -p " CFG_ "/strings/0x409 " G_ "/functions "
                         USB_STATE_PATH "/fake-udc.0") == 0);
    put(G_ "/UDC", "");
    put(CFG_ "/MaxPower", "500\n");
    put(USB_STATE_PATH "/fake-udc.0/state", "not attached\n");
    if (svc) zyl_usb_destroy(svc);
    svc = zyl_usb_create();
}

static void test_mode_switch_links_only_the_new_function(void) {
    assert(svc->current_mode == ZYL_USB_MODE_CHARGING);
    assert(zyl_usb_set_mode(svc, ZYL_USB_MODE_MTP) == 0);
    assert(is_link(CFG_ "/ffs.mtp"));
    char *target = g_file_read_link(CFG_ "/ffs.mtp", NULL);
    assert(target && strcmp(target, G_ "/functions/ffs.mtp") == 0);
    g_free(target);
    char *udc = slurp(G_ "/UDC");
    assert(strcmp(udc, "fake-udc.0") == 0);                 /* bound again */
    g_free(udc);

    assert(zyl_usb_set_mode(svc, ZYL_USB_MODE_ADB) == 0);
    assert(!is_link(CFG_ "/ffs.mtp") && is_link(CFG_ "/ffs.adb"));
    assert(g_file_test(CFG_ "/strings/0x409", G_FILE_TEST_IS_DIR));   /* untouched */
    char *power = slurp(CFG_ "/MaxPower");
    assert(strcmp(power, "500\n") == 0);
    g_free(power);
    assert(svc->current_mode == ZYL_USB_MODE_ADB);
    assert(strcmp(mode_to_string[svc->current_mode], "adb") == 0);
}

static void test_charging_really_unbinds_the_gadget(void) {
    assert(zyl_usb_set_mode(svc, ZYL_USB_MODE_TETHERING) == 0);
    assert(is_link(CFG_ "/rndis.usb0"));
    assert(zyl_usb_set_mode(svc, ZYL_USB_MODE_CHARGING) == 0);
    assert(!is_link(CFG_ "/rndis.usb0"));
    /* configfs only acts on a write; fputs("") issues none, so the gadget
       used to stay bound. A lone newline is the unbind. */
    char *udc = slurp(G_ "/UDC");
    assert(strcmp(udc, "\n") == 0);
    g_free(udc);
}

static void test_failed_bind_is_reported_and_mode_kept(void) {
    /* The kernel refuses the bind (EBUSY, ENODEV for an FFS function nobody
       serves): with stdio that surfaces only at fclose */
    g_assert_true(unlink(G_ "/UDC") == 0 && symlink("/dev/full", G_ "/UDC") == 0);
    assert(zyl_usb_set_mode(svc, ZYL_USB_MODE_MTP) == -1);
    assert(svc->current_mode == ZYL_USB_MODE_CHARGING);
}

static void test_no_controller_means_no_mode(void) {
    g_assert_true(system("rm -rf " USB_STATE_PATH "/fake-udc.0") == 0);
    assert(zyl_usb_set_mode(svc, ZYL_USB_MODE_PTP) == -1);
    assert(svc->current_mode == ZYL_USB_MODE_CHARGING);
    g_assert_true(system("rm -rf " USB_STATE_PATH) == 0);
    assert(zyl_usb_set_mode(svc, ZYL_USB_MODE_PTP) == -1);
}

static void test_unknown_mode_names_are_refused(void) {
    assert(zyl_usb_set_mode(svc, ZYL_USB_MODE_MTP) == 0);
    const char *bad[] = { "MTP", "garbage", "", "mtp ", "adb\n", NULL };
    for (int i = 0; bad[i]; i++)
        assert(zyl_usb_set_mode(svc, string_to_mode(bad[i])) == -1);
    assert(zyl_usb_set_mode(svc, string_to_mode(NULL)) == -1);
    assert(svc->current_mode == ZYL_USB_MODE_MTP);           /* still plugged */
    assert(is_link(CFG_ "/ffs.mtp"));
    assert(zyl_usb_set_mode(svc, (ZylUsbMode)42) == -1);
    assert(zyl_usb_set_mode(svc, (ZylUsbMode)-1) == -1);
    assert(zyl_usb_set_mode(NULL, ZYL_USB_MODE_MTP) == -1);
    /* every name GetMode can return is accepted back */
    for (int m = ZYL_USB_MODE_NONE; m <= ZYL_USB_MODE_TETHERING; m++)
        assert((int)string_to_mode(mode_to_string[m]) == m);
}

static void test_connection_follows_udc_state(void) {
    poll_usb_state(svc);
    assert(!svc->connected);
    put(USB_STATE_PATH "/fake-udc.0/state", "configured\n");
    poll_usb_state(svc);
    assert(svc->connected);
    put(USB_STATE_PATH "/fake-udc.0/state", "configured-but-not-really\n");
    poll_usb_state(svc);
    assert(!svc->connected);
    put(USB_STATE_PATH "/fake-udc.0/state", "configured\n");
    poll_usb_state(svc);
    g_assert_true(unlink(USB_STATE_PATH "/fake-udc.0/state") == 0);
    poll_usb_state(svc);                                     /* unreadable */
    assert(!svc->connected);
    g_assert_true(system("rm -rf " USB_STATE_PATH) == 0);
    poll_usb_state(svc);                                     /* no UDC class */
    assert(!svc->connected);
}

int main(void) {
    printf("=== Zyl OS USB Gadget Mode Tests ===\n");
    RUN_TEST(test_mode_switch_links_only_the_new_function);
    RUN_TEST(test_charging_really_unbinds_the_gadget);
    RUN_TEST(test_failed_bind_is_reported_and_mode_kept);
    RUN_TEST(test_no_controller_means_no_mode);
    RUN_TEST(test_unknown_mode_names_are_refused);
    RUN_TEST(test_connection_follows_udc_state);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    zyl_usb_destroy(svc);
    (void)vtable; (void)introspection_xml;
    return tests_pass == tests_run ? 0 : 1;
}
