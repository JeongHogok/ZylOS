/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: Wi-Fi 네트워크 목록(runtime/services/wifi) 단위 테스트
 * 수행범위: nmcli -t 출력 해석 — 이름·BSSID 의 이스케이프(\: \\) 복원,
 *           숨은 네트워크 제외, 같은 이름의 AP 여럿은 한 네트워크(연결 중·
 *           가장 강한 신호), 개방형 표시
 * 의존방향: runtime/services/wifi/wifi.c (main 이름 변경), PATH 의 가짜 nmcli
 * SOLID: SRP — 목록 해석 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#define main wifi_main
#include "../runtime/services/wifi/wifi.c"
#undef main

#define DIR_ "/tmp/zyl-wifi-test"

int main(void) {
    printf("=== Wi-Fi network list (nmcli) ===\n");
    assert(system("rm -rf " DIR_ " && mkdir -p " DIR_) == 0);
    /* SSID:SIGNAL:SECURITY:ACTIVE:BSSID, as nmcli -t prints them */
    assert(g_file_set_contents(DIR_ "/nmcli",
        "#!/bin/sh\n"
        "cat <<'OUT'\n"
        "Cafe\\:Wifi:80:WPA2:no:AA\\:BB\\:CC\\:00\\:00\\:01\n"
        "Home:40:WPA2 WPA3:no:AA\\:BB\\:CC\\:00\\:00\\:02\n"
        "Home:70:WPA2 WPA3:yes:AA\\:BB\\:CC\\:00\\:00\\:03\n"
        ":90:WPA2:no:AA\\:BB\\:CC\\:00\\:00\\:04\n"
        "Lobby:55::no:AA\\:BB\\:CC\\:00\\:00\\:05\n"
        "OUT\n", -1, NULL));
    assert(chmod(DIR_ "/nmcli", 0755) == 0);
    char *path = g_strconcat(DIR_ ":", g_getenv("PATH"), NULL);
    g_setenv("PATH", path, TRUE);
    g_free(path);

    ZylWifiService svc = {0};
    ZylWifiNetwork *nets = NULL;
    int n = 0;
    assert(zyl_wifi_get_networks(&svc, &nets, &n) == 0);

    printf("  %-56s ", "names and BSSIDs keep their colons");
    assert(n == 3);
    assert(strcmp(nets[0].ssid, "Cafe:Wifi") == 0);
    assert(strcmp(nets[0].bssid, "AA:BB:CC:00:00:01") == 0);
    printf("PASS\n");

    printf("  %-56s ", "one row per network: in use, strongest signal");
    assert(strcmp(nets[1].ssid, "Home") == 0 && nets[1].connected);
    assert(nets[1].signal == -100 + 70 * 70 / 100);
    printf("PASS\n");

    printf("  %-56s ", "hidden networks left out; open ones say so");
    assert(strcmp(nets[2].ssid, "Lobby") == 0 && strcmp(nets[2].security, "Open") == 0);
    printf("PASS\n");

    zyl_wifi_network_free(nets, n);
    assert(system("rm -rf " DIR_) == 0);
    printf("\nResults: 3/3 passed\n");
    return 0;
}
