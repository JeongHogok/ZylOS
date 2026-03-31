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
#include <fcntl.h>
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

/*
 * Async-signal-safe write helpers.
 * Signal handlers must NOT call stdio (fopen/fprintf/fclose), malloc,
 * localtime, or backtrace_symbols — they can deadlock or corrupt state.
 * We use only raw fd I/O: open/write/close, and backtrace_symbols_fd.
 */
static void write_str(int fd, const char *s) {
    if (!s) return;
    size_t len = 0;
    while (s[len]) len++;
    (void)write(fd, s, len);
}

static void write_int(int fd, int val) {
    char buf[32];
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    int i = 0;
    do { buf[i++] = '0' + (val % 10); val /= 10; } while (val > 0);
    if (neg) buf[i++] = '-';
    /* Reverse */
    for (int a = 0, b = i - 1; a < b; a++, b--) {
        char t = buf[a]; buf[a] = buf[b]; buf[b] = t;
    }
    (void)write(fd, buf, i);
}

static void write_ptr(int fd, const void *p) {
    char buf[20] = "0x";
    unsigned long v = (unsigned long)p;
    int i = 18;
    buf[i--] = '\0';
    do { int d = v & 0xF; buf[i--] = d < 10 ? '0' + d : 'a' + d - 10; v >>= 4; } while (v && i >= 2);
    (void)write(fd, buf + i + 1, 18 - i - 1);
}

static void crash_handler(int sig, siginfo_t *info, void *context) {
    (void)context;

    /* Ensure crash directory exists (async-signal-safe) */
    mkdir(CRASH_DIR, 0700);

    /* Build filename using only pid (avoid localtime — not signal-safe) */
    char filename[256];
    /* Use a fixed format: crash_<pid>_<sig>.txt */
    int pos = 0;
    const char *prefix = CRASH_DIR "/crash_";
    while (*prefix && pos < 200) filename[pos++] = *prefix++;
    /* Write pid digits */
    {
        int pid = getpid();
        char digits[16]; int nd = 0;
        do { digits[nd++] = '0' + (pid % 10); pid /= 10; } while (pid > 0);
        for (int i = nd - 1; i >= 0 && pos < 220; i--) filename[pos++] = digits[i];
    }
    filename[pos++] = '_';
    /* Write signal number */
    {
        int s = sig;
        char digits[8]; int nd = 0;
        do { digits[nd++] = '0' + (s % 10); s /= 10; } while (s > 0);
        for (int i = nd - 1; i >= 0 && pos < 240; i--) filename[pos++] = digits[i];
    }
    const char *suffix = ".txt";
    while (*suffix && pos < 250) filename[pos++] = *suffix++;
    filename[pos] = '\0';

    /* Use open/write/close instead of fopen/fprintf/fclose (signal-safe) */
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        write_str(fd, "=== ZylOS Crash Report ===\n");
        write_str(fd, "Signal: "); write_str(fd, signal_name(sig));
        write_str(fd, " ("); write_int(fd, sig); write_str(fd, ")\n");
        write_str(fd, "PID: "); write_int(fd, getpid()); write_str(fd, "\n");

        if (info) {
            write_str(fd, "Fault addr: "); write_ptr(fd, info->si_addr); write_str(fd, "\n");
            write_str(fd, "si_code: "); write_int(fd, info->si_code); write_str(fd, "\n");
        }

        /* Backtrace — use backtrace_symbols_fd (async-signal-safe variant) */
#if HAS_BACKTRACE
        write_str(fd, "\nBacktrace:\n");
        void *frames[MAX_FRAMES];
        int nframes = backtrace(frames, MAX_FRAMES);
        backtrace_symbols_fd(frames, nframes, fd);
#else
        write_str(fd, "\nBacktrace: not available (non-glibc)\n");
#endif

        /* /proc/self/maps — raw fd copy */
        write_str(fd, "\nMemory maps:\n");
        int maps_fd = open("/proc/self/maps", O_RDONLY);
        if (maps_fd >= 0) {
            char buf[1024];
            ssize_t n;
            while ((n = read(maps_fd, buf, sizeof(buf))) > 0) {
                (void)write(fd, buf, n);
            }
            close(maps_fd);
        }

        close(fd);
    }

    /* Write to stderr (fd 2 — signal-safe) */
    write_str(2, "\n[CRASH] ");
    write_str(2, signal_name(sig));
    write_str(2, " at ");
    write_ptr(2, info ? info->si_addr : NULL);
    write_str(2, "\n[CRASH] Report saved to ");
    write_str(2, filename);
    write_str(2, "\n");

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
                /* Only delete crash reports (same prefix as GetReports) */
                if (!g_str_has_prefix(name, "crash_")) continue;
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
