/* SPDX-License-Identifier: GPL-2.0-only */
/* Standalone bring-up PID 1: RAM filesystems and diagnostic logging only. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int kmsg = -1;

static void log_message(const char *format, ...)
{
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (kmsg >= 0)
        dprintf(kmsg, "<6>wayne-init: %s\n", message);
    else
        dprintf(STDOUT_FILENO, "wayne-init: %s\n", message);
}

static void directory(const char *path)
{
    if (mkdir(path, 0755) && errno != EEXIST)
        log_message("mkdir %s: %s", path, strerror(errno));
}

static void mount_ram_filesystem(const char *type, const char *target)
{
    directory(target);
    if (mount(type, target, type, MS_NOSUID | MS_NODEV, NULL) && errno != EBUSY)
        log_message("mount %s: %s", target, strerror(errno));
}

static void dump_text(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[512];
    unsigned int total = 0;
    if (!file) {
        log_message("read %s: %s", path, strerror(errno));
        return;
    }
    log_message("BEGIN %s", path);
    while (total < 32768 && fgets(line, sizeof(line), file)) {
        total += strlen(line);
        line[strcspn(line, "\n")] = '\0';
        log_message("%s", line);
    }
    fclose(file);
    log_message("END %s", path);
}

static void list_directory(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    if (!dir) {
        log_message("list %s: %s", path, strerror(errno));
        return;
    }
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] != '.')
            log_message("%s/%s", path, entry->d_name);
    }
    closedir(dir);
}

int main(int argc, char **argv)
{
    struct utsname name;
    unsigned long heartbeat = 0;
    int console;
    if (argc == 2 && !strcmp(argv[1], "--self-test")) {
        puts("WAYNE_INIT_SELF_TEST_OK");
        return 0;
    }
    if (getpid() != 1) {
        fputs("This init must run as PID 1, or with --self-test.\n", stderr);
        return 1;
    }

    directory("/dev");
    /* Device nodes must work here; devtmpfs intentionally has no MS_NODEV. */
    if (mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755") && errno != EBUSY)
        mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
    console = open("/dev/console", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (console >= 0) {
        dup2(console, STDIN_FILENO);
        dup2(console, STDOUT_FILENO);
        dup2(console, STDERR_FILENO);
        if (console > STDERR_FILENO)
            close(console);
    }
    kmsg = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    log_message("WAYNE_INIT_STARTED pid=%ld", (long)getpid());
    if (!uname(&name))
        log_message("kernel=%s machine=%s", name.release, name.machine);
    mount_ram_filesystem("proc", "/proc");
    mount_ram_filesystem("sysfs", "/sys");
    directory("/sys/fs/pstore");
    mount_ram_filesystem("pstore", "/sys/fs/pstore");
    directory("/run");
    mount_ram_filesystem("tmpfs", "/run");
    dump_text("/proc/cmdline");
    dump_text("/proc/meminfo");
    dump_text("/proc/iomem");
    list_directory("/sys/class/mmc_host");
    list_directory("/sys/block");
    list_directory("/sys/fs/pstore");
    log_message("BRINGUP_READY; no disk partitions mounted; waiting for external reset");
    for (;;) {
        struct timespec delay = {30, 0};
        while (nanosleep(&delay, &delay) && errno == EINTR) {}
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
        log_message("heartbeat=%lu", ++heartbeat);
    }
}
