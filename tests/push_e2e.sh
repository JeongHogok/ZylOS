#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test
#
# 역할: 푸시 종단 테스트 — 앱 서버가 끝점에 보낸 것이 기기에 닿는지
# 수행범위: 로컬 HTTPS 의 ntfy 흉내 서버(구독 스트림 /<토픽들>/json?since=, 발행
#           POST/PUT /<토픽>) → 사설 시스템 버스의 push·notification 데몬(테스트 경로로
#           빌드) → 등록하면 끝점, 앱이 떠 있지 않으면 그 앱이 준 알림, 서버가 끊겼다
#           돌아오면 다시 붙고 끊긴 동안 온 것도(since) 받음, 등록을 지우면 더는 안 옴
# 의존방향: openssl, curl, python3, dbus-run-session, gdbus
# SOLID: SRP — 푸시 종단 흐름 검증만
#
# meson test 'push-e2e' 가 빌드된 데몬 경로를 넘긴다. 떠 있는 앱에 가는 PushReceived
# 는 WAM 이 있어야 해 여기서는 보지 않는다(브리지의 주인 전용 전달은 test_bridge_dispatch)
# ──────────────────────────────────────────────────────────
set -euo pipefail
PUSH=$1 NOTIF=$2
R=/tmp/zyl-push-e2e
rm -rf "$R"
mkdir -p "$R"/{data,notifications}
APP=com.example.chat

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$R/tls.key" -out "$R/tls.crt" -days 1 \
    -subj /CN=127.0.0.1 -addext subjectAltName=IP:127.0.0.1 2>/dev/null
PORT=$(python3 -I -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')

# What ntfy does, as much as a device uses: a stream of the topics asked for
# (and what came after `since`), and publishing to a topic
cat > "$R/server.py" <<'PY'
import http.server, json, ssl, sys, threading, time
port, crt, key = int(sys.argv[1]), sys.argv[2], sys.argv[3]
lock = threading.Condition()
cache = []                       # every message: (id, topic, text)
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        path, _, query = self.path.partition('?')
        topics = path.strip('/').split('/')[0].split(',')
        since = dict(p.split('=', 1) for p in query.split('&') if '=' in p).get('since')
        self.send_response(200); self.send_header('Content-Type', 'application/x-ndjson'); self.end_headers()
        def line(o): self.wfile.write((json.dumps(o) + '\n').encode()); self.wfile.flush()
        line({'id': 'open', 'event': 'open', 'topic': ','.join(topics)})
        sent = 0
        if since:
            ids = [m[0] for m in cache]
            sent = ids.index(since) + 1 if since in ids else 0
        else:
            sent = len(cache)
        try:
            while True:
                with lock:
                    lock.wait_for(lambda: len(cache) > sent, timeout=1)
                    new, sent = cache[sent:], len(cache)
                for mid, topic, text in new:
                    if topic in topics:
                        line({'id': mid, 'time': int(time.time()), 'event': 'message', 'topic': topic, 'message': text})
                if not new: line({'id': 'ka', 'event': 'keepalive', 'topic': ','.join(topics)})
        except (BrokenPipeError, ConnectionResetError):
            pass
    def publish(self):
        topic = self.path.strip('/').split('?')[0]
        text = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode()
        with lock:
            cache.append(('m%d' % len(cache), topic, text)); lock.notify_all()
        self.send_response(200); self.send_header('Content-Length', '0'); self.end_headers()
    do_POST = do_PUT = publish
srv = http.server.ThreadingHTTPServer(('127.0.0.1', port), H)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(crt, key)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
PY
python3 -I "$R/server.py" "$PORT" "$R/tls.crt" "$R/tls.key" >/dev/null 2>&1 & SERVER=$!
trap 'kill $SERVER 2>/dev/null || true; rm -rf "$R"' EXIT   # it may be gone already (the restart)
printf '{"url": "https://127.0.0.1:%s"}' "$PORT" > "$R/push.json"
export CURL_CA_BUNDLE="$R/tls.crt"
for i in $(seq 50); do curl -sf -X POST -d warm "https://127.0.0.1:$PORT/warmup" >/dev/null && break; sleep 0.1; done

dbus-run-session -- bash -s "$PUSH" "$NOTIF" "$R" "$APP" "$PORT" <<'BUS'
set -euo pipefail
PUSH=$1 NOTIF=$2 R=$3 APP=$4 PORT=$5
export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
"$NOTIF" & NP=$!
"$PUSH" & PP=$!
SP=
trap 'kill $NP $PP $SP 2>/dev/null || true' EXIT     # nothing outlives the test
for n in Notification Push; do
  for i in $(seq 50); do gdbus introspect --system -d org.zylos.$n -o /org/zylos/$n >/dev/null 2>&1 && break; sleep 0.1; done
done
call() { gdbus call --system -d "org.zylos.$1" -o "/org/zylos/$1" -m "org.zylos.$1.$2" "${@:3}"; }
fail() { echo "✗ $*"; exit 1; }
# Within a few seconds the user sees the app's notification with this title
shown() { for i in $(seq 100); do call Notification GetActive | grep -q "$1" && return 0; sleep 0.1; done; return 1; }

OUT=$(call Push Register "$APP" "Chat" "New message")
ENDPOINT=$(echo "$OUT" | sed -n "s/^('\(.*\)',)$/\1/p")
case "$ENDPOINT" in https://127.0.0.1:$PORT/up????????????'?up=1') ;; *) fail "endpoint: $OUT" ;; esac
[ "$(call Push Register "$APP" "Chat" "New message")" = "$OUT" ] || fail "a second register changed the endpoint"
echo "  ✓ register: an endpoint of the app's own, the same each time"

curl -sf -X PUT -d 'version=1' "$ENDPOINT" >/dev/null
shown "'Chat'" || fail "a message for an app that is not running showed nothing"
echo "  ✓ a message while the app is not running: the notification it gave"

# The server goes away and comes back; what was sent meanwhile still arrives
kill "$(pgrep -f server.py)"; sleep 1
python3 -I "$R/server.py" "$PORT" "$R/tls.crt" "$R/tls.key" >/dev/null 2>&1 & SP=$!
sleep 1
call Notification ClearAll >/dev/null
curl -sf -X POST -d 'version=2' "$ENDPOINT" >/dev/null
for i in $(seq 200); do call Notification GetActive | grep -q "'Chat'" && break; sleep 0.1; done
call Notification GetActive | grep -q "'Chat'" || fail "no reconnect after the server came back"
echo "  ✓ reconnects after the server comes back"

call Push Unregister "$APP" | grep -q true || fail "unregister"
call Notification ClearAll >/dev/null
curl -sf -X POST -d 'version=3' "$ENDPOINT" >/dev/null; sleep 2
call Notification GetActive | grep -q "'Chat'" && fail "a message came after unregister"
python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); assert not d["apps"], d' "$R/data/registrations.json" \
    || fail "the registration stayed on disk"
echo "  ✓ unregistered: nothing more, nothing kept"
BUS
