/* SPDX-License-Identifier: GPL-2.0-only */
/* Standalone bring-up PID 1 with opt-in hardware diagnostics. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <poll.h>
#include <signal.h>
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

#define CACHE_BLOCK "/dev/mmcblk1p60"
#define CACHE_MOUNT "/run/wayne-cache"
#define CACHE_LIMIT (2U * 1024 * 1024)

static pid_t cache_pid;
static int cache_pipe = -1;
static volatile sig_atomic_t cache_stop;

static int cache_path_matches(const char *path, int delimiter, const char *expected)
{
    char *line = NULL;
    size_t capacity = 0;
    size_t length = strlen(expected) + 1;
    int matched = 0;
    ssize_t got;
    FILE *file = fopen(path, "r");

    if (!file)
        return 0;
    while ((got = getdelim(&line, &capacity, delimiter, file)) >= 0) {
        if (got == (ssize_t)length && !memcmp(line, expected, length - 1) &&
            line[length - 1] == delimiter) {
            matched = 1;
            break;
        }
    }
    free(line);
    fclose(file);
    return matched;
}

static int cache_target_valid(void)
{
    const char *uevent = "/sys/class/block/mmcblk1p60/uevent";
    struct stat st;

    return cache_path_matches("/sys/firmware/devicetree/base/compatible", 0, "xiaomi,wayne") &&
           cache_path_matches(uevent, '\n', "PARTNAME=cache") &&
           cache_path_matches(uevent, '\n', "PARTN=60") &&
           cache_path_matches("/sys/class/block/mmcblk1/device/type", '\n', "MMC") &&
           cache_path_matches("/sys/class/block/mmcblk1/removable", '\n', "0") &&
           !lstat(CACHE_BLOCK, &st) && S_ISBLK(st.st_mode);
}

static void cache_stop_handler(int signo)
{
    (void)signo;
    cache_stop = 1;
}

static int cache_write(int file, const char *data, size_t len, size_t *total)
{
    if (len > CACHE_LIMIT - *total) {
        errno = EFBIG;
        return -1;
    }
    while (len) {
        ssize_t count = write(file, data, len);

        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            if (!count)
                errno = EIO;
            return -1;
        }
        data += count;
        len -= count;
        *total += count;
    }
    return 0;
}

static int cache_drain(int input, int file, size_t *total)
{
    char record[16384];
    size_t start = *total;
    int pending = 1, saved_errno;

    for (unsigned int i = 0; i < 128; i++) {
        ssize_t count = read(input, record, sizeof(record));

        if (count < 0 && (errno == EINTR || errno == EPIPE))
            continue;
        if (count < 0 && errno == EAGAIN) {
            pending = 0;
            break;
        }
        if (count <= 0 || cache_write(file, record, count, total)) {
            if (!count)
                errno = EIO;
            pending = -1;
            break;
        }
    }
    saved_errno = errno;
    if (*total != start && fdatasync(file))
        return -1;
    errno = saved_errno;
    return pending;
}

static void cache_logger(int ready)
{
    static const char marker[] = "CACHE_KMSG_READY\n";
    struct sigaction action = {.sa_handler = cache_stop_handler};
    struct pollfd input = {.fd = -1, .events = POLLIN};
    size_t total = 0;
    int dir = -1, file = -1, result, status = 1;
    const char *phase = "mount";

    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL))
        goto failed;
    if (mkdir(CACHE_MOUNT, 0700) ||
        mount(CACHE_BLOCK, CACHE_MOUNT, "ext4", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_NOATIME,
              "errors=remount-ro"))
        goto failed;
    phase = "open";
    dir = open(CACHE_MOUNT, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0)
        goto unmount_failed;
    file = openat(dir, "wayne-bringup-kmsg.log",
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0)
        goto unmount_failed;
    input.fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (input.fd < 0)
        goto unmount_failed;

    phase = "sync";
    do {
        result = cache_drain(input.fd, file, &total);
    } while (result > 0 && !cache_stop);
    if (cache_stop)
        goto unmount;
    if (result < 0 || cache_write(file, marker, sizeof(marker) - 1, &total) ||
        fdatasync(file) || fsync(dir) || write(ready, "1", 1) != 1)
        goto unmount_failed;
    close(ready);
    ready = -1;
    phase = "stream";
    while (!cache_stop) {
        result = poll(&input, 1, 100);
        if (!result || (result < 0 && errno == EINTR))
            continue;
        if (result < 0)
            goto unmount_failed;
        if (input.revents & (POLLHUP | POLLNVAL)) {
            errno = EIO;
            goto unmount_failed;
        }
        if (cache_drain(input.fd, file, &total) < 0)
            goto unmount_failed;
    }
    if (cache_drain(input.fd, file, &total) < 0)
        goto unmount_failed;
    status = 0;
    goto unmount;
unmount_failed:
    log_message("CACHE_KMSG_FAILED phase=%s: %s", phase, strerror(errno));
unmount:
    if (file >= 0)
        close(file);
    if (dir >= 0)
        close(dir);
    if (input.fd >= 0)
        close(input.fd);
    if (umount(CACHE_MOUNT))
        log_message("CACHE_KMSG_UNMOUNT_FAILED: %s", strerror(errno));
    else
        log_message("CACHE_KMSG_UNMOUNTED");
    _exit(status);
failed:
    log_message("CACHE_KMSG_FAILED phase=%s: %s", phase, strerror(errno));
    _exit(1);
}

static void cache_start(void)
{
    int pipefd[2];

    if (!cache_target_valid()) {
        log_message("CACHE_KMSG_REJECTED board or cache device did not match");
        return;
    }
    if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK)) {
        log_message("CACHE_KMSG_PIPE_FAILED: %s", strerror(errno));
        return;
    }
    cache_pid = fork();
    if (!cache_pid) {
        close(pipefd[0]);
        cache_logger(pipefd[1]);
    }
    close(pipefd[1]);
    if (cache_pid < 0) {
        close(pipefd[0]);
        log_message("CACHE_KMSG_FORK_FAILED: %s", strerror(errno));
        return;
    }
    cache_pipe = pipefd[0];
}

static int cache_ready(void)
{
    char byte;
    ssize_t got;

    if (cache_pid <= 0 || cache_pipe < 0)
        return 0;
    do {
        got = read(cache_pipe, &byte, 1);
    } while (got < 0 && errno == EINTR);
    return got == 1 && byte == '1';
}

static void cache_shutdown(void)
{
    if (cache_pid > 0) {
        kill(cache_pid, SIGTERM);
        for (unsigned int i = 0; i < 50; i++) {
            if (waitpid(cache_pid, NULL, WNOHANG) == cache_pid) {
                cache_pid = 0;
                break;
            }
            usleep(20000);
        }
        if (cache_pid > 0)
            kill(cache_pid, SIGKILL);
    }
    if (cache_pipe >= 0)
        close(cache_pipe);
}

static void udc_attribute(const char *udc, const char *attribute,
                          char *value, size_t size)
{
    char path[512];
    size_t length;
    FILE *file;

    value[0] = '\0';
    if (snprintf(path, sizeof(path), "/sys/class/udc/%s/%s", udc, attribute)
        >= (int)sizeof(path)) {
        snprintf(value, size, "<path-too-long>");
        return;
    }
    file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT)
            snprintf(value, size, "<missing>");
        else
            snprintf(value, size, "<unreadable: %s>", strerror(errno));
        return;
    }
    if (fgets(value, size, file)) {
        length = strlen(value);
        while (length && (value[length - 1] == '\n' || value[length - 1] == '\r'))
            value[--length] = '\0';
        if (!value[0])
            snprintf(value, size, "<empty>");
    } else if (ferror(file)) {
        snprintf(value, size, "<unreadable: %s>", strerror(errno));
    } else {
        snprintf(value, size, "<empty>");
    }
    fclose(file);
}

static void log_udc_state(const char *phase)
{
    DIR *dir = opendir("/sys/class/udc");
    struct dirent *entry;
    char function[128], state[128];
    unsigned int count = 0;
    int error;

    if (!dir) {
        if (errno == ENOENT)
            log_message("USB_UDC_STATE %s class=<missing>", phase);
        else
            log_message("USB_UDC_STATE %s class=<unreadable: %s>", phase,
                        strerror(errno));
        return;
    }
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (!entry)
            break;
        if (entry->d_name[0] == '.')
            continue;
        udc_attribute(entry->d_name, "function", function, sizeof(function));
        udc_attribute(entry->d_name, "state", state, sizeof(state));
        log_message("USB_UDC_STATE %s udc=%s function=%s state=%s",
                    phase, entry->d_name, function, state);
        count++;
    }
    error = errno;
    closedir(dir);
    if (error)
        log_message("USB_UDC_STATE %s class=<unreadable: %s>", phase,
                    strerror(error));
    else if (!count)
        log_message("USB_UDC_STATE %s class=<empty>", phase);
}

static void enable_usb_gadget(void)
{
    int fd, saved_errno;
    ssize_t written;

    log_udc_state("before");
    log_message("USB_DEFERRED_START");
    fd = open("/sys/module/g_serial/parameters/enable", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        log_message("USB_DEFERRED_RESULT open failed: %s", strerror(errno));
        return;
    }
    written = write(fd, "1\n", 2);
    saved_errno = errno;
    close(fd);
    if (written == 2) {
        log_message("USB_DEFERRED_RESULT request_accepted");
        log_udc_state("after");
    } else if (written < 0) {
        log_message("USB_DEFERRED_RESULT write failed: %s", strerror(saved_errno));
    } else {
        log_message("USB_DEFERRED_RESULT short write: %zd", written);
    }
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
    unsigned long usb_state_due = 0;
    unsigned int reboot_after;
    int bootloader_target;
    int test_pid1_exit;
    int deferred_usb;
    int cache_opt_in;
    pid_t reaped;
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
    cache_opt_in = command_has("wayne.cache_kmsg=1");
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
    if (cache_opt_in) {
        log_message("BRINGUP_READY; cache logger requested");
        cache_start();
    } else {
        log_message("BRINGUP_READY; no disk partitions mounted");
    }
    if (reboot_after)
        log_message("AUTO_REBOOT_AFTER=%u seconds", reboot_after);
    for (;;) {
        unsigned int step = reboot_after || deferred_usb || usb_state_due ? 1 : 30;
        struct timespec delay = {step, 0};
        while (nanosleep(&delay, &delay) && errno == EINTR) {}
        elapsed += step;
        while ((reaped = waitpid(-1, NULL, WNOHANG)) > 0) {
            if (reaped == cache_pid)
                cache_pid = 0;
        }
        if (!(elapsed % 30))
            log_message("heartbeat=%lu pid=%ld kernel=%s", ++heartbeat,
                        (long)getpid(), name.release);
        if (test_pid1_exit && elapsed >= 5) {
            log_message("WAYNE_TEST_PID1_EXIT");
            _exit(1);
        }
        if (deferred_usb && elapsed >= 10) {
            deferred_usb = 0;
            if (!cache_opt_in || cache_ready()) {
                enable_usb_gadget();
                usb_state_due = elapsed + 5;
            } else {
                log_message("CACHE_KMSG_USB_SKIPPED logger not ready");
            }
        }
        if (usb_state_due && elapsed >= usb_state_due) {
            usb_state_due = 0;
            log_udc_state("settled");
        }
        if (reboot_after && elapsed >= reboot_after) {
            log_message("REBOOTING_AFTER_TEST");
            if (bootloader_target)
                log_message("REBOOT_TARGET=bootloader");
            if (cache_opt_in)
                cache_shutdown();
            if (bootloader_target) {
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
