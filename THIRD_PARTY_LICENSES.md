# Third-Party Licenses

Zyl OS는 MIT 라이선스 하에 배포됩니다. 아래 LGPL 의존성은 동적 링킹으로 사용되며,
LGPL 2.1+ 요구사항에 따라 소스 코드 접근이 보장됩니다.

## LGPL 2.1+ Dependencies (dynamically linked)

| 라이브러리 | 라이선스 | 용도 |
|-----------|---------|------|
| WebKitGTK | LGPL 2.1+ | 웹 앱 렌더링 엔진 |
| GTK 4 | LGPL 2.1+ | UI 툴킷 (WAM 윈도우) |
| GLib / GIO | LGPL 2.1+ | D-Bus IPC, 유틸리티 |
| json-glib | LGPL 2.1+ | JSON 파싱 |
| libinput | MIT | 터치/입력 처리 |
| Pixman | MIT | 2D 픽셀 처리 |

## MIT Dependencies

| 라이브러리 | 라이선스 | 용도 |
|-----------|---------|------|
| wlroots | MIT | Wayland 컴포지터 프레임워크 |
| xkbcommon | MIT | 키보드 레이아웃 |
| wayland | MIT | 디스플레이 프로토콜 |

## LGPL 준수 안내

Zyl OS는 LGPL 라이브러리를 **동적 링킹**으로 사용합니다.
사용자는 수정된 LGPL 라이브러리로 재링킹할 권리가 있습니다.

LGPL 라이브러리 소스 코드:
- WebKitGTK: https://webkitgtk.org/
- GTK: https://gitlab.gnome.org/GNOME/gtk
- GLib: https://gitlab.gnome.org/GNOME/glib
