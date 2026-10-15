/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Vibrator HAL Linux 구현 — force-feedback 진동 모터
 * 수행범위: /sys/class/input/event<N>/device/capabilities/ff 에 FF_RUMBLE 이 있는
 *           입력 장치를 진동 모터로 찾음(pwm-vibrator, gpio-vibrator 등이 이렇게
 *           드러난다), 패턴(켬·끔 ms)을 별도 스레드에서 재생 — 호출은 바로 돌아옴
 * 의존방향: hal.h (Domain), glib, linux/input.h (EVIOCSFF), pthread
 * SOLID: SRP — 진동 장치 접근만. 진동할지 말지(설정)는 서비스 몫
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include "hal.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <glib.h>

/* Overridable so tests point at a fake sysfs */
#ifndef ZYL_SYSFS_ROOT
#define ZYL_SYSFS_ROOT ""
#endif
#ifndef ZYL_DEV_ROOT
#define ZYL_DEV_ROOT ""
#endif
#define MAX_PULSES 32
#define MAX_PATTERN_MS 10000

/* The ff file is a bitmap printed as hex longs, most significant first */
static bool bitmap_has(const char *text, unsigned bit) {
    char **words = g_strsplit(text, " ", -1);
    unsigned n = g_strv_length(words), per = sizeof(unsigned long) * 8, idx = bit / per;
    bool set = idx < n && ((strtoul(words[n - 1 - idx], NULL, 16) >> (bit % per)) & 1);
    g_strfreev(words);
    return set;
}

static bool find_vibrator(char *dev, size_t len) {
    GDir *dir = g_dir_open(ZYL_SYSFS_ROOT "/sys/class/input", 0, NULL);
    const char *name;
    bool found = false;
    while (dir && !found && (name = g_dir_read_name(dir))) {
        if (!g_str_has_prefix(name, "event")) continue;
        char *path = g_strdup_printf(ZYL_SYSFS_ROOT "/sys/class/input/%s/device/capabilities/ff", name);
        char *bits = NULL;
        if (g_file_get_contents(path, &bits, NULL, NULL) && bitmap_has(g_strstrip(bits), FF_RUMBLE)) {
            snprintf(dev, len, ZYL_DEV_ROOT "/dev/input/%s", name);
            found = true;
        }
        g_free(bits);
        g_free(path);
    }
    if (dir) g_dir_close(dir);
    return found;
}

static bool vib_present(void) {
    char dev[256];
    return find_vibrator(dev, sizeof(dev));
}

typedef struct { char dev[256]; int pattern[MAX_PULSES]; int count; } Pattern;

static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0) {}
}

static void *play_pattern(void *arg) {
    Pattern *p = arg;
    int fd = open(p->dev, O_RDWR | O_CLOEXEC);
    struct ff_effect effect = { .type = FF_RUMBLE, .id = -1 };
    effect.u.rumble.strong_magnitude = 0xc000;
    for (int i = 0; fd >= 0 && i < p->count; i += 2) {
        effect.replay.length = (uint16_t)p->pattern[i];
        if (ioctl(fd, EVIOCSFF, &effect) < 0) break;      /* upload, or update with the same id */
        struct input_event play = { .type = EV_FF, .code = (uint16_t)effect.id, .value = 1 };
        if (write(fd, &play, sizeof(play)) != (ssize_t)sizeof(play)) break;
        sleep_ms(p->pattern[i] + (i + 1 < p->count ? p->pattern[i + 1] : 0));
    }
    if (fd >= 0) {
        if (effect.id >= 0) ioctl(fd, EVIOCRMFF, effect.id);
        close(fd);
    }
    free(p);
    return NULL;
}

static bool vib_vibrate(const int *pattern_ms, int count) {
    Pattern *p = calloc(1, sizeof(*p));
    if (!p || count <= 0 || !find_vibrator(p->dev, sizeof(p->dev))) {
        free(p);
        return false;
    }
    int total = 0;
    for (int i = 0; i < count && i < MAX_PULSES && total < MAX_PATTERN_MS; i++) {
        p->pattern[i] = CLAMP(pattern_ms[i], 0, MAX_PATTERN_MS - total);
        total += p->pattern[i];
        p->count = i + 1;
    }
    pthread_t t;
    if (pthread_create(&t, NULL, play_pattern, p) != 0) {
        free(p);
        return false;
    }
    pthread_detach(t);
    return true;
}

static const ZylVibratorHal vibrator_hal = {
    .present = vib_present,
    .vibrate = vib_vibrate,
};

const ZylVibratorHal *zyl_hal_vibrator(void) { return &vibrator_hal; }
