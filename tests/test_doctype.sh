#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test Script
#
# 역할: 모든 HTML 파일이 <!DOCTYPE html>로 시작하는지 검증
# 수행범위: apps/, emulator/, emulator-app/ui/ 내 *.html 파일
# 의존방향: 없음 (순수 bash)
# SOLID: SRP — DOCTYPE 위치 검증만 담당
#
# 배경: Tauri WKWebView는 <!DOCTYPE html> 이전에 주석이 있으면
#        Content-Type을 application/octet-stream으로 판단하여
#        페이지가 렌더링되지 않음 (흰 화면 문제)
# ──────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ERRORS=0
CHECKED=0

# apps/ 와 emulator/ 내 모든 HTML 파일 검사
for f in $(find "$PROJECT_DIR/apps/" "$PROJECT_DIR/emulator/" -name '*.html' 2>/dev/null); do
    CHECKED=$((CHECKED + 1))
    first_line=$(head -1 "$f")
    if ! echo "$first_line" | grep -qi '<!doctype'; then
        echo "FAIL: $f"
        echo "  First line: $first_line"
        echo "  Expected: <!DOCTYPE html>"
        ERRORS=$((ERRORS + 1))
    fi
done

# emulator-app/ui/ 내 HTML 파일도 검사 (존재하는 경우)
if [ -d "$PROJECT_DIR/emulator-app/ui" ]; then
    for f in $(find "$PROJECT_DIR/emulator-app/ui/" -name '*.html' 2>/dev/null); do
        CHECKED=$((CHECKED + 1))
        first_line=$(head -1 "$f")
        if ! echo "$first_line" | grep -qi '<!doctype'; then
            echo "FAIL: $f"
            echo "  First line: $first_line"
            echo "  Expected: <!DOCTYPE html>"
            ERRORS=$((ERRORS + 1))
        fi
    done
fi

echo "DOCTYPE check: $CHECKED files checked, $ERRORS errors"
exit $ERRORS
