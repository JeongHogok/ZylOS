#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Verification
# 역할: 전체 코드베이스 품질 검증 (CA, SOLID, i18n, 기술부채)
# 수행범위: apps/emulator-app/runtime/system/tests 전 영역 자동 검출
# 의존방향: grep/find/python3, 프로젝트 소스 전체 (CI: rules.yml)
# SOLID: SRP — 코드베이스 검증만 담당
# ──────────────────────────────────────────────────────────

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ERRORS=0
WARNINGS=0
SECTION=0

RED='\033[0;31m'
YELLOW='\033[0;33m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

pass()  { echo -e "  ${GREEN}✓${NC} $1"; }
fail()  { echo -e "  ${RED}✗${NC} $1"; ERRORS=$((ERRORS + 1)); }
warn()  { echo -e "  ${YELLOW}!${NC} $1"; WARNINGS=$((WARNINGS + 1)); }
section() { SECTION=$((SECTION + 1)); echo -e "\n${CYAN}${BOLD}[$SECTION] $1${NC}"; }

echo "╔═══════════════════════════════════════════════════════╗"
echo "║     Zyl OS — 전체 코드베이스 검증 (verify-all.sh)     ║"
echo "╚═══════════════════════════════════════════════════════╝"

# ═════════════════════════════════════════════════════════════
# [A] apps/ — OS 이미지 영역
# ═════════════════════════════════════════════════════════════
section "apps/ — OS 이미지 독립성"

# A1: Tauri 참조 금지
HITS=$(grep -rn '__TAURI__' "$ROOT/apps" --include='*.js' 2>/dev/null || true)
if [ -n "$HITS" ]; then fail "Tauri 참조 발견: $HITS"; else pass "Tauri 참조 없음"; fi

# A2: postMessage 직접 호출 금지
HITS=$(grep -rn 'window\.parent\.postMessage' "$ROOT/apps" --include='*.js' 2>/dev/null \
  | grep -v 'shared/bridge.js' || true)
if [ -n "$HITS" ]; then fail "postMessage 직접 호출:\n$HITS"; else pass "IPC가 ZylBridge 경유"; fi

# A3: 에뮬레이터 경로 금지
HITS=$(grep -rn 'emulator-app\|hal-tauri' "$ROOT/apps" --include='*.js' --include='*.html' --include='*.css' 2>/dev/null \
  | grep -v '//' | grep -v 'See ' || true)
if [ -n "$HITS" ]; then fail "에뮬레이터 참조:\n$HITS"; else pass "에뮬레이터 참조 없음"; fi

# A3b: 인라인 스크립트 없음 — OS 의 CSP 가 어느 앱에도 허락하지 않아(app_scheme.c,
# sandbox.js) 기기에서 조용히 돌지 않는다. 마크업으로 그린 남의 문자열이 돌지 않게 한 규칙
HITS=$( { grep -rnE '<script(\s[^>]*)?>' "$ROOT/apps" --include='*.html' | grep -v 'src='
          grep -rnE '\son[a-z]+\s*=' "$ROOT/apps" --include='*.html'
          grep -rnE "\son[a-z]+=['\"\\\\]" "$ROOT/apps" --include='*.js'; } 2>/dev/null || true)
if [ -n "$HITS" ]; then fail "인라인 스크립트·핸들러 (CSP 가 막음):\n$HITS"; else pass "인라인 스크립트·핸들러 없음"; fi

# ─────────────────────────────────────────────────────────────
section "apps/ — ES5 호환성"

# A4: let/const/arrow
for kw in 'let ' 'const '; do
  HITS=$(grep -rn "^\s*${kw}\|[;{,(]\s*${kw}" "$ROOT/apps" --include='*.js' 2>/dev/null | grep -v '^\s*//' || true)
  if [ -n "$HITS" ]; then fail "${kw}사용 발견 (ES5 위반)"; else pass "${kw}없음"; fi
done
HITS=$(grep -rn '=>' "$ROOT/apps" --include='*.js' 2>/dev/null | grep -v '^\s*//' | grep -v 'data-' || true)
if [ -n "$HITS" ]; then fail "Arrow function (=>) 발견"; else pass "Arrow function 없음"; fi

# ─────────────────────────────────────────────────────────────
section "apps/ — i18n 완전성"

# A5: 하드코딩 영어 탐지 (notification body, textContent에서 영어 문장)
# 영어 단어가 둘 이상 이어지는 문자열 리터럴만 잡는다 — 기호('×', '/'),
# 숫자 형식('00:00'), 접두어('v' + 버전)는 번역할 말이 아니다
HITS=$(grep -rnE "(textContent|innerText|title|placeholder)\s*=\s*['\"][A-Za-z]+[ ,.!?:]+[A-Za-z]+" \
  "$ROOT/apps" --include='*.js' 2>/dev/null | grep -v 'zylI18n\|\.t(' | grep -v '^\s*//' || true)
if [ -n "$HITS" ]; then
  warn "i18n 미적용 의심 (영어 하드코딩 가능):\n$(echo "$HITS" | head -5)"
else
  pass "textContent에 하드코딩 미발견"
fi

# A5b: HTML 의 data-i18n* 키가 그 앱(또는 공용) i18n 에 정의돼 있는지
#      없으면 applyTranslations 가 키 이름("settings.battery")을 그대로 그린다
UNDEFINED_KEYS=$(python3 - "$ROOT" <<'PYCHK'
import re, glob, os, sys
root = sys.argv[1]
shared = open(root + '/apps/shared/i18n.js').read()
out = []
for f in sorted(glob.glob(root + '/apps/*/index.html')):
    app = f.split('/')[-2]
    p = root + '/apps/%s/i18n.js' % app
    own = open(p).read() if os.path.exists(p) else ''
    for k in sorted(set(re.findall(r'data-i18n(?:-aria-label|-placeholder)?="([^"]+)"', open(f).read()))):
        if "'%s'" % k not in own and "'%s'" % k not in shared:
            out.append(app + ':' + k)
print(' '.join(out))
PYCHK
)
if [ -n "$UNDEFINED_KEYS" ]; then
  fail "정의되지 않은 i18n 키 (화면에 키 이름이 그대로 보임): $UNDEFINED_KEYS"
else
  pass "HTML 의 i18n 키 전부 정의됨"
fi

# A6: 5개 언어 키 균등 검사 (각 앱 i18n.js에서 ko/en/ja/zh/es 블록 수 비교)
I18N_MISMATCH=""
for i18nfile in $(find "$ROOT/apps" -name 'i18n.js' -not -path '*/shared/*' -not -path '*/system/*' -not -path '*/home/js/*' 2>/dev/null); do
  KO=$(grep -c "'ko'" "$i18nfile" 2>/dev/null || echo 0)
  EN=$(grep -c "'en'" "$i18nfile" 2>/dev/null || echo 0)
  JA=$(grep -c "'ja'" "$i18nfile" 2>/dev/null || echo 0)
  ZH=$(grep -c "'zh'" "$i18nfile" 2>/dev/null || echo 0)
  ES=$(grep -c "'es'" "$i18nfile" 2>/dev/null || echo 0)
  if [ "$KO" != "$EN" ] || [ "$EN" != "$JA" ] || [ "$JA" != "$ZH" ] || [ "$ZH" != "$ES" ]; then
    I18N_MISMATCH="${I18N_MISMATCH}  $(basename $(dirname $i18nfile)): ko=$KO en=$EN ja=$JA zh=$ZH es=$ES\n"
  fi
done
if [ -n "$I18N_MISMATCH" ]; then
  warn "i18n 언어 블록 수 불일치:\n$I18N_MISMATCH"
else
  pass "모든 앱 i18n 5개 언어 블록 균등"
fi

# ─────────────────────────────────────────────────────────────
# I3: 글꼴은 OS 스택 경유 — 앱이 글꼴을 직접 지정하면 실기기에 없는 글꼴
#     (-apple-system, Segoe UI)로 떨어지거나 한·중·일 자형이 섞인다
RAW_FONTS=$(grep -rnE "font-family:" "$ROOT/apps" --include='*.css' 2>/dev/null \
  | grep -v "/shared/fonts.css" | grep -vE "font-family:\s*(inherit|var\(--zyl-font-)" || true)
if [ -n "$RAW_FONTS" ]; then
  warn "글꼴을 직접 지정한 앱 CSS (var(--zyl-font-sans|mono) 사용):\n$(echo "$RAW_FONTS" | sed "s|$ROOT/||")"
else
  pass "앱 글꼴이 OS 스택(--zyl-font-*) 경유"
fi
if ! grep -q "system/fonts/50-zyl-fonts.conf" "$ROOT/meson.build"; then
  warn "fontconfig 설정이 설치되지 않음 (meson.build)"
fi

section "apps/ — Clean Architecture 헤더"

# A7: JS 파일에 Clean Architecture 헤더 존재 확인
MISSING_HEADER=""
for jsfile in $(find "$ROOT/apps" -name '*.js' -not -path '*/node_modules/*' 2>/dev/null); do
  HEAD=$(head -5 "$jsfile" | grep -c 'Clean Architecture\|클린아키텍처' || true)
  if [ "$HEAD" -eq 0 ]; then
    MISSING_HEADER="${MISSING_HEADER}  $(echo $jsfile | sed "s|$ROOT/||")\n"
  fi
done
if [ -n "$MISSING_HEADER" ]; then
  warn "Clean Architecture 헤더 누락:\n$MISSING_HEADER"
else
  pass "모든 JS 파일에 CA 헤더 존재"
fi

# A8: CSS 파일에 헤더 존재
MISSING_CSS=""
for cssfile in $(find "$ROOT/apps" -name '*.css' 2>/dev/null); do
  HEAD=$(head -5 "$cssfile" | grep -c 'Clean Architecture\|클린아키텍처' || true)
  if [ "$HEAD" -eq 0 ]; then
    MISSING_CSS="${MISSING_CSS}  $(echo $cssfile | sed "s|$ROOT/||")\n"
  fi
done
if [ -n "$MISSING_CSS" ]; then
  warn "CSS Clean Architecture 헤더 누락:\n$MISSING_CSS"
else
  pass "모든 CSS 파일에 CA 헤더 존재"
fi

# ─────────────────────────────────────────────────────────────
section "apps/ — CSS 소프트웨어 렌더링 호환"

BD_WARN=""
for cssfile in $(grep -rl 'backdrop-filter' "$ROOT/apps" --include='*.css' 2>/dev/null); do
  # opacity < 0.85 → 0.0–0.84 범위 탐지 (CLAUDE.md: opacity ≥ 0.85 필수)
  LOW=$(grep -B2 'backdrop-filter' "$cssfile" | grep -oP 'rgba\([^)]*,\s*(0\.[0-7]\d*|0\.8[0-4]\d*)\)' 2>/dev/null || true)
  if [ -n "$LOW" ]; then
    BD_WARN="${BD_WARN}  $(echo $cssfile | sed "s|$ROOT/||"): $LOW\n"
  fi
done
if [ -n "$BD_WARN" ]; then warn "backdrop-filter 폴백 부족 (opacity < 0.85):\n$BD_WARN"; else pass "backdrop-filter 폴백 충분 (opacity ≥ 0.85)"; fi

# ─────────────────────────────────────────────────────────────
section "apps/ — Mock/Demo 데이터 금지"

HITS=$(grep -rn 'mock\|MOCK\|Mock\|dummy\|DUMMY\|Dummy' "$ROOT/apps" --include='*.js' 2>/dev/null \
  | grep -v '^\s*//' | grep -v '/\*' | grep -v 'node_modules' | grep -v 'no mock' | grep -v 'mock/demo' || true)
if [ -n "$HITS" ]; then warn "Mock/Demo 키워드 발견:\n$(echo "$HITS" | head -5)"; else pass "Mock/Demo 데이터 없음"; fi

# ─────────────────────────────────────────────────────────────
section "apps/ — 앱 매니페스트 (app.json)"

for appdir in "$ROOT/apps"/*/; do
  APPNAME=$(basename "$appdir")
  # shared/, system/ are libraries, not apps — skip
  if [ "$APPNAME" = "shared" ] || [ "$APPNAME" = "system" ]; then continue; fi
  MANIFEST="$appdir/app.json"
  if [ ! -f "$MANIFEST" ]; then
    fail "$APPNAME: app.json 누락"
    continue
  fi
  # JSON 유효성
  if ! python3 -c "import json; json.load(open('$MANIFEST'))" 2>/dev/null; then
    fail "$APPNAME: app.json JSON 파싱 실패"
    continue
  fi
  # 필수 필드 확인
  for field in id name version entry; do
    VAL=$(python3 -c "import json; d=json.load(open('$MANIFEST')); print(d.get('$field',''))" 2>/dev/null)
    if [ -z "$VAL" ]; then fail "$APPNAME: app.json '$field' 필드 누락"; fi
  done
done
pass "앱 매니페스트 검사 완료"

# 시스템 앱: 기기에서는 이미지에 들어간 앱(meson.build 의 install_subdir)이, 에뮬레이터에서는
# permissions.js 의 SYSTEM_APPS 가 시스템 앱이다. 두 집합과 apps/ 의 앱이 같아야 한다 —
# 어긋나면 한쪽에서만 시스템 권한을 받는 앱이 생긴다
SYSAPP_DRIFT=$(python3 - "$ROOT" <<'PYCHK'
import json, os, re, sys
root = sys.argv[1]
src = open(os.path.join(root, 'apps/system/permissions.js')).read()
m = re.search(r'var SYSTEM_APPS = \[(.*?)\]', src, re.S)
listed = set(re.findall(r"'([^']+)'", m.group(1))) if m else set()
def app_id(d):
    return json.load(open(os.path.join(root, 'apps', d, 'app.json')))['id']
dirs = [d for d in os.listdir(os.path.join(root, 'apps'))
        if os.path.isfile(os.path.join(root, 'apps', d, 'app.json'))]
in_tree = {app_id(d) for d in dirs}
meson = open(os.path.join(root, 'meson.build')).read()
installed = {app_id(d) for d in re.findall(r"install_subdir\('apps/([^']+)'", meson) if d in dirs}
for name, ids in (('SYSTEM_APPS', listed), ('meson.build 설치 목록', installed)):
    for i in sorted(in_tree - ids): print('%s 에 없음: %s' % (name, i))
    for i in sorted(ids - in_tree): print('%s 에만 있음: %s' % (name, i))
PYCHK
)
if [ -n "$SYSAPP_DRIFT" ]; then fail "시스템 앱 집합이 어긋남:\n$SYSAPP_DRIFT"; else pass "시스템 앱 = apps/ = 이미지 설치 목록 = SYSTEM_APPS"; fi

# 폼팩터 목록: 앱이 아는 PROFILES(form-factor.js)와 빌드가 받는 -Dprofile choices 가 같아야 한다
PROFILE_DRIFT=$(python3 - "$ROOT" <<'PYCHK'
import os, re, sys
root = sys.argv[1]
js = open(os.path.join(root, 'apps/shared/form-factor.js')).read()
block = js[js.index('var PROFILES = {'):]
block = block[:block.index('\n  };')]
js_ids = set(re.findall(r'^    ([a-z]+): *\{', block, re.M))
opts = open(os.path.join(root, 'meson_options.txt')).read()
m = re.search(r"option\('profile'.*?choices: *\[(.*?)\]", opts, re.S)
meson_ids = set(re.findall(r"'([^']+)'", m.group(1))) if m else set()
if js_ids != meson_ids:
    print('form-factor.js 에만: %s / meson_options.txt 에만: %s' % (sorted(js_ids - meson_ids), sorted(meson_ids - js_ids)))
PYCHK
)
if [ -n "$PROFILE_DRIFT" ]; then fail "폼팩터 목록이 어긋남: $PROFILE_DRIFT"; else pass "폼팩터 = PROFILES = meson_options.txt"; fi

# 시스템 UI 의 높이: shell.h 가 정하고, 그리는 CSS(상태바 앱·에뮬레이터 프레임)가 따른다
BAR_DRIFT=$(python3 - "$ROOT" <<'PYCHK'
import os, re, sys
root = sys.argv[1]
h = open(os.path.join(root, 'runtime/wam/src/shell/shell.h')).read()
bar = re.search(r'#define ZYL_SHELL_BAR_PX +(\d+)', h).group(1)
banner = re.search(r'#define ZYL_SHELL_BANNER_PX +(\d+)', h).group(1)
sb = open(os.path.join(root, 'apps/statusbar/css/statusbar.css')).read()
emu = open(os.path.join(root, 'emulator-app/ui/css/emulator.css')).read()
checks = [
    ('statusbar.css --sb-height', re.search(r'--sb-height: *(\d+)px', sb), bar),
    ('emulator.css #system-frame', re.search(r'#system-frame \{[^}]*?height: *(\d+)px', emu), bar),
    ('emulator.css #app-viewport margin-top', re.search(r'#app-viewport \{[^}]*?margin-top: *(\d+)px', emu), bar),
    ('emulator.css .panel-banner', re.search(r'#system-frame\.panel-banner \{ *height: *(\d+)px', emu), banner),
]
for name, m, want in checks:
    if not m or m.group(1) != want:
        print('%s = %s, shell.h = %s' % (name, m.group(1) if m else '?', want))
PYCHK
)
if [ -n "$BAR_DRIFT" ]; then fail "시스템 UI 높이가 어긋남:\n$BAR_DRIFT"; else pass "시스템 UI 높이 = shell.h"; fi

# ═════════════════════════════════════════════════════════════
# [B] emulator-app/ — 에뮬레이터 영역
# ═════════════════════════════════════════════════════════════
section "emulator-app/ — 에뮬레이터 규칙"

# B1: 에뮬레이터에 비즈니스 로직 없음 (services.js는 순수 라우터)
EMU_SVC="$ROOT/emulator-app/ui/js/services.js"
if [ -f "$EMU_SVC" ]; then
  LOGIC=$(grep -c 'if.*permission\|checkPermission\|isProtectedPath\|SYSTEM_APPS' "$EMU_SVC" 2>/dev/null || true)
  if [ "$LOGIC" -gt 0 ]; then
    fail "에뮬레이터 services.js에 비즈니스 로직 발견 (${LOGIC}건)"
  else
    pass "에뮬레이터 services.js: 순수 IPC 라우터"
  fi
fi

# B2: Rust 파일 헤더
MISSING_RS=""
for rsfile in $(find "$ROOT/emulator-app/src" -name '*.rs' 2>/dev/null); do
  HEAD=$(head -5 "$rsfile" | grep -c 'Clean Architecture\|클린아키텍처' || true)
  if [ "$HEAD" -eq 0 ]; then
    MISSING_RS="${MISSING_RS}  $(echo $rsfile | sed "s|$ROOT/||")\n"
  fi
done
if [ -n "$MISSING_RS" ]; then warn "Rust CA 헤더 누락:\n$MISSING_RS"; else pass "Rust 파일 CA 헤더 존재"; fi

# Rust 가 컴파일되는지는 emulator.yml 의 test-emulator 가 본다 (Tauri 개발 패키지가 있는 잡)

# ═════════════════════════════════════════════════════════════
# [C] runtime/ — 실기기 서비스 (C)
# ═════════════════════════════════════════════════════════════
section "runtime/ — C 서비스 코드 품질"

# C1: TODO/FIXME/HACK/STUB 탐지
TODO_COUNT=$(grep -rEn 'TODO|FIXME|HACK|STUB' --exclude='verify-all.sh' "$ROOT/runtime" --include='*.c' --include='*.h' 2>/dev/null | wc -l || echo 0)
if [ "$TODO_COUNT" -gt 0 ]; then
  TODO_LIST=$(grep -rEn 'TODO|FIXME|HACK|STUB' --exclude='verify-all.sh' "$ROOT/runtime" --include='*.c' --include='*.h' 2>/dev/null | sed "s|$ROOT/||")
  warn "runtime/ TODO/FIXME ${TODO_COUNT}건:\n$TODO_LIST"
else
  pass "runtime/ TODO/FIXME 없음"
fi

# C2: 빈 함수 탐지 (함수 선언 직후 빈 중괄호)
EMPTY_FUNCS=$(grep -A1 '^[a-zA-Z_].*{$' "$ROOT/runtime" -r --include='*.c' 2>/dev/null | grep -B1 '^\s*}$' | grep -v '^--$' | grep '{$' | sed "s|$ROOT/||" || true)
if [ -n "$EMPTY_FUNCS" ]; then
  warn "빈 함수 구현체 발견:\n$EMPTY_FUNCS"
else
  pass "빈 함수 없음"
fi

# C3: Clean Architecture 헤더
MISSING_C=""
for cfile in $(find "$ROOT/runtime" -name '*.c' 2>/dev/null); do
  HEAD=$(head -5 "$cfile" | grep -c 'Clean Architecture\|클린아키텍처' || true)
  if [ "$HEAD" -eq 0 ]; then
    MISSING_C="${MISSING_C}  $(echo $cfile | sed "s|$ROOT/||")\n"
  fi
done
if [ -n "$MISSING_C" ]; then warn "C 파일 CA 헤더 누락:\n$MISSING_C"; else pass "C 파일 CA 헤더 존재"; fi

# C4-1: 도구·테스트 스크립트와 선언 파일도 같은 헤더 규칙을 지킨다 (규칙 7).
#       이미지·부팅 경로가 스크립트와 선언 파일로 되어 있어 여기가 비면
#       "이 파일이 무엇을 책임지는가"를 아무도 적지 않은 채 늘어난다.
MISSING_SH=""
for f in "$ROOT"/tools/*.sh "$ROOT"/tools/zyl-selftest "$ROOT"/tests/*.sh \
         "$ROOT"/board/*/partitions.conf "$ROOT"/tools/cross/*.ini \
         "$ROOT"/system/packages.list \
         "$ROOT"/system/*.conf.in "$ROOT"/system/tmpfiles.d/*.conf \
         "$ROOT"/system/dbus-1/*.conf; do
  [ -f "$f" ] || continue
  if [ "$(head -14 "$f" | grep -c 'Clean Architecture\|클린아키텍처' || true)" -eq 0 ]; then
    MISSING_SH="${MISSING_SH}  $(echo "$f" | sed "s|$ROOT/||")\n"
  fi
done
if [ -n "$MISSING_SH" ]; then
  fail "스크립트·선언 파일 CA 헤더 누락:\n$MISSING_SH"
else
  pass "도구·테스트·선언 파일 CA 헤더 존재"
fi

# C5: 실기기 WAM 권한 정책(서비스·메서드 단위)이 permissions.js 와 같은지
#     permissions.js 가 권위(규칙 8). bridge.c 는 사본이며, 어긋나면 에뮬레이터와
#     실기기가 서로 다른 권한으로 동작한다. 라우트가 있는데 정책이 없는 서비스는
#     제3자 앱에 늘 닫혀(app_may_call) 에뮬레이터와 달라지므로 따로 확인한다.
POLICY_DRIFT=$(python3 - "$ROOT" <<'PYCHK'
import re, sys
root = sys.argv[1]
js = open(root + '/apps/system/permissions.js').read()
c = open(root + '/runtime/wam/src/bridge/bridge.c').read()

def js_table():
    blk = js.split('var SERVICE_PERMISSIONS = {', 1)[1].split('};', 1)[0]
    return {n: set(re.findall(r"'([^']+)'", p))
            for n, p in re.findall(r"'([a-z.]+)':\s*\[([^\]]*)\]", blk)}

def c_table():
    blk = c.split('SERVICE_PERMISSIONS[] = {', 1)[1].split('{NULL, {NULL}}', 1)[0]
    return {n: set(re.findall(r'"([^"]+)"', p))
            for n, p in re.findall(r'\{"([a-z.]+)",\s*\{([^}]*)\}\}', blk)}

def js_methods():
    blk = js.split('var METHOD_PERMISSIONS = {', 1)[1].split('};', 1)[0]
    return {n: set(re.findall(r"'([^']+)'", p))
            for n, p in re.findall(r"'([a-zA-Z.]+)':\s*\[([^\]]*)\]", blk)}

def c_methods():
    blk = c.split('METHOD_PERMISSIONS[] = {', 1)[1].split('{NULL, {NULL}}', 1)[0]
    return {n: set(re.findall(r'"([^"]+)"', p))
            for n, p in re.findall(r'\{"([a-zA-Z.]+)",\s*\{([^}]*)\}\}', blk)}

js_res = set(re.findall(r"'([a-z.]+)'",
             js.split('var RESERVED_PERMISSIONS = [', 1)[1].split('];', 1)[0]))
c_res = set(re.findall(r'"([a-z.]+)"',
            c.split('RESERVED_PERMISSIONS[] = {', 1)[1].split('};', 1)[0]))
routes = re.findall(r'^\s+\{"([a-z]+)",\s+"org\.zylos', c, re.M)

J, C = js_table(), c_table()
out = []
for svc in sorted(set(J) | set(C)):
    if J.get(svc) != C.get(svc):
        out.append('%s: js=%s c=%s' % (svc, sorted(J.get(svc, [])) if svc in J else '-',
                                        sorted(C.get(svc, [])) if svc in C else '-'))
def js_owned():
    blk = js.split('var OWNED_EVENTS = [', 1)[1].split('];', 1)[0]
    return sorted(re.findall(r"service:\s*'([a-z]+)',\s*event:\s*(?:'([A-Za-z]+)'|null),\s*readAll:\s*(?:'([a-z.]+)'|null)", blk))

def c_owned():
    blk = c.split('OWNED_EVENTS[] = {', 1)[1].split('{NULL, NULL, NULL}', 1)[0]
    return sorted((s, e or '', r or '') for s, e, r in re.findall(
        r'\{"([a-z]+)",\s*(?:"([A-Za-z]+)"|NULL),\s*(?:"([a-z.]+)"|NULL)\}', blk))

if js_owned() != c_owned():
    out.append('owned events: js=%s c=%s' % (js_owned(), c_owned()))
JM, CM = js_methods(), c_methods()
for m in sorted(set(JM) | set(CM)):
    if JM.get(m) != CM.get(m):
        out.append('%s: js=%s c=%s' % (m, sorted(JM.get(m, [])) if m in JM else '-',
                                        sorted(CM.get(m, [])) if m in CM else '-'))
if js_res != c_res:
    out.append('reserved: js=%s c=%s' % (sorted(js_res), sorted(c_res)))
for r in routes:
    if r not in C:
        out.append('route "%s" has no policy entry (would deny all non-system apps)' % r)
print('; '.join(out))
PYCHK
)
if [ -n "$POLICY_DRIFT" ]; then
  fail "실기기 권한 정책이 permissions.js 와 다름: $POLICY_DRIFT"
else
  pass "실기기 권한 정책(bridge.c) = permissions.js"
fi

# C5b: 네트워크 서비스의 허용 도메인이 sandbox.js 와 같은지
#      sandbox.js 가 권위. network.c 는 사본이며, 어긋나면 에뮬레이터와 실기기가
#      서로 다른 곳에 앱 대신 요청을 보낸다.
DOMAIN_DRIFT=$(python3 - "$ROOT" <<'PYCHK'
import re, sys
root = sys.argv[1]
js = open(root + '/apps/system/sandbox.js').read().split('var ALLOWED_DOMAINS = [', 1)[1].split('];', 1)[0]
c = open(root + '/runtime/services/network/network.c').read().split('ALLOWED_DOMAINS[] = {', 1)[1].split('};', 1)[0]
J, C = set(re.findall(r"'([^']+)'", js)), set(re.findall(r'"([^"]+)"', c))
print('' if J == C else 'js=%s c=%s' % (sorted(J), sorted(C)))
PYCHK
)
if [ -n "$DOMAIN_DRIFT" ]; then
  fail "네트워크 허용 도메인이 sandbox.js 와 다름: $DOMAIN_DRIFT"
else
  pass "네트워크 허용 도메인(network.c) = sandbox.js"
fi

# C5c: 알람 한도가 alarm.h 와 같은지 — 기기가 권위, alarm.js 는 사본. 어긋나면 에뮬레이터에서
#      되는 알람이 기기에서 거부된다
ALARM_DRIFT=$(python3 - "$ROOT" <<'PYCHK'
import re, sys
root = sys.argv[1]
h = dict(re.findall(r'#define\s+ZYL_ALARM_(MAX|MAX_PER_APP|MIN_INTERVAL_MS|MAX_INTERVAL_MS)\s+(\d+)',
                    open(root + '/runtime/services/alarm/alarm.h').read()))
names = {'MAX': 'MAX_ALARMS', 'MAX_PER_APP': 'MAX_PER_APP',
         'MIN_INTERVAL_MS': 'MIN_INTERVAL_MS', 'MAX_INTERVAL_MS': 'MAX_INTERVAL_MS'}
js = dict(re.findall(r'var\s+(\w+)\s*=\s*(\d+);', open(root + '/apps/system/services/alarm.js').read()))
bad = [k for k in names if k not in h or js.get(names[k]) != h[k]]
print(('h=%s js=%s' % (h, {names[k]: js.get(names[k]) for k in names})) if bad or len(h) != 4 else '')
PYCHK
)
if [ -n "$ALARM_DRIFT" ]; then
  fail "알람 한도가 alarm.h 와 다름: $ALARM_DRIFT"
else
  pass "알람 한도(alarm.js) = alarm.h"
fi

# C5: 앱의 서비스 호출이 실기기 D-Bus 메서드와 맞는지 (래칫)
#     브릿지는 params 의 멤버를 서비스 인트로스펙션의 인자 이름으로 채운다.
#     메서드나 인자가 없으면 에뮬레이터에서는 되고 기기에서는 실패한다.
#     기존 공백(settings 데몬 부재, JS 전용 메서드)은 상한으로 묶고, 늘면 실패.
#     공백을 메우면 상한을 낮출 것.
CONTRACT_GAP_MAX=0
CONTRACT_GAPS=$(python3 - "$ROOT" <<'PYCHK'
import re, glob, sys
root = sys.argv[1]
br = open(root + '/runtime/wam/src/bridge/bridge.c').read()
routes = dict(re.findall(r'\{"(\w+)",\s*"org\.zylos\.\w+",\s*"/[^"]+",\s*"([^"]+)"\}', br))
defs = {}
for h in glob.glob(root + '/runtime/**/*.[ch]', recursive=True):
    defs.update(re.findall(r'#define\s+(\w+)\s+"([^"]*)"', open(h, errors='ignore').read()))
ifaces = {}
# Every D-Bus interface the runtime serves — the services and the WAM itself (apps)
for f in glob.glob(root + '/runtime/**/*.c', recursive=True):
    src = re.sub(r'"\s*([A-Z_][A-Z0-9_]+)\s*"', lambda m: defs.get(m.group(1), m.group(0)),
                 open(f, errors='ignore').read())
    xml = ''.join(re.findall(r'"((?:[^"\\\n]|\\.)*)"', src))
    for name, body in re.findall(r"<interface name='([\w.]+)'>(.*?)</interface>", xml, re.S):
        d = ifaces.setdefault(name, {})
        for m in re.finditer(r"<method name='(\w+)'\s*(/?)>(.*?)(?=<method|$)", body, re.S):
            args = [] if m.group(2) else re.findall(r'<arg\b([^>]*)>', m.group(3))
            # An a{sv} named params takes the whole request, a caller is filled by the
            # bridge (bridge.c) — no key to match
            d[m.group(1)] = [re.search(r"name='(\w+)'", a).group(1) for a in args
                             if "direction='in'" in a and "name='" in a
                             and not ("name='params'" in a and "type='a{sv}'" in a)
                             and not ("name='caller'" in a and "type='s'" in a)
                             and not ("name='container'" in a and "type='s'" in a)]
norm = lambda s: s.replace('_', '').lower()
gaps = []
for f in glob.glob(root + '/apps/**/*.js', recursive=True):
    for svc, meth, obj in re.findall(
            r"requestService(?:Fire)?\(\s*'(\w+)'\s*,\s*'(\w+)'\s*(?:,\s*(\{[^{}]*\}))?", open(f).read()):
        if svc not in routes: continue
        methods = ifaces.get(routes[svc])
        M = meth[0].upper() + meth[1:]
        keys = [norm(k) for k in re.findall(r'(\w+)\s*:', obj)]
        if methods is None or M not in methods or any(norm(a) not in keys for a in methods[M]):
            gaps.append(svc + '.' + meth)
print(len(gaps), ' '.join(sorted(set(gaps))))
PYCHK
)
GAP_COUNT=${CONTRACT_GAPS%% *}
if [ "$GAP_COUNT" -gt "$CONTRACT_GAP_MAX" ]; then
  fail "앱→기기 서비스 계약 공백 ${GAP_COUNT}건 (상한 ${CONTRACT_GAP_MAX}): ${CONTRACT_GAPS#* }"
elif [ "$GAP_COUNT" -lt "$CONTRACT_GAP_MAX" ]; then
  fail "앱→기기 서비스 계약 공백이 ${GAP_COUNT}건으로 줄었음 — CONTRACT_GAP_MAX 를 ${GAP_COUNT} 로 낮출 것"
else
  pass "앱→기기 서비스 계약 공백 ${GAP_COUNT}건 (상한 고정, 늘면 실패)"
fi

# Services apps call that no device route serves at all: on a device every such
# request is refused ("unknown service"). The gap check above sees only routed
# services, so these are counted on their own.
UNROUTED_MAX=0
UNROUTED=$(python3 - "$ROOT" <<'PYCHK'
import re, glob, sys
root = sys.argv[1]
br = open(root + '/runtime/wam/src/bridge/bridge.c').read()
routes = set(re.findall(r'\{"(\w+)",\s*"org\.zylos\.\w+"', br))
called = set()
for f in glob.glob(root + '/apps/**/*.js', recursive=True):
    if '/apps/system/' in f: continue
    src = open(f).read()
    called.update(re.findall(r"requestService(?:Fire|Async)?\(\s*'(\w+)'", src))
    called.update(re.findall(r"service:\s*'(\w+)',\s*method:", src))
missing = sorted(called - routes)
print(len(missing), ' '.join(missing))
PYCHK
)
UNROUTED_COUNT=${UNROUTED%% *}
if [ "$UNROUTED_COUNT" -gt "$UNROUTED_MAX" ]; then
  fail "기기 라우트 없는 서비스 ${UNROUTED_COUNT}개 (상한 ${UNROUTED_MAX}): ${UNROUTED#* }"
elif [ "$UNROUTED_COUNT" -lt "$UNROUTED_MAX" ]; then
  fail "기기 라우트 없는 서비스가 ${UNROUTED_COUNT}개로 줄었음 — UNROUTED_MAX 를 ${UNROUTED_COUNT} 로 낮출 것"
else
  pass "기기 라우트 없는 서비스 ${UNROUTED_COUNT}개 (상한 고정, 늘면 실패): ${UNROUTED#* }"
fi

# ═════════════════════════════════════════════════════════════
# [D] system/ — 디바이스 설정
# ═════════════════════════════════════════════════════════════
section "system/ — systemd 서비스 무결성"

# D1: systemd unit 파일 존재 확인
SVC_COUNT=$(find "$ROOT/system" -name '*.service' 2>/dev/null | wc -l || echo 0)
if [ "$SVC_COUNT" -gt 0 ]; then
  pass "systemd 서비스 파일 ${SVC_COUNT}개 존재"
else
  warn "systemd 서비스 파일 없음"
fi

# D2: 유닛이 쓰는 경로가 tmpfiles.d 로 만들어지는지
#     ProtectSystem=strict 유닛은 ReadWritePaths 경로가 없으면 네임스페이스 설정
#     단계에서 기동에 실패한다. /data 만 보던 검사가 /var/log/zyl-os 와
#     /var/lib/zyl-os/credentials 를 놓쳐 logger·credential 이 부팅에서 죽었다
#     (QEMU 레인이 잡음). 커널·sysfs 경로는 tmpfiles 가 만들 것이 아니라 제외.
UNPROVISIONED=$(python3 - "$ROOT" <<'PYCHK'
import glob, re, sys
root = sys.argv[1]
EXEMPT = ('/sys/', '/proc/', '/dev/', '/run/')      # 커널이 주는 경로
MOUNTS = ('/boot', '/data')                        # 파티션이라 fstab 이 올린다
made = set()
for f in glob.glob(root + '/system/tmpfiles.d/*.conf'):
    made |= set(re.findall(r'^[dDfv]\s+(\S+)', open(f).read(), re.M))
need = set()
for f in glob.glob(root + '/system/*.service'):
    body = open(f).read()
    for line in re.findall(r'^ReadWritePaths=(.*)$', body, re.M):
        for p in line.split():
            p = p.lstrip('-')
            if p.startswith('/') and not p.startswith(EXEMPT) and p not in MOUNTS:
                need |= {p}
# 상위 디렉터리가 만들어지면 그 아래도 쓸 수 있다
missing = {p for p in need if p not in made and not any(
    p.startswith(m.rstrip('/') + '/') for m in made)}
print(' '.join(sorted(missing)))
PYCHK
)
if [ -n "$UNPROVISIONED" ]; then
  fail "tmpfiles.d 가 만들지 않는 유닛 쓰기 경로 (기동 실패): $UNPROVISIONED"
else
  pass "유닛의 쓰기 경로 전부 tmpfiles.d 로 생성"
fi

# D3: 유닛이 쓰는 경로를 그 서비스의 AppArmor 프로필도 허용하는지
#     systemd 는 ReadWritePaths 로 열어 주는데 프로필 경로가 다르면 쓰기가 전부
#     거부된다. credential 프로필은 .../credential/(s 없이)이라 키체인이 기기에서
#     한 번도 쓰지 못했고, telephony 프로필은 통화 기록·긴급 경보가 사는
#     /data/telephony·/data/alerts 를 몰랐다 (QEMU 레인이 키체인으로 잡았다)
APPARMOR_GAPS=$(python3 - "$ROOT" <<'PYCHK'
import glob, os, re, sys
root = sys.argv[1]
bad = []
for prof in sorted(glob.glob(root + '/system/apparmor/zyl-*')):
    name = os.path.basename(prof)
    unit = root + '/system/' + name + '.service'
    if not os.path.exists(unit):
        continue
    paths = [p.lstrip('-') for l in re.findall(r'^ReadWritePaths=(.*)$', open(unit).read(), re.M)
             for p in l.split()]
    rules = re.findall(r'^\s*(?:owner\s+)?(/\S+)\s+([a-z]+),', open(prof).read(), re.M)
    def covers(rule, path):
        if not rule.endswith('/**'):
            return False
        base = rule[:-3].rstrip('/')
        return path == base or path.startswith(base + '/')
    for p in paths:
        if not any('w' in mask and covers(r, p) for r, mask in rules):
            bad.append('%s: %s' % (name, p))
print(' '.join(bad))
PYCHK
)
if [ -n "$APPARMOR_GAPS" ]; then
  fail "AppArmor 프로필이 유닛의 쓰기 경로를 막는다: $APPARMOR_GAPS"
else
  pass "유닛의 쓰기 경로 전부 AppArmor 프로필이 허용"
fi

# ═════════════════════════════════════════════════════════════
# [E] 전체 — 기술부채 탐지
# ═════════════════════════════════════════════════════════════
section "전체 — 기술부채 + 코드 위생"

# E0: OS 버전·보안 패치 수준은 apps/system/release.js 하나에서 온다
#     meson 도 불일치 시 빌드를 거부하지만, meson 은 CI 빌드에서만 돈다.
REL_VER=$(python3 "$ROOT/tools/release_info.py" version 2>&1)
REL_PATCH=$(python3 "$ROOT/tools/release_info.py" securityPatch 2>&1)
MESON_VER=$(grep -m1 -oE "version: '[^']+'" "$ROOT/meson.build" | sed "s/version: '//; s/'//")
if [ "$REL_VER" != "$MESON_VER" ]; then
  fail "release.js 버전($REL_VER) ≠ meson.build 버전($MESON_VER)"
elif ! echo "$REL_PATCH" | grep -qE '^[0-9]{4}-[0-9]{2}-[0-9]{2}$'; then
  fail "보안 패치 수준이 올바르지 않음: $REL_PATCH"
else
  pass "OS 버전 $REL_VER / 보안 패치 수준 $REL_PATCH (release.js 단일 출처)"
fi
# A copy of the current version string in C code means it will go stale on
# the next release ("0.0.0" sentinels for "no version" are fine)
LEFTOVER_VER=$(grep -rnF "\"$MESON_VER\"" "$ROOT/runtime" --include='*.c' --include='*.h' 2>/dev/null || true)
if [ -n "$LEFTOVER_VER" ]; then
  warn "C 코드에 OS 버전 \"$MESON_VER\" 사본 (ZYL_OS_VERSION 사용):\n$(echo "$LEFTOVER_VER" | sed "s|$ROOT/||")"
fi

# E1: TODO/FIXME 전체 카운트
ALL_TODO=$(grep -rEn 'TODO|FIXME|HACK' --exclude='verify-all.sh' "$ROOT" --include='*.js' --include='*.c' --include='*.h' --include='*.rs' 2>/dev/null \
  | grep -v 'node_modules' | grep -v 'target/' | wc -l || echo 0)
if [ "$ALL_TODO" -gt 0 ]; then
  warn "전체 코드베이스 TODO/FIXME: ${ALL_TODO}건"
else
  pass "TODO/FIXME 없음"
fi

# E2: console.log 남은 것 (디버깅 잔여물)
DBG_LOG=$(grep -rn 'console\.log(' "$ROOT/apps" --include='*.js' 2>/dev/null \
  | grep -v '^\s*//' | grep -v 'DEBUG' | wc -l || echo 0)
if [ "$DBG_LOG" -gt 5 ]; then
  warn "apps/에 console.log ${DBG_LOG}건 (디버깅 잔여물?)"
else
  pass "console.log 정상 범위 (${DBG_LOG}건)"
fi

# E3: 하드코딩된 비밀/키
SECRETS=$(grep -rn 'api_key\s*=\|apiKey\s*=\|secret\s*=\|password\s*=' "$ROOT/apps" --include='*.js' 2>/dev/null \
  | grep -v '^\s*//' | grep -v 'placeholder\|data-i18n\|credential\.\|\.secret\|\.password' || true)
if [ -n "$SECRETS" ]; then
  warn "하드코딩 비밀 키워드:\n$(echo "$SECRETS" | head -3)"
else
  pass "하드코딩 비밀 없음"
fi

# ═════════════════════════════════════════════════════════════
# Summary
# ═════════════════════════════════════════════════════════════
echo ""
echo "╔═══════════════════════════════════════════════════════╗"
if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
  echo -e "║  ${GREEN}✅ 전체 검증 통과 — 에러 0, 경고 0${NC}                  ║"
elif [ $ERRORS -eq 0 ]; then
  echo -e "║  ${YELLOW}⚠️  경고 ${WARNINGS}건 — 에러 없음${NC}                           ║"
else
  echo -e "║  ${RED}❌ 에러 ${ERRORS}건, 경고 ${WARNINGS}건${NC}                            ║"
fi
echo "╚═══════════════════════════════════════════════════════╝"

if [ $ERRORS -gt 0 ]; then exit 1; else exit 0; fi
