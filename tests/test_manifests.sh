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

# Valid form-factor ids come from the OS policy file, never a second list here.
VALID_FF=$(python3 -c "
import re, sys
src = open('$PROJECT_DIR/apps/shared/form-factor.js').read()
block = src.split('var PROFILES = {', 1)[1].split(chr(10) + chr(32)*2 + chr(125) + chr(59), 1)[0]
print(','.join(re.findall(r'^    ([a-z]+): [{]', block, re.M)))
")
if [ -z "$VALID_FF" ]; then
    echo "FAILED to read form-factor ids from apps/shared/form-factor.js"
    exit 1
fi

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
if 'launcher' in m and not isinstance(m['launcher'], bool):
    print(f'launcher must be true or false in $f')
    sys.exit(1)
# What the app opens (apps/system/intent.js, runtime/wam/src/intent): known
# actions, a type/subtype mime (subtype may be *)
import re
ACTIONS = {'VIEW', 'SEND', 'EDIT', 'DIAL', 'CAPTURE', 'SEARCH'}
for it in m.get('intents', []):
    if not isinstance(it, dict) or it.get('action', '').replace('zyl.intent.action.', '', 1) not in ACTIONS \
            or not it['action'].startswith('zyl.intent.action.') \
            or not re.fullmatch(r'[a-z*]+/[a-z0-9.+*-]+', str(it.get('mimeType', ''))):
        print(f'BAD intent {it} in $f (action zyl.intent.action.{sorted(ACTIONS)}, mimeType type/subtype)')
        sys.exit(1)
ff = m.get('formFactors')
if ff is not None:
    valid = set('$VALID_FF'.split(',')) | {'*'}
    if not isinstance(ff, list) or not ff:
        print(f'formFactors must be a non-empty list in $f')
        sys.exit(1)
    bad = [x for x in ff if x not in valid]
    if bad:
        print(f'UNKNOWN formFactors {bad} in $f (valid: {sorted(valid)})')
        sys.exit(1)
" || ERRORS=$((ERRORS + 1))
done

echo "Manifest validation: $CHECKED files checked, $ERRORS errors (form factors: $VALID_FF)"
exit $ERRORS
