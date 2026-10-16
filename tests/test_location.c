/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 위치 서비스(runtime/services/location) 단위 테스트
 * 수행범위: GeoIP·Wi-Fi(MLS) 응답 해석 — 깨진·잘린·지나치게 큰 응답, NaN·inf·
 *           지구 밖 좌표는 위치가 되지 않음, wpa_cli 스캔 결과의 잡음 무시,
 *           gpsd 가 모른다고(NaN) 남긴 값은 앱에 NaN 으로 가지 않음,
 *           GPS > Wi-Fi > GeoIP 순서와 못 쓰는 위치일 때의 다음 순위,
 *           지오펜스는 부른 앱의 것(다른 앱이 지우지 못함, 앱별·기기별 한도,
 *           앱 삭제 때 정리), 불가능한 지오펜스 거부, 진입/이탈 판정,
 *           갱신 간격의 범위와 즉시 멈춤(긴 간격에도 메인 루프를 막지 않음)
 * 의존방향: runtime/services/location 의 네 .c (ZYL_TEST, GEOIP_URL·MLS_URL 을
 *           file:// 로 교체, PATH 의 가짜 wpa_cli, gps_* 를 가짜 gpsd 로),
 *           gio, libcurl, libgps(있으면)
 * SOLID: SRP — 위치 입력 검증과 지오펜스 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#define ROOT_ "/tmp/zyl-location-test"
#define GEOIP_URL "file://" ROOT_ "/geoip.json"
#define MLS_URL "file://" ROOT_ "/mls.json"
#include <assert.h>
#include <limits.h>
#include <sys/stat.h>

#ifdef HAVE_GPSD
#include <gps.h>
/* A fake gpsd behind the service's own gps_* calls: what it reports next */
static bool gpsd_up, gpsd_broken;
static struct gps_fix_t gpsd_next;
static int fake_gps_open(const char *h, const char *p, struct gps_data_t *d) {
    (void)h; (void)p; (void)d;
    return gpsd_up ? 0 : -1;
}
static int fake_gps_stream(struct gps_data_t *d, unsigned int f, void *x) {
    (void)d; (void)f; (void)x;
    return 0;
}
static bool fake_gps_waiting(const struct gps_data_t *d, int t) {
    (void)d; (void)t;
    return true;
}
static int fake_gps_read(struct gps_data_t *d, char *m, int l) {
    (void)m; (void)l;
    if (gpsd_broken) return -1;
    d->fix = gpsd_next;
    return 1;
}
static int fake_gps_close(struct gps_data_t *d) {
    (void)d;
    return 0;
}
#define gps_open fake_gps_open
#define gps_stream fake_gps_stream
#define gps_waiting fake_gps_waiting
#define gps_read fake_gps_read
#define gps_close fake_gps_close
#endif

#include "../runtime/services/location/location.c"
#include "../runtime/services/location/location_gps.c"
#include "../runtime/services/location/location_geoip.c"
#include "../runtime/services/location/location_wifi.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)
static ZylLocationService svc;
static ZylLocation loc;

#define HEADER "bssid / frequency / signal level / flags / ssid\n"
#define AP1 "00:11:22:33:44:55\t2412\t-55\t[WPA2-PSK-CCMP][ESS]\tHome\n"
#define AP2 "66:77:88:99:aa:bb\t5180\t-71\t[ESS]\tCafe\n"
#define SEOUL "{\"lat\": 37.5665, \"lon\":126.978,\"city\":\"Seoul\",\"query\":\"203.0.113.9\"}"
#define MLS_OK "{\"location\":{\"lat\":37.4979,\"lng\":127.0276},\"accuracy\":35.0}"

static void put(const char *path, const char *text) {
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
}

static void reset(void) {
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++) {
        free(svc.geofences[i].fence.tag);
        free(svc.geofences[i].app);
    }
    memset(svc.geofences, 0, sizeof(svc.geofences));
    svc.geofence_count = 0;
    memset(&loc, 0, sizeof(loc));
#ifdef HAVE_GPSD
    svc.gpsd_connected = gpsd_up = gpsd_broken = false;
#endif
    g_assert_true(system("rm -rf " ROOT_ " && mkdir -p " ROOT_ "/bin") == 0);
    /* wpa_cli scan_results prints whatever the scenario put in ROOT_/scan */
    put(ROOT_ "/bin/wpa_cli", "#!/bin/sh\nexec cat " ROOT_ "/scan\n");
    g_assert_true(chmod(ROOT_ "/bin/wpa_cli", 0755) == 0);
}

static bool fix_from(const char *geoip_answer) {
    put(ROOT_ "/geoip.json", geoip_answer);
    memset(&loc, 0, sizeof(loc));
    return read_fix(&svc, &loc);
}

static void test_geoip_answer_becomes_a_network_fix(void) {
    assert(fix_from(SEOUL));
    assert(strcmp(loc.provider, "network") == 0);
    assert(loc.latitude == 37.5665 && loc.longitude == 126.978);
    assert(loc.accuracy_m == NETWORK_ACCURACY_M && loc.timestamp_ms > 0);
    assert(fix_from("{\"lat\":-33.8688,\"lng\":151.2093}"));      /* Google-style */
    assert(loc.longitude == 151.2093);
}

static void test_malformed_geoip_answers_give_no_fix(void) {
    g_assert_true(unlink(ROOT_ "/geoip.json") != 0);               /* nothing served */
    assert(!read_fix(&svc, &loc));
    const char *bad[] = {
        "", "{", "{\"lat\":", "{\"lat\":37.5", "<html>502 Bad Gateway</html>",
        "{\"lat\":\"abc\",\"lon\":\"def\"}", "{\"status\":\"fail\",\"message\":\"private range\"}",
        "{\"lat\":0,\"lon\":0}", "{\"flat\":37.5,\"slon\":127}", "\"lat\":,\"lon\":",
        NULL };
    for (int i = 0; bad[i]; i++) assert(!fix_from(bad[i]));
}

static void test_geoip_numbers_off_the_globe_are_dropped(void) {
    /* plain HTTP: anyone on the path writes this answer */
    const char *bad[] = {
        "{\"lat\":1e999,\"lon\":5}", "{\"lat\":nan,\"lon\":nan}", "{\"lat\":inf,\"lon\":1}",
        "{\"lat\":-infinity,\"lon\":1}", "{\"lat\":91,\"lon\":10}", "{\"lat\":10,\"lon\":-180.5}",
        "{\"lat\":1e308,\"lon\":1e308}", "{\"lat\":45,\"lng\":nan}",
        NULL };
    for (int i = 0; bad[i]; i++) assert(!fix_from(bad[i]));
    assert(fix_from("{\"lat\":-90,\"lon\":180}"));                  /* the edges are on Earth */
}

static void test_oversized_geoip_answer_is_refused(void) {
    GString *big = g_string_new(NULL);
    for (int i = 0; i < 1000; i++) g_string_append_c(big, ' ');
    g_string_append(big, SEOUL);
    assert(fix_from(big->str));                                     /* padding is fine */
    for (int i = 0; i < 64 * 1024; i++) g_string_prepend_c(big, ' ');
    assert(!fix_from(big->str));                                    /* streaming forever is not */
    g_string_free(big, TRUE);
}

static void test_wifi_scan_and_mls_answer_give_a_wifi_fix(void) {
    put(ROOT_ "/scan", HEADER AP1 AP2);
    put(ROOT_ "/mls.json", MLS_OK);
    assert(fix_from(SEOUL));                                        /* Wi-Fi before GeoIP */
    assert(strcmp(loc.provider, "wifi") == 0);
    assert(loc.latitude == 37.4979 && loc.longitude == 127.0276);
    assert(loc.accuracy_m == WIFI_ACCURACY_M);
    GString *many = g_string_new(HEADER);                           /* past the 64-AP table */
    for (int i = 0; i < 200; i++)
        g_string_append_printf(many, "02:00:00:00:%02x:%02x\t2437\t-%d\t[ESS]\tAP%d\n",
                               i / 256, i % 256, 40 + i % 50, i);
    put(ROOT_ "/scan", many->str);
    g_string_free(many, TRUE);
    assert(fix_from(SEOUL) && strcmp(loc.provider, "wifi") == 0);
}

static void test_wifi_noise_and_bad_answers_fall_back(void) {
    put(ROOT_ "/mls.json", MLS_OK);
    put(ROOT_ "/scan", HEADER);                                     /* no APs: MLS not asked */
    assert(!wifi_triangulate(&svc, &loc));
    put(ROOT_ "/scan", HEADER "FAIL\n\nzz yy\nSelected interface 'wlan0'\n"
                       "00:11:22:33:44:55\n00:11:22:33:44:55\tabc\t-50\n");
    assert(!wifi_triangulate(&svc, &loc));
    g_assert_true(unlink(ROOT_ "/bin/wpa_cli") == 0);               /* no wpa_cli at all */
    assert(!wifi_triangulate(&svc, &loc));

    reset();
    put(ROOT_ "/scan", HEADER AP1);
    const char *bad[] = {
        "{\"error\":{\"code\":404,\"message\":\"Not found\"}}", "", "{\"location\":{\"lat\":",
        "{\"location\":{\"lat\":\"x\",\"lng\":\"y\"}}", "{\"location\":{\"lat\":0,\"lng\":0}}",
        "{\"location\":{\"lat\":nan,\"lng\":nan}}", "{\"location\":{\"lat\":1e400,\"lng\":3}}",
        "{\"location\":{\"lat\":12,\"lng\":-999}}",
        NULL };
    for (int i = 0; bad[i]; i++) {
        put(ROOT_ "/mls.json", bad[i]);
        assert(fix_from(SEOUL));
        assert(strcmp(loc.provider, "network") == 0 && loc.latitude == 37.5665);
    }
}

#ifdef HAVE_GPSD
static void gpsd_reports(int mode, double lat, double lon) {
    memset(&gpsd_next, 0, sizeof(gpsd_next));
    gpsd_next.mode = mode;
    gpsd_next.latitude = lat;
    gpsd_next.longitude = lon;
    /* gpsd's word for "unknown" */
    gpsd_next.altMSL = gpsd_next.eph = gpsd_next.speed = gpsd_next.track = NAN;
    gpsd_up = true;
}

static void test_gps_unknown_fields_never_reach_apps_as_nan(void) {
    put(ROOT_ "/mls.json", MLS_OK);
    put(ROOT_ "/scan", HEADER AP1);
    gpsd_reports(MODE_2D, 37.5512, 126.9882);
    assert(fix_from(SEOUL));                                        /* GPS before Wi-Fi */
    assert(strcmp(loc.provider, "gps") == 0 && loc.latitude == 37.5512);
    assert(loc.altitude_m == 0.0);
    assert(isfinite(loc.accuracy_m) && isfinite(loc.speed_mps) && isfinite(loc.bearing_deg));
    gpsd_reports(MODE_3D, 37.5512, 126.9882);                       /* 3D, altitude unknown */
    assert(fix_from(SEOUL) && loc.altitude_m == 0.0);
    gpsd_next.altMSL = 38.5; gpsd_next.eph = 4.0; gpsd_next.speed = 1.5; gpsd_next.track = 90.0;
    assert(fix_from(SEOUL));
    assert(loc.altitude_m == 38.5 && loc.accuracy_m == 4.0f);
    assert(loc.speed_mps == 1.5f && loc.bearing_deg == 90.0f);
}

static void test_gps_without_a_usable_fix_falls_back(void) {
    gpsd_reports(MODE_NO_FIX, 0, 0);
    assert(fix_from(SEOUL) && strcmp(loc.provider, "network") == 0);
    gpsd_reports(MODE_2D, NAN, 126.9);
    assert(fix_from(SEOUL) && strcmp(loc.provider, "network") == 0);
    gpsd_reports(MODE_3D, 37.5, 200.0);
    assert(fix_from(SEOUL) && strcmp(loc.provider, "network") == 0);
    gpsd_reports(MODE_3D, 37.5, 127.0);
    gpsd_broken = true;                                             /* gpsd went away */
    assert(fix_from(SEOUL) && strcmp(loc.provider, "network") == 0);
    assert(!svc.gpsd_connected);
    gpsd_up = false;                                                /* and stays away */
    assert(fix_from(SEOUL) && strcmp(loc.provider, "network") == 0);
}
#endif

static ZylGeofence fence(double lat, double lon, double r, char *tag) {
    ZylGeofence f = { lat, lon, r, tag };
    return f;
}

#define APP_A "com.example.a"
#define APP_B "com.example.b"

static void test_geofence_slots_tags_and_limits(void) {
    char tag[16] = "home";
    ZylGeofence f = fence(37.5663, 126.9779, 200, tag);
    assert(zyl_location_add_geofence(&svc, APP_A, &f) == 0);
    assert(zyl_location_add_geofence(&svc, APP_A, &f) == -1);       /* same tag twice */
    tag[0] = 'X';                                                   /* the service keeps a copy */
    assert(zyl_location_remove_geofence(&svc, APP_A, "home") == 0);
    assert(zyl_location_remove_geofence(&svc, APP_A, "home") == -1);
    assert(zyl_location_remove_geofence(&svc, APP_A, NULL) == -1);
    f.tag = NULL;
    assert(zyl_location_add_geofence(&svc, APP_A, &f) == -1);
    assert(zyl_location_add_geofence(&svc, APP_A, NULL) == -1);
    f = fence(10, 10, 50, "nobody");
    assert(zyl_location_add_geofence(&svc, NULL, &f) == -1);        /* every fence has an owner */
    assert(zyl_location_add_geofence(&svc, "", &f) == -1);
    assert(zyl_location_remove_geofence(&svc, NULL, "nobody") == -1);
    for (int i = 0; i < ZYL_GEOFENCE_PER_APP_MAX; i++) {            /* one app can't take them all */
        snprintf(tag, sizeof(tag), "f%d", i);
        f = fence(10, 10, 50, tag);
        assert(zyl_location_add_geofence(&svc, APP_A, &f) == 0);
    }
    f = fence(10, 10, 50, "one-too-many");
    assert(zyl_location_add_geofence(&svc, APP_A, &f) == -1);
    assert(zyl_location_add_geofence(&svc, APP_B, &f) == 0);
    char app[32];
    for (int n = 0; svc.geofence_count < ZYL_GEOFENCE_MAX; n++) {   /* all apps: the device's limit */
        snprintf(app, sizeof(app), "com.example.app%d", n / ZYL_GEOFENCE_PER_APP_MAX);
        snprintf(tag, sizeof(tag), "g%d", n);
        f = fence(10, 10, 50, tag);
        assert(zyl_location_add_geofence(&svc, app, &f) == 0);
    }
    f = fence(10, 10, 50, "late");
    assert(zyl_location_add_geofence(&svc, "com.example.late", &f) == -1);
    assert(svc.geofence_count == ZYL_GEOFENCE_MAX);
    assert(zyl_location_remove_geofence(&svc, APP_A, "f0") == 0);
    assert(zyl_location_add_geofence(&svc, "com.example.late", &f) == 0);  /* the slot is free again */
}

static void test_geofences_belong_to_the_calling_app(void) {
    ZylGeofence home = fence(37.5663, 126.9779, 200, "home");
    ZylGeofence work = fence(37.4979, 127.0276, 300, "work");
    assert(zyl_location_add_geofence(&svc, APP_A, &home) == 0);
    assert(zyl_location_add_geofence(&svc, APP_B, &home) == 0);     /* the same tag, B's own */
    assert(zyl_location_add_geofence(&svc, APP_A, &work) == 0);
    assert(zyl_location_remove_geofence(&svc, APP_B, "work") == -1);/* not B's to remove */
    assert(zyl_location_remove_geofence(&svc, APP_B, "home") == 0);
    assert(zyl_location_remove_geofence(&svc, APP_B, "home") == -1);
    assert(svc.geofence_count == 2);
    assert(zyl_location_remove_geofence(&svc, APP_A, "home") == 0); /* A's outlived B's removal */
    assert(zyl_location_add_geofence(&svc, APP_B, &home) == 0);
    assert(zyl_location_forget_app(&svc, APP_A) == 1);              /* uninstalled: its fences go */
    assert(zyl_location_forget_app(&svc, APP_A) == 0);
    assert(zyl_location_remove_geofence(&svc, APP_A, "work") == -1);
    assert(svc.geofence_count == 1 && zyl_location_remove_geofence(&svc, APP_B, "home") == 0);
    assert(zyl_location_forget_app(&svc, NULL) == 0 && zyl_location_forget_app(NULL, APP_A) == 0);
}

static void test_impossible_geofences_are_refused(void) {
    ZylGeofence bad[] = {
        fence(91, 0, 100, "a"), fence(-90.5, 0, 100, "b"), fence(0, 181, 100, "c"),
        fence(NAN, 0, 100, "d"), fence(0, INFINITY, 100, "e"), fence(0, 0, 0, "f"),
        fence(0, 0, -5, "g"), fence(0, 0, NAN, "h"), fence(0, 0, INFINITY, "i"),
    };
    for (size_t i = 0; i < G_N_ELEMENTS(bad); i++)
        assert(zyl_location_add_geofence(&svc, APP_A, &bad[i]) == -1);
    assert(svc.geofence_count == 0);
}

static void test_geofence_enter_and_exit_follow_distance(void) {
    double seoul_busan = haversine_m(37.5663, 126.9779, 35.1796, 129.0756);
    assert(seoul_busan > 320000 && seoul_busan < 330000);
    assert(haversine_m(37.5, 127.0, 37.5, 127.0) == 0.0);
    ZylGeofence f = fence(37.5663, 126.9779, 200, "home");
    assert(zyl_location_add_geofence(&svc, APP_A, &f) == 0);
    ZylLocation at = { .latitude = 37.5672, .longitude = 126.9779 };   /* ~100 m north */
    check_geofences(&svc, &at);
    assert(svc.geofences[0].inside);
    at.latitude = 37.5763;                                              /* ~1.1 km */
    check_geofences(&svc, &at);
    assert(!svc.geofences[0].inside);
    at.latitude = 37.5663;
    check_geofences(&svc, &at);
    assert(svc.geofences[0].inside);
}

static double since_ms(const struct timespec *t0) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (double)(t1.tv_sec - t0->tv_sec) * 1000.0 + (double)(t1.tv_nsec - t0->tv_nsec) / 1e6;
}

static void test_interval_is_bounded_and_stopping_is_prompt(void) {
    alarm(10);                                                      /* a hang fails the run */
    assert(zyl_location_request_updates(&svc, -5, NULL, NULL) == 0);
    assert(svc.interval_ms == 100);
    assert(zyl_location_request_updates(&svc, INT_MAX, NULL, NULL) == 0);  /* restarts it */
    assert(svc.interval_ms == 10 * 60 * 1000);
    usleep(300000);                                                 /* the thread is asleep now */
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    zyl_location_stop_updates(&svc);                                /* on the D-Bus main loop */
    assert(since_ms(&t0) < 1000 && !svc.updates_active);
    assert(zyl_location_request_updates(&svc, 60 * 60 * 1000, NULL, NULL) == 0);
    usleep(300000);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    assert(zyl_location_request_updates(&svc, 1000, NULL, NULL) == 0);
    assert(since_ms(&t0) < 1000);
    zyl_location_stop_updates(&svc);
    alarm(0);
}

int main(void) {
    printf("=== Zyl OS Location Service Tests ===\n");
    init_sync(&svc);
    const char *path = g_getenv("PATH");
    char *with_fake = g_strconcat(ROOT_ "/bin:", path ? path : "/usr/bin:/bin", NULL);
    g_setenv("PATH", with_fake, TRUE);
    g_free(with_fake);
    RUN_TEST(test_geoip_answer_becomes_a_network_fix);
    RUN_TEST(test_malformed_geoip_answers_give_no_fix);
    RUN_TEST(test_geoip_numbers_off_the_globe_are_dropped);
    RUN_TEST(test_oversized_geoip_answer_is_refused);
    RUN_TEST(test_wifi_scan_and_mls_answer_give_a_wifi_fix);
    RUN_TEST(test_wifi_noise_and_bad_answers_fall_back);
#ifdef HAVE_GPSD
    RUN_TEST(test_gps_unknown_fields_never_reach_apps_as_nan);
    RUN_TEST(test_gps_without_a_usable_fix_falls_back);
#endif
    RUN_TEST(test_geofence_slots_tags_and_limits);
    RUN_TEST(test_geofences_belong_to_the_calling_app);
    RUN_TEST(test_impossible_geofences_are_refused);
    RUN_TEST(test_geofence_enter_and_exit_follow_distance);
    RUN_TEST(test_interval_is_bounded_and_stopping_is_prompt);
    reset();
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
