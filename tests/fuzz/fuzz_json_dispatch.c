/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Fuzz Test
 *
 * 역할: 실제 bridge.c JSON dispatch 퍼징 하네스
 * 수행범위: 임의 JSON 입력으로 zyl_bridge_dispatch() robustness 검증
 * 의존방향: GLib/JSON-GLib, runtime/wam/src/bridge/bridge.c, mock WebKit shim
 * SOLID: SRP — 실제 브릿지 디스패치 경로 퍼징만 담당
 *
 * 예시 빌드:
 * clang -g -O1 -fsanitize=fuzzer,address \
 *   -DZYL_USE_WEBKIT2GTK \
 *   -Itests/fuzz/include -I. \
 *   tests/fuzz/fuzz_json_dispatch.c \
 *   $(pkg-config --cflags --libs glib-2.0 gio-2.0 json-glib-1.0) \
 *   -o fuzz_json_dispatch
 * ────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "include/mock_webkit.h"
#include "../../runtime/wam/src/bridge/bridge.h"

static void fuzz_handler(const char *type,
                         gpointer msg_obj,
                         ZylAppManifest *manifest,
                         ZylBridgeReplyCtx *reply_ctx,
                         gpointer user_data)
{
    (void)type;
    (void)msg_obj;
    (void)manifest;
    (void)user_data;
    if (reply_ctx && reply_ctx->callback_id >= 0) {
        zyl_bridge_respond(reply_ctx->webview,
                           reply_ctx->callback_id,
                           "{\"ok\":true}");
    }
}

/* Pull real implementation in so we fuzz the production parser/dispatcher. */
#include "../../runtime/wam/src/bridge/bridge.c"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data || size == 0 || size > 8192)
        return 0;

    char *buf = malloc(size + 1);
    if (!buf)
        return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    WebKitWebView view = {0};
    ZylAppManifest manifest = {
        .id = (char *)"com.zylos.fuzz",
        .name = (char *)"Fuzz App",
        .version = (char *)"1.0.0",
    };

    zyl_bridge_init();
    /* Override with safe handlers so fuzzing does not require a live D-Bus. */
    zyl_bridge_register_handler("test.echo", fuzz_handler, NULL);
    zyl_bridge_register_handler("notification.create", fuzz_handler, NULL);

    zyl_bridge_dispatch(&view, &manifest, buf);

    g_free(view.last_script);
    zyl_bridge_cleanup();
    free(buf);
    return 0;
}
