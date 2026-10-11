#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test
#
# 역할: 부트로더 환경(board/bpi-f3/u-boot-env.txt) 결정 로직 테스트
# 수행범위: 실제 환경 파일을 읽어 bootcmd 를 실행 — U-Boot 명령(setenv,
#           saveenv, run, button, mmc, part, load, bootm, reset)은 기록만 하는
#           셸 함수로 대신한다. A/B 시도·롤백, 부팅 실패 시 롤백 예약,
#           잠금 전환(허용·버튼 확인·데이터 삭제), 잠금 해제 시 검증 해제
# 의존방향: bash. U-Boot hush 와 공통인 문법만 쓰는 스크립트라 그대로 실행된다
# SOLID: SRP — 부트 정책 검증만 담당
# ──────────────────────────────────────────────────────────
set -uo pipefail

ENV_FILE="$(cd "$(dirname "$0")/.." && pwd)/board/bpi-f3/u-boot-env.txt"
PASS=0
FAIL=0

# Parse the env text: '#' comments, NAME=VALUE, '\' line continuation
load_env() {
  local line acc=""
  while IFS= read -r line || [ -n "$line" ]; do
    if [ -z "$acc" ] && { [[ "$line" =~ ^[[:space:]]*# ]] || [[ -z "${line// }" ]]; }; then continue; fi
    if [[ "$line" == *\\ ]]; then acc+="${line%\\} "; continue; fi
    acc+="$line"
    local name="${acc%%=*}" value="${acc#*=}"
    printf -v "$name" '%s' "$value"
    acc=""
  done < "$ENV_FILE"
}

# U-Boot command stand-ins
LOG=""
STATE="(did not boot)"
log()     { LOG+="$*"$'\n'; }
setenv()  { local n="$1"; shift; if [ $# -eq 0 ]; then unset "$n"; else printf -v "$n" '%s' "$*"; fi; }
saveenv() { log "saveenv"; }
run()     { eval "${!1}"; }
button()  { [ "${FAKE_BUTTON:-}" = "$1" ]; }
sleep()   { :; }
mmc()     { log "mmc $*"; }
part()    { printf -v "$5" '%s' "0x1000"; }
load()    { log "load $4"; }
bootm()   { log "bootm verify=${verify}"; [ -z "${FAKE_BOOT_FAIL:-}" ] && finish; return 1; }
reset()   { log "reset"; finish; }
echo()    { log "echo $*"; }
finish()  { STATE="slot=${zyl_slot} active=${zyl_active_slot} next=${zyl_next_slot:-} count=${zyl_boot_count} unlocked=${zyl_unlocked} seen=${zyl_lock_seen} allowed=${zyl_oem_unlock_allowed}"; exit 0; }

# boot VAR=value ... — one boot from the stored env, prints the outcome
boot() {
  ( load_env
    for kv in "$@"; do printf -v "${kv%%=*}" '%s' "${kv#*=}"; done
    trap 'builtin echo "$STATE"; builtin echo "$LOG"' EXIT
    eval "$bootcmd" )
}

check() {   # name, output, expected substrings...
  local name="$1" out="$2"; shift 2
  for want in "$@"; do
    if [[ "$want" == !* ]]; then
      if [[ "$out" == *"${want:1}"* ]]; then builtin echo "  ✗ $name — unexpected: ${want:1}"; FAIL=$((FAIL+1)); return; fi
    elif [[ "$out" != *"$want"* ]]; then
      builtin echo "  ✗ $name — missing: $want"; builtin echo "$out" | sed 's/^/      /'; FAIL=$((FAIL+1)); return
    fi
  done
  builtin echo "  ✓ $name"; PASS=$((PASS+1))
}

builtin echo "=== Zyl OS boot environment (u-boot-env.txt) ==="

out=$(boot)
check "locked boot runs the active slot, verified, writes nothing" "$out" \
  "slot=a" "load fit-image-a.itb" "bootm verify=yes" "!saveenv" "!mmc erase"

out=$(boot zyl_next_slot=b zyl_boot_count=0)
check "an installed update is tried and counted" "$out" \
  "slot=b" "count=1" "saveenv" "load fit-image-b.itb"

out=$(boot zyl_next_slot=b zyl_boot_count=2)
check "third try is the last" "$out" "slot=b" "count=3"

out=$(boot zyl_next_slot=b zyl_boot_count=3)
check "after three failed tries it rolls back" "$out" \
  "slot=a" "next= " "count=0" "rolling back" "load fit-image-a.itb"

out=$(boot zyl_next_slot=b zyl_boot_count=0 FAKE_BOOT_FAIL=1)
check "a tried slot that will not boot is given up at once" "$out" \
  "count=3" "reset"

out=$(boot zyl_unlocked=1)
check "unlock without the Settings permission is refused" "$out" \
  "unlocked=0" "not allowed" "bootm verify=yes" "!mmc erase"

out=$(boot zyl_unlocked=1 zyl_oem_unlock_allowed=1)
check "unlock not confirmed at the device changes nothing" "$out" \
  "unlocked=0" "not confirmed" "bootm verify=yes" "!mmc erase"

out=$(boot zyl_unlocked=1 zyl_oem_unlock_allowed=1 FAKE_BUTTON=user)
check "confirmed unlock erases data, then boots unverified with a warning" "$out" \
  "mmc erase 0x1000 0x1000" "unlocked=1" "seen=1" "allowed=0" "bootm verify=no" "WARNING"

out=$(boot zyl_unlocked=1 zyl_lock_seen=1)
check "an unlocked device keeps booting unverified, data kept" "$out" \
  "bootm verify=no" "WARNING" "!mmc erase" "!saveenv"

out=$(boot zyl_unlocked=0 zyl_lock_seen=1 FAKE_BUTTON=user)
check "relocking also erases data, then verification is back" "$out" \
  "mmc erase" "unlocked=0" "seen=0" "bootm verify=yes" "!WARNING"

out=$(boot zyl_unlocked=0 zyl_lock_seen=1)
check "an unconfirmed relock stays unlocked" "$out" "unlocked=1" "bootm verify=no"

builtin echo ""
builtin echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
