#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test Script
#
# 역할: 모든 app.json 매니페스트 파일 유효성 검사
# 수행범위: apps/ 디렉토리의 app.json에서 필수 필드 존재 여부 검증
# 의존방향: Python 3 (json 모듈)
# SOLID: SRP — 매니페스트 유효성 검증만 담당
# ──────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ERRORS=0
CHECKED=0

for f in $(find "$PROJECT_DIR/apps/" -name 'app.json'); do
    CHECKED=$((CHECKED + 1))
    python3 -c "
import json, sys
try:
    m = json.load(open('$f'))
except json.JSONDecodeError as e:
    print(f'INVALID JSON in $f: {e}')
    sys.exit(1)
for field in ['id', 'name', 'version', 'entry']:
    if field not in m:
        print(f'MISSING {field} in $f')
        sys.exit(1)
if not isinstance(m.get('id'), str) or len(m['id']) == 0:
    print(f'EMPTY id in $f')
    sys.exit(1)
" || ERRORS=$((ERRORS + 1))
done

echo "Manifest validation: $CHECKED files checked, $ERRORS errors"
exit $ERRORS
