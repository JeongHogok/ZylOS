/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Fuzz Test
 *
 * 역할: bridge.c JSON dispatch 퍼징 하네스
 * 수행범위: 임의 JSON 입력으로 dispatch 함수 robustness 검증
 * 의존방향: json-glib, bridge.c dispatch API
 * SOLID: SRP — JSON dispatch 퍼징만 담당
 *
 * 빌드: clang -g -O1 -fsanitize=fuzzer,address fuzz_json_dispatch.c -o fuzz_json
 * 실행: ./fuzz_json corpus/ -max_len=4096
 * ────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * Stub: simulates the WAM bridge dispatch parser.
 * In production, link against the actual bridge.c dispatch function.
 * This stub validates the JSON structure without crashing.
 */

/* Known service names for validation */
static const char *VALID_SERVICES[] = {
    "fs", "device", "storage", "apps", "settings",
    "terminal", "wifi", "bluetooth", "network", "browser",
    "notification", "power", "display", "input", "sensors",
    "location", "telephony", "contacts", "messaging", "usb",
    "user", "credential", "appstore", "updater", "sandbox",
    "logger", "accessibility", "audio", NULL
};

static int is_valid_json_start(const uint8_t *data, size_t size) {
    if (size == 0) return 0;
    /* Skip whitespace */
    size_t i = 0;
    while (i < size && (data[i] == ' ' || data[i] == '\t' || data[i] == '\n' || data[i] == '\r')) i++;
    if (i >= size) return 0;
    return (data[i] == '{' || data[i] == '[');
}

/* Simple JSON string extractor (no full parser — crash resistance check) */
static int extract_field(const char *json, const char *key, char *out, size_t out_len) {
    if (!json || !key || !out || out_len == 0) return -1;
    out[0] = '\0';

    char search[128];
    int n = snprintf(search, sizeof(search), "\"%s\"", key);
    if (n < 0 || (size_t)n >= sizeof(search)) return -1;

    const char *pos = strstr(json, search);
    if (!pos) return -1;

    pos += strlen(search);
    /* Skip : and whitespace */
    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;
    if (*pos == '"') {
        pos++;
        size_t j = 0;
        while (*pos && *pos != '"' && j < out_len - 1) {
            out[j++] = *pos++;
        }
        out[j] = '\0';
        return 0;
    }
    return -1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 8192) return 0; /* Limit input size */
    if (!is_valid_json_start(data, size)) return 0;

    /* Null-terminate for string operations */
    char *buf = (char *)malloc(size + 1);
    if (!buf) return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    /* Extract service and method fields */
    char service[64] = {0};
    char method[64] = {0};
    char params[4096] = {0};

    extract_field(buf, "service", service, sizeof(service));
    extract_field(buf, "method", method, sizeof(method));
    extract_field(buf, "params", params, sizeof(params));

    /* Validate service name against known list */
    if (service[0]) {
        int valid = 0;
        for (int i = 0; VALID_SERVICES[i]; i++) {
            if (strcmp(service, VALID_SERVICES[i]) == 0) {
                valid = 1;
                break;
            }
        }
        /* Invalid service should not crash — just reject */
        (void)valid;
    }

    /* Simulate dispatch: ensure no crash on any input combination */
    if (service[0] && method[0]) {
        /* Would call: bridge_dispatch(service, method, params) */
        /* For fuzz purposes: verify no UB in string operations */
        volatile size_t slen = strlen(service);
        volatile size_t mlen = strlen(method);
        (void)slen;
        (void)mlen;
    }

    free(buf);
    return 0;
}
