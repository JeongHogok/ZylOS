#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Test Layer - Recovery Boot Lane
#
# 역할: 사용자 데이터 파티션이 깨진 기기 — 멈추지 않고 복구 화면이 뜨며, 아무것도
#       지우지 않는다
# 수행범위: 한 번 부팅해 /data 가 만들어진 이미지에서 그 파티션의 첫 64KiB 를 덮는다
#           (파일 시스템도 없고 비어 있지도 않다 — 첫 블록이 깨진 ext4) → QEMU 부팅 →
#           자기점검이 복구 부팅과 복구 화면을 말하는지, 끝난 뒤 그 바이트가 그대로인지
#           (makefs 가 말없이 포맷하지 않았다 — system/storage/zyl-data-blank)
# 의존방향: tools/run-qemu.sh, builddir/zyl-board.conf, board/<id>/partitions.conf, sfdisk
# SOLID: SRP — 부팅해 보고 판정만. 무엇이 복구 부팅인지는 셸 정책·zyl-selftest 가 안다
#
# 사용법: tests/recovery_boot.sh --image IMG --builddir DIR [--timeout S]
# ──────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="" BUILDDIR="" TIMEOUT=600
while [ $# -gt 0 ]; do
    case "$1" in
        --image)    IMAGE="$2"; shift 2 ;;
        --builddir) BUILDDIR="$2"; shift 2 ;;
        --timeout)  TIMEOUT="$2"; shift 2 ;;
        *) echo "알 수 없는 인자: $1" >&2; exit 2 ;;
    esac
done
[ -f "$IMAGE" ] && [ -f "$BUILDDIR/zyl-board.conf" ] || { echo "사용법: $0 --image IMG --builddir DIR" >&2; exit 2; }
# shellcheck disable=SC1091
. "$BUILDDIR/zyl-board.conf"
# shellcheck disable=SC1090
. "$ROOT/board/$ZYL_BOARD/partitions.conf"

PASS=0 FAIL=0
ok()  { echo "  ✓ $*"; PASS=$((PASS + 1)); }
bad() { echo "  ✗ $*"; FAIL=$((FAIL + 1)); }

START="$(sfdisk -J "$IMAGE" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["partitiontable"]["partitions"][int(sys.argv[1]) - 1]["start"])' "$ZYL_PART_DATA")"
python3 -I - "$IMAGE" "$START" <<'PY'
import sys
with open(sys.argv[1], 'r+b') as f:
    f.seek(int(sys.argv[2]) * 512); f.write(b'\x5a' * 65536)
PY

echo "═══ 복구 부팅 레인 ($ZYL_BOARD, /data 파티션 p$ZYL_PART_DATA 의 첫 64KiB 를 덮음) ═══"
LOG="$(mktemp -t zyl-recovery.XXXXXX)"
bash "$ROOT/tools/run-qemu.sh" --image "$IMAGE" --builddir "$BUILDDIR" --check --timeout "$TIMEOUT" --log "$LOG" >/dev/null 2>&1
grep -q 'zyl-selftest: FAILED' "$LOG" && ok "자기점검이 끝까지 돌고 기기가 성하지 않다고 말함" \
    || bad "자기점검이 끝나지 않았다 — 복구 부팅이 서지 못했다"
grep -q 'zyl-selftest: FAIL /data 의 파티션을 열지 못했다' "$LOG" && ok "복구 부팅(휘발 /data)" \
    || bad "복구 부팅이 아니었다"
grep -q 'zyl-selftest: ok   복구 화면' "$LOG" && ok "앞에 복구 화면" || bad "복구 화면이 뜨지 않았다"
python3 -I - "$IMAGE" "$START" <<'PY' && ok "깨진 파티션을 포맷하지도 지우지도 않음" || bad "파티션이 바뀌었다 — 묻지 않고 지웠다"
import sys
with open(sys.argv[1], 'rb') as f:
    f.seek(int(sys.argv[2]) * 512); sys.exit(0 if f.read(65536) == b'\x5a' * 65536 else 1)
PY
[ $FAIL -eq 0 ] || grep -E "zyl|Zyl|Failed|FAILED|data" "$LOG" | tail -40
rm -f "$LOG"
echo "결과: $PASS 통과, $FAIL 실패"
[ "$FAIL" -eq 0 ]
