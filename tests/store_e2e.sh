#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test
#
# 역할: 앱스토어 종단 테스트 — 설치부터 삭제까지, 앱이 남기는 것이 없는지
# 수행범위: 스토어 키·TLS 인증서 생성 → tools/build-store.py 로 패키지·서명된
#           카탈로그 → 로컬 HTTPS 서버 → 사설 세션 버스의 appstore·appdata·fs·
#           notification 데몬(테스트 경로로 빌드) → 카탈로그, 설치, 앱이 데이터·
#           파일·알림·웹 저장소를 만들고 → 삭제 → 기기 쪽 어디에도 앱 흔적 없음.
#           변조된 패키지·서명 안 맞는 카탈로그는 거부
# 의존방향: openssl, curl, python3, dbus-run-session, gdbus
# SOLID: SRP — 스토어 종단 흐름 검증만
#
# meson test 'store-e2e' 가 빌드된 데몬과 도구 경로를 넘긴다
# ──────────────────────────────────────────────────────────
set -euo pipefail
APPSTORE=$1 APPDATA=$2 FS=$3 NOTIF=$4 TOOL=$5
R=/tmp/zyl-store-e2e
rm -rf "$R"
mkdir -p "$R"/{apps,appdata,files,system,notifications,srv,src/hello}
ID=com.example.hello

openssl genrsa -out "$R/store.key" 2048 2>/dev/null
openssl rsa -in "$R/store.key" -pubout -out "$R/store.pub" 2>/dev/null
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$R/tls.key" -out "$R/tls.crt" -days 1 \
    -subj /CN=127.0.0.1 -addext subjectAltName=IP:127.0.0.1 2>/dev/null

printf '{"id":"%s","name":"Hello","version":"1.0.0","entry":"index.html","permissions":["network"]}' "$ID" \
    > "$R/src/hello/app.json"
echo '<!doctype html><title>hello</title>' > "$R/src/hello/index.html"
python3 -I "$TOOL" package "$R/src/hello" "$R/srv" >/dev/null
python3 -I "$TOOL" catalog --key "$R/store.key" "$R/srv" >/dev/null

PORT=$(python3 -I -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
python3 -I - "$R/srv" "$PORT" "$R/tls.crt" "$R/tls.key" >/dev/null 2>&1 <<'PY' &
import functools, http.server, ssl, sys
root, port, crt, key = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
h = functools.partial(http.server.SimpleHTTPRequestHandler, directory=root)
srv = http.server.HTTPServer(('127.0.0.1', port), h)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(crt, key)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
PY
SERVER=$!
trap 'kill $SERVER 2>/dev/null; rm -rf "$R"' EXIT
python3 -I - "$R" "$PORT" <<'PY'
import json, sys
r, port = sys.argv[1], sys.argv[2]
json.dump({'url': 'https://127.0.0.1:%s/catalog.json' % port, 'publicKey': open(r + '/store.pub').read()},
          open(r + '/store.json', 'w'))
PY
export CURL_CA_BUNDLE="$R/tls.crt"
for i in $(seq 50); do curl -sf "https://127.0.0.1:$PORT/catalog.json" >/dev/null && break; sleep 0.1; done

dbus-run-session -- bash -s "$APPSTORE" "$APPDATA" "$FS" "$NOTIF" "$R" "$ID" <<'BUS'
set -euo pipefail
APPSTORE=$1 APPDATA=$2 FS=$3 NOTIF=$4 R=$5 ID=$6
"$APPSTORE" & "$APPDATA" & "$FS" & "$NOTIF" &
for n in AppStore AppData FileSystem Notification; do
  for i in $(seq 50); do gdbus introspect --session -d org.zylos.$n -o /org/zylos/$n >/dev/null 2>&1 && break; sleep 0.1; done
done
call() { gdbus call --session -d "org.zylos.$1" -o "/org/zylos/$1" -m "org.zylos.$1.$2" "${@:3}"; }
fail() { echo "✗ $*"; exit 1; }

call AppStore GetCatalog | grep -q "'state': <'ok'>" || fail "catalog not ok"
call AppStore GetCatalog | grep -q "'id': <'$ID'>" || fail "app not listed"
OUT=$(call AppStore Install "$ID" 2>&1); echo "$OUT" | grep -q "'ok': <true>" || fail "install failed: $OUT"
[ -f "$R/apps/$ID/index.html" ] || fail "not installed"
call AppStore Install "$ID" | grep -q "up_to_date" || fail "reinstalled the same version"
echo "  ✓ catalog, install"

# What an installed app makes: private data, its files, a notification, web storage
call AppData SetItem "$ID" k v >/dev/null
call FileSystem WriteFile "$ID" "{'path': <'Documents/n.txt'>, 'content': <'mine'>}" >/dev/null
call Notification Post "$ID" general T B "" 1 >/dev/null
mkdir -p "$R/appdata/$ID/webkit/data" && echo ls > "$R/appdata/$ID/webkit/data/localstorage"
[ -f "$R/appdata/$ID/files/Documents/n.txt" ] && [ -f "$R/appdata/$ID/appdata" ] || fail "app data not in its container"

call AppStore Uninstall "$ID" | grep -q "'ok': <true>" || fail "uninstall failed"
sleep 0.5
left=$(find "$R/apps" "$R/appdata" "$R/notifications" -path "*$ID*" 2>/dev/null; \
       grep -l "$ID" "$R/notifications/active.json" 2>/dev/null || true)
[ -z "$left" ] || fail "left behind: $left"
call Notification GetActive | grep -q "$ID" && fail "notification left"
call AppStore Uninstall "$ID" | grep -q "not_installed" || fail "uninstalled twice"
echo "  ✓ uninstall leaves nothing of the app"

# A package changed after signing, and a catalog not signed by the store
cp "$R/srv/$ID-1.0.0.zpkg" "$R/pkg.bak"
printf 'x' >> "$R/srv/$ID-1.0.0.zpkg"
call AppStore Install "$ID" | grep -q "bad_package\|download_failed" || fail "tampered package accepted"
[ ! -e "$R/apps/$ID" ] || fail "tampered package installed"
cp "$R/pkg.bak" "$R/srv/$ID-1.0.0.zpkg"
sed -i 's/"Hello"/"Evil"/' "$R/srv/catalog.json"
call AppStore GetCatalog | grep -q "'state': <'untrusted'>" || fail "unsigned catalog trusted"
echo "  ✓ tampered package and catalog refused"
BUS
echo "=== store e2e passed ==="
