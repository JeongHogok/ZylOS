#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Image Build
#
# 역할: 소스에서 설치 가능한 Zyl OS 디스크 이미지 한 장을 만든다
# 수행범위: 최소 rootfs 부트스트랩(debootstrap) → `ninja install` 로 Zyl 설치 →
#           부팅 설정(fstab·기본 타겟·사용자·디렉터리) → ext4 rootfs 이미지 →
#           보드 레이아웃대로 파티션 디스크 조립
# 의존방향: 읽는 사실은 전부 남이 소유한다 —
#           builddir/zyl-board.conf      (meson: 보드·아키텍처·프로파일)
#           board/<id>/partitions.conf   (보드: 디스크 레이아웃)
#           system/packages.list         (OS: 실행 시점 패키지)
#           ninja install                (meson: 무엇이 이미지에 들어가는지)
#           이 스크립트는 그 사실을 조립만 한다
# SOLID: SRP — 이미지 조립만, DIP — 레이아웃·패키지·파일 목록을 스스로 알지 않음
#
# 사용법: tools/build-image.sh [--builddir DIR] [--out DIR] [--suite NAME]
#                              [--mirror URL] [--rootfs-only]
#   기본: --builddir builddir, --out out/image
#   서명·verity·A/B 는 tools/sign-image.sh 가 맡는다 (이 스크립트의 rootfs 이미지를 입력으로)
# ──────────────────────────────────────────────────────────
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILDDIR="$HERE/builddir"
OUT="$HERE/out/image"
SUITE="trixie"
MIRROR="http://deb.debian.org/debian"
ROOTFS_ONLY=0

while [ $# -gt 0 ]; do
    case "$1" in
        --builddir)    BUILDDIR="$2"; shift 2 ;;
        --out)         OUT="$2"; shift 2 ;;
        --suite)       SUITE="$2"; shift 2 ;;
        --mirror)      MIRROR="$2"; shift 2 ;;
        --rootfs-only) ROOTFS_ONLY=1; shift ;;
        -h|--help)     sed -n '2,24p' "$0"; exit 0 ;;
        *) echo "✗ 알 수 없는 인자: $1" >&2; exit 2 ;;
    esac
done

# ── 빌드가 남긴 사실 ──
FACTS="$BUILDDIR/zyl-board.conf"
[ -f "$FACTS" ] || { echo "✗ $FACTS 가 없다 — 먼저 meson setup $BUILDDIR" >&2; exit 1; }
# shellcheck disable=SC1090
. "$FACTS"

# 유닛의 ExecStart 가 /usr/bin 을 가리킨다: prefix 가 다르면 이미지가 조용히 깨진다
if [ "${ZYL_PREFIX:-}" != "/usr" ]; then
    echo "✗ 이 빌드의 prefix 는 '${ZYL_PREFIX:-?}' 다 — 이미지는 /usr 여야 한다" >&2
    echo "  meson setup $BUILDDIR --prefix=/usr -Dboard=... -Dprofile=..." >&2
    exit 1
fi

LAYOUT="$HERE/board/$ZYL_BOARD/partitions.conf"
[ -f "$LAYOUT" ] || { echo "✗ $LAYOUT 가 없다 — 보드가 파티션 레이아웃을 선언해야 한다" >&2; exit 1; }
# shellcheck disable=SC1090
. "$LAYOUT"

PKGS="$HERE/system/packages.list"
[ -f "$PKGS" ] || { echo "✗ $PKGS 가 없다" >&2; exit 1; }

# ── Debian 아키텍처 이름 ──
case "$ZYL_ARCH" in
    x86_64)  DEB_ARCH=amd64 ;;
    riscv64) DEB_ARCH=riscv64 ;;
    aarch64) DEB_ARCH=arm64 ;;
    native)  DEB_ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)" ;;
    *)       echo "✗ 모르는 아키텍처: $ZYL_ARCH" >&2; exit 1 ;;
esac

for c in debootstrap mkfs.ext4 sfdisk ninja python3; do
    command -v "$c" >/dev/null 2>&1 || { echo "✗ 필요한 명령이 없다: $c" >&2; exit 1; }
done
[ "$(id -u)" = "0" ] || { echo "✗ root 로 실행해야 한다 (debootstrap) — 컨테이너를 쓸 것" >&2; exit 1; }

# 절대 경로로 바꾼다: DESTDIR 은 `ninja -C builddir` 의 작업 디렉터리 기준으로
# 풀리므로 상대 경로를 그대로 넘기면 설치물이 빌드 디렉터리 안에 쌓인다
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
W="$OUT/work"
ROOTFS="$W/rootfs"
STAGE="$W/stage"
rm -rf "$W"
mkdir -p "$ROOTFS" "$STAGE"

step() { echo "▸ $*"; }
echo "═══ Zyl OS 이미지 (board=$ZYL_BOARD profile=$ZYL_PROFILE arch=$DEB_ARCH v$ZYL_VERSION) ═══"

# ── 1. Zyl 을 staging 으로: 무엇이 들어가는지는 meson 이 안다 ──
step "ninja install → staging"
DESTDIR="$STAGE" ninja -C "$BUILDDIR" install >"$W/install.log" 2>&1 || {
    tail -20 "$W/install.log"; exit 1; }
# 설치가 정말 거기 갔는지 — 비어 있어도 cp 는 조용히 성공한다
[ -f "$STAGE/usr/lib/systemd/system/zyl-os.target" ] || {
    echo "✗ staging 에 zyl-os.target 이 없다: $STAGE" >&2
    echo "  (prefix 가 /usr 인지, DESTDIR 이 절대 경로인지 확인)" >&2; exit 1; }

# ── 2. 최소 rootfs ──
step "rootfs 부트스트랩 ($SUITE/$DEB_ARCH)"
INCLUDE="$(grep -vE '^\s*(#|$)' "$PKGS" | sed 's/#.*//' | tr -d ' \t' | paste -sd, -)"
# 다른 아키텍처면 2단계 부트스트랩 — 2단계는 binfmt(qemu-user-static)가 필요하다
FOREIGN=""
[ "$DEB_ARCH" = "$(dpkg --print-architecture 2>/dev/null || echo "$DEB_ARCH")" ] || FOREIGN="--foreign"
debootstrap $FOREIGN --arch="$DEB_ARCH" --variant=minbase \
    --include="$INCLUDE" "$SUITE" "$ROOTFS" "$MIRROR" >"$W/bootstrap.log" 2>&1 || {
    tail -30 "$W/bootstrap.log"; exit 1; }
if [ -n "$FOREIGN" ]; then
    step "2단계 부트스트랩 (binfmt)"
    chroot "$ROOTFS" /debootstrap/debootstrap --second-stage >>"$W/bootstrap.log" 2>&1 || {
        tail -30 "$W/bootstrap.log"
        echo "✗ 2단계 실패 — qemu-user-static 와 binfmt 등록이 필요하다" >&2; exit 1; }
fi

step "Zyl 파일 설치"
cp -a "$STAGE"/. "$ROOTFS"/

# ── 3. 부팅 설정 ──
step "부팅 설정 (fstab·기본 타겟·사용자·디렉터리)"
cat > "$ROOTFS/etc/fstab" <<EOF
# tools/build-image.sh 가 board/$ZYL_BOARD/partitions.conf 에서 생성
${ZYL_DISK_NODE}p${ZYL_PART_ROOT_A}  /       ext4  defaults,noatime  0 1
${ZYL_DISK_NODE}p${ZYL_PART_DATA}    /data   ext4  defaults,noatime  0 2
EOF
mkdir -p "$ROOTFS/data"

echo "zyl" > "$ROOTFS/etc/hostname"
printf '127.0.0.1\tlocalhost\n127.0.1.1\tzyl\n' > "$ROOTFS/etc/hosts"

# 서비스 사용자와 /data 하위 디렉터리: 유닛들이 이것 없이는 기동 전에 실패한다
systemd-sysusers --root="$ROOTFS" >/dev/null 2>&1 || true
systemd-tmpfiles --root="$ROOTFS" --create >/dev/null 2>&1 || true

# graphical.target → zyl-os.target → 컴포지터·WAM·서비스 (유닛의 [Install] 그대로)
systemctl --root="$ROOTFS" set-default graphical.target >/dev/null
systemctl --root="$ROOTFS" enable zyl-os.target >/dev/null
systemctl --root="$ROOTFS" enable zyl-apparmor.service >/dev/null 2>&1 || true

# 로그인 콘솔은 두지 않는다 (셸 없는 기기 — CLAUDE.md 규칙)
systemctl --root="$ROOTFS" mask getty@tty1.service >/dev/null 2>&1 || true
systemctl --root="$ROOTFS" mask serial-getty@ttyS0.service >/dev/null 2>&1 || true

# ── 4. rootfs 이미지 ──
step "rootfs ext4 (${ZYL_SIZE_ROOT_MIB} MiB)"
ROOT_IMG="$OUT/rootfs-$ZYL_PROFILE.img"
rm -f "$ROOT_IMG"
truncate -s "${ZYL_SIZE_ROOT_MIB}M" "$ROOT_IMG"
mkfs.ext4 -q -F -L zyl-root -d "$ROOTFS" "$ROOT_IMG"

if [ $ROOTFS_ONLY = 1 ]; then
    echo "✓ rootfs 이미지: $ROOT_IMG"
    echo "  다음: tools/sign-image.sh --slot a $ROOT_IMG  (verity + 서명 FIT)"
    exit 0
fi

# ── 5. 디스크 조립 — 보드가 선언한 표 그대로 ──
step "디스크 파티션"
DISK="$OUT/zyl-os-$ZYL_BOARD-$ZYL_PROFILE.img"
DATA_MIB="$ZYL_SIZE_DATA_MIB"
[ "$DATA_MIB" = "0" ] && DATA_MIB=512          # 0 = 설치 시 남은 공간 전부, 이미지에서는 최소치
TOTAL=$(( ZYL_RESERVED_HEAD_MIB + ZYL_SIZE_BOOT_MIB + 2 * ZYL_SIZE_ROOT_MIB \
          + 2 * ZYL_SIZE_VERITY_MIB + DATA_MIB + 2 ))
rm -f "$DISK"
truncate -s "${TOTAL}M" "$DISK"

# sfdisk: 번호 순서대로 적어야 보드의 표와 일치한다
{
    echo "label: gpt"
    echo "first-lba: $(( ZYL_RESERVED_HEAD_MIB * 2048 ))"
    echo "size=${ZYL_SIZE_BOOT_MIB}MiB, name=boot"
    echo "size=${ZYL_SIZE_ROOT_MIB}MiB, name=rootfs-a"
    echo "size=${ZYL_SIZE_VERITY_MIB}MiB, name=verity-a"
    echo "size=${ZYL_SIZE_ROOT_MIB}MiB, name=rootfs-b"
    echo "size=${ZYL_SIZE_VERITY_MIB}MiB, name=verity-b"
    echo "size=${DATA_MIB}MiB, name=userdata"
} | sfdisk -q "$DISK" >/dev/null

# 선언한 번호와 실제로 만들어진 순서가 같은지 — 어긋나면 커널 인자가 틀려진다
# (sfdisk -d 를 "번호 이름 시작섹터" 로 펴서 읽는다)
parts() { sfdisk -d "$DISK" | python3 -I -c '
import re, sys
for line in sys.stdin:
    m = re.match(r"^(\S+?)(\d+)\s*:\s*(.*)$", line.strip())
    if not m:
        continue
    attrs = dict(re.findall(r"([\w-]+)=\s*\"?([^\",]+)\"?", m.group(3)))
    print(m.group(2), attrs.get("name", ""), attrs.get("start", ""))
'; }

LAYOUT_SEEN="$(parts)"
expect() {   # expect <번호> <이름>
    echo "$LAYOUT_SEEN" | awk -v n="$1" -v want="$2" '
        $1 == n { if ($2 != want) { printf "✗ 파티션 %s 가 %s — %s 여야 한다\n", n, $2, want; exit 1 } found=1 }
        END { if (!found) { printf "✗ 파티션 %s 가 만들어지지 않았다\n", n; exit 1 } }' || exit 1
}
expect "$ZYL_PART_BOOT"     boot
expect "$ZYL_PART_ROOT_A"   rootfs-a
expect "$ZYL_PART_VERITY_A" verity-a
expect "$ZYL_PART_ROOT_B"   rootfs-b
expect "$ZYL_PART_VERITY_B" verity-b
expect "$ZYL_PART_DATA"     userdata

start_of() { echo "$LAYOUT_SEEN" | awk -v n="$1" '$1 == n { print $3 }'; }

step "rootfs → 슬롯 A, /data 포맷"
OFF="$(start_of "$ZYL_PART_ROOT_A")"
dd if="$ROOT_IMG" of="$DISK" bs=512 seek="$OFF" conv=notrunc status=none

DATA_IMG="$W/data.img"
truncate -s "${DATA_MIB}M" "$DATA_IMG"
mkfs.ext4 -q -F -L zyl-data "$DATA_IMG"
DOFF="$(start_of "$ZYL_PART_DATA")"
dd if="$DATA_IMG" of="$DISK" bs=512 seek="$DOFF" conv=notrunc status=none

echo
echo "✓ 디스크 이미지: $DISK  (${TOTAL} MiB)"
echo "  rootfs 단독:   $ROOT_IMG"
echo "  슬롯 B·verity 는 비어 있다 — tools/sign-image.sh 와 OTA 가 채운다"
echo "  QEMU:  tools/run-qemu.sh --image $DISK"
echo "  SD:    sudo dd if=$DISK of=/dev/sdX bs=4M conv=fsync status=progress"
