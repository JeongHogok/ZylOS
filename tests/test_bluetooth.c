/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 블루투스 서비스(runtime/services/bluetooth) 기기 목록 단위 테스트
 * 수행범위: GetDevices 의 필드(에뮬레이터와 같은 name·address·deviceType·paired·
 *           connected·rssi), 읽을 수 없는 목록은 빈 목록, 최대 64개, HAL 의 고정
 *           버퍼에서 글자 중간이 잘린 이름(근처 아무 기기나 광고하는 이름)도 유효한
 *           UTF-8 로 나감
 * 의존방향: runtime/services/bluetooth/bluetooth.c (ZYL_TEST), 이 파일의 가짜 BT HAL, gio
 * SOLID: SRP — 블루투스 서비스가 내보내는 값 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include "../runtime/services/bluetooth/bluetooth.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); fflush(stdout); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)

/* ─── The adapter: a fake Bluetooth HAL ─── */
static ZylBtDevice nearby[MAX_DEVICES + 6];
static int nearby_n;                                 /* -1: the list cannot be read */
static int hal_list(ZylBtDevice *out, int max) {
    if (nearby_n < 0) return -1;
    int n = MIN(nearby_n, max);
    memcpy(out, nearby, (size_t)n * sizeof(*out));
    return n;
}
static const ZylBtHal fake_bt = { .list_devices = hal_list };
const ZylBtHal *zyl_hal_bt(void) { return &fake_bt; }

static void reset(void) {
    memset(nearby, 0, sizeof(nearby));
    nearby_n = 0;
}

static GVariant *devices(void) {
    GVariant *r = devices_variant();
    assert(r && g_variant_is_of_type(r, G_VARIANT_TYPE("(aa{sv})")));
    GVariant *list = g_variant_get_child_value(r, 0);
    g_variant_unref(r);
    return list;
}

static void test_devices_carry_the_fields_the_emulator_gives(void) {
    nearby[0] = (ZylBtDevice){ "Buds", "AA:BB:CC:DD:EE:01", "audio-headphones", true, true, -40 };
    nearby[1] = (ZylBtDevice){ "AA:BB:CC:DD:EE:02", "AA:BB:CC:DD:EE:02", "", false, false, 0 };
    nearby_n = 2;
    GVariant *list = devices();
    assert(g_variant_n_children(list) == 2);
    GVariant *d = g_variant_get_child_value(list, 0);
    const char *name, *address, *type;
    gboolean paired = FALSE, connected = FALSE;
    gint32 rssi = 0;
    assert(g_variant_lookup(d, "name", "&s", &name) && strcmp(name, "Buds") == 0);
    assert(g_variant_lookup(d, "address", "&s", &address) && strcmp(address, "AA:BB:CC:DD:EE:01") == 0);
    assert(g_variant_lookup(d, "deviceType", "&s", &type) && strcmp(type, "audio-headphones") == 0);
    assert(g_variant_lookup(d, "paired", "b", &paired) && paired);
    assert(g_variant_lookup(d, "connected", "b", &connected) && connected);
    assert(g_variant_lookup(d, "rssi", "i", &rssi) && rssi == -40);
    g_variant_unref(d);
    d = g_variant_get_child_value(list, 1);
    assert(g_variant_lookup(d, "paired", "b", &paired) && !paired);
    assert(g_variant_lookup(d, "deviceType", "&s", &type) && *type == '\0');
    g_variant_unref(d);
    g_variant_unref(list);
}

static void test_an_unreadable_list_is_empty(void) {
    nearby_n = -1;
    GVariant *list = devices();
    assert(g_variant_n_children(list) == 0);
    g_variant_unref(list);
}

static void test_at_most_sixty_four_devices(void) {
    for (int i = 0; i < MAX_DEVICES + 6; i++) {
        snprintf(nearby[i].address, sizeof(nearby[i].address), "AA:BB:CC:DD:%02X:%02X", i / 256, i % 256);
        g_strlcpy(nearby[i].name, nearby[i].address, sizeof(nearby[i].name));
    }
    nearby_n = MAX_DEVICES + 6;
    GVariant *list = devices();
    assert(g_variant_n_children(list) == MAX_DEVICES);
    g_variant_unref(list);
}

static void test_a_name_cut_inside_a_character_still_goes_out(void) {
    /* Any device in range picks its own name; the HAL keeps 63 bytes of it
       the way hal_bt.c does, which here ends partway into a character */
    GString *advertised = g_string_new("A");
    for (int i = 0; i < 30; i++) g_string_append(advertised, "가");
    nearby[0] = (ZylBtDevice){ "", "AA:BB:CC:DD:EE:03", "phone", false, false, -70 };
    g_strlcpy(nearby[0].name, advertised->str, sizeof(nearby[0].name));
    assert(!g_utf8_validate(nearby[0].name, -1, NULL));
    nearby[1] = (ZylBtDevice){ "Keyboard", "AA:BB:CC:DD:EE:04", "input-keyboard", true, false, 0 };
    nearby_n = 2;

    GVariant *list = devices();
    assert(g_variant_n_children(list) == 2);                    /* the others are still listed */
    GVariant *d = g_variant_get_child_value(list, 0);
    const char *name = NULL;
    assert(g_variant_lookup(d, "name", "&s", &name));
    assert(g_utf8_validate(name, -1, NULL));
    assert(strncmp(name, advertised->str, 61) == 0);            /* "A" and the 20 whole characters */
    g_variant_unref(d);
    g_variant_unref(list);
    g_string_free(advertised, TRUE);
}

int main(void) {
    printf("=== Zyl OS Bluetooth Service Tests ===\n");
    RUN_TEST(test_devices_carry_the_fields_the_emulator_gives);
    RUN_TEST(test_an_unreadable_list_is_empty);
    RUN_TEST(test_at_most_sixty_four_devices);
    RUN_TEST(test_a_name_cut_inside_a_character_still_goes_out);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
