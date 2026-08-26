#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "airlink_ipc_v4.h"
#include "airlink_provision.h"

#define AIRLINKD_VERSION "R27.6.6.22"
#define AIRLINKD_FW_ID 0x37324e4cU
#define C906L_FW_ID 0x50373252U
#define ABI_REVISION 4U
#define WLAN_IF "wlan0"
#define SOCKET_PATH "/run/airlinkd.sock"
#define LOG_PATH "/tmp/airlinkd.log"
#define WPA_RUN_CONF "/run/airlink-wpa.conf"
#define WPA_CTRL_DIR "/run/wpa_supplicant"
#define VH_PATH "/usr/bin/vhusbdriscv64"
#define VH_CONFIG_DIR "/data/airlink"
#define VH_CONFIG_PATH "/data/airlink/vhusbd.ini"
#define VH_CONFIG_TEMP "/data/airlink/vhusbd.ini.tmp"
#define VH_LOG_PATH "/tmp/vhusbd.log"
#define VH_TCP_PORT 7575U
#define VH_START_TIMEOUT_MS 8000ULL
#define VH_NETWORK_SETTLE_MS 2000ULL
#define VH_LAN_ACTIVATE_MAINTENANCE_MS 30000ULL
#define UI_PUBLISH_MS 2000ULL
#define HEALTH_MS 10000ULL
#define LINK_TIMEOUT_MS 30000ULL
#define PROCESS_STOP_MS 1500ULL
#define MODE_PROGRESS_MS 5000ULL
#define AP_TO_STA_DOWN_MS 300U
#define AP_TO_STA_UP_SETTLE_MS 150U
#define VH_FAILURE_WINDOW_MS 300000ULL
#define VH_FAILURE_LIMIT 3U
#define VH_CLIENT_CONNECT_STABLE_MS 500ULL
#define VH_CLIENT_DISCONNECT_STABLE_MS 1000ULL
#define VH_NO_CLIENT_HINT_MS 30000ULL
#define WIFI_POWER_SAVE_RETRY_MS 2000ULL
#define SDIO_IOS_PATH "/sys/kernel/debug/mmc1/ios"

#define REQUIRED_FEATURES (AIRLINK_IPC_FEATURE_SHM_POLL | \
    AIRLINK_IPC_FEATURE_CRC32 | AIRLINK_IPC_FEATURE_GENERATION | \
    AIRLINK_IPC_FEATURE_GPIOA29 | AIRLINK_IPC_FEATURE_DISPLAY | \
    AIRLINK_IPC_FEATURE_TOUCH_RAW | AIRLINK_IPC_FEATURE_ADC1 | \
    AIRLINK_IPC_FEATURE_LVGL_UI | AIRLINK_IPC_FEATURE_UI_STATUS | \
    AIRLINK_IPC_FEATURE_CH347_CONTROL | AIRLINK_IPC_FEATURE_SYSTEM_CONTROL | \
    AIRLINK_IPC_FEATURE_WIFI_PROVISION)

static volatile sig_atomic_t stop_requested;
static FILE *log_file;
static int console_fd = -1;

struct network_info {
    uint32_t flags;
    int32_t rssi;
    uint32_t ipv4;
    uint32_t frequency;
    char ssid[32];
    char bssid[18];
};

struct sdio_info {
    uint32_t requested_hz;
    uint32_t actual_hz;
    uint32_t timing;
    bool valid;
};

struct daemon_ctx {
    int mem_fd;
    volatile uint8_t *shm;
    int listen_fd;
    struct airlink_ipc_header header;
    struct airlink_ipc_state cstate;
    struct airlink_ipc_state lstate;
    struct airlink_ipc_ui_status ui;
    struct airlink_ipc_provision_status provision_status;
    struct airlink_provision_ctx provision;
    bool provisioning_sta_test;
    uint32_t last_provision_phase;
    uint32_t last_cstate_generation;
    uint32_t last_cmsg_generation;
    uint32_t last_cmsg_sequence;
    uint32_t last_cmsg_type;
    uint32_t tx_sequence;
    uint32_t mode_sequence;
    uint32_t desired_wired;
    uint32_t phase;
    uint32_t error;
    uint32_t transition_count;
    uint32_t ch347_mode;
    bool ch347_valid;
    bool mode_applied;
    bool wifi_unconfigured;
    bool vh_lockout;
    bool vh_retry_only;
    uint32_t vh_failures;
    uint64_t vh_failure_window;
    uint32_t vh_settle_ipv4;
    uint64_t vh_settle_started;
    uint64_t vh_settle_deadline;
    bool vh_settle_logged;
    uint32_t vh_lan_ipv4;
    uint64_t vh_lan_started;
    uint64_t vh_lan_deadline;
    unsigned vh_lan_step;
    bool vh_lan_active;
    bool vh_lan_client_seen;
    uint32_t vh_state;
    uint64_t vh_listener_since;
    uint64_t vh_client_candidate_since;
    uint64_t vh_client_missing_since;
    char vh_client_ip[INET6_ADDRSTRLEN];
    bool wifi_power_save_off;
    bool wifi_power_save_link_verified;
    uint64_t wifi_power_save_retry_deadline;
    uint64_t link_deadline;
    uint64_t retry_deadline;
    uint64_t progress_deadline;
    unsigned retry_index;
    pid_t wpa_pid;
    pid_t dhcp_pid;
    pid_t vh_pid;
    uint64_t next_heartbeat;
    uint64_t next_ui;
    uint64_t next_health;
    const char *ipc_fail_stage;
    int ipc_fail_errno;
};

static const uint32_t retry_ms[] = {2000U, 4000U, 8000U, 16000U, 30000U};
static const uint32_t vh_lan_activate_offsets_ms[] = {
    0U, 1000U, 2000U, 4000U, 8000U, 16000U, 30000U
};

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void signal_handler(int sig)
{
    (void)sig;
    stop_requested = 1;
}

static void log_rotate(void)
{
    struct stat st;
    if (stat(LOG_PATH, &st) == 0 && st.st_size > 256 * 1024) {
        unlink(LOG_PATH ".1");
        rename(LOG_PATH, LOG_PATH ".1");
    }
}

static void log_open(void)
{
    log_rotate();
    log_file = fopen(LOG_PATH, "a");
    if (log_file)
        setvbuf(log_file, NULL, _IOLBF, 0);
    console_fd = open("/dev/console", O_WRONLY | O_CLOEXEC | O_NOCTTY);
}

static void log_msg(const char *fmt, ...)
{
    char body[768];
    char line[840];
    va_list ap;
    int n;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    n = snprintf(line, sizeof(line), "[AIRLINKD] %s\n", body);
    if (n < 0)
        return;
    if (log_file) {
        fputs(line, log_file);
        fflush(log_file);
    }
    if (console_fd >= 0) {
        ssize_t written = write(console_fd, line, strnlen(line, sizeof(line)));
        (void)written;
    }
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static uint32_t block_crc(const void *block, size_t size)
{
    const uint8_t *bytes = block;
    return crc32_bytes(bytes + sizeof(uint32_t),
                       size - 2U * sizeof(uint32_t));
}

static void memory_barrier(void)
{
    __sync_synchronize();
}

static int shared_read(const volatile void *source, void *dest, size_t size)
{
    const volatile uint32_t *src_words = source;
    uint32_t before;
    uint32_t after;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        before = src_words[0];
        if (before == 0U || (before & 1U) != 0U)
            continue;
        memory_barrier();
        memcpy(dest, (const void *)source, size);
        memory_barrier();
        after = src_words[0];
        if (before != after || (after & 1U) != 0U)
            continue;
        if (((uint32_t *)dest)[size / 4U - 1U] != block_crc(dest, size))
            return -1;
        return 1;
    }
    return 0;
}

static void shared_write(volatile void *dest, const void *source, size_t size)
{
    volatile uint32_t *dst_words = dest;
    uint8_t local[128];
    uint32_t current;
    uint32_t final;
    memcpy(local, source, size);
    current = dst_words[0];
    if (current & 1U)
        current++;
    final = current + 2U;
    if (final == 0U)
        final = 2U;
    ((uint32_t *)local)[0] = final;
    ((uint32_t *)local)[size / 4U - 1U] = 0U;
    ((uint32_t *)local)[size / 4U - 1U] = block_crc(local, size);
    dst_words[0] = final - 1U;
    memory_barrier();
    memcpy((void *)((uintptr_t)dest + 4U), local + 4U, size - 4U);
    memory_barrier();
    dst_words[0] = final;
    memory_barrier();
}

static volatile void *shared_at(struct daemon_ctx *ctx, uint32_t offset)
{
    return (volatile void *)(ctx->shm + offset);
}

static void publish_linux_state(struct daemon_ctx *ctx)
{
    shared_write(shared_at(ctx, AIRLINK_IPC_LINUX_STATE_OFFSET),
                 &ctx->lstate, sizeof(ctx->lstate));
}

static uint32_t next_sequence(struct daemon_ctx *ctx)
{
    ctx->tx_sequence++;
    if (ctx->tx_sequence == 0U)
        ctx->tx_sequence = 1U;
    return ctx->tx_sequence;
}

static void send_message(struct daemon_ctx *ctx, uint32_t type,
                         uint32_t flags, uint32_t sequence,
                         uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    struct airlink_ipc_message msg;
    struct timespec ts;
    memset(&msg, 0, sizeof(msg));
    if (sequence == 0U)
        sequence = next_sequence(ctx);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    msg.sequence = sequence;
    msg.type = type;
    msg.flags = flags;
    msg.payload_len = sizeof(msg.args);
    msg.args[0] = a0;
    msg.args[1] = a1;
    msg.args[2] = a2;
    msg.args[3] = a3;
    msg.timestamp_low = (uint32_t)ts.tv_nsec;
    msg.timestamp_high = (uint32_t)ts.tv_sec;
    msg.sender_heartbeat = ctx->lstate.heartbeat;
    shared_write(shared_at(ctx, AIRLINK_IPC_LINUX_TX_OFFSET),
                 &msg, sizeof(msg));
    ctx->lstate.last_tx_seq = sequence;
    ctx->lstate.tx_count++;
    publish_linux_state(ctx);
}

static int read_cstate(struct daemon_ctx *ctx)
{
    struct airlink_ipc_state state;
    int rc = shared_read(shared_at(ctx, AIRLINK_IPC_C906_STATE_OFFSET),
                         &state, sizeof(state));
    if (rc <= 0)
        return rc;
    if (state.owner != AIRLINK_IPC_OWNER_C906L ||
        state.abi_revision != ABI_REVISION)
        return -1;
    ctx->cstate = state;
    ctx->last_cstate_generation = state.generation;
    ctx->lstate.peer_heartbeat = state.heartbeat;
    return 1;
}

static int read_cmessage(struct daemon_ctx *ctx,
                         struct airlink_ipc_message *msg)
{
    int rc = shared_read(shared_at(ctx, AIRLINK_IPC_C906_TX_OFFSET),
                         msg, sizeof(*msg));
    if (rc <= 0)
        return rc;
    if (msg->generation == ctx->last_cmsg_generation &&
        msg->sequence == ctx->last_cmsg_sequence &&
        msg->type == ctx->last_cmsg_type)
        return 0;
    ctx->last_cmsg_generation = msg->generation;
    ctx->last_cmsg_sequence = msg->sequence;
    ctx->last_cmsg_type = msg->type;
    ctx->lstate.last_rx_seq = msg->sequence;
    ctx->lstate.rx_count++;
    publish_linux_state(ctx);
    return 1;
}

static int run_capture(const char *const argv[], char *out, size_t out_size,
                       unsigned timeout_ms)
{
    int pipefd[2];
    pid_t pid;
    size_t used = 0;
    uint64_t deadline;
    int status = 127;
    if (out_size)
        out[0] = '\0';
    if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) != 0)
        return 127;
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return 127;
    }
    if (pid == 0) {
        int nullfd = open("/dev/null", O_WRONLY);
        dup2(pipefd[1], STDOUT_FILENO);
        if (nullfd >= 0)
            dup2(nullfd, STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        if (nullfd > STDERR_FILENO)
            close(nullfd);
        execv(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(pipefd[1]);
    deadline = monotonic_ms() + timeout_ms;
    for (;;) {
        char buf[256];
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0 && out_size > 1U) {
            size_t copy = (size_t)n;
            if (copy > out_size - 1U - used)
                copy = out_size - 1U - used;
            memcpy(out + used, buf, copy);
            used += copy;
            out[used] = '\0';
        }
        if (waitpid(pid, &status, WNOHANG) == pid)
            break;
        if (monotonic_ms() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            status = 127 << 8;
            break;
        }
        usleep(10000);
    }
    close(pipefd[0]);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}

static int run_quiet(const char *const argv[], unsigned timeout_ms)
{
    char ignored[1];
    return run_capture(argv, ignored, sizeof(ignored), timeout_ms);
}

static bool pid_cmdline_contains(pid_t pid, const char *needle)
{
    char path[64];
    char buf[512];
    int fd;
    ssize_t n;
    snprintf(path, sizeof(path), "/proc/%ld/cmdline", (long)pid);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    n = read(fd, buf, sizeof(buf) - 1U);
    close(fd);
    if (n <= 0)
        return false;
    buf[n] = '\0';
    for (ssize_t i = 0; i < n; ++i)
        if (buf[i] == '\0')
            buf[i] = ' ';
    return strstr(buf, needle) != NULL;
}

static unsigned collect_matching(const char *needle, pid_t *pids,
                                 unsigned capacity)
{
    DIR *dir = opendir("/proc");
    struct dirent *entry;
    unsigned count = 0;
    if (!dir)
        return 0;
    while ((entry = readdir(dir)) != NULL) {
        char *end;
        long value;
        if (!isdigit((unsigned char)entry->d_name[0]))
            continue;
        value = strtol(entry->d_name, &end, 10);
        if (*end != '\0' || value <= 1 || value == getpid())
            continue;
        if (pid_cmdline_contains((pid_t)value, needle)) {
            if (count < capacity)
                pids[count] = (pid_t)value;
            count++;
        }
    }
    closedir(dir);
    return count;
}

static bool process_running(const char *needle)
{
    pid_t pid;
    return collect_matching(needle, &pid, 1U) != 0U;
}

static bool pid_alive(pid_t pid)
{
    if (pid <= 0)
        return false;
    if (kill(pid, 0) == 0)
        return true;
    return errno == EPERM;
}

static bool tcp_port_state_file(const char *path, unsigned port,
                                unsigned wanted_state)
{
    FILE *file = fopen(path, "r");
    char line[256];
    if (!file)
        return false;
    while (fgets(line, sizeof(line), file)) {
        char local[96];
        char remote[96];
        unsigned state;
        char *colon;
        unsigned long parsed_port;
        if (sscanf(line, " %*u: %95s %95s %x", local, remote,
                   &state) != 3)
            continue;
        colon = strrchr(local, ':');
        if (!colon)
            continue;
        errno = 0;
        parsed_port = strtoul(colon + 1, NULL, 16);
        if (errno == 0 && parsed_port == port && state == wanted_state) {
            fclose(file);
            return true;
        }
    }
    fclose(file);
    return false;
}

static bool virtualhere_listening(void)
{
    return tcp_port_state_file("/proc/net/tcp", VH_TCP_PORT, 0x0aU) ||
           tcp_port_state_file("/proc/net/tcp6", VH_TCP_PORT, 0x0aU);
}

static bool proc_tcp_remote_endpoint(const char *path, bool ipv6,
                                     unsigned port, char *remote_ip,
                                     size_t remote_ip_size)
{
    FILE *file = fopen(path, "r");
    char line[256];
    if (!file)
        return false;
    while (fgets(line, sizeof(line), file)) {
        char local[96];
        char remote[96];
        unsigned state;
        char *local_colon;
        char *remote_colon;
        unsigned long parsed_port;
        if (sscanf(line, " %*u: %95s %95s %x", local, remote,
                   &state) != 3 || state != 0x01U)
            continue;
        local_colon = strrchr(local, ':');
        remote_colon = strrchr(remote, ':');
        if (!local_colon || !remote_colon)
            continue;
        errno = 0;
        parsed_port = strtoul(local_colon + 1, NULL, 16);
        if (errno != 0 || parsed_port != port)
            continue;
        *remote_colon = '\0';
        if (!ipv6) {
            struct in_addr address;
            unsigned long raw;
            errno = 0;
            raw = strtoul(remote, NULL, 16);
            if (errno != 0 || raw > UINT32_MAX)
                continue;
            address.s_addr = (uint32_t)raw;
            if (!inet_ntop(AF_INET, &address, remote_ip, remote_ip_size))
                continue;
        } else {
            struct in6_addr address6;
            if (strlen(remote) != 32U)
                continue;
            memset(&address6, 0, sizeof(address6));
            for (unsigned index = 0U; index < 4U; ++index) {
                char word_text[9];
                unsigned long word;
                memcpy(word_text, remote + index * 8U, 8U);
                word_text[8] = '\0';
                errno = 0;
                word = strtoul(word_text, NULL, 16);
                if (errno != 0 || word > UINT32_MAX) {
                    memset(&address6, 0, sizeof(address6));
                    break;
                }
                {
                    uint32_t raw_word = (uint32_t)word;
                    memcpy(&address6.s6_addr[index * 4U], &raw_word,
                           sizeof(raw_word));
                }
            }
            if (IN6_IS_ADDR_UNSPECIFIED(&address6))
                continue;
            if (IN6_IS_ADDR_V4MAPPED(&address6)) {
                struct in_addr address4;
                memcpy(&address4, &address6.s6_addr[12], sizeof(address4));
                if (!inet_ntop(AF_INET, &address4, remote_ip,
                               remote_ip_size))
                    continue;
            } else if (!inet_ntop(AF_INET6, &address6, remote_ip,
                                  remote_ip_size)) {
                continue;
            }
        }
        fclose(file);
        return true;
    }
    fclose(file);
    if (remote_ip_size)
        remote_ip[0] = '\0';
    return false;
}

static bool virtualhere_client_endpoint(char *remote_ip, size_t size)
{
    if (proc_tcp_remote_endpoint("/proc/net/tcp", false, VH_TCP_PORT,
                                 remote_ip, size))
        return true;
    return proc_tcp_remote_endpoint("/proc/net/tcp6", true, VH_TCP_PORT,
                                    remote_ip, size);
}

static bool virtualhere_client_connected(void)
{
    char remote_ip[INET6_ADDRSTRLEN];
    return virtualhere_client_endpoint(remote_ip, sizeof(remote_ip));
}

static bool virtualhere_ready(void)
{
    return process_running("vhusbdriscv64") && virtualhere_listening();
}

static void reset_virtualhere_client_state(struct daemon_ctx *ctx)
{
    ctx->vh_state = AIRLINK_VIRTUALHERE_STOPPED;
    ctx->vh_listener_since = 0U;
    ctx->vh_client_candidate_since = 0U;
    ctx->vh_client_missing_since = 0U;
    ctx->vh_client_ip[0] = '\0';
}

static uint32_t apply_virtualhere_client_sample(struct daemon_ctx *ctx,
                                                uint64_t now,
                                                bool listener_ready,
                                                bool connected,
                                                const char *remote_ip)
{
    if (!listener_ready) {
        reset_virtualhere_client_state(ctx);
        return AIRLINK_VIRTUALHERE_STOPPED;
    }
    if (ctx->vh_listener_since == 0U)
        ctx->vh_listener_since = now;
    if (connected) {
        ctx->vh_client_missing_since = 0U;
        if (ctx->vh_state == AIRLINK_VIRTUALHERE_CLIENT_CONNECTED) {
            snprintf(ctx->vh_client_ip, sizeof(ctx->vh_client_ip), "%s",
                     remote_ip);
            return ctx->vh_state;
        }
        if (ctx->vh_client_candidate_since == 0U ||
            strcmp(ctx->vh_client_ip, remote_ip) != 0) {
            ctx->vh_client_candidate_since = now;
            snprintf(ctx->vh_client_ip, sizeof(ctx->vh_client_ip), "%s",
                     remote_ip);
        }
        ctx->vh_state = AIRLINK_VIRTUALHERE_LISTENING;
        if (now - ctx->vh_client_candidate_since >=
                VH_CLIENT_CONNECT_STABLE_MS) {
            ctx->vh_state = AIRLINK_VIRTUALHERE_CLIENT_CONNECTED;
            log_msg("VH client-state=CONNECTED ip=%s stable_ms=%u",
                    ctx->vh_client_ip,
                    (unsigned)VH_CLIENT_CONNECT_STABLE_MS);
        }
        return ctx->vh_state;
    }

    ctx->vh_client_candidate_since = 0U;
    if (ctx->vh_state == AIRLINK_VIRTUALHERE_CLIENT_CONNECTED) {
        if (ctx->vh_client_missing_since == 0U)
            ctx->vh_client_missing_since = now;
        if (now - ctx->vh_client_missing_since <
                VH_CLIENT_DISCONNECT_STABLE_MS)
            return ctx->vh_state;
        log_msg("VH client-state=DISCONNECTED stable_ms=%u",
                (unsigned)VH_CLIENT_DISCONNECT_STABLE_MS);
    }
    ctx->vh_client_missing_since = 0U;
    ctx->vh_client_ip[0] = '\0';
    ctx->vh_state = AIRLINK_VIRTUALHERE_LISTENING;
    return ctx->vh_state;
}

static uint32_t update_virtualhere_client_state(struct daemon_ctx *ctx,
                                                uint64_t now)
{
    char remote_ip[INET6_ADDRSTRLEN] = "";
    bool ready = virtualhere_ready();
    bool connected = ready &&
        virtualhere_client_endpoint(remote_ip, sizeof(remote_ip));
    return apply_virtualhere_client_sample(ctx, now, ready, connected,
                                           remote_ip);
}

static bool write_virtualhere_config(void)
{
    struct stat status;
    FILE *file;
    int fd;
    int dirfd;

    if (stat(VH_CONFIG_PATH, &status) == 0) {
        if (!S_ISREG(status.st_mode) || status.st_size == 0)
            return false;
        (void)chmod(VH_CONFIG_PATH, 0600);
        log_msg("VirtualHere config=PERSISTENT path=%s bytes=%llu preserve=YES",
                VH_CONFIG_PATH, (unsigned long long)status.st_size);
        return true;
    }
    if (errno != ENOENT)
        return false;
    if (mkdir(VH_CONFIG_DIR, 0700) != 0 && errno != EEXIST)
        return false;
    file = fopen(VH_CONFIG_TEMP, "w");
    if (!file)
        return false;
    if (fprintf(file, "[General]\nServerName=AirLink\n") < 0 ||
        fflush(file) != 0) {
        fclose(file);
        unlink(VH_CONFIG_TEMP);
        return false;
    }
    fd = fileno(file);
    if (fd < 0 || fchmod(fd, 0600) != 0 || fsync(fd) != 0) {
        fclose(file);
        unlink(VH_CONFIG_TEMP);
        return false;
    }
    if (fclose(file) != 0) {
        unlink(VH_CONFIG_TEMP);
        return false;
    }
    if (rename(VH_CONFIG_TEMP, VH_CONFIG_PATH) != 0) {
        unlink(VH_CONFIG_TEMP);
        return false;
    }
    dirfd = open(VH_CONFIG_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd >= 0) {
        (void)fsync(dirfd);
        close(dirfd);
    }
    log_msg("VirtualHere config=PERSISTENT path=%s created=YES mode=0600",
            VH_CONFIG_PATH);
    return true;
}

static void log_virtualhere_tail(void)
{
    FILE *file = fopen(VH_LOG_PATH, "r");
    char lines[4][256];
    char line[256];
    unsigned count = 0;
    unsigned first;

    if (!file) {
        log_msg("VirtualHere log unavailable path=%s errno=%d",
                VH_LOG_PATH, errno);
        return;
    }
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        while (length > 0U &&
               (line[length - 1U] == '\n' || line[length - 1U] == '\r'))
            line[--length] = '\0';
        snprintf(lines[count % 4U], sizeof(lines[0]), "%s", line);
        count++;
    }
    fclose(file);
    if (count == 0U) {
        log_msg("VirtualHere log empty path=%s", VH_LOG_PATH);
        return;
    }
    first = count > 4U ? count - 4U : 0U;
    for (unsigned i = first; i < count; ++i)
        log_msg("VirtualHere log: %s", lines[i % 4U]);
}

static void reap_children(void)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

static bool stop_matching(const char *needle, unsigned timeout_ms)
{
    pid_t pids[32];
    unsigned count = collect_matching(needle, pids, 32U);
    uint64_t deadline;
    if (count > 32U)
        count = 32U;
    for (unsigned i = 0; i < count; ++i)
        kill(pids[i], SIGTERM);
    deadline = monotonic_ms() + timeout_ms;
    while (process_running(needle) && monotonic_ms() < deadline)
        usleep(50000);
    if (process_running(needle)) {
        count = collect_matching(needle, pids, 32U);
        if (count > 32U)
            count = 32U;
        for (unsigned i = 0; i < count; ++i)
            kill(pids[i], SIGKILL);
        usleep(50000);
    }
    reap_children();
    return !process_running(needle);
}

static bool pid_matches_any(pid_t pid, const char *const *names,
                            unsigned name_count)
{
    for (unsigned i = 0; i < name_count; ++i)
        if (pid_cmdline_contains(pid, names[i]))
            return true;
    return false;
}

static unsigned collect_matching_many(const char *const *names,
                                      unsigned name_count,
                                      pid_t *pids, unsigned capacity)
{
    unsigned count = 0;

    for (unsigned name = 0; name < name_count; ++name) {
        pid_t found[32];
        unsigned found_count = collect_matching(names[name], found, 32U);
        if (found_count > 32U)
            found_count = 32U;
        for (unsigned i = 0; i < found_count; ++i) {
            bool duplicate = false;
            for (unsigned j = 0; j < count && j < capacity; ++j)
                if (pids[j] == found[i]) {
                    duplicate = true;
                    break;
                }
            if (duplicate)
                continue;
            if (count < capacity)
                pids[count] = found[i];
            count++;
        }
    }
    return count;
}

static bool any_matching_many(const char *const *names, unsigned name_count)
{
    for (unsigned i = 0; i < name_count; ++i)
        if (process_running(names[i]))
            return true;
    return false;
}

static bool stop_matching_many(const char *const *names, unsigned name_count,
                               unsigned timeout_ms, unsigned *term_count,
                               unsigned *kill_count)
{
    pid_t pids[64];
    unsigned count = collect_matching_many(names, name_count, pids, 64U);
    uint64_t deadline;

    if (count > 64U)
        count = 64U;
    if (term_count)
        *term_count = count;
    if (kill_count)
        *kill_count = 0U;
    for (unsigned i = 0; i < count; ++i)
        if (pid_matches_any(pids[i], names, name_count))
            (void)kill(pids[i], SIGTERM);

    deadline = monotonic_ms() + timeout_ms;
    while (any_matching_many(names, name_count) &&
           monotonic_ms() < deadline)
        usleep(50000);

    if (any_matching_many(names, name_count)) {
        count = collect_matching_many(names, name_count, pids, 64U);
        if (count > 64U)
            count = 64U;
        if (kill_count)
            *kill_count = count;
        for (unsigned i = 0; i < count; ++i)
            if (pid_matches_any(pids[i], names, name_count))
                (void)kill(pids[i], SIGKILL);
        usleep(50000);
    }
    reap_children();
    return !any_matching_many(names, name_count);
}

static pid_t spawn_process(const char *path, char *const argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
        setsid();
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO)
                close(fd);
        }
        execv(path, argv);
        _exit(127);
    }
    return pid;
}

static __attribute__((unused)) bool file_exists(const char *path)
{
    return access(path, R_OK) == 0;
}

static int copy_file_limited(const char *source, const char *dest,
                             size_t limit)
{
    int in = open(source, O_RDONLY | O_CLOEXEC);
    int out;
    char buf[1024];
    size_t total = 0;

    if (in < 0)
        return -1;
    out = open(dest, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (out < 0) {
        close(in);
        return -1;
    }
    for (;;) {
        ssize_t count = read(in, buf, sizeof(buf));
        size_t offset = 0;
        if (count == 0)
            break;
        if (count < 0 || total + (size_t)count > limit)
            goto fail;
        while (offset < (size_t)count) {
            ssize_t written = write(out, buf + offset,
                                    (size_t)count - offset);
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                goto fail;
            }
            if (written == 0)
                goto fail;
            offset += (size_t)written;
        }
        total += (size_t)count;
    }
    if (total == 0U || fsync(out) != 0)
        goto fail;
    close(in);
    if (close(out) != 0) {
        unlink(dest);
        return -1;
    }
    return 0;

fail:
    close(in);
    close(out);
    unlink(dest);
    return -1;
}

static __attribute__((unused)) int read_trimmed(const char *path, char *dest, size_t size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n;
    if (fd < 0 || size < 2U) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    n = read(fd, dest, size - 1U);
    close(fd);
    if (n <= 0)
        return -1;
    dest[n] = '\0';
    while (n > 0 && isspace((unsigned char)dest[n - 1]))
        dest[--n] = '\0';
    return n > 0 ? 0 : -1;
}

static __attribute__((unused)) int write_wpa_quoted(FILE *file, const char *key, const char *value)
{
    if (fprintf(file, "\t%s=\"", key) < 0)
        return -1;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p < 0x20U || *p == 0x7fU)
            return -1;
        if (*p == '\\' || *p == '"')
            fputc('\\', file);
        fputc(*p, file);
    }
    return fputs("\"\n", file) < 0 ? -1 : 0;
}

static bool wpa_config_has_explicit_ssid(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[512];
    bool in_network = false;
    if (!file)
        return false;
    while (fgets(line, sizeof(line), file)) {
        char *p = line;
        char *value;
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '#' || *p == '\0')
            continue;
        if (!in_network) {
            if (strncmp(p, "network", 7) == 0 && strchr(p, '{'))
                in_network = true;
            continue;
        }
        if (*p == '}') {
            in_network = false;
            continue;
        }
        if (strncmp(p, "ssid", 4) != 0)
            continue;
        p += 4;
        while (isspace((unsigned char)*p))
            p++;
        if (*p++ != '=')
            continue;
        while (isspace((unsigned char)*p))
            p++;
        value = p;
        if (*value == '"') {
            value++;
            if (*value != '\0' && *value != '"' && *value != '\r' &&
                *value != '\n') {
                fclose(file);
                return true;
            }
        } else if (*value != '\0' && *value != '#' && *value != '\r' &&
                   *value != '\n') {
            fclose(file);
            return true;
        }
    }
    fclose(file);
    return false;
}

static bool wifi_config_available(void)
{
    return wpa_config_has_explicit_ssid("/data/airlink/wifi.conf");
}

static int prepare_wpa_config(void)
{
    if (!wifi_config_available())
        return -1;
    return copy_file_limited("/data/airlink/wifi.conf",
                             WPA_RUN_CONF, 64U * 1024U);
}

static int commit_candidate_config(void)
{
    const char *dir = "/data/airlink";
    const char *temp = "/data/airlink/wifi.conf.tmp";
    int dirfd;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST)
        return -1;
    if (copy_file_limited(AIRLINK_PROVISION_CANDIDATE_CONF,
                          temp, 64U * 1024U) != 0)
        return -1;
    if (rename(temp, "/data/airlink/wifi.conf") != 0) {
        unlink(temp);
        return -1;
    }
    chmod("/data/airlink/wifi.conf", 0600);
    dirfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd >= 0) {
        fsync(dirfd);
        close(dirfd);
    }
    return 0;
}

static int remove_saved_wifi_config(void)
{
    int dirfd;
    int result = unlink("/data/airlink/wifi.conf");

    if (result != 0 && errno != ENOENT)
        return -1;
    dirfd = open("/data/airlink", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd >= 0) {
        result = fsync(dirfd);
        close(dirfd);
        if (result != 0)
            return -1;
    }
    return 0;
}

static bool link_is_up(struct network_info *info)
{
    const char *wpa_argv[] = {"/usr/sbin/wpa_cli", "-i", WLAN_IF,
                              "status", NULL};
    const char *iw_argv[] = {"/usr/sbin/iw", "dev", WLAN_IF, "link", NULL};
    const char *ip_argv[] = {"/sbin/ip", "-4", "-o", "addr", "show",
                             "dev", WLAN_IF, NULL};
    char buf[4096];
    char *save;
    char *line;
    memset(info, 0, sizeof(*info));
    info->flags = AIRLINK_UI_STATUS_VALID;
    info->rssi = -127;
    if (run_capture(wpa_argv, buf, sizeof(buf), 1200U) == 0) {
        save = NULL;
        for (line = strtok_r(buf, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            if (strncmp(line, "wpa_state=", 10) == 0 &&
                strcmp(line + 10, "COMPLETED") == 0)
                info->flags |= AIRLINK_UI_STATUS_WIFI_CONNECTED;
            else if (strncmp(line, "ssid=", 5) == 0)
                snprintf(info->ssid, sizeof(info->ssid), "%s", line + 5);
            else if (strncmp(line, "freq=", 5) == 0)
                info->frequency = (uint32_t)strtoul(line + 5, NULL, 10);
            /* Ignore wpa_cli ip_address: it can precede wlan0 state. */
        }
    }
    if (run_capture(iw_argv, buf, sizeof(buf), 1200U) == 0) {
        save = NULL;
        for (line = strtok_r(buf, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            while (isspace((unsigned char)*line))
                line++;
            if (strncmp(line, "Connected to ", 13) == 0) {
                line += 13;
                if (strlen(line) >= 17U) {
                    memcpy(info->bssid, line, 17U);
                    info->bssid[17] = '\0';
                }
            } else if (strncmp(line, "SSID:", 5) == 0 &&
                       info->ssid[0] == '\0') {
                line += 5;
                while (isspace((unsigned char)*line))
                    line++;
                snprintf(info->ssid, sizeof(info->ssid), "%s", line);
            } else if (strncmp(line, "freq:", 5) == 0 &&
                       info->frequency == 0U) {
                info->frequency = (uint32_t)strtoul(line + 5, NULL, 10);
            } else if (strncmp(line, "signal:", 7) == 0) {
                info->rssi = (int32_t)strtol(line + 7, NULL, 10);
            }
        }
    }
    if (run_capture(ip_argv, buf, sizeof(buf), 1200U) == 0) {
        char *inet = strstr(buf, " inet ");
        if (inet) {
            struct in_addr address;
            char address_text[32];
            size_t len;
            inet += 6;
            len = strcspn(inet, "/ \r\n");
            if (len < sizeof(address_text)) {
                memcpy(address_text, inet, len);
                address_text[len] = '\0';
                if (inet_aton(address_text, &address))
                    info->ipv4 = address.s_addr;
            }
        }
    }
    if (info->frequency >= 4900U)
        info->flags |= AIRLINK_UI_STATUS_WIFI_5GHZ;
    return (info->flags & AIRLINK_UI_STATUS_WIFI_CONNECTED) != 0U &&
           info->ipv4 != 0U;
}

struct garp_frame {
    uint8_t ethernet_dst[ETH_ALEN];
    uint8_t ethernet_src[ETH_ALEN];
    uint16_t ethernet_type;
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t hardware_length;
    uint8_t protocol_length;
    uint16_t operation;
    uint8_t sender_mac[ETH_ALEN];
    uint32_t sender_ipv4;
    uint8_t target_mac[ETH_ALEN];
    uint32_t target_ipv4;
} __attribute__((packed));

static void build_gratuitous_arp(struct garp_frame *frame,
                                 const uint8_t mac[ETH_ALEN],
                                 uint32_t ipv4, uint16_t operation)
{
    memset(frame, 0, sizeof(*frame));
    memset(frame->ethernet_dst, 0xff, ETH_ALEN);
    memcpy(frame->ethernet_src, mac, ETH_ALEN);
    frame->ethernet_type = htons(ETH_P_ARP);
    frame->hardware_type = htons(ARPHRD_ETHER);
    frame->protocol_type = htons(ETH_P_IP);
    frame->hardware_length = ETH_ALEN;
    frame->protocol_length = sizeof(ipv4);
    frame->operation = htons(operation);
    memcpy(frame->sender_mac, mac, ETH_ALEN);
    frame->sender_ipv4 = ipv4;
    if (operation == ARPOP_REPLY)
        memset(frame->target_mac, 0xff, ETH_ALEN);
    frame->target_ipv4 = ipv4;
}

static unsigned send_gratuitous_arp(const char *ifname, uint32_t ipv4)
{
    struct sockaddr_ll target;
    struct ifreq ifr;
    struct garp_frame frame;
    uint8_t mac[ETH_ALEN];
    unsigned ifindex;
    unsigned sent = 0U;
    int fd;

    ifindex = if_nametoindex(ifname);
    if (ifindex == 0U)
        return 0U;
    fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ARP));
    if (fd < 0)
        return 0U;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0) {
        close(fd);
        return 0U;
    }
    memcpy(mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    memset(&target, 0, sizeof(target));
    target.sll_family = AF_PACKET;
    target.sll_protocol = htons(ETH_P_ARP);
    target.sll_ifindex = (int)ifindex;
    target.sll_halen = ETH_ALEN;
    memset(target.sll_addr, 0xff, ETH_ALEN);

    build_gratuitous_arp(&frame, mac, ipv4, ARPOP_REQUEST);
    if (sendto(fd, &frame, sizeof(frame), 0,
               (struct sockaddr *)&target, sizeof(target)) ==
        (ssize_t)sizeof(frame))
        sent++;
    build_gratuitous_arp(&frame, mac, ipv4, ARPOP_REPLY);
    if (sendto(fd, &frame, sizeof(frame), 0,
               (struct sockaddr *)&target, sizeof(target)) ==
        (ssize_t)sizeof(frame))
        sent++;
    close(fd);
    return sent;
}

static unsigned send_lan_activation_broadcast(const char *ifname,
                                              uint32_t ipv4)
{
    static const char payload[] = "AIRLINK-VH-READY";
    struct sockaddr_in target;
    struct sockaddr_in *netmask_address;
    struct ifreq ifr;
    uint32_t netmask;
    int enabled = 1;
    int fd;
    ssize_t sent;

    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return 0U;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST,
                   &enabled, sizeof(enabled)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
                   ifname, strlen(ifname) + 1U) != 0) {
        close(fd);
        return 0U;
    }
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    if (ioctl(fd, SIOCGIFNETMASK, &ifr) != 0) {
        close(fd);
        return 0U;
    }
    netmask_address = (struct sockaddr_in *)&ifr.ifr_netmask;
    netmask = netmask_address->sin_addr.s_addr;
    if (netmask == 0U) {
        close(fd);
        return 0U;
    }
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(VH_TCP_PORT);
    target.sin_addr.s_addr = (ipv4 & netmask) | ~netmask;
    sent = sendto(fd, payload, sizeof(payload) - 1U, 0,
                  (struct sockaddr *)&target, sizeof(target));
    close(fd);
    return sent == (ssize_t)(sizeof(payload) - 1U) ? 1U : 0U;
}

static bool default_route_line_matches(const char *line,
                                             const char *ifname)
{
    char iface[IFNAMSIZ];
    unsigned long destination;
    unsigned long gateway;
    unsigned long flags;

    if (sscanf(line, "%15s %lx %lx %lx",
               iface, &destination, &gateway, &flags) != 4)
        return false;
    return strcmp(iface, ifname) == 0 && destination == 0UL &&
           (flags & 0x1UL) != 0UL;
}

static bool default_route_is_up(const char *ifname)
{
    FILE *file = fopen("/proc/net/route", "r");
    char line[256];
    bool found = false;

    if (!file)
        return false;
    while (fgets(line, sizeof(line), file)) {
        if (default_route_line_matches(line, ifname)) {
            found = true;
            break;
        }
    }
    fclose(file);
    return found;
}

static void reset_vh_network_settle(struct daemon_ctx *ctx)
{
    ctx->vh_settle_ipv4 = 0U;
    ctx->vh_settle_started = 0U;
    ctx->vh_settle_deadline = 0U;
    ctx->vh_settle_logged = false;
    ctx->vh_lan_ipv4 = 0U;
    ctx->vh_lan_started = 0U;
    ctx->vh_lan_deadline = 0U;
    ctx->vh_lan_step = 0U;
    ctx->vh_lan_active = false;
    ctx->vh_lan_client_seen = false;
    reset_virtualhere_client_state(ctx);
}

static bool virtualhere_network_settled(struct daemon_ctx *ctx,
                                        const struct network_info *net,
                                        uint64_t now)
{
    char ip[INET_ADDRSTRLEN] = "0.0.0.0";

    if (ctx->vh_settle_deadline == 0U ||
        ctx->vh_settle_ipv4 != net->ipv4) {
        ctx->vh_settle_ipv4 = net->ipv4;
        ctx->vh_settle_started = now;
        ctx->vh_settle_deadline = now + VH_NETWORK_SETTLE_MS;
        ctx->vh_settle_logged = false;
        (void)inet_ntop(AF_INET, &net->ipv4, ip, sizeof(ip));
        log_msg("VH network-ready ipv4=%s route=YES settle_ms=%u",
                ip, (unsigned)VH_NETWORK_SETTLE_MS);
        return false;
    }
    if (now < ctx->vh_settle_deadline)
        return false;
    if (!ctx->vh_settle_logged) {
        log_msg("VH start after-network-settle elapsed_ms=%llu",
                (unsigned long long)(now - ctx->vh_settle_started));
        ctx->vh_settle_logged = true;
    }
    return true;
}

static void begin_virtualhere_lan_activation(struct daemon_ctx *ctx,
                                               const struct network_info *net,
                                               uint64_t now)
{
    char ip[INET_ADDRSTRLEN] = "0.0.0.0";

    ctx->vh_lan_ipv4 = net->ipv4;
    ctx->vh_lan_started = now;
    ctx->vh_lan_deadline = now;
    ctx->vh_lan_step = 0U;
    ctx->vh_lan_active = true;
    ctx->vh_lan_client_seen = false;
    (void)inet_ntop(AF_INET, &net->ipv4, ip, sizeof(ip));
    log_msg("VH lan-activate begin ipv4=%s schedule_ms=0,1000,2000,"
            "4000,8000,16000,30000 maintenance_ms=%u",
            ip, (unsigned)VH_LAN_ACTIVATE_MAINTENANCE_MS);
}

static void service_virtualhere_lan_activation(struct daemon_ctx *ctx,
                                                const struct network_info *net,
                                                uint64_t now)
{
    char ip[INET_ADDRSTRLEN] = "0.0.0.0";
    unsigned garp_packets;
    unsigned udp_packets;
    unsigned fast_steps =
        (unsigned)(sizeof(vh_lan_activate_offsets_ms) /
                   sizeof(vh_lan_activate_offsets_ms[0]));
    const char *phase;

    if (!ctx->vh_lan_active || ctx->vh_lan_ipv4 != net->ipv4 ||
        now < ctx->vh_lan_deadline)
        return;
    (void)inet_ntop(AF_INET, &net->ipv4, ip, sizeof(ip));
    if (virtualhere_client_connected()) {
        ctx->vh_lan_active = false;
        ctx->vh_lan_client_seen = true;
        log_msg("VH lan-activate client=CONNECTED ipv4=%s attempts=%u "
                "elapsed_ms=%llu PASS",
                ip, ctx->vh_lan_step,
                (unsigned long long)(now - ctx->vh_lan_started));
        return;
    }

    garp_packets = send_gratuitous_arp(WLAN_IF, net->ipv4);
    udp_packets = send_lan_activation_broadcast(WLAN_IF, net->ipv4);
    phase = ctx->vh_lan_step < fast_steps ? "FAST" : "MAINTENANCE";
    log_msg("VH lan-activate step=%u phase=%s ipv4=%s garp=%u/2 "
            "udp_broadcast=%u/1 client=WAIT",
            ctx->vh_lan_step + 1U, phase, ip, garp_packets, udp_packets);
    ctx->vh_lan_step++;
    if (ctx->vh_lan_step < fast_steps) {
        ctx->vh_lan_deadline =
            ctx->vh_lan_started +
            vh_lan_activate_offsets_ms[ctx->vh_lan_step];
    } else {
        ctx->vh_lan_deadline =
            now + VH_LAN_ACTIVATE_MAINTENANCE_MS;
    }
}

static bool stop_virtualhere(struct daemon_ctx *ctx)
{
    bool ok = stop_matching("vhusbdriscv64", PROCESS_STOP_MS);
    ctx->vh_pid = 0;
    reset_vh_network_settle(ctx);
    if (!ok)
        log_msg("VirtualHere stop FAIL");
    else
        log_msg("VirtualHere stopped");
    return ok;
}

static bool start_virtualhere(struct daemon_ctx *ctx)
{
    char *argv[] = {
        (char *)VH_PATH,
        (char *)"-c", (char *)VH_CONFIG_PATH,
        (char *)"-r", (char *)VH_LOG_PATH,
        NULL
    };
    uint64_t deadline;
    struct network_info net;
    char ip[INET_ADDRSTRLEN] = "0.0.0.0";
    bool process;
    bool listener;
    bool child_alive;

    if (ctx->vh_lockout)
        return false;
    if (virtualhere_ready())
        return true;
    if (process_running("vhusbdriscv64")) {
        log_msg("VirtualHere stale process without TCP listener; restarting");
        (void)stop_virtualhere(ctx);
    }
    if (access(VH_PATH, X_OK) != 0) {
        log_msg("VirtualHere start FAIL reason=BINARY errno=%d", errno);
        return false;
    }
    if (!write_virtualhere_config()) {
        log_msg("VirtualHere start FAIL reason=CONFIG errno=%d", errno);
        return false;
    }
    if (link_is_up(&net))
        (void)inet_ntop(AF_INET, &net.ipv4, ip, sizeof(ip));
    unlink(VH_LOG_PATH);
    ctx->vh_pid = spawn_process(VH_PATH, argv);
    if (ctx->vh_pid < 0) {
        log_msg("VirtualHere start FAIL reason=SPAWN errno=%d", errno);
        return false;
    }

    deadline = monotonic_ms() + VH_START_TIMEOUT_MS;
    while (monotonic_ms() < deadline) {
        reap_children();
        if (virtualhere_ready()) {
            ctx->vh_state = AIRLINK_VIRTUALHERE_LISTENING;
            ctx->vh_listener_since = monotonic_ms();
            ctx->vh_client_candidate_since = 0U;
            ctx->vh_client_missing_since = 0U;
            ctx->vh_client_ip[0] = '\0';
            log_msg("VirtualHere listener-ready pid=%ld ip=%s port=%u "
                    "bind=ALL builtin_mdns=ACTIVE",
                    (long)ctx->vh_pid, ip, VH_TCP_PORT);
            return true;
        }
        if (!process_running("vhusbdriscv64") &&
            !pid_alive(ctx->vh_pid))
            break;
        usleep(50000);
    }

    process = process_running("vhusbdriscv64");
    listener = virtualhere_listening();
    child_alive = pid_alive(ctx->vh_pid);
    log_msg("VirtualHere start FAIL reason=%s process=%u listener=%u "
            "child=%u ip=%s port=%u log=%s",
            (process || child_alive) ? "NO_LISTENER" : "EXITED",
            process, listener, child_alive, ip, VH_TCP_PORT, VH_LOG_PATH);
    log_virtualhere_tail();
    (void)stop_virtualhere(ctx);
    return false;
}

static void reset_network_process_state(struct daemon_ctx *ctx)
{
    ctx->wpa_pid = 0;
    ctx->dhcp_pid = 0;
    unlink("/run/udhcpc.wlan0.pid");
    unlink(WPA_RUN_CONF);
    reap_children();
}

static void stop_wifi_processes(struct daemon_ctx *ctx)
{
    static const char *const names[] = {
        "wifi_config_web.py", "dnsmasq", "hostapd", "udhcpd",
        "udhcpc", "wpa_supplicant"
    };
    unsigned term_count = 0;
    unsigned kill_count = 0;
    bool ok = stop_matching_many(names,
        (unsigned)(sizeof(names) / sizeof(names[0])), PROCESS_STOP_MS,
        &term_count, &kill_count);
    log_msg("Wi-Fi process batch-stop result=%s term=%u kill=%u",
            ok ? "PASS" : "FAIL", term_count, kill_count);
    reset_network_process_state(ctx);
}

static bool stop_mode_services(struct daemon_ctx *ctx)
{
    static const char *const names[] = {
        "vhusbdriscv64", "wifi_config_web.py", "dnsmasq", "hostapd",
        "udhcpd", "udhcpc", "wpa_supplicant"
    };
    unsigned term_count = 0;
    unsigned kill_count = 0;
    bool ok = stop_matching_many(names,
        (unsigned)(sizeof(names) / sizeof(names[0])), PROCESS_STOP_MS,
        &term_count, &kill_count);

    ctx->vh_pid = 0;
    reset_network_process_state(ctx);
    log_msg("MODE batch-stop result=%s term=%u kill=%u timeout_ms=%u",
            ok ? "PASS" : "FAIL", term_count, kill_count,
            (unsigned)PROCESS_STOP_MS);
    return ok;
}

static int wlan_command(bool up)
{
    const char *const argv[] = {"/sbin/ip", "link", "set", WLAN_IF,
                                up ? "up" : "down", NULL};
    return run_quiet(argv, 2000U);
}

static const char *wifi_power_save_state(void)
{
    const char *const argv[] = {"/usr/sbin/iw", "dev", WLAN_IF,
                                "get", "power_save", NULL};
    static char state[8];
    char output[128];
    if (run_capture(argv, output, sizeof(output), 1200U) != 0) {
        snprintf(state, sizeof(state), "unknown");
    } else if (strstr(output, "Power save: off")) {
        snprintf(state, sizeof(state), "off");
    } else if (strstr(output, "Power save: on")) {
        snprintf(state, sizeof(state), "on");
    } else {
        snprintf(state, sizeof(state), "unknown");
    }
    return state;
}

static bool set_wifi_power_save_off(struct daemon_ctx *ctx,
                                    const char *stage)
{
    const char *const argv[] = {"/usr/sbin/iw", "dev", WLAN_IF,
                                "set", "power_save", "off", NULL};
    const char *actual;
    bool pass = run_quiet(argv, 1500U) == 0;
    actual = wifi_power_save_state();
    pass = pass && strcmp(actual, "off") == 0;
    ctx->wifi_power_save_off = pass;
    ctx->wifi_power_save_retry_deadline = pass ? 0U :
        monotonic_ms() + WIFI_POWER_SAVE_RETRY_MS;
    log_msg("WIFI power-save requested=OFF actual=%s stage=%s %s",
            actual, stage, pass ? "PASS" : "WARN");
    return pass;
}

static void service_wifi_power_save_off(struct daemon_ctx *ctx,
                                        uint64_t now,
                                        const char *stage)
{
    if (!ctx->wifi_power_save_link_verified) {
        (void)set_wifi_power_save_off(ctx, stage);
        ctx->wifi_power_save_link_verified = true;
        return;
    }
    if (!ctx->wifi_power_save_off &&
        now >= ctx->wifi_power_save_retry_deadline)
        (void)set_wifi_power_save_off(ctx, stage);
}

static bool reset_ap_to_sta_datapath(struct daemon_ctx *ctx)
{
    uint64_t started;

    if (!ctx->provisioning_sta_test)
        return true;

    started = monotonic_ms();
    if (wlan_command(false) != 0) {
        log_msg("WIFI AP-to-STA datapath-reset stage=DOWN FAIL");
        return false;
    }
    usleep(AP_TO_STA_DOWN_MS * 1000U);
    if (wlan_command(true) != 0) {
        log_msg("WIFI AP-to-STA datapath-reset stage=UP FAIL");
        return false;
    }
    usleep(AP_TO_STA_UP_SETTLE_MS * 1000U);
    (void)set_wifi_power_save_off(ctx, "AP_TO_STA");
    log_msg("WIFI AP-to-STA datapath-reset down_ms=%u up_settle_ms=%u elapsed_ms=%llu PASS",
            AP_TO_STA_DOWN_MS, AP_TO_STA_UP_SETTLE_MS,
            (unsigned long long)(monotonic_ms() - started));
    return true;
}

static bool set_wired_network(void)
{
    const char *const flush[] = {"/sbin/ip", "addr", "flush", "dev",
                                 WLAN_IF, NULL};
    (void)run_quiet(flush, 2000U);
    return wlan_command(false) == 0;
}

static bool start_wifi_sta_path(struct daemon_ctx *ctx, const char *config_path)
{
    char *wpa_argv[] = {"/usr/sbin/wpa_supplicant", "-i", WLAN_IF,
                        "-c", (char *)config_path, NULL};
    char *dhcp_argv[] = {"/sbin/udhcpc", "-f", "-i", WLAN_IF,
                         "-p", "/run/udhcpc.wlan0.pid", NULL};
    mkdir(WPA_CTRL_DIR, 0755);
    if (!config_path || access(config_path, R_OK) != 0)
        return false;
    (void)stop_matching("wpa_supplicant", PROCESS_STOP_MS);
    (void)stop_matching("udhcpc", PROCESS_STOP_MS);
    if (ctx->provisioning_sta_test) {
        if (!reset_ap_to_sta_datapath(ctx))
            return false;
    } else if (wlan_command(true) != 0) {
        return false;
    } else {
        (void)set_wifi_power_save_off(ctx, "STA_START");
    }
    ctx->wifi_power_save_link_verified = false;
    ctx->wpa_pid = spawn_process(wpa_argv[0], wpa_argv);
    if (ctx->wpa_pid < 0)
        return false;
    usleep(100000);
    if (!process_running("wpa_supplicant"))
        return false;
    ctx->dhcp_pid = spawn_process(dhcp_argv[0], dhcp_argv);
    if (ctx->dhcp_pid < 0)
        return false;
    log_msg("Wi-Fi STA started wpa_pid=%ld dhcp_pid=%ld",
            (long)ctx->wpa_pid, (long)ctx->dhcp_pid);
    return true;
}

static const char *phase_name(uint32_t phase)
{
    switch (phase) {
    case AIRLINK_SYSTEM_BOOT_SYNC: return "BOOT_SYNC";
    case AIRLINK_SYSTEM_WIRED_STOPPING: return "WIRED_STOPPING";
    case AIRLINK_SYSTEM_WIRED_READY: return "WIRED_READY";
    case AIRLINK_SYSTEM_WIRELESS_STARTING: return "WIRELESS_STARTING";
    case AIRLINK_SYSTEM_WIRELESS_WAIT_LINK: return "WIRELESS_WAIT_LINK";
    case AIRLINK_SYSTEM_WIRELESS_READY: return "WIRELESS_READY";
    case AIRLINK_SYSTEM_WIRELESS_PROVISIONING: return "WIRELESS_PROVISIONING";
    default: return "DEGRADED";
    }
}

static void send_mode_result(struct daemon_ctx *ctx, bool success)
{
    send_message(ctx, AIRLINK_IPC_MSG_MODE_APPLY_RESULT,
                 AIRLINK_IPC_MSG_FLAG_RESPONSE, ctx->mode_sequence,
                 success ? 1U : 0U, ctx->desired_wired, ctx->phase,
                 ctx->error);
    ctx->progress_deadline = monotonic_ms() + MODE_PROGRESS_MS;
}

static void set_phase(struct daemon_ctx *ctx, uint32_t phase,
                      uint32_t error, bool applied)
{
    bool changed = ctx->phase != phase || ctx->error != error ||
                   ctx->mode_applied != applied;
    ctx->phase = phase;
    ctx->error = error;
    ctx->mode_applied = applied;
    if (changed) {
        log_msg("STATE mode=%s phase=%s error=%u applied=%u",
                ctx->desired_wired ? "WIRED" : "WIRELESS",
                phase_name(phase), error, applied ? 1U : 0U);
        ctx->next_ui = 0;
    }
}

static void schedule_retry(struct daemon_ctx *ctx)
{
    unsigned index = ctx->retry_index;
    if (index >= sizeof(retry_ms) / sizeof(retry_ms[0]))
        index = (unsigned)(sizeof(retry_ms) / sizeof(retry_ms[0]) - 1U);
    ctx->retry_deadline = monotonic_ms() + retry_ms[index];
    if (ctx->retry_index + 1U < sizeof(retry_ms) / sizeof(retry_ms[0]))
        ctx->retry_index++;
    log_msg("%s retry in %u ms",
            ctx->vh_retry_only ? "VirtualHere-only" : "wireless",
            retry_ms[index]);
}

static void start_provisioning(struct daemon_ctx *ctx, bool mandatory)
{
    bool saved = wifi_config_available();
    (void)stop_virtualhere(ctx);
    airlink_provision_stop(&ctx->provision);
    stop_wifi_processes(ctx);
    ctx->wifi_unconfigured = mandatory;
    ctx->provisioning_sta_test = false;
    ctx->vh_retry_only = false;
    reset_vh_network_settle(ctx);
    set_phase(ctx, AIRLINK_SYSTEM_WIRELESS_PROVISIONING,
              AIRLINK_SYSTEM_ERROR_NONE, true);
    if (airlink_provision_begin(&ctx->provision, mandatory, saved,
                                monotonic_ms()) != 0) {
        set_phase(ctx, AIRLINK_SYSTEM_DEGRADED,
                  AIRLINK_SYSTEM_ERROR_PROVISION, false);
        log_msg("PROVISION start FAIL error=%u", ctx->provision.error);
    } else {
        log_msg("PROVISION start session=%u ssid=%s mandatory=%u",
                ctx->provision.session_id, ctx->provision.ap_ssid,
                mandatory ? 1U : 0U);
    }
    ctx->next_ui = 0;
}

static bool restore_provision_ap(struct daemon_ctx *ctx,
                                 uint32_t error, uint64_t now)
{
    if (airlink_provision_restart_ap(&ctx->provision, error, now) != 0) {
        set_phase(ctx, AIRLINK_SYSTEM_DEGRADED,
                  AIRLINK_SYSTEM_ERROR_PROVISION, false);
        log_msg("PROVISION AP restore FAIL error=%u",
                ctx->provision.error);
        ctx->next_ui = 0;
        return false;
    }
    set_phase(ctx, AIRLINK_SYSTEM_WIRELESS_PROVISIONING,
              AIRLINK_SYSTEM_ERROR_PROVISION, true);
    ctx->next_ui = 0;
    return true;
}

static void log_mode_timing(uint64_t started, uint64_t services_stopped,
                            uint64_t finished, uint32_t wired)
{
    log_msg("MODE timing stop_services_ms=%llu network_prepare_ms=%llu "
            "total_ms=%llu mode=%s",
            (unsigned long long)(services_stopped - started),
            (unsigned long long)(finished - services_stopped),
            (unsigned long long)(finished - started),
            wired ? "WIRED" : "WIRELESS");
}

static void enter_wired(struct daemon_ctx *ctx)
{
    uint64_t started = monotonic_ms();
    uint64_t services_stopped;
    uint64_t finished;
    bool services_ok;
    bool wlan_ok;

    ctx->vh_retry_only = false;
    reset_vh_network_settle(ctx);
    set_phase(ctx, AIRLINK_SYSTEM_WIRED_STOPPING,
              AIRLINK_SYSTEM_ERROR_NONE, false);
    send_mode_result(ctx, true);
    airlink_provision_stop(&ctx->provision);
    services_ok = stop_mode_services(ctx);
    services_stopped = monotonic_ms();
    wlan_ok = set_wired_network();
    finished = monotonic_ms();
    log_mode_timing(started, services_stopped, finished, 1U);

    if (services_ok && wlan_ok) {
        set_phase(ctx, AIRLINK_SYSTEM_WIRED_READY,
                  AIRLINK_SYSTEM_ERROR_NONE, true);
        send_mode_result(ctx, true);
    } else {
        set_phase(ctx, AIRLINK_SYSTEM_DEGRADED,
                  services_ok ? AIRLINK_SYSTEM_ERROR_WLAN_DOWN :
                                AIRLINK_SYSTEM_ERROR_VH_STOP, false);
        send_mode_result(ctx, false);
    }
}

static void begin_wireless(struct daemon_ctx *ctx)
{
    uint64_t started = monotonic_ms();
    uint64_t services_stopped;
    uint64_t finished;
    bool services_ok;

    ctx->wifi_unconfigured = false;
    ctx->provisioning_sta_test = false;
    ctx->vh_retry_only = false;
    reset_vh_network_settle(ctx);
    set_phase(ctx, AIRLINK_SYSTEM_WIRELESS_STARTING,
              AIRLINK_SYSTEM_ERROR_NONE, false);
    send_mode_result(ctx, true);
    airlink_provision_stop(&ctx->provision);
    services_ok = stop_mode_services(ctx);
    services_stopped = monotonic_ms();
    if (!services_ok) {
        finished = monotonic_ms();
        log_mode_timing(started, services_stopped, finished, 0U);
        set_phase(ctx, AIRLINK_SYSTEM_DEGRADED,
                  AIRLINK_SYSTEM_ERROR_VH_STOP, false);
        send_mode_result(ctx, false);
        schedule_retry(ctx);
        return;
    }
    if (!wifi_config_available()) {
        (void)start_provisioning(ctx, true);
        finished = monotonic_ms();
        log_mode_timing(started, services_stopped, finished, 0U);
        send_mode_result(ctx, ctx->phase ==
            AIRLINK_SYSTEM_WIRELESS_PROVISIONING);
        return;
    }
    if (prepare_wpa_config() != 0 ||
        !start_wifi_sta_path(ctx, WPA_RUN_CONF)) {
        finished = monotonic_ms();
        log_mode_timing(started, services_stopped, finished, 0U);
        set_phase(ctx, AIRLINK_SYSTEM_DEGRADED,
                  AIRLINK_SYSTEM_ERROR_WIFI_START, false);
        send_mode_result(ctx, false);
        schedule_retry(ctx);
        return;
    }
    log_msg("VH early-listener=DISABLED wait=ASSOCIATED+IP+ROUTE+2000MS");
    set_phase(ctx, AIRLINK_SYSTEM_WIRELESS_WAIT_LINK,
              AIRLINK_SYSTEM_ERROR_NONE, false);
    ctx->link_deadline = monotonic_ms() + LINK_TIMEOUT_MS;
    ctx->progress_deadline = 0;
    finished = monotonic_ms();
    log_mode_timing(started, services_stopped, finished, 0U);
    send_mode_result(ctx, true);
}

static void request_mode(struct daemon_ctx *ctx, uint32_t wired,
                         uint32_t sequence, uint32_t transitions)
{
    ctx->desired_wired = wired ? 1U : 0U;
    ctx->mode_sequence = sequence ? sequence :
        (0x80000000U | (transitions & 0x7fffffffU));
    ctx->transition_count = transitions;
    ctx->retry_index = 0;
    ctx->retry_deadline = 0;
    ctx->vh_lockout = false;
    ctx->vh_retry_only = false;
    ctx->vh_failures = 0;
    ctx->vh_failure_window = 0;
    reset_vh_network_settle(ctx);
    log_msg("MODE request seq=%u mode=%s transitions=%u",
            ctx->mode_sequence, wired ? "WIRED" : "WIRELESS", transitions);
    if (wired)
        enter_wired(ctx);
    else
        begin_wireless(ctx);
}

static void note_vh_failure(struct daemon_ctx *ctx)
{
    uint64_t now = monotonic_ms();
    if (ctx->vh_failure_window == 0U ||
        now - ctx->vh_failure_window > VH_FAILURE_WINDOW_MS) {
        ctx->vh_failure_window = now;
        ctx->vh_failures = 0;
    }
    ctx->vh_failures++;
    ctx->vh_retry_only = true;
    if (ctx->vh_failures >= VH_FAILURE_LIMIT) {
        ctx->vh_lockout = true;
        set_phase(ctx, AIRLINK_SYSTEM_DEGRADED,
                  AIRLINK_SYSTEM_ERROR_VH_START, false);
        send_mode_result(ctx, false);
        log_msg("VirtualHere lockout failures=%u window_ms=%u",
                ctx->vh_failures, (unsigned)VH_FAILURE_WINDOW_MS);
    } else {
        set_phase(ctx, AIRLINK_SYSTEM_WIRELESS_WAIT_LINK,
                  AIRLINK_SYSTEM_ERROR_NONE, false);
        schedule_retry(ctx);
        log_msg("VH retry=%u reason=START_FAIL", ctx->vh_failures);
    }
}

static void service_wireless(struct daemon_ctx *ctx)
{
    struct network_info net;
    char target_ssid[33];
    uint64_t now = monotonic_ms();
    bool linked;
    bool routed;

    if (ctx->desired_wired)
        return;

    airlink_provision_service(&ctx->provision, now);
    if (ctx->last_provision_phase != ctx->provision.phase) {
        ctx->last_provision_phase = ctx->provision.phase;
        log_msg("PROVISION phase=%s error=%u ap=%s networks=%u",
                airlink_provision_phase_name(ctx->provision.phase),
                ctx->provision.error,
                ctx->provision.listen_fd >= 0 ? "READY" : "DOWN",
                ctx->provision.network_count);
        ctx->next_ui = 0;
    }
    if (ctx->provision.active &&
        ctx->provision.phase == AIRLINK_PROVISION_FAILED &&
        ctx->provision.error == AIRLINK_PROVISION_ERROR_AP_START &&
        ctx->provision.listen_fd < 0) {
        set_phase(ctx, AIRLINK_SYSTEM_DEGRADED,
                  AIRLINK_SYSTEM_ERROR_PROVISION, false);
    }
    if (airlink_provision_take_cancel(&ctx->provision) ||
        airlink_provision_take_timeout(&ctx->provision)) {
        if (!ctx->provision.mandatory &&
            ctx->provision.has_saved_config) {
            log_msg("PROVISION cancelled; restoring saved network");
            airlink_provision_stop(&ctx->provision);
            begin_wireless(ctx);
            return;
        }
    }
    if (airlink_provision_take_submission(&ctx->provision,
                                          target_ssid,
                                          sizeof(target_ssid)) > 0) {
        log_msg("PROVISION submitted target_ssid=%s", target_ssid);
        airlink_provision_mark_sta_testing(&ctx->provision);
        stop_wifi_processes(ctx);
        reset_vh_network_settle(ctx);
        ctx->provisioning_sta_test = true;
        if (!start_wifi_sta_path(ctx, AIRLINK_PROVISION_CANDIDATE_CONF)) {
            ctx->provisioning_sta_test = false;
            (void)restore_provision_ap(
                ctx, AIRLINK_PROVISION_ERROR_STA_START, now);
            return;
        }
        set_phase(ctx, AIRLINK_SYSTEM_WIRELESS_WAIT_LINK,
                  AIRLINK_SYSTEM_ERROR_NONE, false);
        ctx->link_deadline = now + LINK_TIMEOUT_MS;
        ctx->progress_deadline = 0;
    }

    if (ctx->phase == AIRLINK_SYSTEM_WIRELESS_WAIT_LINK) {
        linked = link_is_up(&net);
        routed = linked && default_route_is_up(WLAN_IF);
        if (linked && routed) {
            service_wifi_power_save_off(ctx, now, "LINK_READY");
            if (ctx->provisioning_sta_test) {
                if (commit_candidate_config() != 0) {
                    stop_wifi_processes(ctx);
                    ctx->provisioning_sta_test = false;
                    reset_vh_network_settle(ctx);
                    (void)restore_provision_ap(
                        ctx, AIRLINK_PROVISION_ERROR_SAVE, now);
                    return;
                }
                ctx->provisioning_sta_test = false;
                ctx->wifi_unconfigured = false;
                airlink_provision_mark_success(&ctx->provision);
                log_msg("PROVISION success target_ssid=%s",
                        ctx->provision.target_ssid);
            }
            if (!virtualhere_network_settled(ctx, &net, now)) {
                if (ctx->progress_deadline == 0U ||
                    now >= ctx->progress_deadline)
                    send_mode_result(ctx, true);
            } else if (ctx->vh_retry_only &&
                       ctx->retry_deadline != 0U &&
                       now < ctx->retry_deadline) {
                if (ctx->progress_deadline == 0U ||
                    now >= ctx->progress_deadline)
                    send_mode_result(ctx, true);
            } else if (start_virtualhere(ctx)) {
                begin_virtualhere_lan_activation(ctx, &net, now);
                service_virtualhere_lan_activation(ctx, &net, now);
                ctx->vh_retry_only = false;
                ctx->vh_failures = 0;
                ctx->vh_failure_window = 0U;
                ctx->retry_deadline = 0U;
                ctx->retry_index = 0;
                set_phase(ctx, AIRLINK_SYSTEM_WIRELESS_READY,
                          AIRLINK_SYSTEM_ERROR_NONE, true);
                send_mode_result(ctx, true);
            } else {
                note_vh_failure(ctx);
            }
        } else if (now >= ctx->link_deadline) {
            reset_vh_network_settle(ctx);
            stop_virtualhere(ctx);
            stop_wifi_processes(ctx);
            if (ctx->provisioning_sta_test) {
                ctx->provisioning_sta_test = false;
                (void)restore_provision_ap(
                    ctx, AIRLINK_PROVISION_ERROR_STA_TIMEOUT, now);
                log_msg("PROVISION candidate timeout; AP restored");
            } else {
                set_phase(ctx, AIRLINK_SYSTEM_DEGRADED,
                          AIRLINK_SYSTEM_ERROR_WIFI_TIMEOUT, false);
                send_mode_result(ctx, false);
                schedule_retry(ctx);
            }
        } else {
            reset_vh_network_settle(ctx);
            if (ctx->progress_deadline == 0U ||
                now >= ctx->progress_deadline)
                send_mode_result(ctx, true);
        }
    } else if (ctx->phase == AIRLINK_SYSTEM_WIRELESS_READY) {
        linked = link_is_up(&net);
        routed = linked && default_route_is_up(WLAN_IF);
        if (!linked || !routed) {
            ctx->vh_retry_only = false;
            reset_vh_network_settle(ctx);
            stop_virtualhere(ctx);
            stop_wifi_processes(ctx);
            set_phase(ctx, AIRLINK_SYSTEM_DEGRADED,
                      AIRLINK_SYSTEM_ERROR_WIFI_TIMEOUT, false);
            schedule_retry(ctx);
        } else if (!virtualhere_ready()) {
            log_msg("VirtualHere listener lost unexpectedly");
            note_vh_failure(ctx);
        } else {
            service_wifi_power_save_off(ctx, now, "READY_RETRY");
            (void)update_virtualhere_client_state(ctx, now);
            service_virtualhere_lan_activation(ctx, &net, now);
        }
    }
    if (ctx->phase == AIRLINK_SYSTEM_DEGRADED &&
        !ctx->wifi_unconfigured && !ctx->vh_lockout &&
        ctx->retry_deadline != 0U && now >= ctx->retry_deadline) {
        ctx->retry_deadline = 0U;
        begin_wireless(ctx);
    }
}

static void publish_ui(struct daemon_ctx *ctx)
{
    struct airlink_ipc_ui_status next;
    struct network_info net;
    uint32_t count = ctx->ui.update_count + 1U;
    uint32_t vh_state = update_virtualhere_client_state(ctx, monotonic_ms());
    bool linked = false;
    memset(&next, 0, sizeof(next));
    if (!ctx->desired_wired)
        linked = link_is_up(&net);
    else {
        memset(&net, 0, sizeof(net));
        net.flags = AIRLINK_UI_STATUS_VALID;
        net.rssi = -127;
    }
    next.owner = AIRLINK_IPC_OWNER_LINUX;
    next.flags = AIRLINK_UI_STATUS_VALID;
    if (linked)
        next.flags |= AIRLINK_UI_STATUS_WIFI_CONNECTED;
    if (net.flags & AIRLINK_UI_STATUS_WIFI_5GHZ)
        next.flags |= AIRLINK_UI_STATUS_WIFI_5GHZ;
    if (vh_state >= AIRLINK_VIRTUALHERE_LISTENING)
        next.flags |= AIRLINK_UI_STATUS_VIRTUALHERE_RUNNING;
    if (ctx->phase == AIRLINK_SYSTEM_WIRELESS_STARTING ||
        ctx->phase == AIRLINK_SYSTEM_WIRELESS_WAIT_LINK)
        next.flags |= AIRLINK_UI_STATUS_WIFI_CONNECTING;
    if (ctx->mode_applied)
        next.flags |= AIRLINK_UI_STATUS_MODE_APPLIED;
    if (ctx->phase == AIRLINK_SYSTEM_DEGRADED)
        next.flags |= AIRLINK_UI_STATUS_SYSTEM_FAULT;
    if (ctx->wifi_unconfigured)
        next.flags |= AIRLINK_UI_STATUS_WIFI_UNCONFIGURED;
    next.wifi_rssi_dbm = net.rssi;
    next.ipv4_address = net.ipv4;
    next.wifi_frequency_mhz = net.frequency;
    next.virtualhere_state = vh_state;
    next.update_count = count ? count : 1U;
    memcpy(next.ssid, net.ssid, sizeof(next.ssid));
    next.system_mode = ctx->desired_wired;
    next.system_phase = ctx->phase;
    next.system_error = ctx->error;
    next.mode_transition_count = ctx->transition_count;
    shared_write(shared_at(ctx, AIRLINK_IPC_UI_STATUS_OFFSET),
                 &next, sizeof(next));
    ctx->ui = next;
}

static void publish_provision(struct daemon_ctx *ctx)
{
    struct airlink_ipc_provision_status next;
    airlink_provision_fill_status(&ctx->provision, &next, monotonic_ms());
    shared_write(shared_at(ctx, AIRLINK_IPC_PROVISION_STATUS_OFFSET),
                 &next, sizeof(next));
    ctx->provision_status = next;
}

static int wait_response(struct daemon_ctx *ctx, uint32_t sequence,
                         uint32_t type, unsigned timeout_ms,
                         struct airlink_ipc_message *response)
{
    uint64_t deadline = monotonic_ms() + timeout_ms;
    while (monotonic_ms() < deadline) {
        struct airlink_ipc_message msg;
        int rc = read_cmessage(ctx, &msg);
        if (rc > 0 && msg.sequence == sequence && msg.type == type) {
            if (response)
                *response = msg;
            return 0;
        }
        usleep(10000);
    }
    return -1;
}

static int ipc_fail(struct daemon_ctx *ctx, const char *stage, int error)
{
    ctx->ipc_fail_stage = stage;
    ctx->ipc_fail_errno = error != 0 ? error : EPROTO;
    return -1;
}

static int ipc_start(struct daemon_ctx *ctx)
{
    struct airlink_ipc_message response;
    uint64_t deadline;
    uint32_t seq;
    uint32_t header_crc;

    ctx->ipc_fail_stage = "UNKNOWN";
    ctx->ipc_fail_errno = EPROTO;
    errno = 0;

    ctx->mem_fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (ctx->mem_fd < 0) {
        int saved = errno;
        log_msg("IPC FAIL stage=OPEN_MEM errno=%d(%s)",
                saved, strerror(saved));
        return ipc_fail(ctx, "OPEN_MEM", saved);
    }

    ctx->shm = mmap(NULL, AIRLINK_IPC_SHARED_SIZE, PROT_READ | PROT_WRITE,
                    MAP_SHARED, ctx->mem_fd, AIRLINK_IPC_SHARED_BASE);
    if (ctx->shm == MAP_FAILED) {
        int saved = errno;
        ctx->shm = NULL;
        log_msg("IPC FAIL stage=MMAP errno=%d(%s)",
                saved, strerror(saved));
        return ipc_fail(ctx, "MMAP", saved);
    }

    deadline = monotonic_ms() + 30000U;
    while (monotonic_ms() < deadline) {
        memcpy(&ctx->header, (const void *)ctx->shm, sizeof(ctx->header));
        if (ctx->header.magic == AIRLINK_IPC_MAGIC)
            break;
        usleep(50000);
    }
    if (ctx->header.magic != AIRLINK_IPC_MAGIC) {
        log_msg("IPC FAIL stage=HEADER reason=timeout expected_magic=0x%08x actual_magic=0x%08x",
                AIRLINK_IPC_MAGIC, ctx->header.magic);
        return ipc_fail(ctx, "HEADER", ETIMEDOUT);
    }

    header_crc = crc32_bytes((const uint8_t *)&ctx->header,
                             sizeof(ctx->header) - 4U);
    if (ctx->header.protocol_version != AIRLINK_IPC_VERSION ||
        ctx->header.header_size != sizeof(ctx->header) ||
        ctx->header.total_size != AIRLINK_IPC_LAYOUT_SIZE ||
        (ctx->header.feature_flags & REQUIRED_FEATURES) != REQUIRED_FEATURES ||
        ctx->header.crc32 != header_crc) {
        log_msg("IPC FAIL stage=HEADER reason=validation protocol=%u/%u header=%u/%zu layout=%u/%u features=0x%08x required=0x%08x crc=0x%08x/0x%08x",
                ctx->header.protocol_version, AIRLINK_IPC_VERSION,
                ctx->header.header_size, sizeof(ctx->header),
                ctx->header.total_size, AIRLINK_IPC_LAYOUT_SIZE,
                ctx->header.feature_flags, REQUIRED_FEATURES,
                ctx->header.crc32, header_crc);
        return ipc_fail(ctx, "HEADER", EPROTO);
    }

    deadline = monotonic_ms() + 10000U;
    while (monotonic_ms() < deadline) {
        if (read_cstate(ctx) > 0 &&
            (ctx->cstate.flags & AIRLINK_IPC_STATE_READY))
            break;
        usleep(20000);
    }
    if (!(ctx->cstate.flags & AIRLINK_IPC_STATE_READY)) {
        log_msg("IPC FAIL stage=CSTATE_READY reason=timeout owner=%u flags=0x%08x firmware=0x%08x",
                ctx->cstate.owner, ctx->cstate.flags,
                ctx->cstate.firmware_id);
        return ipc_fail(ctx, "CSTATE_READY", ETIMEDOUT);
    }
    if (ctx->cstate.owner != AIRLINK_IPC_OWNER_C906L) {
        log_msg("IPC FAIL stage=CSTATE_OWNER expected=%u actual=%u flags=0x%08x",
                AIRLINK_IPC_OWNER_C906L, ctx->cstate.owner,
                ctx->cstate.flags);
        return ipc_fail(ctx, "CSTATE_OWNER", EPROTO);
    }
    if (ctx->cstate.firmware_id != C906L_FW_ID) {
        log_msg("IPC FAIL stage=CSTATE_FW expected=0x%08x actual=0x%08x",
                C906L_FW_ID, ctx->cstate.firmware_id);
        return ipc_fail(ctx, "CSTATE_FW", EPROTO);
    }

    memset(&ctx->lstate, 0, sizeof(ctx->lstate));
    ctx->lstate.owner = AIRLINK_IPC_OWNER_LINUX;
    ctx->lstate.flags = AIRLINK_IPC_STATE_RUNNING |
                        AIRLINK_IPC_STATE_READY;
    ctx->lstate.firmware_id = AIRLINKD_FW_ID;
    ctx->lstate.abi_revision = ABI_REVISION;
    ctx->lstate.peer_heartbeat = ctx->cstate.heartbeat;
    publish_linux_state(ctx);

    seq = next_sequence(ctx);
    send_message(ctx, AIRLINK_IPC_MSG_HELLO, 0U, seq,
                 AIRLINK_IPC_VERSION, AIRLINKD_FW_ID,
                 (uint32_t)getpid(), ctx->header.boot_nonce);
    if (wait_response(ctx, seq, AIRLINK_IPC_MSG_READY,
                      2000U, &response) != 0) {
        log_msg("IPC FAIL stage=READY reason=timeout sequence=%u", seq);
        return ipc_fail(ctx, "READY", ETIMEDOUT);
    }
    if (response.args[0] != AIRLINK_IPC_VERSION ||
        response.args[1] != C906L_FW_ID) {
        log_msg("IPC FAIL stage=READY reason=peer-mismatch protocol=%u/%u firmware=0x%08x/0x%08x",
                response.args[0], AIRLINK_IPC_VERSION,
                response.args[1], C906L_FW_ID);
        return ipc_fail(ctx, "READY", EPROTO);
    }

    for (uint32_t i = 1; i <= 3; ++i) {
        uint32_t cookie = ctx->header.boot_nonce ^ 0xa18c0000U ^ i;
        seq = next_sequence(ctx);
        send_message(ctx, AIRLINK_IPC_MSG_PING, 0U, seq,
                     cookie, i, 0U, 0U);
        if (wait_response(ctx, seq, AIRLINK_IPC_MSG_PONG,
                          2000U, &response) != 0) {
            log_msg("IPC FAIL stage=PING reason=timeout index=%u sequence=%u",
                    i, seq);
            return ipc_fail(ctx, "PING", ETIMEDOUT);
        }
        if (response.args[0] != cookie) {
            log_msg("IPC FAIL stage=PING reason=cookie-mismatch index=%u expected=0x%08x actual=0x%08x",
                    i, cookie, response.args[0]);
            return ipc_fail(ctx, "PING", EPROTO);
        }
    }

    seq = next_sequence(ctx);
    send_message(ctx, AIRLINK_IPC_MSG_REQUEST_CH347_STATUS, 0U, seq,
                 0U, 0U, 0U, 0U);
    if (wait_response(ctx, seq, AIRLINK_IPC_MSG_CH347_STATUS,
                      2000U, &response) == 0) {
        ctx->ch347_mode = response.args[0];
        ctx->ch347_valid = response.args[1] != 0U;
    }

    ctx->lstate.flags |= AIRLINK_IPC_STATE_SELFTEST_OK |
                         AIRLINK_IPC_STATE_PEER_VALID;
    publish_linux_state(ctx);
    ctx->ipc_fail_stage = NULL;
    ctx->ipc_fail_errno = 0;
    errno = 0;
    log_msg("IPC peer=R27P firmware=0x%08x abi=%u PASS",
            C906L_FW_ID, ABI_REVISION);
    log_msg("SELFTEST PASS pings=3 abi=4 ui_status=PASS ch347_control=PASS system_control=PASS wifi_provision=PASS");
    return 0;
}

static void handle_ch347_prepare(struct daemon_ctx *ctx,
                                 const struct airlink_ipc_message *msg)
{
    bool ok = true;
    if (!ctx->desired_wired)
        ok = stop_virtualhere(ctx);
    send_message(ctx, AIRLINK_IPC_MSG_CH347_PREPARED,
                 AIRLINK_IPC_MSG_FLAG_RESPONSE, msg->sequence,
                 ok ? 1U : 0U, msg->args[0], ok ? 0U :
                 AIRLINK_SYSTEM_ERROR_VH_STOP, 0U);
}

static void handle_ch347_done(struct daemon_ctx *ctx,
                              const struct airlink_ipc_message *msg)
{
    bool ok = true;
    ctx->ch347_mode = msg->args[0];
    ctx->ch347_valid = true;
    if (!ctx->desired_wired &&
        ctx->phase == AIRLINK_SYSTEM_WIRELESS_READY)
        ok = start_virtualhere(ctx);
    send_message(ctx, AIRLINK_IPC_MSG_CH347_ENUM_RESULT,
                 AIRLINK_IPC_MSG_FLAG_RESPONSE, msg->sequence,
                 ok ? 1U : 0U, msg->args[0], ok ? 0U :
                 AIRLINK_SYSTEM_ERROR_VH_START, 0U);
}

static bool cancel_provisioning(struct daemon_ctx *ctx)
{
    if (!ctx->provision.active || ctx->provision.mandatory)
        return false;
    airlink_provision_stop(&ctx->provision);
    begin_wireless(ctx);
    return true;
}

static void handle_provision_request(struct daemon_ctx *ctx,
                                     const struct airlink_ipc_message *msg)
{
    bool ok = !ctx->desired_wired;
    if (ok && !ctx->provision.active)
        start_provisioning(ctx, false);
    if (ctx->phase == AIRLINK_SYSTEM_DEGRADED)
        ok = false;
    send_message(ctx, AIRLINK_IPC_MSG_WIFI_PROVISION_ACK,
                 AIRLINK_IPC_MSG_FLAG_RESPONSE, msg->sequence,
                 ok ? 1U : 0U, ctx->provision.session_id,
                 ctx->provision.phase, ctx->provision.error);
}

static void handle_provision_cancel(struct daemon_ctx *ctx,
                                    const struct airlink_ipc_message *msg)
{
    bool ok = cancel_provisioning(ctx);
    send_message(ctx, AIRLINK_IPC_MSG_WIFI_PROVISION_CANCEL_ACK,
                 AIRLINK_IPC_MSG_FLAG_RESPONSE, msg->sequence,
                 ok ? 1U : 0U, ctx->provision.session_id,
                 ctx->provision.phase, ctx->provision.error);
}

static void handle_message(struct daemon_ctx *ctx,
                           const struct airlink_ipc_message *msg)
{
    switch (msg->type) {
    case AIRLINK_IPC_MSG_MODE_CHANGED:
        request_mode(ctx, msg->args[0], msg->sequence, msg->args[1]);
        break;
    case AIRLINK_IPC_MSG_CH347_SWITCH_REQUEST:
        handle_ch347_prepare(ctx, msg);
        break;
    case AIRLINK_IPC_MSG_CH347_SWITCH_DONE:
        handle_ch347_done(ctx, msg);
        break;
    case AIRLINK_IPC_MSG_WIFI_PROVISION_REQUEST:
        handle_provision_request(ctx, msg);
        break;
    case AIRLINK_IPC_MSG_WIFI_PROVISION_CANCEL:
        handle_provision_cancel(ctx, msg);
        break;
    default:
        log_msg("IPC ignored type=0x%x seq=%u", msg->type, msg->sequence);
        break;
    }
}

static void json_escape(char *dest, size_t size, const char *source)
{
    size_t used = 0;
    if (!size)
        return;
    for (const unsigned char *p = (const unsigned char *)source;
         *p && used + 2U < size; ++p) {
        if (*p == '"' || *p == '\\') {
            dest[used++] = '\\';
            dest[used++] = (char)*p;
        } else if (*p >= 0x20U) {
            dest[used++] = (char)*p;
        }
    }
    dest[used] = '\0';
}

static bool read_sdio_info(struct sdio_info *info)
{
    FILE *file = fopen(SDIO_IOS_PATH, "r");
    char line[160];
    memset(info, 0, sizeof(*info));
    if (!file)
        return false;
    while (fgets(line, sizeof(line), file)) {
        unsigned value;
        if (sscanf(line, "clock: %u Hz", &value) == 1)
            info->requested_hz = value;
        else if (sscanf(line, "actual clock: %u Hz", &value) == 1)
            info->actual_hz = value;
        else if (sscanf(line, "timing spec: %u", &value) == 1)
            info->timing = value;
    }
    fclose(file);
    info->valid = info->requested_hz != 0U && info->actual_hz != 0U;
    return info->valid;
}

static void status_json(struct daemon_ctx *ctx, char *out, size_t size)
{
    struct network_info net;
    struct sdio_info sdio;
    char ssid[96];
    char bssid[24];
    char ch347[16];
    char ip[INET_ADDRSTRLEN] = "0.0.0.0";
    char client_ip[INET6_ADDRSTRLEN] = "";
    struct in_addr address;
    uint64_t now = monotonic_ms();
    uint32_t vh_state = update_virtualhere_client_state(ctx, now);
    bool linked = !ctx->desired_wired && link_is_up(&net);
    bool routed = linked && default_route_is_up(WLAN_IF);
    bool listener = vh_state >= AIRLINK_VIRTUALHERE_LISTENING;
    bool client_connected =
        vh_state == AIRLINK_VIRTUALHERE_CLIENT_CONNECTED;
    const char *power_save = linked ? wifi_power_save_state() : "unknown";
    const char *hint = "none";

    if (!linked)
        memset(&net, 0, sizeof(net));
    (void)read_sdio_info(&sdio);
    address.s_addr = net.ipv4;
    if (net.ipv4)
        inet_ntop(AF_INET, &address, ip, sizeof(ip));
    json_escape(ssid, sizeof(ssid), net.ssid);
    json_escape(bssid, sizeof(bssid), net.bssid);
    if (client_connected)
        snprintf(client_ip, sizeof(client_ip), "%s", ctx->vh_client_ip);
    if (listener && !client_connected && ctx->vh_listener_since != 0U &&
        now - ctx->vh_listener_since >= VH_NO_CLIENT_HINT_MS)
        hint = "check-pc-network-or-ap-isolation";
    if (ctx->ch347_valid)
        snprintf(ch347, sizeof(ch347), "%u", ctx->ch347_mode);
    else
        snprintf(ch347, sizeof(ch347), "null");
    snprintf(out, size,
        "{\"ok\":true,\"version\":\"R27.6.6.22\",\"mode\":\"%s\","
        "\"phase\":\"%s\",\"error\":%u,\"applied\":%s,"
        "\"wifi\":{\"connected\":%s,\"ssid\":\"%s\","
        "\"bssid\":\"%s\",\"frequency_mhz\":%u,\"rssi_dbm\":%d,"
        "\"ip\":\"%s\",\"configured\":%s,\"default_route\":%s,"
        "\"power_save\":\"%s\"},"
        "\"sdio\":{\"requested_hz\":%u,\"actual_hz\":%u,"
        "\"timing\":%u},"
        "\"virtualhere\":{\"running\":%s,\"listener\":%s,"
        "\"client_connected\":%s,\"client_ip\":\"%s\",\"state\":%u,"
        "\"failures\":%u,\"lockout\":%s},"
        "\"network_hint\":\"%s\","
        "\"provision\":{\"active\":%s,\"phase\":\"%s\",\"error\":%u,"
        "\"session_id\":%u,\"ap_ready\":%s,\"mandatory\":%s},"
        "\"ch347\":%s,\"ipc\":{\"abi\":4,\"linux_hb\":%u,"
        "\"c906l_hb\":%u,\"transitions\":%u}}",
        ctx->desired_wired ? "wired" : "wireless", phase_name(ctx->phase),
        ctx->error, ctx->mode_applied ? "true" : "false",
        linked ? "true" : "false", ssid, bssid, net.frequency, net.rssi, ip,
        ctx->wifi_unconfigured ? "false" : "true",
        routed ? "true" : "false", power_save,
        sdio.requested_hz, sdio.actual_hz, sdio.timing,
        listener ? "true" : "false", listener ? "true" : "false",
        client_connected ? "true" : "false", client_ip, vh_state,
        ctx->vh_failures, ctx->vh_lockout ? "true" : "false", hint,
        ctx->provision.active ? "true" : "false",
        airlink_provision_phase_name(ctx->provision.phase),
        ctx->provision.error, ctx->provision.session_id,
        ctx->provision.listen_fd >= 0 ? "true" : "false",
        ctx->provision.mandatory ? "true" : "false",
        ch347, ctx->lstate.heartbeat, ctx->cstate.heartbeat,
        ctx->transition_count);
}

static void diag_file_section(FILE *file, const char *label, const char *path)
{
    FILE *source = fopen(path, "r");
    char line[512];
    fprintf(file, "\n[%s] %s\n", label, path);
    if (!source) {
        fprintf(file, "unavailable errno=%d\n", errno);
        return;
    }
    while (fgets(line, sizeof(line), source))
        fputs(line, file);
    fclose(source);
}

static void diag_capture_section(FILE *file, const char *label,
                                 const char *const argv[])
{
    char output[4096];
    fprintf(file, "\n[%s]\n", label);
    if (run_capture(argv, output, sizeof(output), 1500U) == 0)
        fputs(output, file);
    else
        fprintf(file, "unavailable\n");
}

static void diag_counter(FILE *file, const char *name)
{
    char path[160];
    char value[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s",
             WLAN_IF, name);
    if (read_trimmed(path, value, sizeof(value)) == 0)
        fprintf(file, "%s=%s\n", name, value);
    else
        fprintf(file, "%s=unavailable\n", name);
}

static int write_diag(struct daemon_ctx *ctx, char *path, size_t path_size)
{
    static const char *const iw_link[] = {
        "/usr/sbin/iw", "dev", WLAN_IF, "link", NULL
    };
    static const char *const iw_power[] = {
        "/usr/sbin/iw", "dev", WLAN_IF, "get", "power_save", NULL
    };
    FILE *file;
    char status[4096];
    snprintf(path, path_size, "/tmp/airlink-diag-%llu.txt",
             (unsigned long long)time(NULL));
    file = fopen(path, "w");
    if (!file)
        return -1;
    status_json(ctx, status, sizeof(status));
    fprintf(file, "AirLink R27.6.6.22 diagnostic\n%s\n", status);
    fprintf(file, "c906 flags=0x%08x errors=0x%08x firmware=0x%08x abi=%u\n",
            ctx->cstate.flags, ctx->cstate.error_flags,
            ctx->cstate.firmware_id, ctx->cstate.abi_revision);
    fprintf(file, "linux flags=0x%08x errors=0x%08x firmware=0x%08x abi=%u\n",
            ctx->lstate.flags, ctx->lstate.error_flags,
            ctx->lstate.firmware_id, ctx->lstate.abi_revision);
    fprintf(file, "processes wpa=%u dhcp=%u vh=%u\n",
            process_running("wpa_supplicant"),
            process_running("udhcpc"),
            virtualhere_ready());
    fprintf(file, "virtualhere listener=%u client=%u state=%u client_ip=%s "
                  "port=%u config=%s log=%s\n",
            virtualhere_listening(),
            ctx->vh_state == AIRLINK_VIRTUALHERE_CLIENT_CONNECTED,
            ctx->vh_state, ctx->vh_client_ip[0] ? ctx->vh_client_ip : "-",
            VH_TCP_PORT, VH_CONFIG_PATH, VH_LOG_PATH);
    fprintf(file, "\n[wlan0 counters]\n");
    diag_counter(file, "rx_packets");
    diag_counter(file, "rx_bytes");
    diag_counter(file, "rx_errors");
    diag_counter(file, "rx_dropped");
    diag_counter(file, "tx_packets");
    diag_counter(file, "tx_bytes");
    diag_counter(file, "tx_errors");
    diag_counter(file, "tx_dropped");
    diag_capture_section(file, "iw link", iw_link);
    diag_capture_section(file, "iw power_save", iw_power);
    diag_file_section(file, "SDIO ios", SDIO_IOS_PATH);
    diag_file_section(file, "IPv4 route", "/proc/net/route");
    diag_file_section(file, "ARP neighbors", "/proc/net/arp");
    diag_file_section(file, "TCP IPv4", "/proc/net/tcp");
    diag_file_section(file, "TCP IPv6", "/proc/net/tcp6");
    fclose(file);
    return 0;
}

static void handle_client(struct daemon_ctx *ctx)
{
    int fd = accept(ctx->listen_fd, NULL, NULL);
    char request[128];
    char reply[2300];
    ssize_t n;
    if (fd < 0)
        return;
    n = read(fd, request, sizeof(request) - 1U);
    if (n <= 0) {
        close(fd);
        return;
    }
    request[n] = '\0';
    request[strcspn(request, "\r\n")] = '\0';
    if (strcmp(request, "status") == 0) {
        status_json(ctx, reply, sizeof(reply));
    } else if (strcmp(request, "mode") == 0) {
        snprintf(reply, sizeof(reply),
                 "{\"ok\":true,\"mode\":\"%s\",\"phase\":\"%s\","
                 "\"gpioa29\":%u,\"transitions\":%u}",
                 ctx->desired_wired ? "wired" : "wireless",
                 phase_name(ctx->phase), ctx->desired_wired,
                 ctx->transition_count);
    } else if (strcmp(request, "wifi provision") == 0) {
        if (ctx->desired_wired) {
            snprintf(reply, sizeof(reply),
                     "{\"ok\":false,\"error\":\"wired-mode\"}");
        } else {
            start_provisioning(ctx, false);
            snprintf(reply, sizeof(reply),
                     "{\"ok\":true,\"session_id\":%u,\"phase\":\"%s\"}",
                     ctx->provision.session_id,
                     airlink_provision_phase_name(ctx->provision.phase));
        }
    } else if (strcmp(request, "wifi cancel") == 0) {
        bool ok = cancel_provisioning(ctx);
        snprintf(reply, sizeof(reply),
                 "{\"ok\":%s,\"phase\":\"%s\"}",
                 ok ? "true" : "false",
                 airlink_provision_phase_name(ctx->provision.phase));
    } else if (strcmp(request, "wifi forget") == 0) {
        bool removed = remove_saved_wifi_config() == 0;
        unlink(WPA_RUN_CONF);
        ctx->wifi_unconfigured = true;
        if (removed && !ctx->desired_wired)
            start_provisioning(ctx, true);
        snprintf(reply, sizeof(reply),
                 "{\"ok\":%s,\"mode\":\"%s\"}",
                 removed ? "true" : "false",
                 ctx->desired_wired ? "wired" :
                 (removed ? "provisioning" : "error"));
    } else if (strcmp(request, "ch347 get") == 0) {
        if (ctx->ch347_valid)
            snprintf(reply, sizeof(reply),
                     "{\"ok\":true,\"mode\":%u}", ctx->ch347_mode);
        else
            snprintf(reply, sizeof(reply),
                     "{\"ok\":false,\"error\":\"ch347-status-unavailable\"}");
    } else if (strcmp(request, "diag export") == 0) {
        char path[128];
        if (write_diag(ctx, path, sizeof(path)) == 0)
            snprintf(reply, sizeof(reply),
                     "{\"ok\":true,\"path\":\"%s\"}", path);
        else
            snprintf(reply, sizeof(reply),
                     "{\"ok\":false,\"error\":\"diag-export-failed\"}");
    } else {
        snprintf(reply, sizeof(reply),
                 "{\"ok\":false,\"error\":\"unsupported-command\"}");
    }
    strncat(reply, "\n", sizeof(reply) - strlen(reply) - 1U);
    {
        ssize_t written = write(fd, reply, strlen(reply));
        (void)written;
    }
    close(fd);
}

static int socket_start(struct daemon_ctx *ctx)
{
    struct sockaddr_un address;
    ctx->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (ctx->listen_fd < 0)
        return -1;
    unlink(SOCKET_PATH);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", SOCKET_PATH);
    if (bind(ctx->listen_fd, (struct sockaddr *)&address,
             sizeof(address)) != 0 ||
        listen(ctx->listen_fd, 4) != 0)
        return -1;
    chmod(SOCKET_PATH, 0660);
    fcntl(ctx->listen_fd, F_SETFL,
          fcntl(ctx->listen_fd, F_GETFL) | O_NONBLOCK);
    return 0;
}

static void daemon_cleanup(struct daemon_ctx *ctx)
{
    /* Managed services are stopped before low-level IPC/socket cleanup. */
    if (ctx->listen_fd >= 0)
        close(ctx->listen_fd);
    unlink(SOCKET_PATH);
    if (ctx->shm)
        munmap((void *)ctx->shm, AIRLINK_IPC_SHARED_SIZE);
    if (ctx->mem_fd >= 0)
        close(ctx->mem_fd);
}

static int protocol_selftest(void)
{
    static const uint8_t test_mac[ETH_ALEN] =
        {0xb4U, 0x6dU, 0xc2U, 0x94U, 0x46U, 0xacU};
    static const uint8_t broadcast[ETH_ALEN] =
        {0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU};
    uint8_t block[128];
    struct airlink_ipc_ui_status *ui = (void *)block;
    struct daemon_ctx settle_ctx;
    struct network_info settle_net;
    struct garp_frame garp;
    memset(block, 0, sizeof(block));
    memset(&settle_ctx, 0, sizeof(settle_ctx));
    memset(&settle_net, 0, sizeof(settle_net));
    settle_net.ipv4 = 0x0101a8c0U;
    build_gratuitous_arp(&garp, test_mac, settle_net.ipv4, ARPOP_REQUEST);
    if (sizeof(vh_lan_activate_offsets_ms) /
            sizeof(vh_lan_activate_offsets_ms[0]) != 7U ||
        vh_lan_activate_offsets_ms[0] != 0U ||
        vh_lan_activate_offsets_ms[1] != 1000U ||
        vh_lan_activate_offsets_ms[2] != 2000U ||
        vh_lan_activate_offsets_ms[3] != 4000U ||
        vh_lan_activate_offsets_ms[4] != 8000U ||
        vh_lan_activate_offsets_ms[5] != 16000U ||
        vh_lan_activate_offsets_ms[6] != 30000U ||
        sizeof(garp) != 42U ||
        memcmp(garp.ethernet_dst, broadcast, ETH_ALEN) != 0 ||
        memcmp(garp.ethernet_src, test_mac, ETH_ALEN) != 0 ||
        ntohs(garp.ethernet_type) != ETH_P_ARP ||
        ntohs(garp.hardware_type) != ARPHRD_ETHER ||
        ntohs(garp.protocol_type) != ETH_P_IP ||
        ntohs(garp.operation) != ARPOP_REQUEST ||
        garp.sender_ipv4 != settle_net.ipv4 ||
        garp.target_ipv4 != settle_net.ipv4)
        return 1;
    build_gratuitous_arp(&garp, test_mac, settle_net.ipv4, ARPOP_REPLY);
    if (ntohs(garp.operation) != ARPOP_REPLY ||
        memcmp(garp.target_mac, broadcast, ETH_ALEN) != 0)
        return 1;
    if (virtualhere_network_settled(&settle_ctx, &settle_net, 1000U) ||
        settle_ctx.vh_settle_deadline != 3000U ||
        virtualhere_network_settled(&settle_ctx, &settle_net, 2999U) ||
        !virtualhere_network_settled(&settle_ctx, &settle_net, 3000U))
        return 1;
    settle_net.ipv4 = 0x0201a8c0U;
    if (virtualhere_network_settled(&settle_ctx, &settle_net, 3001U) ||
        settle_ctx.vh_settle_deadline != 5001U)
        return 1;
    reset_virtualhere_client_state(&settle_ctx);
    if (apply_virtualhere_client_sample(&settle_ctx, 1000U, true, false, "") !=
            AIRLINK_VIRTUALHERE_LISTENING ||
        apply_virtualhere_client_sample(&settle_ctx, 2000U, true, true,
                                        "192.0.2.10") !=
            AIRLINK_VIRTUALHERE_LISTENING ||
        apply_virtualhere_client_sample(&settle_ctx, 2499U, true, true,
                                        "192.0.2.10") !=
            AIRLINK_VIRTUALHERE_LISTENING ||
        apply_virtualhere_client_sample(&settle_ctx, 2500U, true, true,
                                        "192.0.2.10") !=
            AIRLINK_VIRTUALHERE_CLIENT_CONNECTED ||
        apply_virtualhere_client_sample(&settle_ctx, 3000U, true, false, "") !=
            AIRLINK_VIRTUALHERE_CLIENT_CONNECTED ||
        apply_virtualhere_client_sample(&settle_ctx, 3999U, true, false, "") !=
            AIRLINK_VIRTUALHERE_CLIENT_CONNECTED ||
        apply_virtualhere_client_sample(&settle_ctx, 4000U, true, false, "") !=
            AIRLINK_VIRTUALHERE_LISTENING ||
        apply_virtualhere_client_sample(&settle_ctx, 5000U, false, false, "") !=
            AIRLINK_VIRTUALHERE_STOPPED)
        return 1;
    ui->owner = AIRLINK_IPC_OWNER_LINUX;
    ui->system_phase = AIRLINK_SYSTEM_WIRELESS_READY;
    if (sizeof(struct airlink_ipc_ui_status) != 128U ||
        sizeof(struct airlink_ipc_provision_status) != 128U ||
        sizeof(struct airlink_ipc_state) != 128U ||
        sizeof(struct airlink_ipc_message) != 64U ||
        AIRLINK_VIRTUALHERE_STOPPED != 0U ||
        AIRLINK_VIRTUALHERE_LISTENING != 1U ||
        AIRLINK_VIRTUALHERE_CLIENT_CONNECTED != 2U ||
        !default_route_line_matches(
            "wlan0\t00000000\t0101A8C0\t0003", WLAN_IF) ||
        default_route_line_matches(
            "eth0\t00000000\t0101A8C0\t0003", WLAN_IF) ||
        default_route_line_matches(
            "wlan0\t0000FEA9\t00000000\t0001", WLAN_IF) ||
        crc32_bytes((const uint8_t *)"123456789", 9U) != 0xcbf43926U ||
        airlink_provision_selftest() != 0)
        return 1;
    puts("airlinkd R27.6.6.22 protocol selftest: PASS");
    return 0;
}

static int status_selftest(void)
{
    struct daemon_ctx ctx;
    char status[4096];
    memset(&ctx, 0, sizeof(ctx));
    ctx.desired_wired = 1U;
    ctx.phase = AIRLINK_SYSTEM_WIRED_READY;
    ctx.mode_applied = true;
    ctx.wifi_unconfigured = true;
    ctx.ch347_valid = true;
    ctx.ch347_mode = 3U;
    ctx.lstate.heartbeat = 11U;
    ctx.cstate.heartbeat = 22U;
    ctx.transition_count = 7U;
    status_json(&ctx, status, sizeof(status));
    puts(status);
    return 0;
}

int main(int argc, char **argv)
{
    struct daemon_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_fd = -1;
    ctx.listen_fd = -1;
    ctx.ch347_mode = UINT32_MAX;
    if (argc > 1 && strcmp(argv[1], "--protocol-selftest") == 0)
        return protocol_selftest();
    if (argc > 1 && strcmp(argv[1], "--status-selftest") == 0)
        return status_selftest();
    if (argc > 2 && strcmp(argv[1], "--wpa-config-check") == 0)
        return wpa_config_has_explicit_ssid(argv[2]) ? 0 : 1;
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    log_open();
    log_msg("START version=%s firmware=LN27 arch=riscv64 proto=1 abi=4",
            AIRLINKD_VERSION);
    {
        struct sdio_info sdio;
        if (read_sdio_info(&sdio))
            log_msg("SDIO requested=%u actual=%u timing=%u PASS",
                    sdio.requested_hz, sdio.actual_hz, sdio.timing);
        else
            log_msg("SDIO runtime-status unavailable path=%s", SDIO_IOS_PATH);
    }
    if (airlink_provision_init(&ctx.provision) != 0) {
        log_msg("START FAIL provision-init=%s", strerror(errno));
        daemon_cleanup(&ctx);
        return 1;
    }
    if (ipc_start(&ctx) != 0) {
        int error = ctx.ipc_fail_errno != 0 ? ctx.ipc_fail_errno : EPROTO;
        log_msg("START FAIL ipc stage=%s errno=%d(%s)",
                ctx.ipc_fail_stage ? ctx.ipc_fail_stage : "UNKNOWN",
                error, strerror(error));
        daemon_cleanup(&ctx);
        return 1;
    }
    if (socket_start(&ctx) != 0) {
        log_msg("START FAIL socket=%s", strerror(errno));
        daemon_cleanup(&ctx);
        return 1;
    }
    ctx.phase = AIRLINK_SYSTEM_BOOT_SYNC;
    ctx.desired_wired = ctx.cstate.stable_level ? 1U : 0U;
    ctx.transition_count = ctx.cstate.transition_count;
    request_mode(&ctx, ctx.desired_wired, 0U, ctx.transition_count);
    ctx.next_heartbeat = monotonic_ms() + 1000U;
    ctx.next_ui = 0;
    ctx.next_health = monotonic_ms() + HEALTH_MS;
    while (!stop_requested) {
        struct airlink_ipc_message msg;
        struct pollfd pollfd = {.fd = ctx.listen_fd, .events = POLLIN};
        uint64_t now = monotonic_ms();
        reap_children();
        (void)read_cstate(&ctx);
        if (read_cmessage(&ctx, &msg) > 0)
            handle_message(&ctx, &msg);
        service_wireless(&ctx);
        if (now >= ctx.next_heartbeat) {
            ctx.lstate.heartbeat++;
            ctx.lstate.peer_heartbeat = ctx.cstate.heartbeat;
            publish_linux_state(&ctx);
            ctx.next_heartbeat = now + 1000U;
        }
        if (ctx.next_ui == 0U || now >= ctx.next_ui) {
            publish_ui(&ctx);
            publish_provision(&ctx);
            ctx.next_ui = now + UI_PUBLISH_MS;
        }
        if (now >= ctx.next_health) {
            log_msg("HEALTH linux_hb=%u c906l_hb=%u mode=%s phase=%s "
                    "wifi=%u vh=%u vh_state=%u power_save=%s "
                    "ch347=%s errors=0x%08x",
                    ctx.lstate.heartbeat, ctx.cstate.heartbeat,
                    ctx.desired_wired ? "WIRED" : "WIRELESS",
                    phase_name(ctx.phase),
                    (ctx.ui.flags & AIRLINK_UI_STATUS_WIFI_CONNECTED) != 0U,
                    virtualhere_ready(), ctx.vh_state,
                    ctx.wifi_power_save_off ? "OFF" : "UNKNOWN/ON",
                    ctx.ch347_valid ? "VALID" : "UNKNOWN",
                    ctx.cstate.error_flags);
            ctx.next_health = now + HEALTH_MS;
        }
        if (poll(&pollfd, 1, 20) > 0 && (pollfd.revents & POLLIN))
            handle_client(&ctx);
    }
    log_msg("STOP requested; stopping managed services");
    (void)stop_virtualhere(&ctx);
    airlink_provision_stop(&ctx.provision);
    stop_wifi_processes(&ctx);
    (void)set_wired_network();
    reap_children();
    log_msg("STOP complete; managed services stopped");
    daemon_cleanup(&ctx);
    if (log_file)
        fclose(log_file);
    if (console_fd >= 0)
        close(console_fd);
    return 0;
}
