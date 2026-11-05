/* SPDX-License-Identifier: GPL-2.0-only */
/* Standalone bring-up PID 1: RAM filesystems and diagnostic logging only. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
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

static unsigned int parse_reboot_argument(const char *argument)
{
    static const char prefix[] = "wayne.reboot_after=";
    const char *value;
    char *end;
    unsigned long seconds;
    if (strncmp(argument, prefix, sizeof(prefix) - 1))
        return 0;
    value = argument + sizeof(prefix) - 1;
    if (*value < '0' || *value > '9')
        return 0;
    errno = 0;
    seconds = strtoul(value, &end, 10);
    if (errno || *end || seconds < 30 || seconds > 300)
        return 0;
    return (unsigned int)seconds;
}

static unsigned int read_reboot_timer(void)
{
    FILE *file = fopen("/proc/cmdline", "r");
    char command[4096], *token, *save;
    unsigned int seconds = 0;
    if (!file)
        return 0;
    if (!fgets(command, sizeof(command), file)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    for (token = strtok_r(command, " \t\n", &save); token;
         token = strtok_r(NULL, " \t\n", &save)) {
        if (!strncmp(token, "wayne.reboot_after=", sizeof("wayne.reboot_after=") - 1))
            seconds = parse_reboot_argument(token);
    }
    return seconds;
}

static int command_has(const char *needle)
{
    FILE *file = fopen("/proc/cmdline", "r");
    char command[4096], *token, *save;
    int found = 0;
    if (!file)
        return 0;
    if (fgets(command, sizeof(command), file)) {
        for (token = strtok_r(command, " \t\n", &save); token;
             token = strtok_r(NULL, " \t\n", &save)) {
            if (!strcmp(token, needle)) {
                found = 1;
                break;
            }
        }
    }
    fclose(file);
    return found;
}

static void enable_usb_gadget(void)
{
    int fd, saved_errno;
    ssize_t written;

    list_directory("/sys/class/udc");
    log_message("USB_DEFERRED_START");
    fd = open("/sys/module/g_serial/parameters/enable", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        log_message("USB_DEFERRED_RESULT open failed: %s", strerror(errno));
        return;
    }
    written = write(fd, "1\n", 2);
    saved_errno = errno;
    close(fd);
    if (written == 2)
        log_message("USB_DEFERRED_RESULT enabled");
    else if (written < 0)
        log_message("USB_DEFERRED_RESULT write failed: %s", strerror(saved_errno));
    else
        log_message("USB_DEFERRED_RESULT short write: %zd", written);
}

static int reboot_to_bootloader(void)
{
    return (int)syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                       LINUX_REBOOT_CMD_RESTART2, "bootloader");
}

static unsigned int read_le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int probe_partition(int index)
{
    char path[64], label[17], uuid[37];
    unsigned char buffer[2048];
    ssize_t got;
    int fd, i;

    snprintf(path, sizeof(path), "/dev/mmcblk1p%d", index);
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return 0;
    got = pread(fd, buffer, sizeof(buffer), 0);
    close(fd);
    if (got < 1082) {
        log_message("fsprobe p%d short read %zd", index, got);
        return 0;
    }
    if (buffer[1080] == 0x53 && buffer[1081] == 0xef) {
        memcpy(label, buffer + 1144, 16);
        label[16] = '\0';
        for (i = 0; i < 16; i++) {
            if (label[i] < 0x20 || label[i] > 0x7e)
                label[i] = '.';
        }
        for (i = 0; i < 16; i++)
            snprintf(uuid + i * 2, 3, "%02x", buffer[1128 + i]);
        log_message("fsprobe p%d ext4 label=%s uuid=%s", index, label, uuid);
        return 1;
    }
    if (read_le32(buffer + 1024) == 0xf2f52010) {
        log_message("fsprobe p%d f2fs", index);
        return 1;
    }
    if (read_le32(buffer + 1024) == 0xe0f5e1e2) {
        log_message("fsprobe p%d erofs", index);
        return 1;
    }
    if (read_le32(buffer) == 0x73717368) {
        log_message("fsprobe p%d squashfs", index);
        return 1;
    }
    log_message("fsprobe p%d unknown magic=%02x%02x%02x%02x", index,
                buffer[0], buffer[1], buffer[2], buffer[3]);
    return 0;
}

static void probe_filesystems(void)
{
    int i, ready = 0;
    for (i = 0; i < 75; i++) {
        if (access("/dev/mmcblk1p61", F_OK) == 0) {
            ready = 1;
            break;
        }
        usleep(200000);
    }
    log_message("fsprobe start ready=%d", ready);
    for (i = 1; i <= 64; i++)
        probe_partition(i);
    log_message("fsprobe done");
}

static void mount_system(void)
{
    int i, ready = 0;

    for (i = 0; i < 75; i++) {
        if (access("/dev/mmcblk1p61", F_OK) == 0) {
            ready = 1;
            break;
        }
        usleep(200000);
    }
    if (!ready) {
        log_message("sysmount p61 not ready");
        return;
    }
    directory("/mnt");
    directory("/mnt/system");
    if (mount("/dev/mmcblk1p61", "/mnt/system", "ext4", MS_RDONLY, "noload")) {
        log_message("sysmount p61: %s", strerror(errno));
        return;
    }
    log_message("sysmount p61 ext4 ro noload mounted");
    if (access("/mnt/system/build.prop", R_OK) == 0)
        dump_text("/mnt/system/build.prop");
    else if (access("/mnt/system/system/build.prop", R_OK) == 0)
        dump_text("/mnt/system/system/build.prop");
    else
        log_message("sysmount build.prop not found");
    if (umount2("/mnt/system", MNT_DETACH))
        log_message("sysmount umount: %s", strerror(errno));
    else
        log_message("sysmount p61 unmounted");
}

int main(int argc, char **argv)
{
    struct utsname name = {0};
    unsigned long heartbeat = 0;
    unsigned long elapsed = 0;
    unsigned int reboot_after;
    int bootloader_target;
    int test_pid1_exit;
    int deferred_usb;
    int console;
    if (argc == 2 && !strcmp(argv[1], "--self-test")) {
        if (parse_reboot_argument("wayne.reboot_after=30") != 30 ||
            parse_reboot_argument("wayne.reboot_after=60") != 60 ||
            parse_reboot_argument("wayne.reboot_after=300") != 300 ||
            parse_reboot_argument("wayne.reboot_after=29") ||
            parse_reboot_argument("wayne.reboot_after=301") ||
            parse_reboot_argument("wayne.reboot_after=-60") ||
            parse_reboot_argument("wayne.reboot_after=60junk") ||
            parse_reboot_argument("other=60"))
            return 1;
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
    reboot_after = read_reboot_timer();
    bootloader_target = command_has("wayne.reboot_mode=bootloader");
    test_pid1_exit = command_has("wayne.test_pid1_exit=1");
    deferred_usb = command_has("wayne.usb_deferred=1");
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
    if (command_has("wayne.fs_probe=1"))
        probe_filesystems();
    if (command_has("wayne.mount_system=1"))
        mount_system();
    list_directory("/sys/fs/pstore");
    log_message("BRINGUP_READY; no disk partitions mounted");
    if (reboot_after)
        log_message("AUTO_REBOOT_AFTER=%u seconds", reboot_after);
    for (;;) {
        unsigned int step = reboot_after ? 1 : 30;
        struct timespec delay = {step, 0};
        while (nanosleep(&delay, &delay) && errno == EINTR) {}
        elapsed += step;
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
        if (!(elapsed % 30))
            log_message("heartbeat=%lu pid=%ld kernel=%s", ++heartbeat,
                        (long)getpid(), name.release);
        if (test_pid1_exit && elapsed >= 5) {
            log_message("WAYNE_TEST_PID1_EXIT");
            _exit(1);
        }
        if (deferred_usb && elapsed >= 10) {
            deferred_usb = 0;
            enable_usb_gadget();
        }
        if (reboot_after && elapsed >= reboot_after) {
            log_message("REBOOTING_AFTER_TEST");
            if (bootloader_target) {
                log_message("REBOOT_TARGET=bootloader");
                if (reboot_to_bootloader()) {
                    int saved_errno = errno;

                    log_message("reboot bootloader failed: %s (errno=%d)",
                                strerror(saved_errno), saved_errno);
                }
            } else if (reboot(RB_AUTOBOOT)) {
                log_message("reboot failed: %s", strerror(errno));
            }
            reboot_after = 0;
        }
    }
}
