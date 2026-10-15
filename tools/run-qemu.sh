#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Emulated Boot Lane
#
# 역할: tools/build-image.sh 가 만든 이미지를 QEMU 에서 부팅한다 (보드 없이)
# 수행범위: 아키텍처별 QEMU 인자 조립, 직렬 콘솔 수집, --check 면 자기점검
#           결과(zyl-selftest: PASS)를 기다려 종료 코드로 돌려준다
# 의존방향: qemu-system-*, 커널+initrd(기기에서는 서명 FIT 가 그 자리),
#           builddir/zyl-board.conf (아키텍처)
# SOLID: SRP — 띄우고 지켜보는 일만. 무엇이 정상인지는 zyl-selftest 가 안다
#
# 네트워크 장치는 붙이지 않는다 (자기점검에 필요 없다). virtio PCI 장치는
# 기본으로 efi-virtio.rom 을 찾으므로 romfile= 로 비운다 — 그 ROM 을 주는
# 패키지(ipxe-qemu)가 없는 설치에서도 뜨게 한다
# 이 레인이 검증하는 것: systemd 유닛 기동, 시스템 버스와 정책, 서비스 이름,
#   앱 디렉터리·/data. virtio-gpu 를 붙여 /dev/dri 가 있으므로 컴포지터도 기동을
#   시도한다 — 하드웨어 조건이 없어 건너뛴 유닛은 실패로 보지 않는다(zyl-selftest)
# 검증하지 않는 것: U-Boot·서명 FIT·dm-verity·A/B 전환(부트로더 빌드와
#   tools/sign-image.sh·tests/test_boot_env.sh 소관), SoC 고유 경로(GPU·모뎀·ISP)
#
# 사용법: tools/run-qemu.sh --image IMG [--check] [--kernel K] [--initrd I]
#                          [--arch x86_64|riscv64] [--timeout 300] [--mem 2048]
# ──────────────────────────────────────────────────────────
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE=""
KERNEL=""
INITRD=""
ARCH=""
CHECK=0
TIMEOUT=300
MEM=2048
BUILDDIR="$HERE/builddir"

while [ $# -gt 0 ]; do
    case "$1" in
        --image)    IMAGE="$2"; shift 2 ;;
        --kernel)   KERNEL="$2"; shift 2 ;;
        --initrd)   INITRD="$2"; shift 2 ;;
        --arch)     ARCH="$2"; shift 2 ;;
        --builddir) BUILDDIR="$2"; shift 2 ;;
        --timeout)  TIMEOUT="$2"; shift 2 ;;
        --mem)      MEM="$2"; shift 2 ;;
        --check)    CHECK=1; shift ;;
        -h|--help)  sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "✗ 알 수 없는 인자: $1" >&2; exit 2 ;;
    esac
done

[ -n "$IMAGE" ] && [ -f "$IMAGE" ] || { echo "✗ --image <디스크 이미지> 가 필요하다" >&2; exit 2; }

# 아키텍처: 인자 > 빌드가 남긴 사실 > 호스트
if [ -z "$ARCH" ] && [ -f "$BUILDDIR/zyl-board.conf" ]; then
    # shellcheck disable=SC1090
    . "$BUILDDIR/zyl-board.conf"
    ARCH="$ZYL_ARCH"
fi
[ -z "$ARCH" ] || [ "$ARCH" = "native" ] && ARCH="$(uname -m)"

# 직렬 콘솔 장치는 머신마다 다르다 — 틀리면 게스트가 아무것도 내보내지 않는다
case "$ARCH" in
    x86_64)  QEMU=qemu-system-x86_64;  MACHINE="-machine q35";  CONSOLE=ttyS0 ;;
    riscv64) QEMU=qemu-system-riscv64; MACHINE="-machine virt"; CONSOLE=ttyS0 ;;
    aarch64) QEMU=qemu-system-aarch64; MACHINE="-machine virt -cpu max"; CONSOLE=ttyAMA0 ;;
    *) echo "✗ QEMU 레인이 모르는 아키텍처: $ARCH" >&2; exit 1 ;;
esac
command -v "$QEMU" >/dev/null 2>&1 || { echo "✗ $QEMU 가 없다" >&2; exit 1; }

# 커널·initrd: 기기에서는 서명 FIT 가 이 자리다. 레인에서는 호스트 것을 쓴다
if [ -z "$KERNEL" ]; then
    KERNEL="$(ls -1 /boot/vmlinuz-* 2>/dev/null | sort -V | tail -1 || true)"
fi
if [ -z "$INITRD" ]; then
    INITRD="$(ls -1 /boot/initrd.img-* 2>/dev/null | sort -V | tail -1 || true)"
fi
[ -n "$KERNEL" ] && [ -f "$KERNEL" ] || { echo "✗ 커널이 없다 — --kernel 로 주거나 linux-image 를 설치할 것" >&2; exit 1; }
[ -n "$INITRD" ] && [ -f "$INITRD" ] || { echo "✗ initrd 가 없다 — --initrd 로 줄 것 (virtio 모듈 필요)" >&2; exit 1; }

# 루트는 보드 레이아웃의 슬롯 A. 레인은 그 숫자를 스스로 알지 않는다
LAYOUT="$HERE/board/${ZYL_BOARD:-emulator}/partitions.conf"
# shellcheck disable=SC1090
[ -f "$LAYOUT" ] && . "$LAYOUT"
ROOT_DEV="${ZYL_DISK_NODE:-/dev/vda}${ZYL_PART_SEP-}${ZYL_PART_ROOT_A:-2}"

APPEND="root=$ROOT_DEV rw console=$CONSOLE,115200 systemd.show_status=1"
[ $CHECK = 1 ] && APPEND="$APPEND systemd.wants=zyl-selftest.service"

LOG="$(mktemp -t zyl-qemu.XXXXXX)"
echo "═══ QEMU ($ARCH, $MEM MiB) ═══"
echo "  image : $IMAGE"
echo "  kernel: $KERNEL"
echo "  root  : $ROOT_DEV"
[ $CHECK = 1 ] && echo "  check : zyl-selftest 를 기다린다 (최대 ${TIMEOUT}s)"

set +e
# -nic none: 기본 NIC 를 만들지 않는다 (그 장치가 efi-virtio.rom 을 찾는다)
"$QEMU" $MACHINE -m "$MEM" -smp 2 -nographic -no-reboot -nic none \
    -kernel "$KERNEL" -initrd "$INITRD" -append "$APPEND" \
    -drive "file=$IMAGE,format=raw,if=none,id=hd0" \
    -device virtio-blk-pci,drive=hd0,romfile= \
    -device virtio-gpu-pci,romfile= \
    > "$LOG" 2>&1 &
QPID=$!

if [ $CHECK = 0 ]; then
    wait $QPID
    rc=$?
    cat "$LOG"
    rm -f "$LOG"
    exit $rc
fi

rc=1
for _ in $(seq "$TIMEOUT"); do
    if grep -q "zyl-selftest: PASS" "$LOG" 2>/dev/null; then rc=0; break; fi
    if grep -q "zyl-selftest: FAILED" "$LOG" 2>/dev/null; then rc=1; break; fi
    kill -0 $QPID 2>/dev/null || break      # QEMU 가 먼저 죽었다
    sleep 1
done
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null

echo "── 직렬 콘솔 (zyl 관련) ──"
grep -E "zyl|Zyl|Failed|error|Kernel panic" "$LOG" | tail -40
if [ $rc -ne 0 ]; then
    echo "── 마지막 30줄 ──"
    tail -30 "$LOG"
    echo "✗ 자기점검이 PASS 하지 않았다 (전체 로그: $LOG)"
    exit 1
fi
rm -f "$LOG"
echo "✓ 부팅 + 자기점검 통과"
