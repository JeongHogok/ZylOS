/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Input HAL Linux 구현 — evdev 하드웨어 키
 * 수행범위: /dev/input/event* 중 OS 키(전원·볼륨·뒤로·홈·메뉴)를 하나라도 가진 장치를
 *           모두 열어 epoll 하나로 묶고, 누름(value 1)만 OS 키로 읽는다. 뗌·자동반복·
 *           다른 이벤트는 건너뛰고, 빠진(끊긴) 장치는 닫는다 — 남은 장치는 계속 읽힌다
 * 의존방향: hal.h (Domain), linux/input.h (EVIOCGBIT), epoll
 * SOLID: SRP — 키 장치 접근과 코드 변환만. 키가 무엇을 할지는 서비스·셸 몫
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include "hal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <linux/input.h>

/* Overridable so tests point at fake device nodes */
#ifndef ZYL_DEV_ROOT
#define ZYL_DEV_ROOT ""
#endif
#define INPUT_DIR ZYL_DEV_ROOT "/dev/input"
#define MAX_KEY_DEVICES 16
#define LONG_BITS (8 * sizeof(unsigned long))

static const struct { unsigned code; ZylInputKey key; } KEYS[] = {
    { KEY_POWER,      ZYL_HAL_KEY_POWER },
    { KEY_VOLUMEUP,   ZYL_HAL_KEY_VOLUME_UP },
    { KEY_VOLUMEDOWN, ZYL_HAL_KEY_VOLUME_DOWN },
    { KEY_BACK,       ZYL_HAL_KEY_BACK },
    { KEY_HOMEPAGE,   ZYL_HAL_KEY_HOME },      /* KEY_HOME is a keyboard's line start */
    { KEY_MENU,       ZYL_HAL_KEY_MENU },
};
#define KEY_COUNT (sizeof(KEYS) / sizeof(KEYS[0]))

static int g_epoll = -1;
static int g_devices[MAX_KEY_DEVICES];
static int g_count;

/* Has one of the OS keys. A touchscreen (BTN_TOUCH) or an accelerometer would
   only wake the service for events it drops; a plain file fails the ioctl */
static bool has_os_key(int fd) {
    unsigned long bits[KEY_MAX / LONG_BITS + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return false;
    for (size_t i = 0; i < KEY_COUNT; i++)
        if ((bits[KEYS[i].code / LONG_BITS] >> (KEYS[i].code % LONG_BITS)) & 1) return true;
    return false;
}

static void input_close(void) {
    for (int i = 0; i < g_count; i++) close(g_devices[i]);
    g_count = 0;
    if (g_epoll >= 0) close(g_epoll);
    g_epoll = -1;
}

/* shortcut: the devices there at open — a keyboard or remote plugged in later is
   not seen; watch /dev/input (inotify) when an accessory's keys must work */
static int input_open(void) {
    input_close();
    DIR *dir = opendir(INPUT_DIR);
    if (!dir) return -1;
    g_epoll = epoll_create1(EPOLL_CLOEXEC);
    struct dirent *entry;
    while (g_epoll >= 0 && g_count < MAX_KEY_DEVICES && (entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        char path[512];
        if (snprintf(path, sizeof(path), INPUT_DIR "/%s", entry->d_name) >= (int)sizeof(path)) continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
        if (!has_os_key(fd) || epoll_ctl(g_epoll, EPOLL_CTL_ADD, fd, &ev) < 0) {
            close(fd);
            continue;
        }
        g_devices[g_count++] = fd;
    }
    closedir(dir);
    if (g_count == 0) {
        input_close();
        return -1;
    }
    return g_epoll;
}

static void drop_device(int fd) {
    epoll_ctl(g_epoll, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    for (int i = 0; i < g_count; i++) {
        if (g_devices[i] != fd) continue;
        g_devices[i] = g_devices[--g_count];
        break;
    }
}

static bool input_read_key(ZylInputKey *key) {
    struct epoll_event ready;
    while (g_epoll >= 0 && epoll_wait(g_epoll, &ready, 1, 0) == 1) {
        struct input_event ev;
        ssize_t n = read(ready.data.fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev)) {
            if (ev.type != EV_KEY || ev.value != 1) continue;   /* only the press: not release (0) or repeat (2) */
            for (size_t i = 0; i < KEY_COUNT; i++) {
                if (KEYS[i].code != ev.code) continue;
                *key = KEYS[i].key;
                return true;
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR) && !(ready.events & (EPOLLHUP | EPOLLERR)))
            return false;                                          /* woken for nothing */
        drop_device(ready.data.fd);                                /* unplugged (ENODEV), hung up or broken */
    }
    return false;
}

static const ZylInputHal input_hal = { .open = input_open, .read_key = input_read_key, .close = input_close };

const ZylInputHal *zyl_hal_input(void) { return &input_hal; }
