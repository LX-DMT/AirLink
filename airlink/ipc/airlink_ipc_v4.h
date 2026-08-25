#ifndef AIRLINK_IPC_V4_H
#define AIRLINK_IPC_V4_H

#include <stdint.h>

#define AIRLINK_IPC_SHARED_BASE          0x8fff0000UL
#define AIRLINK_IPC_SHARED_SIZE          0x00010000UL
#define AIRLINK_IPC_MAGIC                0x4b4e4c41U
#define AIRLINK_IPC_VERSION              1U
#define AIRLINK_IPC_HEADER_OFFSET        0x000U
#define AIRLINK_IPC_C906_STATE_OFFSET    0x100U
#define AIRLINK_IPC_LINUX_STATE_OFFSET   0x180U
#define AIRLINK_IPC_LINUX_TX_OFFSET      0x200U
#define AIRLINK_IPC_C906_TX_OFFSET       0x240U
#define AIRLINK_IPC_UI_STATUS_OFFSET     0x280U
#define AIRLINK_IPC_PROVISION_STATUS_OFFSET 0x300U
#define AIRLINK_IPC_LAYOUT_SIZE          0x380U

#define AIRLINK_IPC_FEATURE_SHM_POLL      (1U << 0)
#define AIRLINK_IPC_FEATURE_CRC32         (1U << 1)
#define AIRLINK_IPC_FEATURE_GENERATION    (1U << 2)
#define AIRLINK_IPC_FEATURE_GPIOA29       (1U << 3)
#define AIRLINK_IPC_FEATURE_DISPLAY       (1U << 4)
#define AIRLINK_IPC_FEATURE_TOUCH_RAW     (1U << 5)
#define AIRLINK_IPC_FEATURE_ADC1          (1U << 6)
#define AIRLINK_IPC_FEATURE_LVGL_UI       (1U << 7)
#define AIRLINK_IPC_FEATURE_UI_STATUS     (1U << 8)
#define AIRLINK_IPC_FEATURE_CH347_CONTROL (1U << 9)
#define AIRLINK_IPC_FEATURE_SYSTEM_CONTROL (1U << 10)
#define AIRLINK_IPC_FEATURE_WIFI_PROVISION (1U << 11)

#define AIRLINK_IPC_TRANSPORT_SHM_POLL   1U
#define AIRLINK_IPC_OWNER_C906L          1U
#define AIRLINK_IPC_OWNER_LINUX          2U

#define AIRLINK_IPC_STATE_RUNNING        (1U << 0)
#define AIRLINK_IPC_STATE_READY          (1U << 1)
#define AIRLINK_IPC_STATE_SELFTEST_OK    (1U << 2)
#define AIRLINK_IPC_STATE_PEER_VALID     (1U << 3)
#define AIRLINK_IPC_STATE_RAW_VALID      (1U << 4)
#define AIRLINK_IPC_STATE_STABLE_VALID   (1U << 5)
#define AIRLINK_IPC_STATE_RAW_HIGH       (1U << 6)
#define AIRLINK_IPC_STATE_STABLE_HIGH    (1U << 7)
#define AIRLINK_IPC_STATE_DISPLAY_READY  (1U << 8)
#define AIRLINK_IPC_STATE_TOUCH_READY    (1U << 9)
#define AIRLINK_IPC_STATE_ADC1_READY     (1U << 10)

#define AIRLINK_IPC_ERROR_UART_TIMEOUT   (1U << 0)
#define AIRLINK_IPC_ERROR_PINMUX_CHANGED (1U << 1)
#define AIRLINK_IPC_ERROR_DIR_CHANGED    (1U << 2)
#define AIRLINK_IPC_ERROR_PEER_CRC       (1U << 3)
#define AIRLINK_IPC_ERROR_PROTOCOL       (1U << 4)
#define AIRLINK_IPC_ERROR_TIMEOUT        (1U << 5)
#define AIRLINK_IPC_ERROR_SPI_TIMEOUT    (1U << 6)
#define AIRLINK_IPC_ERROR_DISPLAY_OWNER  (1U << 7)
#define AIRLINK_IPC_ERROR_TOUCH_I2C      (1U << 8)
#define AIRLINK_IPC_ERROR_TOUCH_OWNER    (1U << 9)
#define AIRLINK_IPC_ERROR_ADC1_TIMEOUT   (1U << 10)
#define AIRLINK_IPC_ERROR_ADC1_OWNER     (1U << 11)

#define AIRLINK_IPC_MSG_FLAG_RESPONSE    (1U << 0)
#define AIRLINK_IPC_MSG_FLAG_EVENT       (1U << 1)

enum airlink_ipc_message_type {
    AIRLINK_IPC_MSG_NONE = 0,
    AIRLINK_IPC_MSG_HELLO = 1,
    AIRLINK_IPC_MSG_PING = 2,
    AIRLINK_IPC_MSG_CLEAR_ERRORS = 3,
    AIRLINK_IPC_MSG_REQUEST_SNAPSHOT = 4,
    AIRLINK_IPC_MSG_CH347_PREPARED = 5,
    AIRLINK_IPC_MSG_CH347_ENUM_RESULT = 6,
    AIRLINK_IPC_MSG_MODE_APPLY_RESULT = 7,
    AIRLINK_IPC_MSG_REQUEST_CH347_STATUS = 8,
    AIRLINK_IPC_MSG_WIFI_PROVISION_ACK = 9,
    AIRLINK_IPC_MSG_WIFI_PROVISION_CANCEL_ACK = 10,
    AIRLINK_IPC_MSG_READY = 0x101,
    AIRLINK_IPC_MSG_PONG = 0x102,
    AIRLINK_IPC_MSG_MODE_CHANGED = 0x103,
    AIRLINK_IPC_MSG_SNAPSHOT = 0x104,
    AIRLINK_IPC_MSG_CH347_SWITCH_REQUEST = 0x105,
    AIRLINK_IPC_MSG_CH347_SWITCH_DONE = 0x106,
    AIRLINK_IPC_MSG_CH347_STATUS = 0x107,
    AIRLINK_IPC_MSG_WIFI_PROVISION_REQUEST = 0x108,
    AIRLINK_IPC_MSG_WIFI_PROVISION_CANCEL = 0x109,
};

#define AIRLINK_UI_STATUS_VALID                    (1U << 0)
#define AIRLINK_UI_STATUS_WIFI_CONNECTED           (1U << 1)
#define AIRLINK_UI_STATUS_VIRTUALHERE_RUNNING      (1U << 2)
#define AIRLINK_UI_STATUS_WIFI_5GHZ                (1U << 3)
#define AIRLINK_UI_STATUS_WIFI_CONNECTING           (1U << 4)
#define AIRLINK_UI_STATUS_MODE_APPLIED              (1U << 5)
#define AIRLINK_UI_STATUS_SYSTEM_FAULT              (1U << 6)
#define AIRLINK_UI_STATUS_WIFI_UNCONFIGURED         (1U << 7)

/* virtualhere_state values; the ABI4 field already exists in ui_status. */
#define AIRLINK_VIRTUALHERE_STOPPED          0U
#define AIRLINK_VIRTUALHERE_LISTENING        1U
#define AIRLINK_VIRTUALHERE_CLIENT_CONNECTED 2U

#define AIRLINK_SYSTEM_MODE_WIRELESS 0U
#define AIRLINK_SYSTEM_MODE_WIRED    1U

enum airlink_system_phase {
    AIRLINK_SYSTEM_BOOT_SYNC = 0,
    AIRLINK_SYSTEM_WIRED_STOPPING = 1,
    AIRLINK_SYSTEM_WIRED_READY = 2,
    AIRLINK_SYSTEM_WIRELESS_STARTING = 3,
    AIRLINK_SYSTEM_WIRELESS_WAIT_LINK = 4,
    AIRLINK_SYSTEM_WIRELESS_READY = 5,
    AIRLINK_SYSTEM_DEGRADED = 6,
    AIRLINK_SYSTEM_WIRELESS_PROVISIONING = 7,
};

enum airlink_system_error {
    AIRLINK_SYSTEM_ERROR_NONE = 0,
    AIRLINK_SYSTEM_ERROR_WIFI_UNCONFIGURED = 1,
    AIRLINK_SYSTEM_ERROR_WIFI_START = 2,
    AIRLINK_SYSTEM_ERROR_WIFI_TIMEOUT = 3,
    AIRLINK_SYSTEM_ERROR_VH_START = 4,
    AIRLINK_SYSTEM_ERROR_VH_STOP = 5,
    AIRLINK_SYSTEM_ERROR_WLAN_DOWN = 6,
    AIRLINK_SYSTEM_ERROR_IPC = 7,
    AIRLINK_SYSTEM_ERROR_PROVISION = 8,
};

#define AIRLINK_PROVISION_FLAG_ACTIVE             (1U << 0)
#define AIRLINK_PROVISION_FLAG_AP_READY           (1U << 1)
#define AIRLINK_PROVISION_FLAG_MANDATORY          (1U << 2)
#define AIRLINK_PROVISION_FLAG_HAS_SAVED_CONFIG   (1U << 3)
#define AIRLINK_PROVISION_FLAG_SUBMITTED          (1U << 4)
#define AIRLINK_PROVISION_FLAG_SUCCESS            (1U << 5)
#define AIRLINK_PROVISION_FLAG_FAILED             (1U << 6)

enum airlink_provision_phase {
    AIRLINK_PROVISION_IDLE = 0,
    AIRLINK_PROVISION_SCANNING = 1,
    AIRLINK_PROVISION_AP_STARTING = 2,
    AIRLINK_PROVISION_AP_READY = 3,
    AIRLINK_PROVISION_SUBMITTED = 4,
    AIRLINK_PROVISION_STA_TESTING = 5,
    AIRLINK_PROVISION_SUCCESS = 6,
    AIRLINK_PROVISION_FAILED = 7,
    AIRLINK_PROVISION_CANCELLING = 8,
};

enum airlink_provision_error {
    AIRLINK_PROVISION_ERROR_NONE = 0,
    AIRLINK_PROVISION_ERROR_SCAN = 1,
    AIRLINK_PROVISION_ERROR_AP_START = 2,
    AIRLINK_PROVISION_ERROR_INVALID_INPUT = 3,
    AIRLINK_PROVISION_ERROR_STA_START = 4,
    AIRLINK_PROVISION_ERROR_STA_TIMEOUT = 5,
    AIRLINK_PROVISION_ERROR_SAVE = 6,
    AIRLINK_PROVISION_ERROR_CANCELLED = 7,
    AIRLINK_PROVISION_ERROR_MODE_CHANGED = 8,
};

struct airlink_ipc_header {
    uint32_t magic, protocol_version, header_size, total_size;
    uint32_t feature_flags, c906_state_offset, linux_state_offset;
    uint32_t linux_to_c906_offset, c906_to_linux_offset;
    uint32_t state_size, message_size, boot_nonce, transport_id;
    uint32_t reserved[2], crc32;
};

struct airlink_ipc_state {
    uint32_t generation, owner, flags, heartbeat, boot_count, error_flags;
    uint32_t last_rx_seq, last_tx_seq, rx_count, tx_count, crc_error_count;
    uint32_t timeout_count, peer_heartbeat, raw_level, stable_level;
    uint32_t transition_count, sample_count, cycle_low, cycle_high, init_stage;
    uint32_t firmware_id, abi_revision, pinmux_current, direction_current;
    uint32_t ext_port_snapshot, high_sample_count, low_sample_count;
    uint32_t debounce_count, warning_count, reserved[2], crc32;
};

struct airlink_ipc_message {
    uint32_t generation, sequence, type, flags, payload_len, args[4];
    uint32_t timestamp_low, timestamp_high, sender_heartbeat;
    uint32_t reserved[3], crc32;
};

struct airlink_ipc_ui_status {
    uint32_t generation, owner, flags;
    int32_t wifi_rssi_dbm;
    uint32_t ipv4_address, wifi_frequency_mhz, virtualhere_state, update_count;
    char ssid[32];
    uint32_t system_mode;
    uint32_t system_phase;
    uint32_t system_error;
    uint32_t mode_transition_count;
    uint32_t reserved[11];
    uint32_t crc32;
};

struct airlink_ipc_provision_status {
    uint32_t generation, owner, flags, phase, error, session_id, ap_ipv4;
    char ap_ssid[24];
    char ap_password[16];
    char target_ssid[32];
    uint32_t submit_count, elapsed_sec, reserved[4], crc32;
};

_Static_assert(sizeof(struct airlink_ipc_header) == 64, "header size");
_Static_assert(sizeof(struct airlink_ipc_state) == 128, "state size");
_Static_assert(sizeof(struct airlink_ipc_message) == 64, "message size");
_Static_assert(sizeof(struct airlink_ipc_ui_status) == 128, "ui status size");
_Static_assert(sizeof(struct airlink_ipc_provision_status) == 128,
               "provision status size");

#endif
