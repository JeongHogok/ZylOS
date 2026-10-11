#!/usr/bin/env python3
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Build Tool
#
# 역할: OS 이미지 정체성(apps/system/release.js)을 빌드 시스템에 읽어 준다
# 수행범위: version / securityPatch 값 출력, securityPatch 형식 검증
# 의존방향: Python 3 표준 라이브러리
# SOLID: SRP — 읽기와 검증만. 값의 출처는 release.js 하나
#
# 사용: python3 tools/release_info.py version|securityPatch
# ──────────────────────────────────────────────────────────
import datetime
import pathlib
import re
import sys

SRC = pathlib.Path(__file__).resolve().parent.parent / 'apps' / 'system' / 'release.js'


def read(key):
    text = SRC.read_text(encoding='utf-8')
    m = re.search(r"\b%s:\s*'([^']+)'" % re.escape(key), text)
    if not m:
        sys.exit('%s has no %s' % (SRC, key))
    return m.group(1)


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ('version', 'securityPatch'):
        sys.exit('usage: release_info.py version|securityPatch')
    key = sys.argv[1]
    value = read(key)
    if key == 'securityPatch':
        try:
            day = datetime.date.fromisoformat(value)
        except ValueError:
            sys.exit('securityPatch %r is not a YYYY-MM-DD date' % value)
        # A patch level in the future is a typo, and would make every device
        # look more up to date than it is
        if day > datetime.date.today() + datetime.timedelta(days=1):
            sys.exit('securityPatch %s is in the future' % value)
    print(value)


if __name__ == '__main__':
    main()
