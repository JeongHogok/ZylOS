#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Test Layer - CI Job Selection
#
# 역할: 바뀐 경로 → 돌릴 CI 잡 선택 규칙이 의도대로인지 검증
# 수행범위: tools/ci-local.sh --dry-run 에 변경 목록을 주입해 고른 잡을 대조
# 의존방향: tools/ci-local.sh, .github/workflows/*.yml (paths 필터가 규칙의 출처)
# SOLID: SRP — 선택 규칙만. 잡이 무엇을 하는지는 보지 않는다
# ──────────────────────────────────────────────────────────
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FAIL=0

picked() {   # picked <변경 파일...> → 고른 잡 이름(정렬)
    CI_LOCAL_CHANGED="$(printf '%s\n' "$@")" bash "$ROOT/tools/ci-local.sh" --dry-run \
        | sed -n 's/^  · \([^ ]*\).*/\1/p' | sort | tr '\n' ' ' | sed 's/ $//'
}
expect() {   # expect "<설명>" "<기대 잡들(정렬)>" <변경 파일...>
    local what="$1" want="$2"; shift 2
    local got; got="$(picked "$@")"
    if [ "$got" = "$want" ]; then echo "  ✓ $what"
    else echo "  ✗ $what"; echo "      기대: $want"; echo "      실제: $got"; FAIL=$((FAIL + 1)); fi
}

echo "CI 잡 선택 규칙"
expect "문서만 바뀌면 아무것도 안 돈다" "" README.md docs/ARCHITECTURE.md
expect "앱 JS 는 웹 검사 + 이미지 부팅(홈 앱이 떠야 한다)" \
       "lint-html lint-js perf qemu-boot test-js" apps/home/js/home.js
expect "C 런타임은 네이티브 + 두 부팅 레인" \
       "build-native fuzz qemu-boot qemu-boot-riscv64 test-integration" runtime/wam/src/wam.c
expect "Dockerfile 은 docker 빌드만" "build-docker" Dockerfile
expect "서비스의 meson.build 는 라이선스 감사도" \
       "audit-licenses build-native fuzz qemu-boot qemu-boot-riscv64 test-integration" \
       runtime/services/alarm/meson.build
expect "부트 정책은 네이티브(테스트) + 부트로더" \
       "build-bootloader build-native fuzz test-integration" board/bpi-f3/u-boot-env.txt
expect "교차 툴체인은 riscv64 레인 (+ tools 를 보는 네이티브)" \
       "build-native fuzz qemu-boot-riscv64 test-integration" tools/cross/riscv64.ini

[ "$FAIL" = 0 ] && echo "통과" || { echo "실패 $FAIL 건"; exit 1; }
