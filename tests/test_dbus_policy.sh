#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test
#
# 역할: Zyl 서비스 버스 — 시스템 버스에서 이름을 갖고 서로 부를 수 있는지
# 수행범위: 배포하는 정책(system/dbus-1/zyl-os.conf)을 올린 사설 시스템 버스에서
#           alarm → notification 호출이 실제로 건너가는지, 정책 밖의 사용자는
#           이름을 갖지 못하는지
# 의존방향: dbus-daemon, gdbus, 빌드된 alarm·notification 데몬
# SOLID: SRP — 버스 소유·호출 권한만 검증
#
# 이 검사가 없을 때 벌어진 일: 서비스들이 세션 버스에 이름을 올리는데 유닛은
# Type=dbus + BusName= 로 시스템 버스를 기다렸다. 기기에서는 모든 서비스가
# 영원히 "activating" 에 머물러 부팅이 끝나지 않는다. 버스를 띄우는 주체도
# 트리에 없었다.
# ──────────────────────────────────────────────────────────
set -euo pipefail

ALARM="${1:?alarm 데몬 경로}"
NOTIF="${2:?notification 데몬 경로}"
POLICY="${3:?정책 파일 경로}"

for c in dbus-daemon gdbus; do
    command -v "$c" >/dev/null 2>&1 || { echo "SKIP: $c 없음"; exit 77; }  # meson: 건너뜀
done

R=/tmp/zyl-bus-test
rm -rf "$R"
mkdir -p "$R/alarms" "$R/notifications"

pass=0
fail=0
ok()   { echo "  ✓ $1"; pass=$((pass + 1)); }
bad()  { echo "  ✗ $1"; fail=$((fail + 1)); }

# ── 실제 시스템 버스의 모양을 그대로: 기본 거부 + 우리 정책 ──
cat > "$R/bus.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>system</type>
  <listen>unix:tmpdir=$R</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow user="*"/>
    <deny own="*"/>
    <deny send_type="method_call"/>
    <allow send_destination="org.freedesktop.DBus"/>
    <allow send_type="signal"/>
    <!-- Replies: without these the call is delivered and the answer dropped,
         which looks like a timeout. The real system.conf has them. -->
    <allow send_requested_reply="true" send_type="method_return"/>
    <allow send_requested_reply="true" send_type="error"/>
    <allow receive_type="method_call"/>
    <allow receive_type="method_return"/>
    <allow receive_type="error"/>
    <allow receive_type="signal"/>
  </policy>
  <include>$POLICY</include>
</busconfig>
EOF

# The WAM's user exists on a device (system/sysusers.d); without it dbus skips
# that policy block and warns. Create it here so the real policy is exercised.
id -u zyl-wam >/dev/null 2>&1 || useradd -r -s /usr/sbin/nologin zyl-wam 2>/dev/null || true

# stdout/stderr go to a file: a forked daemon holding the test's pipes open
# makes the test runner wait forever even after this script is done
dbus-daemon --config-file="$R/bus.conf" --print-address=3 --fork \
    3>"$R/addr" >"$R/dbus.log" 2>&1
ADDR=$(cat "$R/addr")
[ -n "$ADDR" ] || { echo "✗ 버스를 띄우지 못했다"; exit 1; }
export DBUS_SYSTEM_BUS_ADDRESS="$ADDR"
unset DBUS_SESSION_BUS_ADDRESS || true

cleanup() {
    [ -n "${ALARM_PID:-}" ] && kill "$ALARM_PID" 2>/dev/null || true
    [ -n "${NOTIF_PID:-}" ] && kill "$NOTIF_PID" 2>/dev/null || true
    [ -n "${NOBODY_PID:-}" ] && kill "$NOBODY_PID" 2>/dev/null || true
    pkill -f "dbus-daemon --config-file=$R/bus.conf" 2>/dev/null || true
    rm -rf "$R"
}
trap cleanup EXIT

# ── 1. 데몬이 시스템 버스에서 자기 이름을 갖는가 ──
"$NOTIF" >"$R/notif.log" 2>&1 & NOTIF_PID=$!
"$ALARM" >"$R/alarm.log" 2>&1 & ALARM_PID=$!

owns() {   # owns <Name>
    for _ in $(seq 100); do
        gdbus introspect --system -d "org.zylos.$1" -o "/org/zylos/$1" >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    return 1
}
if owns Notification; then ok "notification 이 시스템 버스에서 이름을 가진다"
else bad "notification 이 이름을 갖지 못했다 ($R/notif.log)"; fi
if owns AlarmService; then ok "alarm 이 시스템 버스에서 이름을 가진다"
else bad "alarm 이 이름을 갖지 못했다 ($R/alarm.log)"; fi

# ── 2. 서비스 → 서비스 호출이 버스를 건너가는가 ──
#    알람이 울리면 알림을 띄운다 (앱이 떠 있지 않아도 사용자가 알도록)
TITLE="ZylBusTest"
NOW_MS=$(( $(date +%s) * 1000 ))
gdbus call --system -d org.zylos.AlarmService -o /org/zylos/AlarmService \
    -m org.zylos.AlarmService.Set \
    "bus-test" $((NOW_MS + 300)) 0 "com.zylos.clock" false "$TITLE" >/dev/null

found=1
for _ in $(seq 60); do
    if gdbus call --system -d org.zylos.Notification -o /org/zylos/Notification \
        -m org.zylos.Notification.GetActive 2>/dev/null | grep -q "$TITLE"; then
        found=0; break
    fi
    sleep 0.1
done
if [ $found -eq 0 ]; then ok "알람 트리거가 알림 서비스까지 건너갔다"
else bad "알람이 울렸는데 알림이 없다 (서비스 간 호출 실패)"; fi

# ── 3. 정책 밖의 사용자는 이름을 가질 수 없는가 ──
if command -v setpriv >/dev/null 2>&1 && [ "$(id -u)" = "0" ]; then
    kill "$ALARM_PID" 2>/dev/null || true
    wait "$ALARM_PID" 2>/dev/null || true
    unset ALARM_PID
    for _ in $(seq 30); do
        gdbus introspect --system -d org.zylos.AlarmService -o /org/zylos/AlarmService \
            >/dev/null 2>&1 || break
        sleep 0.1
    done
    chmod 777 "$R" "$R/alarms"
    setpriv --reuid=65534 --regid=65534 --clear-groups "$ALARM" >"$R/nobody.log" 2>&1 &
    NOBODY_PID=$!
    sleep 1
    if gdbus introspect --system -d org.zylos.AlarmService -o /org/zylos/AlarmService >/dev/null 2>&1; then
        bad "정책에 없는 사용자가 Zyl 서비스 이름을 가졌다"
    else
        ok "정책에 없는 사용자는 이름을 갖지 못한다"
    fi
    kill "$NOBODY_PID" 2>/dev/null || true
else
    echo "  - setpriv 없음/비루트: 거부 쪽 검사 생략"
fi

echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ]
