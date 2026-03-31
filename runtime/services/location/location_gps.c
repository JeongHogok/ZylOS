/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Provider
 *
 * 역할: GPS 위치 프로바이더 — GPSD 연결, 읽기, 해제
 * 수행범위: libgps 연동을 통한 하드웨어 GPS 데이터 수집
 * 의존방향: location_internal.h, libgps
 * SOLID: SRP — GPS 프로바이더 기능만 담당
 * ────────────────────────────────────────────────────────── */

#include "location_internal.h"

#ifdef HAVE_GPSD

bool gpsd_connect(ZylLocationService *svc) {
    if (gps_open(GPSD_HOST, GPSD_PORT, &svc->gps_data) != 0) {
        g_message("[Location] Cannot connect to GPSD at %s:%s",
                  GPSD_HOST, GPSD_PORT);
        return false;
    }
    if (gps_stream(&svc->gps_data, WATCH_ENABLE | WATCH_JSON, NULL) != 0) {
        g_warning("[Location] gps_stream() failed, closing GPSD connection");
        gps_close(&svc->gps_data);
        return false;
    }
    svc->gpsd_connected = true;
    g_message("[Location] Connected to GPSD");
    return true;
}

bool gpsd_read(ZylLocationService *svc, ZylLocation *loc) {
    if (!svc->gpsd_connected) return false;

    if (!gps_waiting(&svc->gps_data, 500000)) /* 500ms timeout */
        return false;

    if (gps_read(&svc->gps_data, NULL, 0) == -1) {
        g_warning("[Location] GPSD read error");
        svc->gpsd_connected = false;
        return false;
    }

    if (svc->gps_data.fix.mode < MODE_2D)
        return false;

    loc->latitude = svc->gps_data.fix.latitude;
    loc->longitude = svc->gps_data.fix.longitude;
    loc->altitude_m = (svc->gps_data.fix.mode >= MODE_3D)
                      ? svc->gps_data.fix.altMSL : 0.0;
    loc->accuracy_m = (float)svc->gps_data.fix.eph;
    loc->speed_mps = (float)svc->gps_data.fix.speed;
    loc->bearing_deg = (float)svc->gps_data.fix.track;
    loc->timestamp_ms = now_ms();
    snprintf(loc->provider, sizeof(loc->provider), "gps");
    return true;
}

void gpsd_disconnect(ZylLocationService *svc) {
    if (svc->gpsd_connected) {
        gps_stream(&svc->gps_data, WATCH_DISABLE, NULL);
        gps_close(&svc->gps_data);
        svc->gpsd_connected = false;
    }
}

#endif /* HAVE_GPSD */
