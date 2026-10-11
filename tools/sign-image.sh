#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Image Signing
#
# 역할: 슬롯별 서명 부트 이미지 + dm-verity 해시 트리 생성
# 수행범위: rootfs verity → 슬롯의 파티션·루트 해시·zyl.slot 을 담은 커널 인자를
#           DTB /chosen 에 넣고 → 커널+DTB+램디스크를 FIT 로 묶어 서명 →
#           공개키를 U-Boot DTB 에 기록 → fit_check_sign 으로 검증
# 의존방향: veritysetup, mkimage, fdtput, dtc, openssl (+ fit_check_sign 이 있으면 검증)
# SOLID: SRP — 이미지 서명만 담당
#
# 사용법: tools/sign-image.sh [--slot a|b] [--key-dir DIR] <rootfs-image> [output-dir]
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
KEY_DIR="$ROOT/board/bpi-f3/keys"
KEY_NAME="zylos-boot-key"          # key-name-hint in fit-image.its
ITS_FILE="$ROOT/board/bpi-f3/fit-image.its"

while [ $# -gt 0 ]; do
    case "$1" in
        --slot)    SLOT="$2"; shift 2 ;;
        --key-dir) KEY_DIR="$2"; shift 2 ;;
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

# Partitions per slot — board/bpi-f3/README.md, u-boot-env.txt
case "$SLOT" in
    a) ROOT_PART=2; VERITY_PART=3 ;;
    b) ROOT_PART=4; VERITY_PART=5 ;;
    *) echo "✗ --slot must be a or b" >&2; exit 2 ;;
esac

if [ -z "$ROOTFS" ]; then
    echo "Usage: $0 [--slot a|b] [--key-dir DIR] <rootfs-image> [output-dir]" >&2
    exit 2
fi

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || { echo "✗ Required command not found: $1" >&2; exit 1; }
}
for c in veritysetup mkimage fdtput dtc awk; do need_cmd "$c"; done
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
# The dm table length is in 512-byte sectors, not verity data blocks
SECTORS=$(( DATA_BLOCKS * DATA_BLOCK_SIZE / 512 ))
cat > "$OUT/verity-params-$SLOT.txt" <<EOF
ROOT_HASH=$ROOT_HASH
SALT=$SALT
DATA_BLOCKS=$DATA_BLOCKS
DATA_BLOCK_SIZE=$DATA_BLOCK_SIZE
HASH_BLOCK_SIZE=$HASH_BLOCK_SIZE
EOF

# ── 2. Command line for this slot, inside the DTB that gets signed ──
echo "▸ [2/4] Kernel command line → DTB /chosen/bootargs"
CMDLINE="root=/dev/dm-0 ro dm-mod.create=\"verity,,,ro,0 $SECTORS verity 1 /dev/mmcblk0p$ROOT_PART /dev/mmcblk0p$VERITY_PART $DATA_BLOCK_SIZE $HASH_BLOCK_SIZE $DATA_BLOCKS 0 sha256 $ROOT_HASH $SALT\" console=ttyS0,115200 zyl.slot=$SLOT quiet splash"
printf '%s\n' "$CMDLINE" > "$OUT/cmdline-$SLOT.txt"
cp "$DTB" "$OUT/fdt-$SLOT.dtb"
fdtput -c "$OUT/fdt-$SLOT.dtb" /chosen 2>/dev/null || true
fdtput -t s "$OUT/fdt-$SLOT.dtb" /chosen bootargs "$CMDLINE"

# ── 3. FIT: kernel + this DTB + ramdisk, signed; public key into U-Boot's DTB ──
echo "▸ [3/4] Signing FIT image"
sed -e "s|/incbin/(\"../../vmlinuz\")|/incbin/(\"$(abs "$KERNEL")\")|" \
    -e "s|/incbin/(\"../../system/dts/bpi-f3-zyl.dtb\")|/incbin/(\"$OUT/fdt-$SLOT.dtb\")|" \
    -e "s|/incbin/(\"../../initramfs.cpio.gz\")|/incbin/(\"$(abs "$INITRAMFS")\")|" \
    "$ITS_FILE" > "$OUT/fit-$SLOT.its"
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
echo ""
echo "Flash: fit-image-$SLOT.itb → boot partition (p1),"
echo "       rootfs → mmcblk0p$ROOT_PART, verity-hashtree-$SLOT.img → mmcblk0p$VERITY_PART"
echo "⚠ Keep $KEY_DIR/$KEY_NAME.key offline (HSM or encrypted storage). Never commit it."
