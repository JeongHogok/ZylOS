#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Local CI Runner
#
# 역할: 바뀐 것에 해당하는 CI 잡을 로컬 Docker 에서 act 로 돌린다 — 푸시 전 관문
# 수행범위: 변경 파일 → 워크플로 paths 필터로 잡 선택, 잡별 컨테이너 아키텍처,
#           perf 는 호스트 Chrome, 잡별 로그와 요약. 하나라도 실패하면 1
# 의존방향: act, docker, git, node + Chrome(perf), .github/workflows/*.yml
# SOLID: SRP — 무엇을 언제 검사할지는 워크플로(잡·paths)가 정한다. 여기는
#        그것을 읽어 실행만 한다 (같은 사실을 여기 다시 적지 않는다)
#
# 사용:
#   tools/ci-local.sh                 # main 대비 바뀐 것(커밋 안 한 것 포함)에 걸리는 잡
#   tools/ci-local.sh --all           # 전부
#   tools/ci-local.sh lint-js fuzz    # 잡 이름으로 골라서
#   tools/ci-local.sh --dry-run       # 무엇을 돌릴지만 보여준다
#
# 컨테이너 아키텍처: Arch 이미지 잡은 Arch 가 amd64 만 내므로 amd64 에뮬레이션,
# 나머지는 호스트 그대로 — 에뮬레이션 안에서는 ASan(fuzz)이 깨지고 QEMU 부팅이
# 수십 배 느려진다. QEMU 를 띄우는 잡은 게스트가 2 GiB 씩 써서 하나씩 돈다.
# 실행은 기기마다 하나씩 — 다른 작업 트리의 실행은 끝날 때까지 기다린다.
# ──────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WF_DIR="$ROOT/.github/workflows"
LOGS="${TMPDIR:-/tmp}/zyl-ci-local"
RUNNER_IMAGE="catthehacker/ubuntu:act-24.04"

ALL=0 DRY=0 PICK=""
for a in "$@"; do
    case "$a" in
        --all)     ALL=1 ;;
        --dry-run) DRY=1 ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *)         PICK="$PICK $a" ;;
    esac
done

# ── 워크플로 읽기 (YAML 파서 없이 — 이 저장소 워크플로의 들여쓰기 규약에 기댄다) ──
jobs_of()  { awk '/^jobs:/{j=1;next} j && /^  [a-z0-9_-]+:$/{s=$1; sub(/:$/,"",s); print s}' "$1"; }
job_text() { awk -v id="$2" '/^jobs:/{j=1;next} j && /^  [a-z0-9_-]+:$/{c=$1; sub(/:$/,"",c)} j && c==id' "$1"; }
paths_of() { awk '/^on:/{o=1;next} o && /^[^ #]/{o=0} o' "$1" | sed -n "s/^ *- '\(.*\)'$/\1/p" | sort -u; }

changed_files() {
    local base
    base="$(git -C "$ROOT" merge-base main HEAD 2>/dev/null || echo HEAD)"
    { git -C "$ROOT" diff --name-only "$base"            # 커밋한 것
      git -C "$ROOT" diff --name-only HEAD               # 커밋 안 한 것
      git -C "$ROOT" ls-files --others --exclude-standard # 새 파일
    } 2>/dev/null | sort -u
}

# paths 필터가 없으면 항상, 있으면 바뀐 파일 하나라도 걸리면 (GitHub 의 ** 는
# bash 패턴의 * 로 충분하다 — * 가 / 도 넘으므로 원격보다 넓게 걸 뿐 좁지 않다)
# 패턴은 줄 단위로 읽는다 — `for p in $pats` 로 펼치면 runtime/** 가 경로 확장돼
# 실제 파일 이름들로 바뀐다 (tests/test_ci_select.sh 가 잡았다)
workflow_hit() {
    local pats f p
    pats="$(paths_of "$1")"
    [ -z "$pats" ] && return 0
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        while IFS= read -r p; do
            # shellcheck disable=SC2053  # 오른쪽은 패턴이다
            [[ "$f" == $p ]] && return 0
            [[ "$p" == '**/'* && "$f" == "${p#\*\*/}" ]] && return 0   # **/x 는 최상위 x 도
        done <<< "$pats"
    done <<< "$CHANGED"
    return 1
}

# CI_LOCAL_CHANGED: 변경 목록을 직접 준다 (선택 규칙 테스트용 — tests/test_ci_select.sh)
CHANGED="${CI_LOCAL_CHANGED-$(changed_files)}"
SELECTED=""   # "워크플로파일:잡" 목록
for wf in "$WF_DIR"/*.yml; do
    for j in $(jobs_of "$wf"); do
        if [ -n "$PICK" ]; then
            case " $PICK " in *" $j "*) ;; *) continue ;; esac
        elif [ "$ALL" = 0 ] && ! workflow_hit "$wf"; then
            continue
        fi
        SELECTED="$SELECTED $(basename "$wf"):$j"
    done
done

if [ -z "$SELECTED" ]; then
    echo "돌릴 잡이 없다 — main 대비 워크플로 paths 에 걸리는 변경이 없다 (전부: --all)"
    exit 0
fi

echo "═══ 로컬 CI ═══"
echo "변경: $(echo "$CHANGED" | grep -c . ) 개 파일"
for s in $SELECTED; do echo "  · ${s#*:}  (${s%%:*})"; done
[ "$DRY" = 1 ] && exit 0

command -v act >/dev/null 2>&1 || { echo "✗ act 가 없다 (brew install act)" >&2; exit 1; }
docker info >/dev/null 2>&1 || { echo "✗ docker 데몬에 붙을 수 없다" >&2; exit 1; }
case "$(docker info --format '{{.Architecture}}')" in
    aarch64|arm64) NATIVE=linux/arm64 ;;
    *)             NATIVE=linux/amd64 ;;
esac
# 한 기기에 한 실행만: act 컨테이너 이름은 잡마다 고정이고 로그도 한 곳이라, 다른
# 작업 트리·세션이 동시에 돌리면 서로의 컨테이너를 지우고(exit 137) 결과를 덮는다.
# QEMU 잡을 하나씩 돌린다는 전제도 실행 사이에서 깨진다. macOS 에 flock 이 없어 mkdir
# 로 잡고, 주인이 죽은 잠금은 치운다
LOCK="$LOGS.lock"
until mkdir "$LOCK" 2>/dev/null; do
    owner="$(cat "$LOCK/pid" 2>/dev/null)"
    if [ -n "$owner" ] && ! kill -0 "$owner" 2>/dev/null; then rm -rf "$LOCK"; continue; fi
    echo "  … 다른 ci-local 실행(pid ${owner:-?})이 끝나길 기다린다"
    sleep 15
done
echo $$ > "$LOCK/pid"
trap 'rm -rf "$LOCK"' EXIT

mkdir -p "$LOGS"; : > "$LOGS/results"
echo "로그: $LOGS"

record() {   # record <잡> <rc> <시작시각> <어디서>
    printf '%-20s %-6s %5ss  %s\n' "$1" "$([ "$2" = 0 ] && echo ok || echo FAIL)" \
        "$(( $(date +%s) - $3 ))" "$4" >> "$LOGS/results"
}

run_job() {   # run_job <워크플로파일> <잡>
    local wf="$1" job="$2" text plat="$NATIVE" extra="" t0 rc
    t0=$(date +%s)
    if [ "$job" = perf ]; then
        # 러너 이미지에는 Chrome 이 없고, 에뮬레이션 Chrome 으로는 예산을 잴 수
        # 없다 — 워크플로와 같은 명령을 호스트 Chrome 으로 돈다
        (cd "$ROOT" && node tests/perf/run.js --runs=3) > "$LOGS/$job.log" 2>&1
        record "$job" $? "$t0" host; return
    fi
    text="$(job_text "$WF_DIR/$wf" "$job")"
    case "$text" in *"image: archlinux"*) plat=linux/amd64 ;; esac
    case "$text" in *"--privileged"*) extra="--privileged" ;; esac
    (cd "$ROOT" && act pull_request -W ".github/workflows/$wf" -j "$job" --rm \
        --container-architecture "$plat" -P "ubuntu-24.04=$RUNNER_IMAGE" $extra) \
        > "$LOGS/$job.log" 2>&1
    rc=$?
    record "$job" "$rc" "$t0" "$plat"
}

# QEMU 를 띄우지 않는 잡은 함께, 띄우는 잡은 하나씩
SERIAL=""
for s in $SELECTED; do
    wf="${s%%:*}"; j="${s#*:}"
    case "$(job_text "$WF_DIR/$wf" "$j")" in
        *run-qemu.sh*) SERIAL="$SERIAL $s" ;;
        *)             run_job "$wf" "$j" & ;;
    esac
done
wait
for s in $SERIAL; do
    echo "  … ${s#*:}"
    run_job "${s%%:*}" "${s#*:}"
done

echo
cat "$LOGS/results"
if grep -q ' FAIL ' "$LOGS/results"; then
    echo
    echo "✗ 실패한 잡이 있다 — $LOGS/<잡>.log"
    exit 1
fi
echo "✓ 로컬 CI 통과"
