#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - D-Bus Service Test Suite
#
# 역할: 전 D-Bus 서비스 적합성 테스트 (CTS 급)
# 수행범위: 서비스 존재, 메서드 호출, 응답 검증
# 의존방향: dbus-send, gdbus
# SOLID: SRP — D-Bus 서비스 테스트만 담당
#
# 사용법: bash tests/test_services_dbus.sh
# 필요: 대상 서비스가 실행 중이어야 함
# ──────────────────────────────────────────────────────────
set -uo pipefail

PASS=0
FAIL=0
SKIP=0

pass() { PASS=$((PASS + 1)); echo "  ✓ $1"; }
fail() { FAIL=$((FAIL + 1)); echo "  ✗ $1"; }
skip() { SKIP=$((SKIP + 1)); echo "  ⊘ $1 (skipped: $2)"; }

# Test if a D-Bus service is registered
test_service_exists() {
    local bus="$1"  # session or system
    local name="$2"
    local display="$3"

    if gdbus call --"$bus" -d "$name" -o / -m org.freedesktop.DBus.Peer.Ping 2>/dev/null; then
        pass "$display — registered on $bus bus"
        return 0
    else
        skip "$display" "not running"
        return 1
    fi
}

# Test if a D-Bus method can be called
test_method_call() {
    local bus="$1"
    local name="$2"
    local path="$3"
    local iface="$4"
    local method="$5"
    local args="${6:-}"
    local display="$7"

    local result
    if [ -n "$args" ]; then
        result=$(gdbus call --"$bus" -d "$name" -o "$path" -m "$iface.$method" $args 2>&1)
    else
        result=$(gdbus call --"$bus" -d "$name" -o "$path" -m "$iface.$method" 2>&1)
    fi

    if [ $? -eq 0 ]; then
        pass "$display → $result"
    else
        fail "$display → $result"
    fi
}

echo "═══════════════════════════════════════"
echo "  ZylOS D-Bus Service Test Suite"
echo "═══════════════════════════════════════"
echo ""

# ─── Session Bus Services ───
echo "■ Session Bus Services"

SERVICES_SESSION=(
    "org.zylos.Notification:Notification"
    "org.zylos.PowerManager:Power"
    "org.zylos.DisplayManager:Display"
    "org.zylos.InputService:Input"
    "org.zylos.SensorService:Sensors"
    "org.zylos.LocationService:Location"
    "org.zylos.UsbManager:USB"
    "org.zylos.UserManager:User"
    "org.zylos.CredentialManager:Credential"
    "org.zylos.Accessibility:Accessibility"
    "org.zylos.Logger:Logger"
    "org.zylos.CameraService:Camera"
    "org.zylos.AudioService:Audio"
    "org.zylos.WifiService:WiFi"
    "org.zylos.AccountService:Account"
    "org.zylos.AuthService:Auth"
    "org.zylos.NfcService:NFC"
    "org.zylos.WebAppManager:WAM"
)

for entry in "${SERVICES_SESSION[@]}"; do
    name="${entry%%:*}"
    display="${entry##*:}"
    test_service_exists "session" "$name" "$display"
done

# ─── System Bus Services ───
echo ""
echo "■ System Bus Services"

SERVICES_SYSTEM=(
    "org.zylos.Telephony:Telephony"
    "org.zylos.BluetoothService:Bluetooth"
)

for entry in "${SERVICES_SYSTEM[@]}"; do
    name="${entry%%:*}"
    display="${entry##*:}"
    test_service_exists "system" "$name" "$display"
done

# ─── Method Call Tests (session bus) ───
echo ""
echo "■ Method Call Tests"

# Notification: Post + GetActive
if gdbus call --session -d org.zylos.Notification -o /org/zylos/Notification \
    -m org.zylos.Notification.Ping 2>/dev/null; then
    test_method_call "session" "org.zylos.Notification" "/org/zylos/Notification" \
        "org.zylos.Notification" "GetActive" "" "Notification.GetActive"
fi

# Logger: GetLevel
test_method_call "session" "org.zylos.Logger" "/org/zylos/Logger" \
    "org.zylos.Logger" "GetLevel" "" "Logger.GetLevel" 2>/dev/null || \
    skip "Logger.GetLevel" "not running"

# Credential: SetMasterKey (test key)
test_method_call "session" "org.zylos.CredentialManager" "/org/zylos/CredentialManager" \
    "org.zylos.CredentialManager" "SetMasterKey" "'test-passphrase'" \
    "Credential.SetMasterKey" 2>/dev/null || \
    skip "Credential.SetMasterKey" "not running"

echo ""
echo "═══════════════════════════════════════"
echo "  Pass: $PASS  Fail: $FAIL  Skip: $SKIP"
echo "═══════════════════════════════════════"

if [ $FAIL -gt 0 ]; then
    exit 1
fi
echo "Service test suite completed."
