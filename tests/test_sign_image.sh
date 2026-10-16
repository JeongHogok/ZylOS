#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test
#
# 역할: 슬롯별 서명 이미지 도구(tools/sign-image.sh) 종단 테스트
# 수행범위: 임시 키·커널·DTB·rootfs 로 실제 서명 → U-Boot 와 같은 방식
#           (fit_check_sign)으로 검증, 서명된 DTB 의 커널 인자에 슬롯 파티션·
#           verity 루트 해시·섹터 단위 길이·zyl.slot 이 들어갔는지, 1바이트
#           변조를 거부하는지
# 의존방향: veritysetup, mkimage, dumpimage, fdtget, dtc, openssl (없으면 SKIP),
#           fit_check_sign (있으면 서명 검증·변조 거부까지)
# SOLID: SRP — 서명 체인 검증만 담당
# ──────────────────────────────────────────────────────────
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

for c in veritysetup mkimage dumpimage fdtget fdtput dtc openssl; do
  command -v "$c" >/dev/null 2>&1 || { echo "SKIP: $c not installed"; exit 0; }
done

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
PASS=0; FAIL=0
ok()  { echo "  ✓ $1"; PASS=$((PASS+1)); }
bad() { echo "  ✗ $1"; FAIL=$((FAIL+1)); }

echo "=== Zyl OS image signing (tools/sign-image.sh) ==="
bash "$ROOT/tools/gen-boot-keys.sh" "$W/keys" >/dev/null 2>&1 || { bad "key generation"; exit 1; }
head -c 65536 /dev/urandom | gzip > "$W/vmlinuz"
head -c 4096 /dev/urandom | gzip > "$W/initramfs.cpio.gz"
printf '/dts-v1/;\n/ { model = "test"; };\n' | dtc -I dts -O dtb -o "$W/board.dtb" -
dd if=/dev/zero of="$W/rootfs.img" bs=1M count=8 status=none

if ZYL_KERNEL="$W/vmlinuz" ZYL_DTB="$W/board.dtb" ZYL_INITRAMFS="$W/initramfs.cpio.gz" \
   bash "$ROOT/tools/sign-image.sh" --slot b --key-dir "$W/keys" "$W/rootfs.img" "$W/out" >"$W/log" 2>&1; then
  ok "slot B image is built"
else
  bad "signing failed"; cat "$W/log"; exit 1
fi

dumpimage -T flat_dt -p 1 -o "$W/fdt.dtb" "$W/out/fit-image-b.itb" >/dev/null
ARGS=$(fdtget -t s "$W/fdt.dtb" /chosen bootargs)
. "$W/out/verity-params-b.txt"
case "$ARGS" in
  *"zyl.verity=/dev/mmcblk0p4,/dev/mmcblk0p5,"*"$ROOT_HASH"*"zyl.slot=b"*) ok "signed command line names slot B's partitions, root hash and slot" ;;
  *) bad "signed command line wrong: $ARGS" ;;
esac
case "$ARGS" in
  *"root=/dev/mapper/zyl-root"*" panic="*) ok "the root is the verity device, and one that does not verify reboots (no shell)" ;;
  *) bad "root or panic= missing: $ARGS" ;;
esac
# The initramfs opens the root with exactly these eight values
# (system/initramfs/zyl-verity.local-top) — check they verify the image
SPEC=$(printf '%s\n' "$ARGS" | sed -n 's/.*zyl\.verity=\([^ ]*\).*/\1/p')
IFS=, read -r _DATA _HASH NB DBS HBS HSTART RH SALT_ <<<"$SPEC"
if [ -n "${SALT_:-}" ] && veritysetup verify --no-superblock --format=1 --hash=sha256 \
     --data-blocks "$NB" --data-block-size "$DBS" --hash-block-size "$HBS" \
     --hash-offset $(( HSTART * HBS )) --salt "$SALT_" \
     "$W/rootfs.img" "$W/out/verity-hashtree-b.img" "$RH" >/dev/null 2>&1; then
  ok "the signed verity values open the rootfs the way the initramfs does"
else
  bad "the signed verity values do not verify the rootfs: $SPEC"
fi

# Another board, extra signed arguments, installed straight into a disk image
if command -v mcopy >/dev/null && command -v mkfs.vfat >/dev/null && command -v sfdisk >/dev/null; then
  truncate -s 40M "$W/disk.img"
  printf 'label: gpt\nsize=8MiB, name=boot\nsize=10MiB, name=rootfs-a\nsize=2MiB, name=verity-a\nsize=10MiB, name=rootfs-b\nsize=2MiB, name=verity-b\nsize=4MiB, name=userdata\n' \
    | sfdisk -q "$W/disk.img" >/dev/null
  BOOT_AT=$(sfdisk --json "$W/disk.img" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["partitiontable"]["partitions"][0]["start"])')
  mkfs.vfat -C "$W/boot.img" 8192 >/dev/null
  dd if="$W/boot.img" of="$W/disk.img" bs=512 seek="$BOOT_AT" conv=notrunc status=none
  if ZYL_KERNEL="$W/vmlinuz" ZYL_DTB="$W/board.dtb" ZYL_INITRAMFS="$W/initramfs.cpio.gz" \
     bash "$ROOT/tools/sign-image.sh" --board generic-riscv64 --slot b --key-dir "$W/keys" \
       --cmdline-extra "systemd.wants=zyl-selftest.service" --disk "$W/disk.img" \
       "$W/rootfs.img" "$W/out2" >"$W/log2" 2>&1; then
    dumpimage -T flat_dt -p 1 -o "$W/fdt2.dtb" "$W/out2/fit-image-b.itb" >/dev/null
    ARGS2=$(fdtget -t s "$W/fdt2.dtb" /chosen bootargs)
    case "$ARGS2" in
      *"zyl.verity=/dev/vda4,/dev/vda5,"*"zyl.slot=b"*"systemd.wants=zyl-selftest.service"*) ok "the board's partitions, and extra arguments inside the signature" ;;
      *) bad "generic-riscv64 command line wrong: $ARGS2" ;;
    esac
    ROOTB_AT=$(sfdisk --json "$W/disk.img" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["partitiontable"]["partitions"][3]["start"])')
    if cmp -s "$W/rootfs.img" <(dd if="$W/disk.img" bs=512 skip="$ROOTB_AT" count=$(( 8 * 2048 )) status=none) \
       && MTOOLS_SKIP_CHECK=1 mdir -b -i "$W/disk.img@@$(( BOOT_AT * 512 ))" ::/ 2>/dev/null | grep -q 'fit-image-b.itb'; then
      ok "--disk puts the rootfs in slot B and the FIT in the boot partition"
    else
      bad "--disk did not install slot B"
    fi
  else
    bad "signing for generic-riscv64 failed"; cat "$W/log2"
  fi
else
  echo "  - mtools/dosfstools/sfdisk not installed: --disk check skipped"
fi

grep -q "zylos-boot-key" <(fdtget -l "$W/out/u-boot.dtb" /signature 2>/dev/null) \
  && ok "public key written into U-Boot's DTB" || bad "no key in u-boot.dtb"

if command -v fit_check_sign >/dev/null 2>&1; then
  fit_check_sign -f "$W/out/fit-image-b.itb" -k "$W/out/u-boot.dtb" >/dev/null 2>&1 \
    && ok "signature verifies the way U-Boot checks it" || bad "signature does not verify"
  cp "$W/out/fit-image-b.itb" "$W/tampered.itb"
  SIZE=$(wc -c < "$W/tampered.itb")
  # Flip the byte: writing a fixed value leaves it unchanged 1 time in 256
  B=$(dd if="$W/tampered.itb" bs=1 skip=$(( SIZE / 2 )) count=1 status=none | od -An -tu1 | tr -d ' ')
  printf "\\$(printf '%03o' $(( B ^ 255 )))" | dd of="$W/tampered.itb" bs=1 seek=$(( SIZE / 2 )) conv=notrunc status=none
  if fit_check_sign -f "$W/tampered.itb" -k "$W/out/u-boot.dtb" >/dev/null 2>&1; then
    bad "a modified image still verifies"
  else
    ok "a one-byte change is rejected"
  fi
else
  echo "  - fit_check_sign not installed: signature checks skipped"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
