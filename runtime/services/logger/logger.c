/* ----------------------------------------------------------
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 구조화 로깅 + 크래시 리포팅 서비스 구현
 * 수행범위: JSON Lines 로그 작성, 로그 로테이션, 시그널 핸들러, D-Bus API
 * 의존방향: logger.h, gio/gio.h
 * SOLID: SRP — 로깅 및 크래시 리포팅 로직만 담당
 * ---------------------------------------------------------- */

#include "logger.h"

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <inttypes.h>
#include <gio/gio.h>
#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* -- D-Bus Introspection XML -------------------------------- */

static const char *introspection_xml =
    "<node>"
    "  <interface name='" ZYL_LOGGER_DBUS_IFACE "'>"
    "    <method name='GetLogs'>"
    "      <arg direction='in'  type='x' name='since_timestamp'/>"
    "      <arg direction='out' type='s' name='logs_json'/>"
    "    </method>"
    "    <method name='GetCrashReports'>"
    "      <arg direction='out' type='s' name='reports_json'/>"
    "    </method>"
    "  </interface>"
    "</node>";

/* -- Internal Service Structure ----------------------------- */

struct _ZylLoggerService {
    GDBusConnection   *connection;
    GDBusNodeInfo     *introspection_data;
    guint              bus_owner_id;
    guint              registration_id;
    GMutex             log_mutex;
    FILE              *log_fp;
};

/* Singleton for signal handler access */
static ZylLoggerService *g_logger_instance = NULL;

/* -- Helpers ------------------------------------------------ */

static const char *level_string(ZylLogLevel level)
{
    switch (level) {
    case ZYL_LOG_DEBUG:   return "DEBUG";
    case ZYL_LOG_INFO:    return "INFO";
    case ZYL_LOG_WARNING: return "WARNING";
    case ZYL_LOG_ERROR:   return "ERROR";
    case ZYL_LOG_CRASH:   return "CRASH";
    default:              return "UNKNOWN";
    }
}

static int64_t now_unix_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* -- Log Rotation ------------------------------------------- */

static void rotate_logs(ZylLoggerService *svc)
{
    if (!svc->log_fp) {
        return;
    }

    fclose(svc->log_fp);
    svc->log_fp = NULL;

    /* Shift existing rotated files: .4 -> delete, .3 -> .4, ... */
    for (int i = ZYL_LOG_MAX_ROTATED; i >= 1; i--) {
        char old_path[256];
        char new_path[256];

        snprintf(old_path, sizeof(old_path), "%s.%d", ZYL_LOG_FILE, i);

        if (i == ZYL_LOG_MAX_ROTATED) {
            unlink(old_path);
        } else {
            snprintf(new_path, sizeof(new_path), "%s.%d", ZYL_LOG_FILE, i + 1);
            rename(old_path, new_path);
        }
    }

    /* Current -> .1 */
    char rotated[256];
    snprintf(rotated, sizeof(rotated), "%s.1", ZYL_LOG_FILE);
    rename(ZYL_LOG_FILE, rotated);

    /* Reopen fresh log file */
    svc->log_fp = fopen(ZYL_LOG_FILE, "a");
    if (svc->log_fp) {
        int fd = fileno(svc->log_fp);
        if (fd >= 0) {
            fchmod(fd, 0600);
        }
    }
}

static bool needs_rotation(ZylLoggerService *svc)
{
    if (!svc->log_fp) {
        return false;
    }
    long pos = ftell(svc->log_fp);
    return (pos >= ZYL_LOG_MAX_SIZE);
}

/* -- Ensure log directory and file exist -------------------- */

static bool ensure_log_file(ZylLoggerService *svc)
{
    if (svc->log_fp) {
        return true;
    }

    /* Create log directory via mkdir -p equivalent */
    struct stat st;
    if (stat(ZYL_LOG_DIR, &st) != 0) {
        if (mkdir(ZYL_LOG_DIR, 0750) != 0 && errno != EEXIST) {
            g_warning("Logger: cannot create %s: %s",
                      ZYL_LOG_DIR, strerror(errno));
            return false;
        }
    }

    svc->log_fp = fopen(ZYL_LOG_FILE, "a");
    if (!svc->log_fp) {
        g_warning("Logger: cannot open %s: %s",
                  ZYL_LOG_FILE, strerror(errno));
        return false;
    }

    int fd = fileno(svc->log_fp);
    if (fd >= 0) {
        fchmod(fd, 0600);
    }

    return true;
}

/* -- JSON string escaping ----------------------------------- */

static void json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 6 < dst_size; i++) {
        switch (src[i]) {
        case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
        case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
        case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
        case '\r': dst[j++] = '\\'; dst[j++] = 'r';  break;
        case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
        default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

/* -- Write a structured log entry --------------------------- */

void zyl_logger_write(ZylLoggerService *svc,
                      ZylLogLevel       level,
                      const char       *source,
                      const char       *message)
{
    if (!svc || !source || !message) {
        return;
    }

    g_mutex_lock(&svc->log_mutex);

    if (!ensure_log_file(svc)) {
        g_mutex_unlock(&svc->log_mutex);
        return;
    }

    if (needs_rotation(svc)) {
        rotate_logs(svc);
        if (!ensure_log_file(svc)) {
            g_mutex_unlock(&svc->log_mutex);
            return;
        }
    }

    char esc_source[256];
    char esc_message[2048];
    json_escape(source, esc_source, sizeof(esc_source));
    json_escape(message, esc_message, sizeof(esc_message));

    int64_t ts = now_unix_ms();
    fprintf(svc->log_fp,
            "{\"ts\":%" PRId64 ",\"level\":\"%s\",\"source\":\"%s\","
            "\"msg\":\"%s\"}\n",
            ts, level_string(level), esc_source, esc_message);
    fflush(svc->log_fp);

    g_mutex_unlock(&svc->log_mutex);
}

/* -- Crash Signal Handler ----------------------------------- */

static void crash_signal_handler(int sig)
{
    /* Async-signal-safe: write directly to fd, minimal operations */
    ZylLoggerService *svc = g_logger_instance;

    /* Capture backtrace */
    void *bt_buf[64];
    int bt_count = backtrace(bt_buf, 64);

    /* Write crash info to log via fd (signal-safe) */
    if (svc && svc->log_fp) {
        int fd = fileno(svc->log_fp);
        if (fd >= 0) {
            char buf[512];
            int len = snprintf(buf, sizeof(buf),
                "{\"ts\":%lld,\"level\":\"CRASH\",\"source\":\"signal\","
                "\"msg\":\"Signal %d received, pid=%d\"}\n",
                (long long)time(NULL) * 1000LL, sig, (int)getpid());
            if (len > 0 && (size_t)len < sizeof(buf)) {
                (void)write(fd, buf, (size_t)len);
            }

            /* Write backtrace symbols to stderr and log */
            backtrace_symbols_fd(bt_buf, bt_count, STDERR_FILENO);
            backtrace_symbols_fd(bt_buf, bt_count, fd);
        }
    }

    /* Re-raise to get core dump */
    signal(sig, SIG_DFL);
    raise(sig);
}

void zyl_logger_install_crash_handler(ZylLoggerService *svc)
{
    if (!svc) {
        return;
    }
    g_logger_instance = svc;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
}

/* -- D-Bus: read logs since timestamp ----------------------- */

static char *read_logs_since(int64_t since_ts)
{
    FILE *fp = fopen(ZYL_LOG_FILE, "r");
    if (!fp) {
        return g_strdup("[]");
    }

    GString *result = g_string_new("[");
    char line[4096];
    bool first = true;

    while (fgets(line, sizeof(line), fp)) {
        /* Quick parse: extract ts field */
        const char *ts_start = strstr(line, "\"ts\":");
        if (!ts_start) {
            continue;
        }
        int64_t entry_ts = strtoll(ts_start + 5, NULL, 10);
        if (entry_ts < since_ts) {
            continue;
        }

        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (!first) {
            g_string_append_c(result, ',');
        }
        g_string_append(result, line);
        first = false;
    }

    fclose(fp);
    g_string_append_c(result, ']');
    return g_string_free(result, FALSE);
}

static char *read_crash_reports(void)
{
    FILE *fp = fopen(ZYL_LOG_FILE, "r");
    if (!fp) {
        return g_strdup("[]");
    }

    GString *result = g_string_new("[");
    char line[4096];
    bool first = true;

    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, "\"CRASH\"")) {
            continue;
        }
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (!first) {
            g_string_append_c(result, ',');
        }
        g_string_append(result, line);
        first = false;
    }

    fclose(fp);
    g_string_append_c(result, ']');
    return g_string_free(result, FALSE);
}

/* -- D-Bus method handler ----------------------------------- */

static void handle_method_call(GDBusConnection       *connection,
                               const gchar           *sender,
                               const gchar           *object_path,
                               const gchar           *interface_name,
                               const gchar           *method_name,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer               user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)user_data;

    if (g_strcmp0(method_name, "GetLogs") == 0) {
        gint64 since_ts = 0;
        g_variant_get(parameters, "(x)", &since_ts);

        char *logs = read_logs_since(since_ts);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(s)", logs));
        g_free(logs);

    } else if (g_strcmp0(method_name, "GetCrashReports") == 0) {
        char *reports = read_crash_reports();
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(s)", reports));
        g_free(reports);

    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.zylos.Error.UnknownMethod", "Unknown method");
    }
}

/* -- D-Bus vtable ------------------------------------------- */

static const GDBusInterfaceVTable vtable = {
    .method_call  = handle_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

/* -- Bus acquired callback ---------------------------------- */

static void on_bus_acquired(GDBusConnection *connection,
                            const gchar     *name,
                            gpointer         user_data)
{
    (void)name;
    ZylLoggerService *svc = user_data;
    svc->connection = connection;

    GError *error = NULL;
    svc->registration_id = g_dbus_connection_register_object(
        connection,
        ZYL_LOGGER_DBUS_PATH,
        svc->introspection_data->interfaces[0],
        &vtable,
        svc,
        NULL,
        &error);

    if (error) {
        g_critical("Logger: register_object failed: %s", error->message);
        g_error_free(error);
    }
}

static void on_name_acquired(GDBusConnection *connection,
                             const gchar     *name,
                             gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_message("Logger: D-Bus name acquired: %s", name);
}

static void on_name_lost(GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_warning("Logger: D-Bus name lost: %s", name);
}

/* -- Public API --------------------------------------------- */

ZylLoggerService *zyl_logger_service_create(void)
{
    ZylLoggerService *svc = calloc(1, sizeof(*svc));
    if (!svc) {
        g_critical("Logger: failed to allocate service");
        return NULL;
    }

    g_mutex_init(&svc->log_mutex);
    svc->log_fp = NULL;

    svc->introspection_data = g_dbus_node_info_new_for_xml(
        introspection_xml, NULL);
    if (!svc->introspection_data) {
        g_critical("Logger: failed to parse introspection XML");
        g_mutex_clear(&svc->log_mutex);
        free(svc);
        return NULL;
    }

    svc->bus_owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        ZYL_LOGGER_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        svc,
        NULL);

    return svc;
}

void zyl_logger_service_destroy(ZylLoggerService *svc)
{
    if (!svc) {
        return;
    }

    if (svc->registration_id > 0 && svc->connection) {
        g_dbus_connection_unregister_object(svc->connection,
                                            svc->registration_id);
    }

    if (svc->bus_owner_id > 0) {
        g_bus_unown_name(svc->bus_owner_id);
    }

    if (svc->introspection_data) {
        g_dbus_node_info_unref(svc->introspection_data);
    }

    g_mutex_lock(&svc->log_mutex);
    if (svc->log_fp) {
        fclose(svc->log_fp);
        svc->log_fp = NULL;
    }
    g_mutex_unlock(&svc->log_mutex);
    g_mutex_clear(&svc->log_mutex);

    if (g_logger_instance == svc) {
        g_logger_instance = NULL;
    }

    free(svc);
}

void zyl_crash_report_free(ZylCrashReport *report)
{
    if (!report) {
        return;
    }
    free(report->process_name);
    free(report->backtrace);
    free(report->core_path);
}

/* -- main() ------------------------------------------------- */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    g_message("Zyl OS Logger Service starting...");

    ZylLoggerService *svc = zyl_logger_service_create();
    if (!svc) {
        g_critical("Failed to create logger service");
        return 1;
    }

    zyl_logger_install_crash_handler(svc);
    zyl_logger_write(svc, ZYL_LOG_INFO, "logger", "Logger service started");

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_logger_service_destroy(svc);
    g_message("Zyl OS Logger Service stopped");
    return 0;
}
