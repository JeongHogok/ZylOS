# Zyl OS

우리가 직접 만들어 파는 기기에 얹는 상용 모바일 OS. 리눅스 위에 Wayland
컴포지터, 앱을 WebKitGTK 로 띄우는 WAM, D-Bus 시스템 서비스를 얹고, 앱은
HTML/CSS/JS 웹 앱이다.

## 목표

- **우리 기기용** — 남의 기기에 올리는 배포판이 아니라, 직접 만들어 파는
  제품의 OS 다.
- **1 OS × N 프로파일** — 폼팩터는 하드웨어다. 제조사가 빌드할 때 프로파일
  (`-Dprofile`)로 고르고, 사용자가 바꾸는 설정이 아니다. 유효한 프로파일은
  `apps/shared/form-factor.js` 의 `PROFILES` 와 `meson_options.txt` 의 choices 가
  정한다(둘이 같은지 `tests/verify-all.sh` 가 본다).
- **스토어를 직접 운영한다** — 봉인이 스토어 키까지 이어지지 않는 앱(스토어가
  서명한 앱, 스토어가 발급한 개발자 인증서·프로필로 봉인한 개발 빌드가 아닌 앱)은
  설치하지도 실행하지도 않는다 ([docs/APPSTORE_API.md](docs/APPSTORE_API.md)).
- **앱은 웹 앱** — 기기에서는 WebKitGTK 가 그린다. 실기기 엔진과 맞추려고 앱
  코드는 ES5 로 쓴다.
- **에뮬레이터는 빈 깡통** — OS 이미지(`apps/`)를 띄우고 메시지를 나를 뿐이다.
  권한·정책·서비스 같은 OS 로직은 전부 OS 쪽에 있다.

## 순서를 정하는 원칙

**기능을 더 얹기 전에 소스 → 이미지 → 부팅 → 로그 회수 루프를 먼저 닫는다.**
기기에서 한 번도 켜 본 적 없는 코드는 "있다"가 아니라 "모른다"다.

보드 없이 이 루프를 도는 것이 QEMU 레인이다 — 소스에서 이미지를 만들어 QEMU 로
부팅하고 기기 안의 자기점검 결과를 받는다. riscv64 레인은 기기와 같은 부팅 체인(U-Boot →
서명 FIT → dm-verity → A/B)으로 띄운다. 레인이 보는 것과 보드에서만 확인되는 것은
[DEPLOYMENT_GUIDE.md](docs/DEPLOYMENT_GUIDE.md#4-보드-없이-부팅--toolsrun-qemush).

## 폼팩터 성숙도

"모든 폼팩터에 대응한다"의 바닥은 매 커밋 모든 프로파일이 설정되고 그 유닛 집합이
맞는다는 것이다. 빌드와 L1 은 CI 워크플로에 적힌 구성만 지키고, L2·L3 은 보드와
제품마다 올린다.

| 단계 | 뜻 | 지키는 방법 |
|------|----|------------|
| **L0 빌드됨** | 그 프로파일로 설정·빌드된다 | 설정: 모든 프로파일 — `tests/test_image_profiles.sh` 가 설정만 해서 유닛마다 그 프로파일이 빌드하는 바이너리가 있는지 본다(CI `qemu-boot`). 빌드: CI `build-native` 의 기본 구성과 QEMU 레인의 구성만 |
| **L1 조립·부팅** | 이미지가 조립되고 QEMU 에서 부팅해 자기점검을 통과한다 | QEMU 레인 — `.github/workflows/image.yml`·`riscv64.yml` 에 적힌 프로파일만 |
| **L2 보드 검증** | 실기기에서 입력·화면·전력과 그 폼팩터의 정의 기능이 된다 | 보드 + 사람 |
| **L3 출하** | 인증·OTA·스토어·AS 까지 갖춘다 | 제품 단위 |

폼팩터별 정의 기능(L2 통과 기준): phone 통화·SMS·데이터·카메라·배터리 하루 ·
pda 터치·키패드 입력·Wi-Fi·카메라·배터리 하루 · tablet 회전·대화면·배터리 ·
tv HDMI·리모컨·상시전원 · watch 소형화면·절전 · auto 상시전원·빠른 부팅 ·
desktop 외부 입력·다중 해상도 · appliance 키오스크·무인 복구

## 저장소 지도

| 경로 | 무엇 |
|------|------|
| `apps/` | OS 이미지에 들어가는 시스템 앱. `shared/` 는 앱이 함께 쓰는 OS 런타임(브리지·i18n·폼팩터), `system/` 은 에뮬레이터에서 OS 서비스·권한·인텐트를 맡는 JS 계층 |
| `runtime/` | 기기 쪽 OS — `wam/`(앱을 띄우는 Web Application Manager), `services/`(D-Bus 시스템 서비스), `hal/`(하드웨어 추상화), `lib/`(서비스가 함께 쓰는 코드) |
| `compositor/` | wlroots 기반 Wayland 컴포지터 — 화면·입력·시스템 제스처 |
| `system/` | 이미지에 들어가는 시스템 구성 — systemd 유닛, D-Bus 정책, AppArmor, 실행 시점 패키지 목록, 기본 설정, 복구 |
| `board/` | 보드별 계약(`board/<id>/meson.build`), 파티션 레이아웃, 부트 정책(모든 보드의 `boot-policy.txt` + 보드의 상수·훅) — 보드마다 README |
| `emulator-app/` | Tauri 데스크톱 에뮬레이터 — OS 이미지를 띄우는 컨테이너 |
| `tools/` | 이미지·부트로더·서명·OTA·스토어 도구, 앱 SDK(`tools/zyl`), 로컬 CI(`tools/ci-local.sh`) |
| `tests/` | 단위·정책·통합·퍼즈·성능 검사와 `verify-all.sh` |
| `.github/workflows/` | 관심사별 CI 워크플로 |
| `docs/` | 문서 (아래) |

빌드 진입점은 `meson.build`·`meson_options.txt`(보드·프로파일 선택), 재현 가능한
빌드 환경은 `Dockerfile` 이다.

## 시작하기

- **에뮬레이터로 OS 띄우기** — 준비와 실행은 [emulator-app/README.md](emulator-app/README.md).
- **이미지를 만들어 QEMU 로 부팅하기** — 리눅스(또는 컨테이너)에서:
  [docs/DEPLOYMENT_GUIDE.md](docs/DEPLOYMENT_GUIDE.md) §1–4. 맥에서는 같은 CI 잡을 로컬
  컨테이너로 돌린다(`tools/ci-local.sh qemu-boot`,
  [CONTRIBUTING.md](CONTRIBUTING.md#로컬-ci--푸시-전-관문)).
- **실기기** — 서명·기록·OTA 는 [docs/DEPLOYMENT_GUIDE.md](docs/DEPLOYMENT_GUIDE.md) §5–6.

## 문서

| 문서 | 내용 |
|------|------|
| [CLAUDE.md](CLAUDE.md) | 프로젝트 규칙 — 아키텍처 경계, 사실의 단일 출처, 검사 |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 일하는 방법 — 브랜치, 로컬 CI, 테스트, 커밋 메시지 |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 구조와 경계 |
| [docs/SECURITY.md](docs/SECURITY.md) | 보안 모델 |
| [docs/APP_DEVELOPMENT_GUIDE.md](docs/APP_DEVELOPMENT_GUIDE.md) | 앱 개발 — 매니페스트, 서비스 API, SDK |
| [docs/APPSTORE_API.md](docs/APPSTORE_API.md) | 스토어 프로토콜 — 패키지·카탈로그·서명 |
| [docs/DEPLOYMENT_GUIDE.md](docs/DEPLOYMENT_GUIDE.md) | 이미지 빌드, 플래싱, OTA |
| [docs/INSTALLATION_INFORMATION.md](docs/INSTALLATION_INFORMATION.md) | GPLv3 §6 설치 정보 |
| `board/<id>/README.md` | 보드별 안내 |
| [emulator-app/README.md](emulator-app/README.md) | 에뮬레이터 |

## 라이선스

GNU General Public License v3.0 — [LICENSE](LICENSE). 의존성 라이선스는
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
