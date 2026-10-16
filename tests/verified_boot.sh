#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Test Layer - Verified Boot Lane
#
# 역할: 보드 없이 기기와 같은 부팅 체인을 끝까지 돌린다 — OpenSBI → 부트 정책을 가진
#       U-Boot → 서명 FIT → dm-verity 루트 → OS, 그리고 A/B 업데이트·롤백
# 수행범위: 일회용 키로 슬롯을 서명해 디스크에 넣고(tools/sign-image.sh) QEMU 로
#           여러 번 부팅하며(tools/run-qemu.sh --firmware) 매번 부트 환경을 읽어 본다:
#           1. 슬롯 A 검증 부팅 + 자기점검(읽기 전용 루트, 확정된 슬롯)
#           2. 업데이트(슬롯 B 설치, zyl_next_slot=b) → U-Boot 가 B 를 시도 → OS 의
#              업데이터가 건강 검사 뒤 확정 → zyl_active_slot=b. 이 부팅의 저장된 환경에
#              bootcmd 를 바꿔 넣어 둔다 — U-Boot 는 정책의 Runtime state 밖은 받지 않는다
#           3. 변조된 슬롯(FIT 1 바이트)으로의 업데이트 → 서명 검증 실패 → 시도 소진 →
#              재시작 → 다음 부팅에서 확정된 B 로 롤백, 자기점검 통과
# 의존방향: tools/{build-bootloader,sign-image,run-qemu,gen-boot-keys}.sh, 이미지
#           (tools/build-image.sh --with-kernel, board=generic-riscv64), opensbi,
#           mtools, libubootenv-tool(fw_printenv/fw_setenv), veritysetup, mkimage
# SOLID: SRP — 체인을 돌려 보고 판정만. 무엇이 정상인지는 부트 정책·zyl-selftest 가 안다
#
# 사용법: tests/verified_boot.sh --builddir DIR --image IMG --firmware DIR [--work DIR]
# ──────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILDDIR="" IMAGE="" FIRMWARE="" W=""
while [ $# -gt 0 ]; do
    case "$1" in
        --builddir) BUILDDIR="$2"; shift 2 ;;
        --image)    IMAGE="$2"; shift 2 ;;
        --firmware) FIRMWARE="$2"; shift 2 ;;
        --work)     W="$2"; shift 2 ;;
        *) echo "✗ 알 수 없는 인자: $1" >&2; exit 2 ;;
    esac
done
[ -n "$BUILDDIR" ] && [ -f "$IMAGE" ] && [ -f "$FIRMWARE/u-boot.bin" ] \
    || { echo "사용법: $0 --builddir DIR --image IMG --firmware DIR [--work DIR]" >&2; exit 2; }
# shellcheck disable=SC1090
. "$BUILDDIR/zyl-board.conf"
# shellcheck disable=SC1090
. "$ROOT/board/$ZYL_BOARD/partitions.conf"
[ "$ZYL_ENV_IN" = fat ] || { echo "✗ $ZYL_BOARD: 이 레인은 부트 파티션의 환경 파일을 읽는다" >&2; exit 1; }
W="${W:-$(mktemp -d -t zyl-vboot.XXXXXX)}"
mkdir -p "$W"
IMGDIR="$(cd "$(dirname "$IMAGE")" && pwd)"
ROOTFS="$IMGDIR/rootfs-$ZYL_PROFILE.img"
PASS=0 FAIL=0
ok()  { echo "  ✓ $*"; PASS=$((PASS + 1)); }
bad() { echo "  ✗ $*"; FAIL=$((FAIL + 1)); }

# The boot partition's place in the disk, for mtools
BOOT_AT=$(( $(sfdisk --json "$IMAGE" | python3 -I -c '
import json, re, sys
for p in json.load(sys.stdin)["partitiontable"]["partitions"]:
    if re.search(r"(\d+)$", p["node"]).group(1) == sys.argv[1]:
        print(p["start"])' "$ZYL_PART_BOOT") * 512 ))
BOOTFS="$IMAGE@@$BOOT_AT"
export MTOOLS_SKIP_CHECK=1
# The stored U-Boot environment, read and written as the OS would (fw_printenv/fw_setenv)
echo "$W/env $(printf '0x0') $ZYL_ENV_SIZE" > "$W/fw_env.config"
env_get() { mcopy -o -i "$BOOTFS" "::/$ZYL_ENV_FILE" "$W/env" && fw_printenv -c "$W/fw_env.config" -n "$1" 2>/dev/null; }
env_set() {   # env_set name value [name value ...]
    : > "$W/env-script"
    # libubootenv's script: one name=value per line
    while [ $# -ge 2 ]; do printf '%s=%s\n' "$1" "$2" >> "$W/env-script"; shift 2; done
    mcopy -o -i "$BOOTFS" "::/$ZYL_ENV_FILE" "$W/env" \
        && fw_setenv -c "$W/fw_env.config" -s "$W/env-script" \
        && mcopy -o -i "$BOOTFS" "$W/env" "::/$ZYL_ENV_FILE"
}
boot() {   # boot <name> [--check] — one power-on; the console goes to $W/<name>.log
    local name="$1"; shift
    bash "$ROOT/tools/run-qemu.sh" --image "$IMAGE" --firmware "$FIRMWARE" --dtb "$W/u-boot.dtb" \
        --log "$W/$name.log" --timeout 1500 "$@" > "$W/$name.out" 2>&1
}
sign() {   # sign <slot> — into the disk, the way an update writes a slot
    ZYL_KERNEL="$W/Image.gz" ZYL_DTB="$W/virt.dtb" ZYL_INITRAMFS="$IMGDIR/initrd.img" \
    ZYL_UBOOT_DTB="$W/virt.dtb" \
        bash "$ROOT/tools/sign-image.sh" --board "$ZYL_BOARD" --slot "$1" --key-dir "$W/keys" \
        --cmdline-extra "systemd.wants=zyl-selftest.service systemd.show_status=1" \
        --disk "$IMAGE" "$ROOTFS" "$W/signed" > "$W/sign-$1.log" 2>&1 \
        || { cat "$W/sign-$1.log"; echo "✗ 슬롯 $1 서명 실패" >&2; exit 1; }
    cp "$W/signed/u-boot.dtb" "$W/u-boot.dtb"
}

echo "═══ 검증 부팅 레인 ($ZYL_BOARD/$ZYL_PROFILE) ═══"
bash "$ROOT/tools/gen-boot-keys.sh" "$W/keys" >/dev/null 2>&1 || { echo "✗ 키 생성 실패" >&2; exit 1; }
bash "$ROOT/tools/run-qemu.sh" --dumpdtb "$W/virt.dtb" >/dev/null || exit 1
# The image's kernel (a RISC-V Image) into the FIT, compressed as the board's FIT says
gzip -9 -c "$IMGDIR/vmlinuz" > "$W/Image.gz"
sign a
mcopy -o -i "$BOOTFS" "$FIRMWARE/$ZYL_ENV_FILE" "::/$ZYL_ENV_FILE"

echo "── 1. 슬롯 A 검증 부팅"
if boot 1-slot-a --check; then ok "U-Boot 가 서명을 확인하고 dm-verity 루트로 부팅, 자기점검 통과"
else bad "슬롯 A 가 부팅·자기점검을 통과하지 못했다"; tail -40 "$W/1-slot-a.out"; fi
grep -q 'sha256,rsa2048:zylos-boot-key+ OK' "$W/1-slot-a.log" && ok "FIT 서명 확인 (zylos-boot-key)" \
    || bad "콘솔에 FIT 서명 확인이 없다"
grep -q 'zyl-selftest: ok   루트 읽기 전용' "$W/1-slot-a.log" && ok "루트 읽기 전용" || bad "루트가 읽기 전용이 아니다"

echo "── 2. 업데이트: 슬롯 B 설치 → 시도 → 확정"
sign b
# What a hostile OS might write into the stored env: U-Boot must take only the Runtime state
env_set zyl_next_slot b zyl_boot_count 0 bootcmd 'echo ZYL-HIJACKED; reset' \
    || { echo "✗ 부트 환경을 쓰지 못했다" >&2; exit 1; }
if boot 2-slot-b --check; then ok "U-Boot 가 업데이트된 슬롯 B 를 시도, 자기점검이 확정을 확인"
else bad "슬롯 B 시도가 통과하지 못했다"; tail -40 "$W/2-slot-b.out"; fi
# The kernel line is quiet on the console — the self test says which slot booted
grep -q 'zyl-selftest: ok   슬롯 b 부팅·확정' "$W/2-slot-b.log" && ok "슬롯 B 로 부팅, OS 가 확정" \
    || bad "슬롯 B 로 부팅·확정되지 않았다"
grep -q 'ZYL-HIJACKED' "$W/2-slot-b.log" && bad "저장된 환경의 bootcmd 를 U-Boot 가 받았다" \
    || ok "저장된 환경의 bootcmd 는 무시됐다 (쓰기 허용 목록)"
[ "$(env_get zyl_active_slot)" = b ] && [ -z "$(env_get zyl_next_slot)" ] \
    && ok "부트 환경: zyl_active_slot=b, 시도 끝" \
    || bad "부트 환경이 확정되지 않았다 (active=$(env_get zyl_active_slot) next=$(env_get zyl_next_slot))"

echo "── 3. 변조된 슬롯으로의 업데이트 → 롤백"
sign a
python3 -I - "$W/signed/fit-image-a.itb" <<'PY'
import sys
p = sys.argv[1]; d = bytearray(open(p, 'rb').read()); d[len(d) // 2] ^= 0xff; open(p, 'wb').write(d)
PY
mcopy -o -i "$BOOTFS" "$W/signed/fit-image-a.itb" ::/fit-image-a.itb
env_set zyl_next_slot a zyl_boot_count 0 || exit 1
boot 3-tampered        # U-Boot refuses the FIT, gives the try up and restarts (QEMU ends: -no-reboot)
grep -q 'Bad Data Hash\|Bad hash value\|Verification failed' "$W/3-tampered.log" \
    && ok "변조된 FIT 를 U-Boot 가 거부" || bad "변조된 FIT 가 거부되지 않았다"
! grep -q 'Starting kernel' "$W/3-tampered.log" && ok "변조된 슬롯의 커널은 실행되지 않았다" \
    || bad "변조된 슬롯이 커널을 시작했다"
if boot 4-rollback --check; then ok "다음 부팅에서 확정된 슬롯 B 로 롤백, 자기점검 통과"
else bad "롤백 부팅이 통과하지 못했다"; tail -40 "$W/4-rollback.out"; fi
grep -q 'rolling back' "$W/4-rollback.log" && ok "U-Boot 가 시도 소진을 보고 롤백" || bad "롤백 문구가 없다"
grep -q 'zyl-selftest: ok   슬롯 b 부팅·확정' "$W/4-rollback.log" && ok "롤백한 슬롯 B 로 부팅" || bad "롤백 뒤 슬롯 B 로 부팅하지 않았다"
[ "$(env_get zyl_active_slot)" = b ] && [ -z "$(env_get zyl_next_slot)" ] \
    && ok "부트 환경: zyl_active_slot=b 그대로, 시도 끝" \
    || bad "롤백 뒤 환경 (active=$(env_get zyl_active_slot) next=$(env_get zyl_next_slot))"

echo ""
echo "결과: $PASS 통과, $FAIL 실패   (콘솔 기록: $W/*.log)"
[ "$FAIL" -eq 0 ]
