/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: Wi-Fi HAL Linux 구현(runtime/hal/hal_wifi.c)과 Wi-Fi 서비스 정책 테스트
 * 수행범위: nmcli -t 출력 해석(이름·BSSID 의 \: 복원, 숨은 네트워크 제외, 같은
 *           이름의 AP 여럿은 한 네트워크), Wi-Fi 장치 유무, AP 모드가 없으면
 *           핫스팟 없음, 핫스팟은 IPv4·IPv6 공유로 만들어짐, 핫스팟 이름·암호
 *           규칙(길이, 비우면 생성)
 * 의존방향: hal_wifi.c, runtime/services/wifi/wifi.c (ZYL_TEST), PATH 의 가짜 nmcli
 * SOLID: SRP — 무선 장치 접근·정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <sys/stat.h>
#include "../runtime/hal/hal_wifi.c"
#include "../runtime/services/wifi/wifi.c"

#define DIR_ "/tmp/zyl-wifi-test"

static void put(const char *path, const char *text) { assert(g_file_set_contents(path, text, -1, NULL)); }

static char *calls(void) {
    char *t = NULL;
    if (!g_file_get_contents(DIR_ "/calls", &t, NULL, NULL)) t = g_strdup("");
    return t;
}

int main(void) {
    printf("=== Wi-Fi HAL (nmcli) and service policy ===\n");
    assert(system("rm -rf " DIR_ " && mkdir -p " DIR_) == 0);
    /* Answers the queries the HAL makes; logs everything else */
    put(DIR_ "/nmcli",
        "#!/bin/sh\n"
        "case \"$*\" in\n"
        "  '-t -f DEVICE,TYPE device') [ -f " DIR_ "/nowifi ] && echo 'eth0:ethernet' || printf 'eth0:ethernet\\nwlan0:wifi\\n' ;;\n"
        "  '-t -f WIFI-PROPERTIES.AP device show wlan0') [ -f " DIR_ "/noap ] && echo 'WIFI-PROPERTIES.AP:no' || echo 'WIFI-PROPERTIES.AP:yes' ;;\n"
        "  '-t -f SSID,SIGNAL,SECURITY,ACTIVE,BSSID device wifi list') cat <<'OUT'\n"
        "Cafe\\:Wifi:80:WPA2:no:AA\\:BB\\:CC\\:00\\:00\\:01\n"
        "Home:40:WPA2 WPA3:no:AA\\:BB\\:CC\\:00\\:00\\:02\n"
        "Home:70:WPA2 WPA3:yes:AA\\:BB\\:CC\\:00\\:00\\:03\n"
        ":90:WPA2:no:AA\\:BB\\:CC\\:00\\:00\\:04\n"
        "Lobby:55::no:AA\\:BB\\:CC\\:00\\:00\\:05\n"
        "OUT\n"
        "  ;;\n"
        "  *) echo \"nmcli $*\" >> " DIR_ "/calls ;;\n"
        "esac\n");
    assert(chmod(DIR_ "/nmcli", 0755) == 0);
    char *path = g_strconcat(DIR_ ":", g_getenv("PATH"), NULL);
    g_setenv("PATH", path, TRUE);
    g_free(path);
    const ZylWifiHal *wifi = zyl_hal_wifi();

    printf("  %-60s ", "names and BSSIDs keep their colons; hidden ones left out");
    ZylWifiNetwork nets[16];
    int n = wifi->list_networks(nets, 16);
    assert(n == 3);
    assert(strcmp(nets[0].ssid, "Cafe:Wifi") == 0 && strcmp(nets[0].bssid, "AA:BB:CC:00:00:01") == 0);
    assert(strcmp(nets[2].ssid, "Lobby") == 0 && strcmp(nets[2].security, "Open") == 0);
    printf("PASS\n");

    printf("  %-60s ", "one row per network: in use, strongest signal");
    assert(strcmp(nets[1].ssid, "Home") == 0 && nets[1].connected);
    assert(nets[1].signal_dbm == -100 + 70 * 70 / 100);
    printf("PASS\n");

    printf("  %-60s ", "a hotspot shares IPv4 and IPv6 from a fresh profile");
    assert(wifi->present() && wifi->hotspot_supported());
    assert(wifi->hotspot_start("ZylOS-1A2B", "abcd2345efgh"));
    char *c = calls();
    assert(strstr(c, "nmcli connection delete Hotspot\n"));
    assert(strstr(c, "nmcli connection add type wifi ifname wlan0 con-name Hotspot autoconnect no "
                     "ssid ZylOS-1A2B 802-11-wireless.mode ap ipv4.method shared ipv6.method shared "
                     "wifi-sec.key-mgmt wpa-psk wifi-sec.psk abcd2345efgh\n"));
    assert(strstr(c, "nmcli connection up Hotspot\n"));
    g_free(c);
    printf("PASS\n");

    printf("  %-60s ", "no AP mode, or no Wi-Fi at all: no hotspot");
    put(DIR_ "/noap", "");
    assert(wifi->present() && !wifi->hotspot_supported());
    put(DIR_ "/nowifi", "");
    assert(!wifi->present() && !wifi->hotspot_supported() && !wifi->disconnect());
    printf("PASS\n");

    printf("  %-60s ", "hotspot name and passphrase rules; empty ones are made up");
    char ssid[64], pass[64];
    assert(zyl_wifi_hotspot_credentials("", "", ssid, sizeof(ssid), pass, sizeof(pass)) == NULL);
    assert(g_str_has_prefix(ssid, "ZylOS-") && strlen(ssid) == 10 && strlen(pass) == 12);
    assert(strcmp(zyl_wifi_hotspot_credentials("x", "short", ssid, 64, pass, 64), "passphrase") == 0);
    assert(strcmp(zyl_wifi_hotspot_credentials("123456789012345678901234567890123", "longenough",
                                               ssid, 64, pass, 64), "ssid") == 0);
    assert(zyl_wifi_hotspot_credentials("Mine", "my-passphrase", ssid, 64, pass, 64) == NULL);
    assert(strcmp(ssid, "Mine") == 0 && strcmp(pass, "my-passphrase") == 0);
    printf("PASS\n");

    assert(system("rm -rf " DIR_) == 0);
    printf("\nResults: 5/5 passed\n");
    return 0;
}
