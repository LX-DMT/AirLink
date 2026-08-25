#ifndef AIRLINK_PROVISION_H
#define AIRLINK_PROVISION_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "airlink_ipc_v4.h"

#define AIRLINK_PROVISION_MAX_NETWORKS 16
#define AIRLINK_PROVISION_MAX_CLIENTS 4
#define AIRLINK_PROVISION_HEADER_MAX 4096U
#define AIRLINK_PROVISION_BODY_MAX 512U
#define AIRLINK_PROVISION_REQUEST_MAX \
    (AIRLINK_PROVISION_HEADER_MAX + AIRLINK_PROVISION_BODY_MAX + 1U)
#define AIRLINK_PROVISION_CANDIDATE_CONF "/run/airlink/wifi.candidate.conf"

struct airlink_scan_network {
    char ssid[33];
    int32_t rssi_dbm;
    uint32_t frequency_mhz;
    uint32_t secured;
};

struct airlink_http_client {
    int fd;
    uint64_t deadline_ms;
    uint32_t used;
    char request[AIRLINK_PROVISION_REQUEST_MAX];
};

struct airlink_provision_ctx {
    uint32_t phase;
    uint32_t error;
    uint32_t session_id;
    uint32_t submit_count;
    bool active;
    bool mandatory;
    bool has_saved_config;
    bool submission_pending;
    bool cancel_pending;
    bool timeout_pending;
    uint64_t started_ms;
    uint64_t manual_deadline_ms;
    char ap_ssid[24];
    char ap_password[16];
    char target_ssid[33];
    struct airlink_scan_network networks[AIRLINK_PROVISION_MAX_NETWORKS];
    unsigned network_count;
    int listen_fd;
    struct airlink_http_client clients[AIRLINK_PROVISION_MAX_CLIENTS];
    pid_t hostapd_pid;
    pid_t dnsmasq_pid;
    pid_t scan_pid;
    uint64_t scan_deadline_ms;
};

int airlink_provision_init(struct airlink_provision_ctx *ctx);
int airlink_provision_begin(struct airlink_provision_ctx *ctx, bool mandatory,
                            bool has_saved_config, uint64_t now_ms);
void airlink_provision_service(struct airlink_provision_ctx *ctx,
                               uint64_t now_ms);
void airlink_provision_stop(struct airlink_provision_ctx *ctx);
int airlink_provision_restart_ap(struct airlink_provision_ctx *ctx,
                                 uint32_t error, uint64_t now_ms);
int airlink_provision_take_submission(struct airlink_provision_ctx *ctx,
                                      char *target_ssid, uint32_t size);
bool airlink_provision_take_cancel(struct airlink_provision_ctx *ctx);
bool airlink_provision_take_timeout(struct airlink_provision_ctx *ctx);
void airlink_provision_mark_sta_testing(struct airlink_provision_ctx *ctx);
void airlink_provision_mark_success(struct airlink_provision_ctx *ctx);
void airlink_provision_fill_status(const struct airlink_provision_ctx *ctx,
                                   struct airlink_ipc_provision_status *status,
                                   uint64_t now_ms);
const char *airlink_provision_phase_name(uint32_t phase);
int airlink_provision_selftest(void);

#endif
