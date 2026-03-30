#!/bin/bash
# ──────────────────────────────────────────────────────────
# OS Image Independence Checker
#
# 검증 항목:
# 1. apps/ 내에 Tauri 직접 참조 없음
# 2. apps/ 내에 window.parent.postMessage 직접 호출 없음 (bridge.js/i18n.js 제외)
# 3. apps/ 내에 ES5 위반 없음 (let/const/arrow function)
# 4. CSS backdrop-filter 사용 시 불투명 폴백 존재
# 5. 에뮬레이터 경로 참조 없음
# ──────────────────────────────────────────────────────────

set -e
APPS_DIR="$(cd "$(dirname "$0")/.." && pwd)/apps"
ERRORS=0
WARNINGS=0

RED='\033[0;31m'
YELLOW='\033[0;33m'
GREEN='\033[0;32m'
NC='\033[0m'

echo "═══════════════════════════════════════════════════"
echo "  Zyl OS — OS Image Independence Check"
echo "═══════════════════════════════════════════════════"
echo ""

# ── Check 1: No Tauri references in apps/ ──
echo "── Check 1: Tauri 참조 금지 ──"
TAURI_REFS=$(grep -rn '__TAURI__\|tauri\.invoke\|window\.__TAURI__' "$APPS_DIR" --include='*.js' 2>/dev/null || true)
if [ -n "$TAURI_REFS" ]; then
    echo -e "${RED}FAIL${NC}: apps/ 내에 Tauri 직접 참조 발견:"
    echo "$TAURI_REFS"
    ERRORS=$((ERRORS + 1))
else
    echo -e "${GREEN}PASS${NC}: Tauri 참조 없음"
fi
echo ""

# ── Check 2: No direct window.parent.postMessage (except bridge.js, i18n.js) ──
echo "── Check 2: window.parent.postMessage 직접 호출 금지 ──"
POSTMSG=$(grep -rn 'window\.parent\.postMessage' "$APPS_DIR" --include='*.js' 2>/dev/null | grep -v 'shared/bridge.js' | grep -v 'shared/i18n.js' || true)
if [ -n "$POSTMSG" ]; then
    echo -e "${RED}FAIL${NC}: apps/ 내에 window.parent.postMessage 직접 호출 발견:"
    echo "$POSTMSG"
    echo ""
    echo "  → ZylBridge.sendToSystem() 또는 ZylBridge.requestService()를 사용하세요."
    ERRORS=$((ERRORS + 1))
else
    echo -e "${GREEN}PASS${NC}: 모든 IPC가 ZylBridge 경유"
fi
echo ""

# ── Check 3: ES5 compliance ──
echo "── Check 3: ES5 호환성 ──"
ES5_VIOLATIONS=""

# Check for 'let ' declarations (not in comments)
LET_FOUND=$(grep -rn '^\s*let \|[;{,(] *let \|^let ' "$APPS_DIR" --include='*.js' 2>/dev/null | grep -v '// ' | grep -v '/\*' || true)
if [ -n "$LET_FOUND" ]; then
    ES5_VIOLATIONS="${ES5_VIOLATIONS}let 선언:\n${LET_FOUND}\n\n"
fi

# Check for 'const ' declarations
CONST_FOUND=$(grep -rn '^\s*const \|[;{,(] *const \|^const ' "$APPS_DIR" --include='*.js' 2>/dev/null | grep -v '// ' | grep -v '/\*' || true)
if [ -n "$CONST_FOUND" ]; then
    ES5_VIOLATIONS="${ES5_VIOLATIONS}const 선언:\n${CONST_FOUND}\n\n"
fi

# Check for arrow functions
ARROW_FOUND=$(grep -rn '=>' "$APPS_DIR" --include='*.js' 2>/dev/null | grep -v '// ' | grep -v '/\*' | grep -v 'data-' | grep -v '\.html' || true)
if [ -n "$ARROW_FOUND" ]; then
    ES5_VIOLATIONS="${ES5_VIOLATIONS}Arrow function:\n${ARROW_FOUND}\n\n"
fi

if [ -n "$ES5_VIOLATIONS" ]; then
    echo -e "${RED}FAIL${NC}: ES5 위반 발견:"
    echo -e "$ES5_VIOLATIONS"
    ERRORS=$((ERRORS + 1))
else
    echo -e "${GREEN}PASS${NC}: ES5 호환"
fi
echo ""

# ── Check 4: No emulator-app path references ──
echo "── Check 4: 에뮬레이터 경로 참조 금지 ──"
# Exclude comments (lines starting with // or *) and only check actual code references
EMU_REFS=$(grep -rn 'emulator-app\|hal-tauri' "$APPS_DIR" --include='*.js' --include='*.html' --include='*.css' 2>/dev/null | grep -v '^\s*//' | grep -v '^\s*\*' | grep -v '// ' | grep -v 'See ' || true)
if [ -n "$EMU_REFS" ]; then
    echo -e "${RED}FAIL${NC}: apps/ 내에 에뮬레이터 경로 참조 발견:"
    echo "$EMU_REFS"
    ERRORS=$((ERRORS + 1))
else
    echo -e "${GREEN}PASS${NC}: 에뮬레이터 경로 참조 없음"
fi
echo ""

# ── Check 5: backdrop-filter has fallback ──
echo "── Check 5: CSS backdrop-filter 폴백 확인 ──"
BD_WARNINGS=""
for cssfile in $(grep -rl 'backdrop-filter' "$APPS_DIR" --include='*.css' 2>/dev/null); do
    # Check if any background before backdrop-filter has very low opacity
    # This is a heuristic check — look for rgba with opacity < 0.5 near backdrop-filter
    LOW_OPACITY=$(grep -B2 'backdrop-filter' "$cssfile" | grep -oP 'rgba\([^)]*,\s*0\.[0-4]\d*\)' 2>/dev/null || true)
    if [ -n "$LOW_OPACITY" ]; then
        BD_WARNINGS="${BD_WARNINGS}  ${cssfile}: 낮은 불투명도 배경 발견 (${LOW_OPACITY})\n"
    fi
done
if [ -n "$BD_WARNINGS" ]; then
    echo -e "${YELLOW}WARN${NC}: backdrop-filter 근처에 낮은 불투명도 배경 발견:"
    echo -e "$BD_WARNINGS"
    echo "  → 소프트웨어 렌더링에서 UI가 투명해질 수 있습니다. opacity ≥ 0.85 권장."
    WARNINGS=$((WARNINGS + 1))
else
    echo -e "${GREEN}PASS${NC}: 모든 backdrop-filter에 충분한 불투명도 폴백 있음"
fi
echo ""

# ── Summary ──
echo "═══════════════════════════════════════════════════"
if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✅ 모든 검사 통과${NC}"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}⚠️  경고 ${WARNINGS}건 (에러 없음)${NC}"
    exit 0
else
    echo -e "${RED}❌ 에러 ${ERRORS}건, 경고 ${WARNINGS}건${NC}"
    exit 1
fi
