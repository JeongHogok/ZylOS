#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test
#
# 역할: OTA 패키지 도구(tools/build-ota.sh) 종단 테스트
# 수행범위: 임시 키·커널·DTB·rootfs 로 실제 패키지 생성 → 업데이터가 꺼내는
#           멤버 이름 그대로 들어 있는지, 슬롯별 FIT 가 자기 슬롯 파티션을
#           가리키는지, 서명이 공개키로 검증되고 1바이트 변조는 거부되는지,
#           매니페스트의 sha256·size 가 패키지와 맞는지
# 의존방향: tools/build-ota.sh, sign-image.sh 의 도구들 (없으면 SKIP), openssl
# SOLID: SRP — 패키지 형식 검증만 담당. 설치 쪽은 tests/test_updater.c
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

echo "=== Zyl OS OTA package (tools/build-ota.sh) ==="
bash "$ROOT/tools/gen-boot-keys.sh" "$W/keys" >/dev/null 2>&1 || { bad "key generation"; exit 1; }
head -c 65536 /dev/urandom | gzip > "$W/vmlinuz"
head -c 4096 /dev/urandom | gzip > "$W/initramfs.cpio.gz"
printf '/dts-v1/;\n/ { model = "test"; };\n' | dtc -I dts -O dtb -o "$W/board.dtb" -
dd if=/dev/urandom of="$W/rootfs.img" bs=1M count=4 status=none

if ZYL_KERNEL="$W/vmlinuz" ZYL_DTB="$W/board.dtb" ZYL_INITRAMFS="$W/initramfs.cpio.gz" \
   bash "$ROOT/tools/build-ota.sh" --key-dir "$W/keys" --url https://update.example/zylos-0.2.0.pkg \
   0.2.0 "$W/rootfs.img" "$W/out" >"$W/log" 2>&1; then
  ok "package is built"
else
  bad "build failed"; cat "$W/log"; exit 1
fi
PKG="$W/out/zylos-0.2.0.pkg"
MAN="$W/out/zylos-0.2.0.json"

# Member names are the updater's contract (ota_install.c)
LIST=$(tar -tzf "$PKG" | sort | tr '\n' ' ')
[ "$LIST" = "fit-image-a.itb fit-image-b.itb rootfs.img verity-hashtree-a.img verity-hashtree-b.img " ] \
  && ok "members are the ones the updater extracts" || bad "members: $LIST"
tar -xzOf "$PKG" rootfs.img | cmp -s - "$W/rootfs.img" \
  && ok "rootfs.img is the image itself, not a link" || bad "rootfs.img differs"

for slot in a b; do
  case $slot in a) parts="/dev/mmcblk0p2 /dev/mmcblk0p3" ;; b) parts="/dev/mmcblk0p4 /dev/mmcblk0p5" ;; esac
  tar -xzOf "$PKG" "fit-image-$slot.itb" > "$W/fit-$slot.itb"
  dumpimage -T flat_dt -p 1 -o "$W/fdt-$slot.dtb" "$W/fit-$slot.itb" >/dev/null
  case "$(fdtget -t s "$W/fdt-$slot.dtb" /chosen bootargs)" in
    *"$parts"*"zyl.slot=$slot"*) ok "slot $slot's FIT boots slot $slot's partitions" ;;
    *) bad "slot $slot's FIT has the wrong command line" ;;
  esac
done

field() { sed -n "s/.*\"$1\": *\"\{0,1\}\([^\",}]*\).*/\1/p" "$MAN"; }
[ "$(field sha256)" = "$(openssl dgst -sha256 -r "$PKG" | cut -d' ' -f1)" ] && ok "manifest sha256 matches" || bad "sha256 mismatch"
[ "$(field size)" = "$(wc -c < "$PKG" | tr -d ' ')" ] && ok "manifest size matches" || bad "size mismatch"
[ "$(field type)" = "full" ] && [ "$(field version)" = "0.2.0" ] && [ "$(field url)" = "https://update.example/zylos-0.2.0.pkg" ] \
  && ok "manifest names a full 0.2.0 image and its URL" || bad "manifest fields: $(cat "$MAN")"

# Verified the way the updater does: RSA-SHA256 over the package's SHA-256
field signature | openssl base64 -d -A > "$W/sig"
verify() { openssl dgst -sha256 -binary "$1" | openssl dgst -sha256 -verify "$W/keys/zylos-boot-key.pub" -signature "$W/sig" >/dev/null 2>&1; }
verify "$PKG" && ok "signature verifies with the device's public key" || bad "signature does not verify"
cp "$PKG" "$W/tampered.pkg"
printf x >> "$W/tampered.pkg"
verify "$W/tampered.pkg" && bad "a modified package still verifies" || ok "a one-byte change is rejected"

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
