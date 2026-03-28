#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test Script
#
# 역할: 모든 JS 파일 구문 오류 검사
# 수행범위: apps/, emulator/ 디렉토리의 .js 파일을 node --check로 검증
# 의존방향: Node.js 런타임
# SOLID: SRP — JS 구문 검증만 담당
# ──────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ERRORS=0
CHECKED=0

for f in $(find "$PROJECT_DIR/apps/" "$PROJECT_DIR/emulator/" -name '*.js' 2>/dev/null); do
    CHECKED=$((CHECKED + 1))
    if ! node --check "$f" 2>/dev/null; then
        echo "FAIL: $f"
        ERRORS=$((ERRORS + 1))
    fi
done

echo "JS syntax check: $CHECKED files checked, $ERRORS errors"
exit $ERRORS
