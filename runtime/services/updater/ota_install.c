/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Use Case
 *
 * 역할: 검증된 OTA 패키지의 슬롯 설치 구현
 * 수행범위: ota_install.h 참고
 * 의존방향: ota_install.h, hal.h (Boot HAL), posix_spawn (tar)
 * SOLID: SRP — 슬롯 기록만
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include "ota_install.h"
#include "hal.h"

#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

/* One member of the package straight onto a partition or file. tar
   recognizes the gzip compression itself */
static bool extract_member(const char *pkg, const char *member, const char *dest, int flags) {
    int fd = open(dest, O_WRONLY | flags, 0644);
    if (fd < 0) return false;
    const char *argv[] = { "/usr/bin/tar", "-xOf", pkg, member, NULL };
    char *envp[] = { "PATH=/usr/bin:/bin", NULL };
    posix_spawn_file_actions_t acts;
    posix_spawn_file_actions_init(&acts);
    posix_spawn_file_actions_adddup2(&acts, fd, STDOUT_FILENO);
    pid_t pid;
    int status = -1;
    bool ok = posix_spawn(&pid, argv[0], &acts, NULL, (char *const *)argv, envp) == 0 &&
              waitpid(pid, &status, 0) == pid &&
              WIFEXITED(status) && WEXITSTATUS(status) == 0 && fsync(fd) == 0;
    posix_spawn_file_actions_destroy(&acts);
    close(fd);
    return ok;
}

static void report(ZylOtaStep step, void *data, int pct, const char *msg) {
    if (step) step(pct, msg, data);
}

bool zyl_ota_install_slot(const char *pkg_path, char slot,
                          ZylOtaStep step, void *data, const char **why) {
    const ZylBootHal *boot = zyl_hal_boot();
    char tree[64], fit[64], fit_tmp[512], fit_dst[512];
    snprintf(tree, sizeof(tree), "verity-hashtree-%c.img", slot);
    snprintf(fit, sizeof(fit), "fit-image-%c.itb", slot);
    snprintf(fit_tmp, sizeof(fit_tmp), "%s/.%s.new", boot->boot_dir(), fit);
    snprintf(fit_dst, sizeof(fit_dst), "%s/%s", boot->boot_dir(), fit);

    report(step, data, 5, "Writing system image...");
    if (!extract_member(pkg_path, "rootfs.img", boot->slot_rootfs(slot), 0)) {
        *why = "Writing the system image failed";
        return false;
    }
    report(step, data, 50, "Writing verity tree...");
    if (!extract_member(pkg_path, tree, boot->slot_verity(slot), 0)) {
        *why = "Writing the verity tree failed";
        return false;
    }
    /* Renamed into place: U-Boot never loads a half-written FIT */
    report(step, data, 60, "Installing boot image...");
    if (!extract_member(pkg_path, fit, fit_tmp, O_CREAT | O_TRUNC) || rename(fit_tmp, fit_dst) != 0) {
        unlink(fit_tmp);
        *why = "Installing the boot image failed";
        return false;
    }
    sync();
    return true;
}
