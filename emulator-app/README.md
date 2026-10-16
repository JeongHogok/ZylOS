# Zyl OS 에뮬레이터

개발 호스트(macOS·Linux)에서 OS 이미지의 앱을 돌리는 Tauri 데스크톱 앱이다.
기기의 컴포지터·WAM 자리에 서는 **컨테이너**일 뿐 OS 정책은 갖지 않는다(CLAUDE.md 규칙 2).
OS 안에서 이 컨테이너가 어디에 서는지는 [ARCHITECTURE.md](../docs/ARCHITECTURE.md) 에 있다.

## 하는 일

| | |
|---|---|
| 프레임 | 앱 프레임(`#app-frame`)에는 앞에 있는 앱 하나를 띄운다. 다른 앱으로 넘어갈 때는 떠나는 앱에 상태를 묻고(`lifecycle.save`) 문서를 바꾼다. 시스템 UI 프레임(`#system-frame`)에는 `apps/statusbar` 를 띄우고, 그 크기는 OS 가 정한 대로(`panel.mode`) 바꾼다 |
| IPC 라우팅 | 두 프레임의 메시지만 받는다. 보낸 앱은 메시지 내용이 아니라 어느 프레임에서 왔는지로 정한다. `service.request` 는 `ZylSystemServices.handleRequest` 로 넘기고, 앱 실행·잠금 해제·설정 완료·시스템 UI 크기는 `ZylShell` 에, 인텐트는 `ZylIntent` 에 물어 그 답대로 띄운다 |
| Tauri 커맨드 | 호스트에서만 알 수 있는 사실과 입출력을 맡는다. 가상 저장소의 파일, 앱 비공개 데이터, 설정 파일, 스토어 패키지 내려받기·풀기·반영, HTTP, 호스트의 Wi-Fi·블루투스·배터리, 위치, 자격 증명 저장소가 여기에 든다. 등록 목록은 `src/main.rs` |
| `zyl-app://` | 스토어로 설치한 앱을 기기처럼 자기 출처(`zyl-app://<id>/`)로 내준다. `/shared/`·`/keyboard/` 는 이미지의 OS 코드다(`src/commands/store.rs`) |

무엇이 앞에 올지, 권한, 인텐트 해석, 봉인, 시스템 앱 판별, 설치된 앱 목록은 여기에 두지 않는다.
모두 OS 이미지 `apps/system/` 의 것이고, 기기 쪽 C 구현과 짝을 이룬다.

컨테이너 페이지 `ui/index.html` 은 OS 계층 JS(`apps/system/`)를 `<script>` 로 직접 불러 쓴다.
그래서 서비스 모듈을 새로 만들면 그 파일의 목록에도 넣어야 한다.

## 준비

- Rust 와 Tauri CLI(`cargo install tauri-cli --locked`). CLI 의 메이저 버전은 `Cargo.toml` 의
  `tauri` 와 같아야 한다(`--version '^<메이저>'`).
- Linux 에서는 Tauri 의 리눅스 백엔드(WebKitGTK·GTK·libsoup)의 개발 패키지가 필요하다. 설치
  목록은 `.github/workflows/emulator.yml` 의 `test-emulator` 잡에 있다.
- 가상 저장소용 디스크 이미지 도구: macOS 는 `hdiutil`, Linux 는 `udisksctl`·`mkfs.ext4`.
  마운트하지 못하면 디렉터리로 대신한다.

## 실행

```bash
cd emulator-app
cargo tauri dev
```

`cargo tauri dev` 는 먼저 `scripts/deploy-apps.sh`(`tauri.conf.json` 의 `beforeDevCommand`)를
돌려 저장소의 `apps/` 를 `ui/apps/` 에 동기화한다. 저장소에 `apps/` 가 없으면 OS 이미지
디렉터리에 있는 첫 `.img` 안의 `apps/` 를 쓴다. `ui/apps/` 는 커밋하지 않는다.

설정 화면에서 기기, OS 이미지, 저장 공간, RAM 을 고르고 부팅한다. 기기 목록
`ui/device-profiles.json` 에는 화면·내비게이션·입력 장치 같은 하드웨어 사실만 적는다.
폼팩터는 OS 가 판정한다.

에뮬레이터 창에 포커스가 있을 때 쓰는 키: `h` 홈, `Esc` 뒤로, `r` 최근 앱, `l` 잠금,
`p` 전원, `↑`/`↓` 볼륨.

## OS 이미지와 가상 기기

- 앱 페이지는 언제나 `ui/apps/` 에서 읽는다.
- `scripts/build-image.sh [버전] [크기MB]` 는 저장소의 `apps/` 를 `os-images/<버전>.img` 와 메타데이터
  `<버전>.json` 으로 묶는다. 설정 화면은 이 디렉터리(또는 직접 고른 디렉터리)의 `.img` 를 보여 준다.
  부팅하면 고른 버전의 `os-images/<버전>.img` 를 마운트하고, 그 위치를 부팅 정보에 적는다.
- 데이터 디렉터리는 macOS 에서 `~/Library/Application Support/zyl-emulator/`, Linux 에서
  `~/.local/share/zyl-emulator/` 이다. 안의 구성은 다음과 같다.
  - `os-images/`: OS 이미지
  - `images/`: 기기마다 저장소 이미지와 기기 ID
  - `mnt/`: 저장소 이미지의 마운트 지점
  - `vfs/`: 마운트하지 못했을 때 대신 쓰는 디렉터리
  - `devices/`: 저장한 기기 설정
- 저장소 이미지는 기기의 `/data` 에 해당한다. 설정(`settings.json`), 앱 컨테이너와 스토어 앱
  (`.system/`), 자격 증명(`.credentials/`)이 여기에 있다. 앱의 파일 서비스는 이 보호 경로에
  닿지 못한다.
- 기기 ID 는 가상 기기마다 한 번 만들어 저장소 이미지 옆에 둔다. 개발 빌드의 프로비저닝 프로필이
  이 ID 를 본다. 저장한 기기를 지우면 ID 도 함께 지워진다.
- RAM 은 에뮬레이터 프로세스의 메모리 상한으로 건다. Linux 는 cgroup v2, macOS 는 `setrlimit` 이고
  최선 노력이다.

## 테스트

- Rust: `cargo test --manifest-path emulator-app/Cargo.toml --locked`. CI 의 `test-emulator` 잡
  (`.github/workflows/emulator.yml`)은 같은 명령을 컴파일 경고를 실패로 놓고 돌린다.
  로컬에서는 `tools/ci-local.sh test-emulator` 로 돌린다.
- 컨테이너 JS 중 앱 상태 저장소(`ui/js/app-state.js`)는 `tests/test_services.js` 가 검증한다.
- `tests/verify-all.sh` 의 에뮬레이터 절은 IPC 라우터 `ui/js/services.js` 에 권한 판정이 없는지와
  `src/` 의 Rust 파일에 클린 아키텍처 헤더가 있는지(없으면 경고)를 본다. 컨테이너의 나머지
  코드(`ui/js/emulator.js`, `src/`)에 정책이 들어오지 않게 하는 것은 리뷰의 몫이다.
