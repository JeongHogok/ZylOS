# Zyl OS 기여 가이드

지켜야 할 규칙(아키텍처 경계, ES5, i18n, 파일 헤더, 보안, 사실의 단일 출처)은
[CLAUDE.md](CLAUDE.md) 가 유일한 출처다. 이 문서는 일하는 방법만 적는다.

## 작업 흐름

1. 작업 하나에 브랜치 하나 — `main` 에서 `<종류>/<주제>` 로 딴다
   (예: `feat/riscv64-lane`, `fix/flaky-pin-absence-test`, `ci/local-runner`).
2. 로컬 CI 가 녹색인 것을 본 뒤에만 푸시한다.
3. PR 로 `main` 에 머지한다. 원격 GitHub Actions 는 같은 워크플로의 기록일 뿐
   검증 수단이 아니다.

## 로컬 CI — 푸시 전 관문

```bash
tools/ci-local.sh                 # main 대비 바뀐 것에 걸리는 잡만
tools/ci-local.sh --dry-run       # 무엇이 걸리는지만
tools/ci-local.sh lint-js fuzz    # 잡 이름으로 골라서
tools/ci-local.sh --all           # 전부
```

`act` 와 Docker 가 있어야 한다. 잡 로그는 `${TMPDIR:-/tmp}/zyl-ci-local/<잡>.log`.

**잡 선택 규칙.** 각 워크플로(`.github/workflows/*.yml`)의 `on.pull_request.paths`
가 "어떤 변경이 이 검사를 부르는가" 의 유일한 출처다. `ci-local.sh` 는 `main` 과의
갈림점 대비 바뀐 파일(커밋 안 한 것·새 파일 포함)을 그 패턴에 대 보고, 하나라도
걸리면 그 워크플로의 잡을 전부 돌린다. `paths` 가 없는 워크플로는 늘 돈다. 원격
PR 도 같은 목록을 읽고, 이 규칙은 `tests/test_ci_select.sh` 가 검증한다.

**어디서 도는가.** Arch 이미지를 쓰는 잡은 amd64 에뮬레이션으로(Arch 가 amd64
이미지만 낸다), 나머지는 호스트 아키텍처 그대로 돈다 — 에뮬레이션 안에서는
ASan(퍼저)이 깨지고 QEMU 가 너무 느리다. `perf` 는 호스트 Chrome 으로, QEMU 를
띄우는 잡은 하나씩 돈다.

## 규칙 검사와 커밋 전 훅

```bash
bash tests/verify-all.sh                 # CI 잡 verify-all (rules.yml)
git config core.hooksPath tools/hooks    # 클론마다 한 번
```

`verify-all` 은 CLAUDE.md 규칙을 기계로 검사하고, 사본이 권위와 같은지(예: `bridge.c`
의 권한 정책 = `apps/system/permissions.js`) 대조한다. `python3` 이 필요하다.
훅은 커밋할 때 `tests/check-os-independence.sh` 를 돌려 apps/ 의 위반을 막는다.

## 테스트 돌리기

| 무엇 | 명령 | CI 잡 |
|------|------|-------|
| C 단위·정책 테스트 | `meson setup builddir && meson test -C builddir` (하나만: `meson test -C builddir <이름>`) | `build-native` |
| JS 서비스 모듈 | `node tests/test_services.js` | `test-js` |
| JS 문법·매니페스트 | `bash tests/test_js_syntax.sh`, `bash tests/test_manifests.sh` | `lint-js` |
| HTML 첫 줄 DOCTYPE | `bash tests/test_doctype.sh` | `lint-html` |
| 에뮬레이터(Rust) | `cargo test --manifest-path emulator-app/Cargo.toml --locked` | `test-emulator` |
| 부트 정책·서명·OTA·잡 선택 | `bash tests/test_boot_env.sh`, `tests/test_sign_image.sh`, `tests/test_build_ota.sh`, `tests/test_ci_select.sh` | `test-integration` |
| 프로파일마다 설정되고 유닛이 바이너리와 맞는지 | `bash tests/test_image_profiles.sh` (meson 필요) | `qemu-boot` |
| 이미지 → QEMU 부팅 → 자기점검 | [DEPLOYMENT_GUIDE](docs/DEPLOYMENT_GUIDE.md#4-보드-없이-부팅--toolsrun-qemush) | `qemu-boot`, `qemu-boot-riscv64` |
| 부트로더 빌드·서명 | `tools/build-bootloader.sh` ([DEPLOYMENT_GUIDE](docs/DEPLOYMENT_GUIDE.md)) | `build-bootloader` |
| 재현 가능한 빌드 환경 | `docker build --target builder -t zylos-build .` | `build-docker` |
| 라이선스 호환 | `bash tests/audit_licenses.sh` | `audit-licenses` |
| 퍼저 | 아래 | `fuzz` |
| 성능 예산 | 아래 | `perf` |

어떤 잡이 정확히 무엇을 하는지는 그 워크플로 파일이 출처다. 리눅스가 아닌
호스트에서는 C 빌드가 필요한 것을 `tools/ci-local.sh <잡>` 으로 돌린다.

C 단위 테스트는 `tests/meson.build` 에 정의되고 `assert()` 로 판정한다. 시험
대상은 복사본이 아니라 **실제 소스**다 — 데몬 소스는 `-DZYL_TEST` 로 `main()` 을
빼고 테스트에 넣는다(`tests/test_netstats.c` 참고). `dbus-policy`·`store-e2e` 는
빌드된 데몬을 사설 버스에 띄우는 종단 테스트라 D-Bus 도구(`dbus-daemon`·`gdbus`)가
필요하다.

### 퍼저

`tests/fuzz/fuzz_*.c` 가 libFuzzer 하네스다(clang, `-fsanitize=fuzzer,address`).
`fuzz_json_dispatch` 는 실제 `runtime/wam/src/bridge/bridge.c` 를 mock WebKit
(`tests/include/`)과 함께 컴파일한다. 빌드·실행 줄은 `.github/workflows/native.yml`
의 `fuzz` 잡이 단일 출처이고, 로컬에서는:

```bash
tools/ci-local.sh fuzz
```

새 하네스는 `tests/fuzz/fuzz_<이름>.c` 로 두면 잡이 알아서 집는다. 시드 입력은
`tests/fuzz/corpus/fuzz_<이름>/` 에 둔다.

### 성능 예산

```bash
node tests/perf/run.js                 # 전 앱 (--app=<이름>, --runs=N)
```

앱마다 시작 시간·메인스레드 블로킹·힙·DOM 노드·레이아웃 수를 CPU 스로틀 + GPU
없이 재서 `tests/perf/budgets.json` 과 비교한다. Node(버전은 `.github/workflows/web.yml`
의 `perf` 잡)와 Chrome/Chromium 이 필요하다(못 찾으면 `CHROME_PATH`). 의도한 변경으로 수치가 바뀌면
`node tests/perf/run.js --update` 로 예산을 다시 쓰고 **그 이유를 커밋에 적는다.**
예산을 올리는 것으로 회귀를 덮지 않는다.

## 검사를 더할 때

- **검사가 읽는 경로는 그 워크플로의 `pull_request.paths` 에도 적는다.** 빠지면
  그 경로만 바뀐 PR 에서 검사가 돌지 않는다 — 원격도 로컬도.
- C 테스트는 `tests/meson.build` 에 `test()` 로 더한다(`build-native` 가 돌린다).
  스크립트 검사는 맞는 워크플로의 잡에 step 으로 더한다.
- 워크플로를 바꾸면 로컬에서도 돌아야 한다. `ci-local.sh` 는 YAML 파서 없이
  읽는다 — `jobs:` 아래 두 칸 들여쓴 잡 이름, `paths:` 아래 `- '패턴'` 줄. 같은
  모양으로 쓸 것.
- 새 검사는 일부러 되돌려 실패하는 것을 한 번 본다. 판정 코드의 복사본을 시험하지
  않는다.

## 커밋 메시지

한국어로, **왜가 먼저**.

```
<종류>(<범위>): <무엇이 달라지나> — <왜 / 핵심>

무엇이 깨져 있었고 어떻게 드러났는지, 그래서 무엇을 바꿨는지.

검증: 무엇을 돌려 확인했는지
확인 못 한 것: 이번 검사로 볼 수 없던 것
```

- 종류: `feat` `fix` `refactor` `test` `ci` `docs` `chore` `style`. 범위는 생략
  하거나 영역 이름(`wam`, `image`, `apparmor`, `emulator` 등).
- 제목은 기능 이름이 아니라 결과로 쓴다 — 예:
  `fix(dbus): 서비스를 시스템 버스로 — 세션 버스를 기다리던 유닛은 영원히 뜨지 않았다`
- 성능 예산을 갱신했다면 이유를 본문에 적는다.

## 라이선스

기여한 코드는 **GNU General Public License v3.0** 으로 배포된다. PR 을 제출하면
그 조건에 동의하는 것으로 본다.
