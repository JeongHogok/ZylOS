#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Emulated Boot Lane
#
# 역할: tools/build-image.sh 가 만든 이미지를 QEMU 에서 부팅한다 (보드 없이)
# 수행범위: 아키텍처별 QEMU 인자 조립, 직렬 콘솔 수집, --check 면 자기점검
#           결과(zyl-selftest: PASS)를 기다려 종료 코드로 돌려준다.
#           두 가지로 띄운다 —
#           직접: QEMU 가 이미지의 커널·initrd 를 바로 싣는다(루트는 슬롯 A 파티션)
#           펌웨어(--firmware): OpenSBI → 부트 정책을 가진 U-Boot → 서명 FIT →
#             dm-verity 루트, 기기와 같은 길 (riscv64, board/generic-riscv64).
#             U-Boot 의 제어 DTB(커널 FIT 공개키를 담은 것)는 --dtb 로 QEMU 가 넘긴다
# 의존방향: qemu-system-*, 커널+initrd 또는 U-Boot(tools/build-bootloader.sh)·
#           OpenSBI(배포판 opensbi), builddir/zyl-board.conf (아키텍처)
# SOLID: SRP — 띄우고 지켜보는 일만. 무엇이 정상인지는 zyl-selftest 가 안다
#
# 네트워크 장치는 붙이지 않는다 (자기점검에 필요 없다). virtio PCI 장치는
# 기본으로 efi-virtio.rom 을 찾으므로 romfile= 로 비운다 — 그 ROM 을 주는
# 패키지(ipxe-qemu)가 없는 설치에서도 뜨게 한다. 펌웨어로 띄울 때 디스크는
# virtio-mmio 다: U-Boot 가 환경을 읽는 시점에 PCI 는 아직 열거되지 않는다
#
# 사용법: tools/run-qemu.sh --image IMG [--check] [--kernel K] [--initrd I]
#                          [--arch x86_64|riscv64] [--timeout 300] [--mem 2048]
#                          [--firmware DIR --dtb DTB [--opensbi FW]] [--log FILE]
#         tools/run-qemu.sh --dumpdtb OUT [--mem 2048]   펌웨어 부팅이 쓰는 머신의 DTB
#   --firmware  tools/build-bootloader.sh --board generic-riscv64 의 출력(u-boot.bin)
#   --dtb       U-Boot 의 제어 DTB — --dumpdtb 의 것에 tools/sign-image.sh 가 키를 넣은 것
#   --log       직렬 콘솔을 이 파일에 남긴다 (성공해도 지우지 않는다)
#   --check 없이 펌웨어로 띄우면 QEMU 가 끝날 때까지(재시작하면 끝난다 — -no-reboot) 기다린다
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
FIRMWARE=""
DTB=""
OPENSBI=""
DUMPDTB=""
KEEP_LOG=""

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
        --firmware) FIRMWARE="$2"; shift 2 ;;
        --dtb)      DTB="$2"; shift 2 ;;
        --opensbi)  OPENSBI="$2"; shift 2 ;;
        --dumpdtb)  DUMPDTB="$2"; shift 2 ;;
        --log)      KEEP_LOG="$2"; shift 2 ;;
        -h|--help)  sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "✗ 알 수 없는 인자: $1" >&2; exit 2 ;;
    esac
done

# 아키텍처: 인자 > 빌드가 남긴 사실 > 호스트. 펌웨어 부팅은 riscv64 만
if [ -n "$FIRMWARE$DUMPDTB" ]; then
    ARCH=riscv64
elif [ -z "$ARCH" ] && [ -f "$BUILDDIR/zyl-board.conf" ]; then
    # shellcheck disable=SC1090
    . "$BUILDDIR/zyl-board.conf"
    ARCH="$ZYL_ARCH"
fi
if [ -z "$ARCH" ] || [ "$ARCH" = "native" ]; then
    ARCH="$(uname -m)"
fi

# 직렬 콘솔 장치는 머신마다 다르다 — 틀리면 게스트가 아무것도 내보내지 않는다
case "$ARCH" in
    x86_64)  QEMU=qemu-system-x86_64;  MACHINE="-machine q35";  CONSOLE=ttyS0 ;;
    riscv64) QEMU=qemu-system-riscv64; MACHINE="-machine virt"; CONSOLE=ttyS0 ;;
    aarch64) QEMU=qemu-system-aarch64; MACHINE="-machine virt -cpu max"; CONSOLE=ttyAMA0 ;;
    *) echo "✗ QEMU 레인이 모르는 아키텍처: $ARCH" >&2; exit 1 ;;
esac
command -v "$QEMU" >/dev/null 2>&1 || { echo "✗ $QEMU 가 없다" >&2; exit 1; }
# 머신 자체 — 펌웨어 부팅의 DTB 는 이것으로 뽑으므로 두 쓰임이 반드시 같아야 한다
MACHINE_ARGS="$MACHINE -m $MEM -smp 2 -nographic -nic none"

if [ -n "$DUMPDTB" ]; then
    # shellcheck disable=SC2086
    "$QEMU" $MACHINE_ARGS -machine "dumpdtb=$DUMPDTB" >/dev/null 2>&1 || true
    [ -s "$DUMPDTB" ] || { echo "✗ DTB 를 뽑지 못했다: $DUMPDTB" >&2; exit 1; }
    echo "✓ $DUMPDTB"
    exit 0
fi

[ -n "$IMAGE" ] && [ -f "$IMAGE" ] || { echo "✗ --image <디스크 이미지> 가 필요하다" >&2; exit 2; }

if [ -n "$FIRMWARE" ]; then
    # ── 펌웨어: OpenSBI → U-Boot → 서명 FIT (커널·initrd·커널 인자는 FIT 안) ──
    UBOOT="$FIRMWARE/u-boot.bin"
    [ -f "$UBOOT" ] || { echo "✗ $UBOOT 가 없다 — tools/build-bootloader.sh --board generic-riscv64" >&2; exit 1; }
    [ -n "$DTB" ] && [ -f "$DTB" ] || { echo "✗ --dtb <키를 담은 제어 DTB> 가 필요하다" >&2; exit 2; }
    OPENSBI="${OPENSBI:-$(ls /usr/lib/riscv64-linux-gnu/opensbi/generic/fw_jump.bin 2>/dev/null || true)}"
    [ -n "$OPENSBI" ] && [ -f "$OPENSBI" ] || { echo "✗ OpenSBI fw_jump.bin 이 없다 — --opensbi 또는 opensbi 패키지" >&2; exit 1; }
    BOOT_ARGS=(-bios "$OPENSBI" -kernel "$UBOOT" -dtb "$DTB")
    DISK_DEV="virtio-blk-device,drive=hd0"
    WHAT="펌웨어 $UBOOT (서명 FIT)"
else
    # ── 직접: 기기에서는 서명 FIT 가 이 자리다. 레인에서는 이미지가 자기
    # 커널을 들고 있으면(build-image.sh --with-kernel) 그것을 쓴다 — 호스트 커널은
    # 모듈 버전이 어긋나 DRM·입력 드라이버가 올라오지 않는다
    IMGDIR="$(cd "$(dirname "$IMAGE")" && pwd)"
    if [ -z "$KERNEL" ] && [ -f "$IMGDIR/vmlinuz" ]; then
        KERNEL="$IMGDIR/vmlinuz"
        [ -z "$INITRD" ] && [ -f "$IMGDIR/initrd.img" ] && INITRD="$IMGDIR/initrd.img"
    fi
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
    BOOT_ARGS=(-kernel "$KERNEL" -initrd "$INITRD" -append "$APPEND")
    DISK_DEV="virtio-blk-pci,drive=hd0,romfile="
    WHAT="커널 $KERNEL (루트 $ROOT_DEV)"
fi

LOG="${KEEP_LOG:-$(mktemp -t zyl-qemu.XXXXXX)}"
: > "$LOG"
echo "═══ QEMU ($ARCH, $MEM MiB) ═══"
echo "  image : $IMAGE"
echo "  boot  : $WHAT"
[ $CHECK = 1 ] && echo "  check : zyl-selftest 를 기다린다 (최대 ${TIMEOUT}s)"

set +e
# shellcheck disable=SC2086
"$QEMU" $MACHINE_ARGS -no-reboot "${BOOT_ARGS[@]}" \
    -drive "file=$IMAGE,format=raw,if=none,id=hd0" \
    -device "$DISK_DEV" \
    -device virtio-gpu-pci,romfile= \
    > "$LOG" 2>&1 &
QPID=$!

if [ $CHECK = 0 ]; then
    # 끝날 때까지(또는 시간 제한까지) — 펌웨어가 재시작하면 -no-reboot 로 끝난다
    for _ in $(seq "$TIMEOUT"); do kill -0 $QPID 2>/dev/null || break; sleep 1; done
    kill $QPID 2>/dev/null
    wait $QPID 2>/dev/null
    rc=$?
    [ -n "$KEEP_LOG" ] || { cat "$LOG"; rm -f "$LOG"; }
    exit $rc
fi

rc=1
for _ in $(seq "$TIMEOUT"); do
    if grep -q "zyl-selftest: PASS" "$LOG" 2>/dev/null; then rc=0; break; fi
    if grep -q "zyl-selftest: FAILED" "$LOG" 2>/dev/null; then rc=1; break; fi
    # 자기점검 유닛이 스스로 보고하지 못하고 끝났다 (시간 초과 등)
    if grep -q "Failed to start.*zyl-selftest" "$LOG" 2>/dev/null; then rc=1; break; fi
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
[ -n "$KEEP_LOG" ] || rm -f "$LOG"
echo "✓ 부팅 + 자기점검 통과"
