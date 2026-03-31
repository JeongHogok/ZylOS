#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Performance Benchmark
#
# 역할: 성능 벤치마크 — 앱 launch 시간, 메모리 사용량, IPC 지연
# 수행범위: 반복 측정 + 평균/최대 계산
# 의존방향: time, /proc/meminfo
# SOLID: SRP — 성능 측정만 담당
# ──────────────────────────────────────────────────────────
set -uo pipefail

echo "═══════════════════════════════════════"
echo "  ZylOS Performance Benchmark"
echo "═══════════════════════════════════════"
echo ""

# ─── 1. 서비스 응답 시간 ───
echo "■ D-Bus Service Response Time (10 iterations)"

measure_dbus() {
    local name="$1" path="$2" iface="$3" method="$4" label="$5"
    local total=0 max=0 count=0

    for i in $(seq 1 10); do
        local start=$(date +%s%N)
        gdbus call --session -d "$name" -o "$path" -m "$iface.$method" 2>/dev/null
        local end=$(date +%s%N)
        local ms=$(( (end - start) / 1000000 ))

        if [ $ms -gt 0 ]; then
            total=$((total + ms))
            [ $ms -gt $max ] && max=$ms
            count=$((count + 1))
        fi
    done

    if [ $count -gt 0 ]; then
        local avg=$((total / count))
        echo "  $label: avg=${avg}ms max=${max}ms (${count}/10 ok)"
    else
        echo "  $label: SKIP (service not running)"
    fi
}

measure_dbus "org.zylos.Logger" "/org/zylos/Logger" \
    "org.zylos.Logger" "GetLevel" "Logger.GetLevel"

measure_dbus "org.zylos.Notification" "/org/zylos/Notification" \
    "org.zylos.Notification" "GetActive" "Notification.GetActive"

# ─── 2. 메모리 사용량 ───
echo ""
echo "■ Memory Usage"

if [ -f /proc/meminfo ]; then
    total=$(grep MemTotal /proc/meminfo | awk '{print $2}')
    avail=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
    used=$((total - avail))
    echo "  Total:     $((total / 1024)) MB"
    echo "  Used:      $((used / 1024)) MB"
    echo "  Available: $((avail / 1024)) MB"
    echo "  Usage:     $(( used * 100 / total ))%"
else
    echo "  (not available on this platform)"
fi

# ─── 3. 서비스 프로세스 크기 ───
echo ""
echo "■ Service Process Memory (RSS)"

for proc in zyl-notification zyl-power zyl-credential zyl-camera zyl-audio; do
    pid=$(pgrep -x "$proc" 2>/dev/null | head -1)
    if [ -n "$pid" ] && [ -f "/proc/$pid/status" ]; then
        rss=$(grep VmRSS "/proc/$pid/status" | awk '{print $2}')
        echo "  $proc: ${rss} kB (PID $pid)"
    else
        echo "  $proc: not running"
    fi
done

# ─── 4. 디스크 I/O ───
echo ""
echo "■ Disk I/O (settings.json write latency)"

if [ -d "/data" ] || [ -d "/tmp" ]; then
    test_dir="${TEST_DIR:-/tmp/zyl-bench}"
    mkdir -p "$test_dir"
    total=0
    for i in $(seq 1 20); do
        start=$(date +%s%N)
        echo '{"test": "benchmark", "iter": '$i'}' > "$test_dir/settings.json"
        sync "$test_dir/settings.json" 2>/dev/null || true
        end=$(date +%s%N)
        ms=$(( (end - start) / 1000000 ))
        total=$((total + ms))
    done
    avg=$((total / 20))
    echo "  Write + sync: avg=${avg}ms (20 iterations)"
    rm -rf "$test_dir"
fi

echo ""
echo "═══════════════════════════════════════"
echo "  Benchmark complete"
echo "═══════════════════════════════════════"
