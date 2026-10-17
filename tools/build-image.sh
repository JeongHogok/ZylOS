#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Image Build
#
# 역할: 소스에서 설치 가능한 Zyl OS 디스크 이미지 한 장을 만든다
# 수행범위: 최소 rootfs 부트스트랩(debootstrap) → `ninja install` 로 Zyl 설치 →
#           부팅 설정(fstab·기본 타겟·사용자·디렉터리) → ext4 rootfs 이미지 →
#           보드 레이아웃대로 파티션 디스크 조립(부트 파티션 vfat, /data 는 비워 두고
#           첫 부팅이 encrypt 기능을 켠 ext4 로 만든다).
#           루트는 읽기 전용으로 쓰인다(기기에서는 dm-verity) — 바뀌는 것은 /data
#           (영속)와 /var·/tmp(휘발)뿐이고, 그렇게 되도록 여기서 맞춘다
# 의존방향: 읽는 사실은 전부 남이 소유한다 —
#           builddir/zyl-board.conf      (meson: 보드·아키텍처·프로파일)
#           board/<id>/partitions.conf   (보드: 디스크 레이아웃)
#           system/packages.list         (OS: 실행 시점 패키지)
#           ninja install                (meson: 무엇이 이미지에 들어가는지)
#           이 스크립트는 그 사실을 조립만 한다
# SOLID: SRP — 이미지 조립만, DIP — 레이아웃·패키지·파일 목록을 스스로 알지 않음
#
# 사용법: tools/build-image.sh [--builddir DIR] [--out DIR] [--suite NAME]
#                              [--mirror URL] [--rootfs-only] [--with-kernel]
#   기본: --builddir builddir, --out out/image
#   --with-kernel: 배포판 커널과 모듈을 이미지에 넣는다. 실기기 이미지는 커널을
#                  서명 FIT 으로 받으므로 기본은 끔. QEMU 레인은 켠다 (DRM 등
#                  모듈이 rootfs 에 있어야 컴포지터가 화면을 잡는다)
#   서명·verity·A/B 는 tools/sign-image.sh 가 맡는다 (이 스크립트의 rootfs 이미지를 입력으로)
# ──────────────────────────────────────────────────────────
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILDDIR="$HERE/builddir"
OUT="$HERE/out/image"
SUITE="trixie"
MIRROR="http://deb.debian.org/debian"
ROOTFS_ONLY=0
WITH_KERNEL=0

while [ $# -gt 0 ]; do
    case "$1" in
        --builddir)    BUILDDIR="$2"; shift 2 ;;
        --out)         OUT="$2"; shift 2 ;;
        --suite)       SUITE="$2"; shift 2 ;;
        --mirror)      MIRROR="$2"; shift 2 ;;
        --rootfs-only) ROOTFS_ONLY=1; shift ;;
        --with-kernel) WITH_KERNEL=1; shift ;;
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

for c in debootstrap mkfs.ext4 mkfs.vfat sfdisk ninja python3 readelf; do
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
[ $WITH_KERNEL = 1 ] && INCLUDE="$INCLUDE,linux-image-$DEB_ARCH"
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

# 이미지의 커널을 쓰면 initramfs 를 다시 만든다 — 방금 설치한 dm-verity 훅
# (system/initramfs)이 들어가야 서명된 슬롯의 루트를 연다
if [ $WITH_KERNEL = 1 ]; then
    step "initramfs 재생성 (dm-verity 훅)"
    chroot "$ROOTFS" update-initramfs -u -k all >>"$W/bootstrap.log" 2>&1 || {
        tail -20 "$W/bootstrap.log"; echo "✗ update-initramfs 실패" >&2; exit 1; }
fi

# 글꼴 캐시: 비어 있으면 WAM 이 뜰 때마다 글꼴을 전부 다시 읽는다(첫 화면이 그만큼
# 늦다). /var 는 기기에서 휘발(tmpfs)이라 캐시는 /usr 쪽 자리에 굽는다 —
# system/fonts/50-zyl-fonts.conf 가 그 자리를 fontconfig 에 알린다. 다른 아키텍처면
# binfmt 가 있어야 하므로 실패해도 넘어간다
FC_CACHE=/usr/lib/zyl-os/fontconfig-cache
if chroot "$ROOTFS" /usr/bin/fc-cache -s >>"$W/bootstrap.log" 2>&1; then
    step "글꼴 캐시 생성"
    mkdir -p "$ROOTFS$FC_CACHE"
    mv "$ROOTFS"/var/cache/fontconfig/* "$ROOTFS$FC_CACHE"/ 2>/dev/null || true
else
    echo "  (글꼴 캐시 생성 건너뜀 — 첫 실행이 느려진다)" >&2
fi

# ── 2-1. 링크 검사: 설치한 바이너리가 이미지 안에서 전부 풀리는가 ──
# exit 127 로 부팅에서 죽는 것을 빌드에서 잡는다. 실행하지 않고 ELF 의
# DT_NEEDED 만 읽으므로 다른 아키텍처에서도 된다
step "공유 라이브러리 해석 검사"
python3 -I - "$STAGE" "$ROOTFS" <<'PYLINK' || exit 1
import pathlib, re, subprocess, sys
stage, rootfs = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
have = set()
for d in ('lib', 'usr/lib'):
    base = rootfs / d
    if base.is_dir():
        have |= {p.name for p in base.rglob('*.so*')}
missing = {}
for b in sorted((stage / 'usr/bin').glob('zyl-*')):
    if b.is_symlink() or not b.is_file():
        continue
    with b.open('rb') as f:
        if f.read(4) != b'\x7fELF':
            continue                      # 셸 스크립트(zyl-selftest)는 건너뛴다
    out = subprocess.run(['readelf', '-d', str(b)], capture_output=True, text=True).stdout
    for so in re.findall(r'Shared library: \[([^\]]+)\]', out):
        if so not in have:
            missing.setdefault(so, []).append(b.name)
if missing:
    print('X 이미지에 없는 공유 라이브러리:', file=sys.stderr)
    for so, who in sorted(missing.items()):
        print('    %s  <- %s' % (so, ', '.join(who[:4])), file=sys.stderr)
    print('  system/packages.list 에 그 라이브러리를 주는 패키지를 넣을 것', file=sys.stderr)
    raise SystemExit(1)
PYLINK

# ── 3. 부팅 설정 ──
step "부팅 설정 (fstab·기본 타겟·사용자·디렉터리)"
part_dev() { echo "${ZYL_DISK_NODE}${ZYL_PART_SEP}$1"; }
# / 는 커널이 이미 올린 것(슬롯의 rootfs, 기기에서는 그 dm-verity 장치)이라 여기서는
# 읽기 전용만 정한다. /data 는 비어 있으면(첫 부팅, 지운 뒤) 만들고(makefs — 빈 것만:
# zyl-data-blank) 설치 장치의 남은 공간까지 늘린다(growfs). 열지 못하면 멈추지 않고
# (nofail) 복구 화면으로 — zyl-recovery.service. /var 는 휘발 — 남길 상태는 /data 에
cat > "$ROOTFS/etc/fstab" <<EOF
# tools/build-image.sh 가 board/$ZYL_BOARD/partitions.conf 에서 생성
LABEL=zyl-root                  /      ext4   ro,noatime                                 0 0
$(part_dev "$ZYL_PART_BOOT")    /boot  vfat   nosuid,nodev,noexec,umask=0077,noatime    0 2
$(part_dev "$ZYL_PART_DATA")    /data  ext4   noatime,nosuid,nodev,nofail,x-systemd.makefs,x-systemd.growfs  0 2
tmpfs                           /var   tmpfs  mode=0755,nosuid,nodev                     0 0
EOF
mkdir -p "$ROOTFS/data" "$ROOTFS/boot"

# /data 를 만드는 것은 첫 부팅의 makefs 하나뿐이고(이미지는 비워 둔다 — 잠금 전환·공장
# 초기화 뒤와 같은 길), 그 mkfs.ext4 는 이 파일을 읽는다. 사용자 데이터를 PIN 으로
# 잠그려면(fscrypt — zyl-lock-prepare.service) ext4 에 encrypt 기능이 있어야 한다
sed -i '/^[[:space:]]*ext4 = {/,/}/ s/^\([[:space:]]*features = .*\)$/\1,encrypt/' "$ROOTFS/etc/mke2fs.conf"
sed -n '/^[[:space:]]*ext4 = {/,/}/p' "$ROOTFS/etc/mke2fs.conf" | grep -q 'features = .*,encrypt$' \
    || { echo "✗ mke2fs.conf 의 ext4 에 encrypt 를 넣지 못했다 — /data 가 암호화되지 않는다" >&2; exit 1; }

# 읽기 전용 /etc 에 남는 것: 기기마다 달라야 할 machine-id 는 비워 두면 systemd 가
# 부팅마다 만든다. DNS 는 NetworkManager 가 /run 에 쓴 것을 가리킨다
: > "$ROOTFS/etc/machine-id"
ln -sfn ../run/NetworkManager/resolv.conf "$ROOTFS/etc/resolv.conf"

# 부트로더 환경의 자리 — Boot HAL·업데이터의 fw_printenv/fw_setenv 가 읽는다
case "${ZYL_ENV_IN:-none}" in
    raw)  echo "$ZYL_DISK_NODE $ZYL_ENV_OFFSET $ZYL_ENV_SIZE" > "$ROOTFS/etc/fw_env.config" ;;
    fat)  echo "/boot/$ZYL_ENV_FILE 0x0 $ZYL_ENV_SIZE" > "$ROOTFS/etc/fw_env.config" ;;
    none) rm -f "$ROOTFS/etc/fw_env.config" ;;
    *)    echo "✗ ZYL_ENV_IN=$ZYL_ENV_IN — raw·fat·none 중 하나" >&2; exit 1 ;;
esac

echo "zyl" > "$ROOTFS/etc/hostname"
printf '127.0.0.1\tlocalhost\n127.0.1.1\tzyl\n' > "$ROOTFS/etc/hosts"

# 서비스 사용자와 /data 하위 디렉터리: 유닛들이 이것 없이는 기동 전에 실패한다
systemd-sysusers --root="$ROOTFS" >/dev/null 2>&1 || true
systemd-tmpfiles --root="$ROOTFS" --create >/dev/null 2>&1 || true

# graphical.target → zyl-os.target → 컴포지터·WAM·서비스 (유닛의 [Install] 그대로)
systemctl --root="$ROOTFS" set-default graphical.target >/dev/null
systemctl --root="$ROOTFS" enable zyl-os.target >/dev/null
systemctl --root="$ROOTFS" enable zyl-apparmor.service >/dev/null 2>&1 || true

# 로그인 콘솔은 두지 않는다 (셸 없는 기기 — CLAUDE.md 규칙).
# 템플릿째 막는다: 직렬 콘솔 장치 이름이 보드마다 다르다(ttyS0·ttyAMA0…)
systemctl --root="$ROOTFS" mask getty.target >/dev/null 2>&1 || true
systemctl --root="$ROOTFS" mask getty@.service >/dev/null 2>&1 || true
systemctl --root="$ROOTFS" mask serial-getty@.service >/dev/null 2>&1 || true
systemctl --root="$ROOTFS" mask console-getty.service >/dev/null 2>&1 || true

# ── 3-1. 이미지의 커널·initrd 를 꺼내 둔다 ──
# 기기에서는 서명 FIT 이 커널을 들고 있다. QEMU 레인은 그 자리를 이 두 파일로
# 메운다 — 호스트 커널을 쓰면 모듈 버전이 어긋나 DRM·입력이 올라오지 않는다
if [ $WITH_KERNEL = 1 ]; then
    step "커널·initrd 추출"
    # riscv64 는 vmlinuz 가 아니라 vmlinux(ELF)를 깐다 — 레인은 이름이 아니라
    # "이 이미지의 커널" 이 필요하므로 한 글롭으로 둘 다 본다(있으면 vmlinuz).
    # 없는 이름을 ls 에 따로 주면 ls 가 2 로 끝나고 pipefail+set -e 에 걸려
    # "커널이 없다" 는 메시지 대신 조용히 죽는다
    K="$(ls -1 "$ROOTFS"/boot/vmlinu[xz]-* 2>/dev/null | sort -V | tail -1 || true)"
    R="$(ls -1 "$ROOTFS"/boot/initrd.img-* 2>/dev/null | sort -V | tail -1 || true)"
    [ -n "$K" ] && [ -n "$R" ] || { echo "✗ 이미지에 커널·initrd 가 없다" >&2; exit 1; }
    cp "$K" "$OUT/vmlinuz"
    cp "$R" "$OUT/initrd.img"
    echo "  $(basename "$K") → $OUT/vmlinuz"
fi

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

step "rootfs → 슬롯 A, 부트 파티션 vfat (/data 는 첫 부팅이 만든다)"
OFF="$(start_of "$ZYL_PART_ROOT_A")"
dd if="$ROOT_IMG" of="$DISK" bs=512 seek="$OFF" conv=notrunc status=none

BOOT_IMG="$W/boot.img"
mkfs.vfat -n ZYL-BOOT -C "$BOOT_IMG" $(( ZYL_SIZE_BOOT_MIB * 1024 )) >/dev/null
BOFF="$(start_of "$ZYL_PART_BOOT")"
dd if="$BOOT_IMG" of="$DISK" bs=512 seek="$BOFF" conv=notrunc status=none


echo
echo "✓ 디스크 이미지: $DISK  (${TOTAL} MiB)"
echo "  rootfs 단독:   $ROOT_IMG"
echo "  슬롯 B·verity 는 비어 있다 — tools/sign-image.sh 와 OTA 가 채운다"
echo "  QEMU:  tools/run-qemu.sh --image $DISK"
echo "  SD:    sudo dd if=$DISK of=/dev/sdX bs=4M conv=fsync status=progress"
