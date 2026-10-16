#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Image Signing
#
# 역할: 슬롯별 서명 부트 이미지 + dm-verity 해시 트리 생성
# 수행범위: rootfs verity → 슬롯의 파티션·verity 값·zyl.slot 을 담은 커널 인자를
#           DTB /chosen 에 넣고 → 커널+DTB+램디스크를 FIT 로 묶어 서명 →
#           공개키를 U-Boot DTB 에 기록 → fit_check_sign 으로 검증.
#           --disk 면 결과를 디스크 이미지의 슬롯·부트 파티션에 바로 써 넣는다
# 의존방향: board/<id>/partitions.conf·fit-image.its, veritysetup, mkimage, fdtput, dtc,
#           openssl (+ fit_check_sign 이 있으면 검증, --disk 면 sfdisk·mtools)
# SOLID: SRP — 이미지 서명만 담당
#
# 사용법: tools/sign-image.sh [--board ID] [--slot a|b] [--key-dir DIR]
#                             [--cmdline-extra ARGS] [--disk IMG] <rootfs-image> [output-dir]
#   --board          보드(기본 bpi-f3): 파티션·콘솔·FIT 틀이 그 보드의 것
#   --cmdline-extra  서명된 커널 인자에 더할 것 (공장 검사·QEMU 레인의
#                    systemd.wants=zyl-selftest.service 처럼 — 그 인자로 서명한 이미지)
#   --disk           만든 것을 이 디스크 이미지에 설치: rootfs·verity 트리 → 슬롯
#                    파티션, FIT → 부트 파티션(vfat)
#   입력 (환경변수로 바꿀 수 있음):
#     ZYL_KERNEL     vmlinuz               (gzip 압축 커널)
#     ZYL_DTB        system/dts/bpi-f3-zyl.dtb
#     ZYL_INITRAMFS  initramfs.cpio.gz
#     ZYL_UBOOT_DTB  U-Boot 제어 DTB — 공개키가 여기에 들어간다. 없으면 빈 DTB
#   출력: fit-image-<slot>.itb, u-boot.dtb, verity-hashtree-<slot>.img,
#         cmdline-<slot>.txt, verity-params-<slot>.txt, MANIFEST.sha256
#
# 소유자 빌드 (GPLv3 §6): --key-dir 에 자신의 키를 주면 같은 절차로 자신의
# 이미지를 만든다 — docs/INSTALLATION_INFORMATION.md
# ──────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SLOT=a
BOARD=bpi-f3
KEY_DIR=""
KEY_NAME="zylos-boot-key"          # key-name-hint in fit-image.its
EXTRA=""
DISK=""

while [ $# -gt 0 ]; do
    case "$1" in
        --board)   BOARD="$2"; shift 2 ;;
        --slot)    SLOT="$2"; shift 2 ;;
        --key-dir) KEY_DIR="$2"; shift 2 ;;
        --cmdline-extra) EXTRA="$2"; shift 2 ;;
        --disk)    DISK="$2"; shift 2 ;;
        --) shift; break ;;
        -*) echo "✗ unknown option $1" >&2; exit 2 ;;
        *) break ;;
    esac
done
ROOTFS="${1:-}"
OUTPUT_DIR="${2:-build/signed}"
KERNEL="${ZYL_KERNEL:-vmlinuz}"
DTB="${ZYL_DTB:-system/dts/bpi-f3-zyl.dtb}"
INITRAMFS="${ZYL_INITRAMFS:-initramfs.cpio.gz}"

KEY_DIR="${KEY_DIR:-$ROOT/board/$BOARD/keys}"
ITS_FILE="$ROOT/board/$BOARD/fit-image.its"
[ -f "$ITS_FILE" ] || { echo "✗ $ITS_FILE not found — board $BOARD has no signed boot image" >&2; exit 1; }

# Partitions per slot and the console — the board declares them, this script does not
LAYOUT="$ROOT/board/$BOARD/partitions.conf"
[ -f "$LAYOUT" ] || { echo "✗ $LAYOUT not found" >&2; exit 1; }
# shellcheck disable=SC1090
. "$LAYOUT"
case "$SLOT" in
    a) ROOT_PART=$ZYL_PART_ROOT_A; VERITY_PART=$ZYL_PART_VERITY_A ;;
    b) ROOT_PART=$ZYL_PART_ROOT_B; VERITY_PART=$ZYL_PART_VERITY_B ;;
    *) echo "✗ --slot must be a or b" >&2; exit 2 ;;
esac
DISK_NODE="$ZYL_DISK_NODE"
: "${ZYL_CONSOLE:?board/$BOARD/partitions.conf declares no ZYL_CONSOLE}"
PART_SEP="${ZYL_PART_SEP-}"

if [ -z "$ROOTFS" ]; then
    echo "Usage: $0 [--slot a|b] [--key-dir DIR] <rootfs-image> [output-dir]" >&2
    exit 2
fi

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || { echo "✗ Required command not found: $1" >&2; exit 1; }
}
for c in veritysetup mkimage fdtput dtc awk; do need_cmd "$c"; done
[ -z "$DISK" ] || for c in sfdisk mcopy python3; do need_cmd "$c"; done
[ -z "$DISK" ] || [ -f "$DISK" ] || { echo "✗ Not found: $DISK" >&2; exit 1; }
for f in "$ROOTFS" "$KERNEL" "$DTB" "$INITRAMFS" "$KEY_DIR/$KEY_NAME.key" "$KEY_DIR/$KEY_NAME.crt"; do
    [ -f "$f" ] || { echo "✗ Not found: $f" >&2; exit 1; }
done

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}
abs() { (cd "$(dirname "$1")" && printf '%s/%s\n' "$(pwd)" "$(basename "$1")"); }

mkdir -p "$OUTPUT_DIR"
OUT="$(cd "$OUTPUT_DIR" && pwd)"
echo "═══ Zyl OS Image Signing (slot $SLOT) ═══"

# ── 1. dm-verity: the root hash must exist before the command line is signed ──
echo "▸ [1/4] dm-verity hash tree"
VERITY_OUTPUT=$(veritysetup format "$ROOTFS" "$OUT/verity-hashtree-$SLOT.img" 2>&1)
field() { printf '%s\n' "$VERITY_OUTPUT" | awk -F':[ \t]*' -v k="$1" '$1 == k {print $2; exit}'; }
ROOT_HASH=$(field "Root hash")
SALT=$(field "Salt")
DATA_BLOCKS=$(field "Data blocks")
DATA_BLOCK_SIZE=$(field "Data block size")
HASH_BLOCK_SIZE=$(field "Hash block size")
if [ -z "$ROOT_HASH" ] || [ -z "$SALT" ] || [ -z "$DATA_BLOCKS" ] || [ -z "$DATA_BLOCK_SIZE" ]; then
    echo "✗ veritysetup output parsing failed" >&2
    echo "$VERITY_OUTPUT" >&2
    exit 1
fi
# veritysetup puts its superblock in the first hash block; the tree follows
HASH_START=1
cat > "$OUT/verity-params-$SLOT.txt" <<EOF
ROOT_HASH=$ROOT_HASH
SALT=$SALT
DATA_BLOCKS=$DATA_BLOCKS
DATA_BLOCK_SIZE=$DATA_BLOCK_SIZE
HASH_BLOCK_SIZE=$HASH_BLOCK_SIZE
EOF

# ── 2. Command line for this slot, inside the DTB that gets signed ──
# Every verity value is in the signed arguments — the initramfs opens the root
# with them (system/initramfs/zyl-verity.local-top) and never trusts an on-disk
# superblock. panic=: a root that does not verify reboots (U-Boot counts a
# trial slot's tries) instead of leaving a shell
echo "▸ [2/4] Kernel command line → DTB /chosen/bootargs"
VERITY="${DISK_NODE}${PART_SEP}$ROOT_PART,${DISK_NODE}${PART_SEP}$VERITY_PART,$DATA_BLOCKS,$DATA_BLOCK_SIZE,$HASH_BLOCK_SIZE,$HASH_START,$ROOT_HASH,$SALT"
CMDLINE="root=/dev/mapper/zyl-root ro rootfstype=ext4 zyl.verity=$VERITY panic=10 console=$ZYL_CONSOLE zyl.slot=$SLOT quiet${EXTRA:+ $EXTRA}"
printf '%s\n' "$CMDLINE" > "$OUT/cmdline-$SLOT.txt"
cp "$DTB" "$OUT/fdt-$SLOT.dtb"
fdtput -c "$OUT/fdt-$SLOT.dtb" /chosen 2>/dev/null || true
fdtput -t s "$OUT/fdt-$SLOT.dtb" /chosen bootargs "$CMDLINE"

# ── 3. FIT: kernel + this DTB + ramdisk, signed; public key into U-Boot's DTB ──
echo "▸ [3/4] Signing FIT image"
sed -e "s|@KERNEL@|$(abs "$KERNEL")|" -e "s|@FDT@|$OUT/fdt-$SLOT.dtb|" \
    -e "s|@RAMDISK@|$(abs "$INITRAMFS")|" "$ITS_FILE" > "$OUT/fit-$SLOT.its"
if [ -n "${ZYL_UBOOT_DTB:-}" ]; then
    cp "$ZYL_UBOOT_DTB" "$OUT/u-boot.dtb"
elif [ ! -f "$OUT/u-boot.dtb" ]; then
    echo "  ⚠ ZYL_UBOOT_DTB not set — writing the key into an empty DTB; merge it into the board's u-boot.dtb"
    printf '/dts-v1/;\n/ { };\n' | dtc -I dts -O dtb -o "$OUT/u-boot.dtb" -
fi
mkimage -f "$OUT/fit-$SLOT.its" -k "$KEY_DIR" -K "$OUT/u-boot.dtb" -r "$OUT/fit-image-$SLOT.itb" >/dev/null
rm -f "$OUT/fit-$SLOT.its"

# ── 4. Check the result the way U-Boot will ──
echo "▸ [4/4] Verifying signature"
# Debian/Ubuntu u-boot-tools lack fit_check_sign; Arch and a U-Boot tools build have it
if command -v fit_check_sign >/dev/null 2>&1; then
    fit_check_sign -f "$OUT/fit-image-$SLOT.itb" -k "$OUT/u-boot.dtb" >/dev/null
    echo "  ✓ signature verified"
else
    echo "  ⚠ fit_check_sign not installed — signature not checked here"
fi
( cd "$OUT" && hash_file ./* > MANIFEST.sha256 )

echo "  ✓ $OUT/fit-image-$SLOT.itb (root hash $ROOT_HASH)"

# ── 5. Into a disk image: the same places the updater writes a slot ──
if [ -n "$DISK" ]; then
    echo "▸ [5/5] Installing slot $SLOT into $DISK"
    start_of() {   # start_of <partition number> → first 512-byte sector
        sfdisk --json "$DISK" | python3 -I -c '
import json, re, sys
for p in json.load(sys.stdin)["partitiontable"]["partitions"]:
    if re.search(r"(\d+)$", p["node"]).group(1) == sys.argv[1]:
        print(p["start"])' "$1"; }
    put() {   # put <file> <partition number>
        local at; at="$(start_of "$2")"
        [ -n "$at" ] || { echo "✗ partition $2 not in $DISK" >&2; exit 1; }
        dd if="$1" of="$DISK" bs=512 seek="$at" conv=notrunc status=none
    }
    put "$ROOTFS" "$ROOT_PART"
    put "$OUT/verity-hashtree-$SLOT.img" "$VERITY_PART"
    BOOT_AT=$(( $(start_of "$ZYL_PART_BOOT") * 512 ))
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$DISK@@$BOOT_AT" "$OUT/fit-image-$SLOT.itb" ::/
    echo "  ✓ rootfs → p$ROOT_PART, verity → p$VERITY_PART, fit-image-$SLOT.itb → boot (p$ZYL_PART_BOOT)"
fi
echo ""
echo "Flash: fit-image-$SLOT.itb → boot partition (p$ZYL_PART_BOOT),"
echo "       rootfs → ${DISK_NODE}${PART_SEP}$ROOT_PART, verity-hashtree-$SLOT.img → ${DISK_NODE}${PART_SEP}$VERITY_PART"
echo "⚠ Keep $KEY_DIR/$KEY_NAME.key offline (HSM or encrypted storage). Never commit it."
