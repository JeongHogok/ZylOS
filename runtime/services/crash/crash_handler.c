/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: Crash handler — SIGSEGV/SIGABRT/SIGBUS 시그널 포착 + coredump 수집
 * 수행범위: 시그널 설치, 컨텍스트 덤프 (레지스터, 백트레이스),
 *           /data/crash/ 에 리포트 저장, 선택적 coredump 생성
 * 의존방향: signal.h, execinfo.h (glibc), stdio
 * SOLID: SRP — crash 포착과 리포트만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/resource.h>

/* execinfo.h may not be available on all platforms (e.g. musl) */
#ifdef __GLIBC__
#include <execinfo.h>
#define HAS_BACKTRACE 1
#else
#define HAS_BACKTRACE 0
#endif

#define CRASH_DIR    "/data/crash"
#define MAX_FRAMES   64

static const char *signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS:  return "SIGBUS";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        default:      return "UNKNOWN";
    }
}

static void crash_handler(int sig, siginfo_t *info, void *context) {
    (void)context;

    /* Ensure crash directory exists */
    mkdir(CRASH_DIR, 0700);

    /* Generate crash report filename */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char filename[256];
    snprintf(filename, sizeof(filename),
             "%s/crash_%04d%02d%02d_%02d%02d%02d_%d.txt",
             CRASH_DIR,
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec,
             getpid());

    FILE *f = fopen(filename, "w");
    if (f) {
        fprintf(f, "═══════════════════════════════════\n");
        fprintf(f, "  ZylOS Crash Report\n");
        fprintf(f, "═══════════════════════════════════\n\n");
        fprintf(f, "Signal:    %s (%d)\n", signal_name(sig), sig);
        fprintf(f, "PID:       %d\n", getpid());
        fprintf(f, "Timestamp: %04d-%02d-%02d %02d:%02d:%02d\n",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec);

        if (info) {
            fprintf(f, "Fault addr: %p\n", info->si_addr);
            fprintf(f, "si_code:    %d\n", info->si_code);
        }

        /* Backtrace */
#if HAS_BACKTRACE
        fprintf(f, "\nBacktrace:\n");
        void *frames[MAX_FRAMES];
        int nframes = backtrace(frames, MAX_FRAMES);
        char **symbols = backtrace_symbols(frames, nframes);
        if (symbols) {
            for (int i = 0; i < nframes; i++) {
                fprintf(f, "  #%d %s\n", i, symbols[i]);
            }
            free(symbols);
        }
#else
        fprintf(f, "\nBacktrace: not available (non-glibc)\n");
#endif

        /* /proc/self/maps for address mapping */
        fprintf(f, "\nMemory maps:\n");
        FILE *maps = fopen("/proc/self/maps", "r");
        if (maps) {
            char line[512];
            while (fgets(line, sizeof(line), maps)) {
                fputs(line, f);
            }
            fclose(maps);
        }

        fclose(f);
    }

    /* Write to stderr as well */
    fprintf(stderr, "\n[CRASH] %s (signal %d) at %p\n",
            signal_name(sig), sig, info ? info->si_addr : NULL);
    fprintf(stderr, "[CRASH] Report saved to %s\n", filename);

    /* Enable coredump: set RLIMIT_CORE to unlimited for this process */
    struct rlimit rl;
    rl.rlim_cur = RLIM_INFINITY;
    rl.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CORE, &rl);

    /* Re-raise with default handler to generate coredump */
    signal(sig, SIG_DFL);
    raise(sig);
}

void zyl_crash_handler_install(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND; /* One-shot: reset after first crash */
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
}

/* ─── Standalone daemon mode ─── */
#ifndef ZYL_CRASH_LIB_ONLY
#include <gio/gio.h>

static const char *crash_introspection_xml =
    "<node>"
    "  <interface name='org.zylos.CrashHandler'>"
    "    <method name='GetReports'>"
    "      <arg type='as' name='reports' direction='out'/>"
    "    </method>"
    "    <method name='ClearReports'/>"
    "  </interface>"
    "</node>";

static void handle_crash_method(GDBusConnection *conn, const gchar *sender,
                                 const gchar *path, const gchar *iface,
                                 const gchar *method, GVariant *params,
                                 GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)params; (void)data;

    if (g_strcmp0(method, "GetReports") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));

        GDir *dir = g_dir_open(CRASH_DIR, 0, NULL);
        if (dir) {
            const gchar *name;
            while ((name = g_dir_read_name(dir)) != NULL) {
                if (g_str_has_prefix(name, "crash_")) {
                    char full[512];
                    snprintf(full, sizeof(full), "%s/%s", CRASH_DIR, name);
                    g_variant_builder_add(&builder, "s", full);
                }
            }
            g_dir_close(dir);
        }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(as)", &builder));
    } else if (g_strcmp0(method, "ClearReports") == 0) {
        GDir *dir = g_dir_open(CRASH_DIR, 0, NULL);
        if (dir) {
            const gchar *name;
            while ((name = g_dir_read_name(dir)) != NULL) {
                char full[512];
                snprintf(full, sizeof(full), "%s/%s", CRASH_DIR, name);
                unlink(full);
            }
            g_dir_close(dir);
        }
        g_dbus_method_invocation_return_value(inv, NULL);
    }
}

static const GDBusInterfaceVTable crash_vtable = { .method_call = handle_crash_method };

static void on_crash_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(crash_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, "/org/zylos/CrashHandler",
            info->interfaces[0], &crash_vtable, NULL, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[Crash] D-Bus service registered");
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    zyl_crash_handler_install();
    mkdir(CRASH_DIR, 0700);

    g_bus_own_name(G_BUS_TYPE_SESSION,
        "org.zylos.CrashHandler", G_BUS_NAME_OWNER_FLAGS_NONE,
        on_crash_bus, NULL, NULL, NULL, NULL);

    g_message("[Crash] Handler installed, monitoring started");
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_CRASH_LIB_ONLY */
