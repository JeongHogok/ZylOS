/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 오디오 서비스 인터페이스 — PipeWire 기반 볼륨/재생/녹음
 * 수행범위: 볼륨 제어, 오디오 라우팅, 알림음, 진동 제어
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 오디오 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_AUDIO_H
#define ZYL_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ZYL_AUDIO_STREAM_MEDIA    = 0,
    ZYL_AUDIO_STREAM_NOTIF    = 1,
    ZYL_AUDIO_STREAM_ALARM    = 2,
    ZYL_AUDIO_STREAM_RINGTONE = 3,
    ZYL_AUDIO_STREAM_SYSTEM   = 4,
} ZylAudioStream;

typedef struct ZylAudioService ZylAudioService;

ZylAudioService *zyl_audio_create(void);
void              zyl_audio_destroy(ZylAudioService *svc);

int  zyl_audio_get_volume(ZylAudioService *svc, ZylAudioStream stream);
int  zyl_audio_set_volume(ZylAudioService *svc, ZylAudioStream stream,
                           int percent);

bool zyl_audio_get_mute(ZylAudioService *svc);
void zyl_audio_set_mute(ZylAudioService *svc, bool muted);

int  zyl_audio_play_tone(ZylAudioService *svc, int freq_hz,
                          int duration_ms);

#define ZYL_AUDIO_DBUS_NAME "org.zylos.AudioService"
#define ZYL_AUDIO_DBUS_PATH "/org/zylos/AudioService"

#endif /* ZYL_AUDIO_H */
