/* ----------------------------------------------------------
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 구조화 로깅 및 크래시 리포팅 서비스 인터페이스
 * 수행범위: JSON 로그 쓰기, 로그 로테이션, 크래시 핸들러, D-Bus 인터페이스
 * 의존방향: stdbool.h, stdint.h, stddef.h
 * SOLID: ISP — 로깅/크래시리포팅 인터페이스만 노출
 * ---------------------------------------------------------- */

#ifndef ZYL_LOGGER_H
#define ZYL_LOGGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- D-Bus Constants ---------------------------------------- */

#define ZYL_LOGGER_DBUS_NAME  "org.zylos.Logger"
#define ZYL_LOGGER_DBUS_PATH  "/org/zylos/Logger"
#define ZYL_LOGGER_DBUS_IFACE "org.zylos.Logger"

/* -- Configuration ------------------------------------------ */

#define ZYL_LOG_DIR          "/var/log/zyl-os"
#define ZYL_LOG_FILE         ZYL_LOG_DIR "/system.jsonl"
#define ZYL_LOG_FILTER_FILE  ZYL_LOG_DIR "/logger-filter.conf"
#define ZYL_LOG_MAX_SIZE     (10 * 1024 * 1024)   /* 10 MB */
#define ZYL_LOG_MAX_ROTATED  5

/* -- Log levels --------------------------------------------- */

typedef enum {
    ZYL_LOG_DEBUG   = 0,
    ZYL_LOG_INFO    = 1,
    ZYL_LOG_WARNING = 2,
    ZYL_LOG_ERROR   = 3,
    ZYL_LOG_CRASH   = 4
} ZylLogLevel;

/* -- Crash report ------------------------------------------- */

typedef struct {
    int64_t  timestamp;
    int      signal_number;
    char    *process_name;
    int      pid;
    char    *backtrace;
    char    *core_path;
} ZylCrashReport;

/* Opaque service handle */
typedef struct _ZylLoggerService ZylLoggerService;

/* -- Service Lifecycle -------------------------------------- */

ZylLoggerService *zyl_logger_service_create(void);
void              zyl_logger_service_destroy(ZylLoggerService *svc);

/* -- Logging ------------------------------------------------ */

void zyl_logger_write(ZylLoggerService *svc,
                      ZylLogLevel       level,
                      const char       *source,
                      const char       *message);

/* -- Crash Handling ----------------------------------------- */

void zyl_logger_install_crash_handler(ZylLoggerService *svc);

/* -- Cleanup ------------------------------------------------ */

void zyl_crash_report_free(ZylCrashReport *report);

#ifdef __cplusplus
}
#endif

#endif /* ZYL_LOGGER_H */
