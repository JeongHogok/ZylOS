/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: VPN 서비스 인터페이스 — WireGuard/OpenVPN 연결 관리
 * 수행범위: VPN 프로필 관리, 연결/해제, 상태 조회
 * 의존방향: stdbool.h
 * SOLID: ISP — VPN 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_VPN_H
#define ZYL_VPN_H

#include <stdbool.h>

typedef enum {
    ZYL_VPN_TYPE_WIREGUARD = 0,
    ZYL_VPN_TYPE_OPENVPN   = 1,
} ZylVpnType;

typedef enum {
    ZYL_VPN_DISCONNECTED = 0,
    ZYL_VPN_CONNECTING   = 1,
    ZYL_VPN_CONNECTED    = 2,
    ZYL_VPN_ERROR        = 3,
} ZylVpnState;

typedef struct {
    char *name;
    ZylVpnType type;
    char *config_path;    /* .conf 파일 경로 */
    ZylVpnState state;
} ZylVpnProfile;

int  zyl_vpn_connect(const char *profile_name);
int  zyl_vpn_disconnect(void);
ZylVpnState zyl_vpn_get_state(void);

int  zyl_vpn_import_profile(const char *config_path, const char *name);
int  zyl_vpn_remove_profile(const char *name);
int  zyl_vpn_list_profiles(ZylVpnProfile **out, int *count);
void zyl_vpn_profile_free(ZylVpnProfile *profiles, int count);

#define ZYL_VPN_DBUS_NAME "org.zylos.VpnService"
#define ZYL_VPN_DBUS_PATH "/org/zylos/VpnService"

#endif /* ZYL_VPN_H */
