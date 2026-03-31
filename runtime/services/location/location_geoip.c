/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Provider
 *
 * 역할: GeoIP 위치 프로바이더 — HTTP GeoIP 조회를 통한 네트워크 기반 위치 추정
 * 수행범위: libcurl HTTP 요청, JSON 응답 파싱, 위치 데이터 변환
 * 의존방향: location_internal.h, libcurl
 * SOLID: SRP — GeoIP 네트워크 위치 추정만 담당
 * ────────────────────────────────────────────────────────── */

#include "location_internal.h"

#ifdef HAVE_CURL
#include <curl/curl.h>

typedef struct {
    char *data;
    size_t len;
} CurlBuffer;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb,
                             void *userdata) {
    CurlBuffer *buf = userdata;
    size_t total = size * nmemb;
    char *tmp = realloc(buf->data, buf->len + total + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

bool geoip_query(ZylLocation *loc) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    CurlBuffer buf = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, GEOIP_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || !buf.data) {
        free(buf.data);
        return false;
    }

    /* 간단한 JSON 파싱 (GLib JSON 없이) */
    double lat = 0.0, lon = 0.0;
    const char *p;

    p = strstr(buf.data, "\"lat\":");
    if (p) lat = atof(p + 6);
    p = strstr(buf.data, "\"lon\":");
    if (p) lon = atof(p + 6);

    free(buf.data);

    if (lat == 0.0 && lon == 0.0) return false;

    loc->latitude = lat;
    loc->longitude = lon;
    loc->altitude_m = 0.0;
    loc->accuracy_m = NETWORK_ACCURACY_M;
    loc->speed_mps = 0.0f;
    loc->bearing_deg = 0.0f;
    loc->timestamp_ms = now_ms();
    snprintf(loc->provider, sizeof(loc->provider), "network");

    g_message("[Location] GeoIP fix: lat=%.4f lon=%.4f", lat, lon);
    return true;
}

#else

bool geoip_query(ZylLocation *loc) {
    (void)loc;
    g_message("[Location] GeoIP unavailable (built without libcurl)");
    return false;
}

#endif /* HAVE_CURL */
