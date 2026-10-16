#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test
#
# 역할: 모든 폼팩터 프로파일이 **이미지로 성립하는지** 검사
# 수행범위: 프로파일마다 설정만 해서(빌드 없이) 생성된 zyl-os.target 을 읽고,
#           그 Wants= 의 유닛이 전부 실재하며 각 유닛이 가리키는 바이너리가
#           그 프로파일에서 빌드되는지, 유닛과 서비스 디렉터리가 짝이 맞는지
# 의존방향: meson, board/*/meson.build, meson.build 의 zyl_profile_extras
# SOLID: SRP — 프로파일별 유닛 집합의 정합성만 검증
#        DIP — 기대 목록을 테스트가 들고 있지 않다. meson 이 생성한 타겟이 답이다
#
# "모든 폼팩터 대응"의 최소 보증선이다. 실기기 검증(L2)은 보드가 있어야 하지만,
# 이미지가 조립되는지(L0)는 하드웨어 없이 매 커밋 지킬 수 있다.
# ──────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
command -v meson >/dev/null 2>&1 || { echo "SKIP: meson 없음"; exit 0; }

# 프로파일 목록은 meson_options.txt 의 profile choices 가 출처
PROFILES="$(sed -n "/option('profile'/,/)/s/.*choices: *\[\(.*\)\].*/\1/p" "$ROOT/meson_options.txt" | tr -d "',")"
[ -n "$PROFILES" ] || { echo "  ✗ meson_options.txt 에서 profile choices 를 읽지 못했다"; exit 1; }
W="$(mktemp -d -t zyl-profiles.XXXXXX)"
trap 'rm -rf "$W"' EXIT

pass=0
fail=0
for p in $PROFILES; do
    if ! meson setup "$W/$p" "$ROOT" --prefix=/usr -Dboard=emulator -Dprofile="$p" \
            >"$W/$p.log" 2>&1; then
        echo "  ✗ $p: 설정 실패"; tail -5 "$W/$p.log" | sed 's/^/      /'
        fail=$((fail + 1)); continue
    fi
    out=$(python3 -I - "$ROOT" "$W/$p/zyl-os.target" "$p" <<'PY'
import pathlib, re, sys
root, target, profile = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
problems = []
if not target.exists():
    print('zyl-os.target 이 생성되지 않았다'); raise SystemExit(0)
text = target.read_text()
wants = re.search(r'^Wants=(.*)$', text, re.M)
units = wants.group(1).split() if wants else []
if not units:
    problems.append('Wants= 가 비어 있다')
if f'profile={profile}' not in text:
    problems.append('타겟이 이 프로파일을 적지 않았다')

for u in units:
    # 배포판이 주는 유닛(systemd-timesyncd 등)은 우리 트리에 없다
    if not u.startswith('zyl-'):
        continue
    f = root / 'system' / u
    if not f.exists():
        problems.append(f'{u}: 유닛 파일이 없다')
        continue
    body = f.read_text()
    exe = re.search(r'^ExecStart=(\S+)', body, re.M)
    if not exe:
        problems.append(f'{u}: ExecStart 가 없다')
        continue
    binary = exe.group(1)
    name = binary.rsplit('/', 1)[-1]
    # 데몬이면 그 바이너리를 만드는 서비스 디렉터리가 있어야 한다.
    # 데몬이 아닌 것(zyl-apparmor 는 프로필 로더다)은 /usr/bin/zyl-* 가 아니다
    if binary.startswith('/usr/bin/zyl-'):
        hits = [d for d in (root / 'runtime' / 'services').iterdir()
                if d.is_dir() and f"'{name}'" in (d / 'meson.build').read_text()]
        if not hits:
            problems.append(f'{u}: {name} 를 만드는 서비스가 없다')
    bus = re.search(r'^BusName=(\S+)', body, re.M)
    if bus and not bus.group(1).startswith('org.zylos.'):
        problems.append(f'{u}: BusName 이 org.zylos.* 가 아니다 ({bus.group(1)})')
    if bus and not binary.startswith('/usr/bin/zyl-'):
        problems.append(f'{u}: 버스 이름을 갖는 데몬인데 ExecStart 가 {binary} 다')
print('\n'.join(problems))
PY
)
    if [ -z "$out" ]; then
        n=$(grep -c . <<<"$(sed -n 's/^Wants=//p' "$W/$p/zyl-os.target" | tr ' ' '\n' | grep .)")
        echo "  ✓ $p (유닛 $n개)"
        pass=$((pass + 1))
    else
        echo "  ✗ $p"
        echo "$out" | sed 's/^/      /'
        fail=$((fail + 1))
    fi
done

echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ]
