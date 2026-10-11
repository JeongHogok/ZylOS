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
  *"/dev/mmcblk0p4 /dev/mmcblk0p5"*"$ROOT_HASH"*"zyl.slot=b"*) ok "signed command line names slot B's partitions, root hash and slot" ;;
  *) bad "signed command line wrong: $ARGS" ;;
esac
SECTORS=$(( DATA_BLOCKS * DATA_BLOCK_SIZE / 512 ))
case "$ARGS" in
  *"ro,0 $SECTORS verity"*) ok "dm table length is in 512-byte sectors ($SECTORS)" ;;
  *) bad "dm table length is not $SECTORS sectors: $ARGS" ;;
esac

grep -q "zylos-boot-key" <(fdtget -l "$W/out/u-boot.dtb" /signature 2>/dev/null) \
  && ok "public key written into U-Boot's DTB" || bad "no key in u-boot.dtb"

if command -v fit_check_sign >/dev/null 2>&1; then
  fit_check_sign -f "$W/out/fit-image-b.itb" -k "$W/out/u-boot.dtb" >/dev/null 2>&1 \
    && ok "signature verifies the way U-Boot checks it" || bad "signature does not verify"
  cp "$W/out/fit-image-b.itb" "$W/tampered.itb"
  SIZE=$(wc -c < "$W/tampered.itb")
  printf '\x5a' | dd of="$W/tampered.itb" bs=1 seek=$(( SIZE / 2 )) conv=notrunc status=none
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
