# Third-Party Licenses

Zyl OS는 **GNU General Public License v3.0 (GPL-3.0)** 하에 배포됩니다.
Zyl OS를 수정하거나 포함하여 배포하는 모든 프로젝트는 전체 소스 코드를 GPL-3.0으로 공개해야 합니다.

## GPL-3.0 호환 의존성

### LGPL 2.1+ Dependencies (동적 링킹)

LGPL 라이브러리는 GPL-3.0과 호환됩니다 (LGPL은 GPL의 약한 버전).

| 라이브러리 | 라이선스 | 용도 |
|-----------|---------|------|
| WebKitGTK | LGPL 2.1+ | 웹 앱 렌더링 엔진 |
| GTK 4 | LGPL 2.1+ | UI 툴킷 (WAM 윈도우) |
| GLib / GIO | LGPL 2.1+ | D-Bus IPC, 유틸리티 |
| json-glib | LGPL 2.1+ | JSON 파싱 |

### MIT Dependencies (GPL-3.0 호환)

MIT 라이선스는 GPL-3.0과 완전 호환됩니다.

| 라이브러리 | 라이선스 | 용도 |
|-----------|---------|------|
| wlroots | MIT | Wayland 컴포지터 프레임워크 |
| xkbcommon | MIT | 키보드 레이아웃 |
| wayland | MIT | 디스플레이 프로토콜 |
| libinput | MIT | 터치/입력 처리 |
| Pixman | MIT | 2D 픽셀 처리 |

### Rust Dependencies (에뮬레이터)

| 크레이트 | 라이선스 | 용도 |
|---------|---------|------|
| tauri | MIT/Apache-2.0 | 데스크톱 앱 프레임워크 |
| serde | MIT/Apache-2.0 | 직렬화 |
| tokio | MIT | 비동기 런타임 |
| dirs | MIT/Apache-2.0 | 플랫폼 디렉토리 |

## 라이선스 호환성

GPL-3.0은 다음과 호환됩니다:
- MIT, BSD, Apache-2.0 → GPL-3.0 프로젝트에 포함 가능
- LGPL 2.1+ → 동적/정적 링킹 모두 가능
- GPL-2.0-or-later → GPL-3.0으로 업그레이드 가능

GPL-3.0과 **비호환**:
- GPL-2.0-only (업그레이드 불가)
- 독점 라이선스

## 소스 코드 접근

모든 의존성의 소스 코드는 다음에서 접근할 수 있습니다:
- WebKitGTK: https://webkitgtk.org/
- GTK: https://gitlab.gnome.org/GNOME/gtk
- GLib: https://gitlab.gnome.org/GNOME/glib
- wlroots: https://gitlab.freedesktop.org/wlroots/wlroots
- Tauri: https://github.com/tauri-apps/tauri
