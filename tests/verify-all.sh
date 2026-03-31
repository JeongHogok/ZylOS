#!/bin/bash
# ──────────────────────────────────────────────────────────
# Zyl OS — 전체 코드베이스 검증 스크립트
#
# 모든 영역에 대해 Clean Architecture, SOLID, i18n, 기술부채를
# 자동 검출합니다. 단일 스크립트로 전체 프로젝트 품질을 보증합니다.
#
# 영역:
#   [A] apps/           — OS 이미지 (JS/CSS/HTML)
#   [B] emulator-app/   — Tauri 에뮬레이터 (Rust + JS)
#   [C] runtime/        — 실기기 서비스 (C)
#   [D] system/         — 디바이스 설정 (systemd, DTS, AppArmor)
#   [E] tests/          — 테스트 자체 무결성
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
  | grep -v 'shared/bridge.js' | grep -v 'shared/i18n.js' || true)
if [ -n "$HITS" ]; then fail "postMessage 직접 호출:\n$HITS"; else pass "IPC가 ZylBridge 경유"; fi

# A3: 에뮬레이터 경로 금지
HITS=$(grep -rn 'emulator-app\|hal-tauri' "$ROOT/apps" --include='*.js' --include='*.html' --include='*.css' 2>/dev/null \
  | grep -v '//' | grep -v 'See ' || true)
if [ -n "$HITS" ]; then fail "에뮬레이터 참조:\n$HITS"; else pass "에뮬레이터 참조 없음"; fi

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
# Heuristic: Find strings like textContent = 'Some English' (3+ words)
HITS=$(grep -rn "textContent\s*=\s*['\"]" "$ROOT/apps" --include='*.js' 2>/dev/null \
  | grep -v 'zylI18n\|\.t(' | grep -v '^\s*//' | grep -v "textContent = ''" \
  | grep -v "textContent = '--'" | grep -v 'escapeHtml\|initial\|displayName\|timeStr\|dayName' \
  | grep -v "'0:00'\|'00:00'\|'\\\\u00'" | grep -v 'icon\|emoji\|label\|temp\|maxTemp\|minTemp' || true)
if [ -n "$HITS" ]; then
  warn "i18n 미적용 의심 (영어 하드코딩 가능):\n$(echo "$HITS" | head -5)"
else
  pass "textContent에 하드코딩 미발견"
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
  LOW=$(grep -B2 'backdrop-filter' "$cssfile" | grep -oP 'rgba\([^)]*,\s*0\.[0-4]\d*\)' 2>/dev/null || true)
  if [ -n "$LOW" ]; then
    BD_WARN="${BD_WARN}  $(echo $cssfile | sed "s|$ROOT/||"): $LOW\n"
  fi
done
if [ -n "$BD_WARN" ]; then warn "backdrop-filter 폴백 부족:\n$BD_WARN"; else pass "backdrop-filter 폴백 충분"; fi

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
  for field in id name version; do
    VAL=$(python3 -c "import json; d=json.load(open('$MANIFEST')); print(d.get('$field',''))" 2>/dev/null)
    if [ -z "$VAL" ]; then fail "$APPNAME: app.json '$field' 필드 누락"; fi
  done
done
pass "앱 매니페스트 검사 완료"

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

# B3: Rust 컴파일 체크
if command -v cargo >/dev/null 2>&1 && [ -f "$ROOT/emulator-app/Cargo.toml" ]; then
  if cd "$ROOT/emulator-app" && cargo check 2>&1 | tail -1 | grep -q 'Finished'; then
    pass "Rust cargo check 통과"
  else
    fail "Rust cargo check 실패"
  fi
  cd "$ROOT"
fi

# ═════════════════════════════════════════════════════════════
# [C] runtime/ — 실기기 서비스 (C)
# ═════════════════════════════════════════════════════════════
section "runtime/ — C 서비스 코드 품질"

# C1: TODO/FIXME/HACK/STUB 탐지
TODO_COUNT=$(grep -rPn 'TODO|FIXME|HACK|\bXXX\b|STUB' "$ROOT/runtime" --include='*.c' --include='*.h' 2>/dev/null | wc -l || echo 0)
if [ "$TODO_COUNT" -gt 0 ]; then
  TODO_LIST=$(grep -rPn 'TODO|FIXME|HACK|\bXXX\b|STUB' "$ROOT/runtime" --include='*.c' --include='*.h' 2>/dev/null | sed "s|$ROOT/||")
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

# ═════════════════════════════════════════════════════════════
# [E] 전체 — 기술부채 탐지
# ═════════════════════════════════════════════════════════════
section "전체 — 기술부채 + 코드 위생"

# E1: TODO/FIXME 전체 카운트
ALL_TODO=$(grep -rPn 'TODO|FIXME|HACK|\bXXX\b' "$ROOT" --include='*.js' --include='*.c' --include='*.h' --include='*.rs' 2>/dev/null \
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
