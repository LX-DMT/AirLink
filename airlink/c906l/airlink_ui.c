#include "lvgl.h"
#include "airlink_ui.h"
#include "airlink_fonts.h"
#include "lv_port.h"

#define TIMEBASE_HZ 25000000ULL
#define TICKS_PER_MS (TIMEBASE_HZ / 1000ULL)
#define SCREEN_SAVER_MS 30000U
#define MODEL_REFRESH_MS 250U
#define SWIPE_MIN 36
#define HOLD_MS 1000U
#define PAGE_ANIM_MS 240U
#define PAGE_FRAME_MS 20U
#define LOCAL_ANIM_MS 16U
#define SAVER_ANIM_MS 33U
#define FULL_FRAME_BYTES (240U * 240U * 2U)

#define C_SCREEN 0x000000U
#define C_PANEL 0x10263dU
#define C_BLUE 0x47aaffU
#define C_CYAN 0x62e4ffU
#define C_GREEN 0x55e3a7U
#define C_AMBER 0xffd36eU
#define C_TEXT 0xf3f8ffU
#define C_MUTED 0x9aafc5U
#define C_DIM 0x556a82U
#define C_ERROR 0xff5f68U
#define C_PIN_GROUND 0x9299a2U

enum overlay_kind {
    OVERLAY_NONE,
    OVERLAY_CH347,
    OVERLAY_PINOUT,
    OVERLAY_RESULT,
    OVERLAY_PROVISION,
};

enum pinout_kind {
    PINOUT_SIGNAL,
    PINOUT_POWER,
    PINOUT_GROUND,
    PINOUT_NC,
};

static inline uint64_t ui_read_time(void)
{
    uint64_t value;
    __asm__ volatile ("rdtime %0" : "=r" (value));
    return value;
}

static struct airlink_display_status *hw_display;
static struct airlink_ui_model current;
static struct airlink_ui_stats ui_stats;

static lv_obj_t *screen;
static lv_obj_t *pages_view;
static lv_obj_t *pages_strip;
static lv_obj_t *pages[4];
static lv_obj_t *status_root;
static lv_obj_t *status_wifi;
static lv_obj_t *status_usb;
static lv_obj_t *status_mode;
static lv_obj_t *status_battery_cells[4];
static lv_obj_t *dots[4];

static lv_obj_t *home_badge_text;
static lv_obj_t *home_transition_arc;
static lv_obj_t *home_title;
static lv_obj_t *home_subtitle;
static lv_obj_t *ch347_pin_button;
static lv_obj_t *ch347_pin_button_text;
static lv_obj_t *ch347_button;
static lv_obj_t *ch347_button_text;
static lv_obj_t *ch347_mode;
static lv_obj_t *wifi_group;
static lv_obj_t *wired_group;
static lv_obj_t *wifi_bars[4];
static lv_obj_t *wifi_ssid;
static lv_obj_t *wifi_state_dot;
static lv_obj_t *wifi_state;
static lv_obj_t *wifi_ip;
static lv_obj_t *wifi_button;
static lv_obj_t *wifi_button_text;
static lv_obj_t *battery_cells[4];
static lv_obj_t *battery_voltage;

static lv_obj_t *overlay_root;
static lv_obj_t *ch_overlay;
static lv_obj_t *pinout_overlay;
static lv_obj_t *pinout_cells[12];
static lv_obj_t *pinout_labels[12];
static lv_obj_t *mode_number;
static lv_obj_t *mode_name_label;
static lv_obj_t *mode_note;
static lv_obj_t *mode_warning;
static lv_obj_t *hold_progress;
static lv_obj_t *hold_text;
static lv_obj_t *result_overlay;
static lv_obj_t *result_circle;
static lv_obj_t *result_name;
static lv_obj_t *result_note;
static lv_obj_t *provision_overlay;
static lv_obj_t *provision_state;
static lv_obj_t *provision_line1;
static lv_obj_t *provision_line2;
static lv_obj_t *provision_line3;
static lv_obj_t *provision_line4;
static lv_obj_t *saver_root;
static lv_obj_t *saver_dot;
static lv_obj_t *saver_word;
static lv_obj_t *saver_usb;
static lv_obj_t *saver_usb_pulse;
static lv_obj_t *saver_state;

static uint32_t page_index;
static uint32_t battery_level;
static uint32_t battery_level_initialized;
static enum overlay_kind overlay;
static uint32_t touch_down;
static uint32_t touch_ignored;
static uint32_t hold_sent;
static uint32_t down_x;
static uint32_t down_y;
static uint32_t last_x;
static uint32_t last_y;
static uint64_t down_time;
static uint64_t saver_deadline;
static uint64_t last_model_refresh;
static uint64_t overlay_until;
static uint64_t mode_transition_started;
static uint64_t mode_transition_anim_time;
static uint64_t saver_orbit_time;
static uint64_t saver_usb_time;
static uint32_t mode_transition_phase;
static uint32_t mode_transition_active;
static uint32_t mode_transition_slow;
static uint32_t mode_transition_wired;
static uint32_t saver_orbit_phase;
static uint32_t saver_usb_phase;
static uint32_t pending_event;
static uint32_t pending_arg;
static uint32_t provision_last_session;
static uint32_t provision_last_phase;
static uint32_t provision_dismissed_session;
static uint32_t provision_dismiss_pending;
static uint32_t provision_success_session;
static uint32_t provision_success_without_session;
static uint32_t saver_reset_reason_pending;
static uint32_t perf_active;
static uint32_t perf_kind;
static uint32_t perf_window_frames;
static uint64_t perf_window_start;
static uint64_t perf_last_frame_time;
static uint32_t full_redraw_frames;
static uint32_t page_slide_active;
static lv_coord_t page_slide_start_x;
static lv_coord_t page_slide_target_x;
static uint64_t page_slide_started;
static uint64_t page_slide_last_step;
static uint32_t page_slide_frame_pending;
static uint32_t page_refresh_pending;
static uint64_t page_refresh_pending_since;
static uint64_t page_frame_total_us;

static void show_provision(uint64_t now);
static void leave_saver(uint64_t now);
static void close_overlay(void);
static uint32_t mode_controls_locked(void);
static uint32_t ch347_controls_locked(void);
static lv_point_t check_points[] = {{0, 8}, {6, 14}, {18, 0}};
static lv_point_t hub_input_points[] = {{4, 0}, {4, 8}, {7, 5}};
static lv_point_t hub_port_points[] = {{0, 0}, {0, 6}};
static lv_point_t wifi_outer_points[] = {
    {1, 5}, {3, 3}, {5, 2}, {7, 2}, {9, 2}, {11, 3}, {13, 5}
};
static lv_point_t wifi_inner_points[] = {
    {4, 8}, {5, 7}, {7, 6}, {9, 7}, {10, 8}
};
static lv_point_t saver_usb_stem_points[] = {{26, 8}, {26, 67}};
static lv_point_t saver_usb_arrow_points[] = {{20, 14}, {26, 8}, {32, 14}};
static lv_point_t saver_usb_branch_points[] = {{7, 37}, {45, 55}};
static uint32_t elapsed_ms(uint64_t start, uint64_t now)
{
    return (uint32_t)((now - start) / TICKS_PER_MS);
}

static uint32_t deadline_reached(uint64_t now, uint64_t deadline)
{
    return (int64_t)(now - deadline) >= 0;
}

static void saver_reset(uint64_t now, uint32_t reason, uint32_t notify)
{
    saver_deadline = now + (uint64_t)SCREEN_SAVER_MS * TICKS_PER_MS;
    if (notify)
        saver_reset_reason_pending = reason;
}

static char *append(char *pointer, const char *text)
{
    while (*text != 0) *pointer++ = *text++;
    *pointer = 0;
    return pointer;
}

static char *append_u32(char *pointer, uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < 10U);
    while (count != 0U) *pointer++ = digits[--count];
    *pointer = 0;
    return pointer;
}

static uint32_t bytes_equal(const void *left, const void *right,
                            uint32_t length)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;

    while (length-- != 0U) {
        if (*a++ != *b++)
            return 0U;
    }
    return 1U;
}

/* Ignore IPC generation/CRC/counters which do not change visible content. */
static uint32_t model_visual_changed(const struct airlink_ui_model *next)
{
    const struct airlink_ipc_ui_status *a = &current.network;
    const struct airlink_ipc_ui_status *b = &next->network;
    const struct airlink_ipc_provision_status *pa = &current.provision;
    const struct airlink_ipc_provision_status *pb = &next->provision;

    if (current.wired != next->wired ||
        current.battery_mv != next->battery_mv ||
        current.battery_valid != next->battery_valid ||
        current.ch347_current != next->ch347_current ||
        current.ch347_state != next->ch347_state)
        return 1U;
    if (a->flags != b->flags ||
        a->wifi_rssi_dbm != b->wifi_rssi_dbm ||
        a->ipv4_address != b->ipv4_address ||
        a->virtualhere_state != b->virtualhere_state ||
        a->system_phase != b->system_phase ||
        a->system_error != b->system_error ||
        !bytes_equal(a->ssid, b->ssid, sizeof(a->ssid)))
        return 1U;
    if (pa->flags != pb->flags || pa->phase != pb->phase ||
        pa->error != pb->error || pa->session_id != pb->session_id ||
        !bytes_equal(pa->ap_ssid, pb->ap_ssid, sizeof(pa->ap_ssid)) ||
        !bytes_equal(pa->ap_password, pb->ap_password,
                     sizeof(pa->ap_password)) ||
        !bytes_equal(pa->target_ssid, pb->target_ssid,
                     sizeof(pa->target_ssid)))
        return 1U;
    return 0U;
}

static lv_obj_t *plain(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                       lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
    return object;
}

static lv_obj_t *label_make(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t colour,
                            lv_coord_t x, lv_coord_t y, lv_coord_t width,
                            lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(colour), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text);
    return label;
}

static void panel_style(lv_obj_t *object, uint32_t background_colour,
                        lv_opa_t background_opacity, uint32_t border_colour,
                        lv_opa_t border_opacity, lv_coord_t radius)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(background_colour), 0);
    lv_obj_set_style_bg_opa(object, background_opacity, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(border_colour), 0);
    lv_obj_set_style_border_opa(object, border_opacity, 0);
    lv_obj_set_style_border_width(object, border_opacity == LV_OPA_TRANSP ? 0 : 1, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

static lv_obj_t *line_make(lv_obj_t *parent, lv_point_t *points,
                           uint32_t count, lv_coord_t x, lv_coord_t y,
                           uint32_t colour, lv_coord_t width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, points, count);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_line_color(line, lv_color_hex(colour), 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

static lv_obj_t *arc_make(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t size, uint32_t colour, lv_coord_t width,
                          uint16_t start, uint16_t end)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_set_pos(arc, x, y);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_bg_angles(arc, start, end);
    lv_arc_set_angles(arc, start, end);
    lv_obj_set_style_arc_color(arc, lv_color_hex(colour), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(colour), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

static const char *mode_name(uint32_t mode)
{
    static const char *const names[4] = {
        "双路 UART", "SPI / I²C", "SPI / I²C 免驱", "JTAG / UART"
    };
    return names[mode & 3U];
}

static const char *mode_note_text(uint32_t mode)
{
    static const char *const notes[4] = {
        "UART0与UART1同时工作", "UART1 + SPI、I²C及GPIO",
        "使用系统HID免驱访问", "JTAG调试 + UART1"
    };
    return notes[mode & 3U];
}

static const char *const pinout_text[4][12] = {
    {
        "RI0", "DCD0", "GND", "GND", "5V", "5V",
        "RXD0", "TXD0", "RTS0", "CTS0", "DSR0", "DTR0"
    },
    {
        "SCL", "ACT", "GND", "GND", "5V", "5V",
        "SDA", "MOSI", "MISO", "SCK", "SCS0", "SCS1"
    },
    {
        "SCL", "ACT", "GND", "GND", "5V", "5V",
        "SDA", "MOSI", "MISO", "SCK", "SCS0", "SCS1"
    },
    {
        "NC", "ACT", "GND", "GND", "5V", "5V",
        "NC", "TDI", "TDO", "TCK", "TMS", "TRST"
    },
};

static const uint8_t pinout_kinds[4][12] = {
    {
        PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_GROUND, PINOUT_GROUND,
        PINOUT_POWER, PINOUT_POWER, PINOUT_SIGNAL, PINOUT_SIGNAL,
        PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_SIGNAL
    },
    {
        PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_GROUND, PINOUT_GROUND,
        PINOUT_POWER, PINOUT_POWER, PINOUT_SIGNAL, PINOUT_SIGNAL,
        PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_SIGNAL
    },
    {
        PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_GROUND, PINOUT_GROUND,
        PINOUT_POWER, PINOUT_POWER, PINOUT_SIGNAL, PINOUT_SIGNAL,
        PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_SIGNAL
    },
    {
        PINOUT_NC, PINOUT_SIGNAL, PINOUT_GROUND, PINOUT_GROUND,
        PINOUT_POWER, PINOUT_POWER, PINOUT_NC, PINOUT_SIGNAL,
        PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_SIGNAL, PINOUT_SIGNAL
    },
};

static uint32_t pinout_colour(uint32_t kind)
{
    switch (kind) {
    case PINOUT_POWER:
        return C_ERROR;
    case PINOUT_GROUND:
        return C_PIN_GROUND;
    case PINOUT_NC:
        return C_DIM;
    default:
        return C_BLUE;
    }
}

static void ipv4_text(char *buffer, uint32_t address)
{
    char *p = buffer;
    p = append_u32(p, address & 0xffU); *p++ = '.';
    p = append_u32(p, (address >> 8) & 0xffU); *p++ = '.';
    p = append_u32(p, (address >> 16) & 0xffU); *p++ = '.';
    (void)append_u32(p, (address >> 24) & 0xffU);
}

static void wifi_state_set_text(const char *text)
{
    lv_coord_t text_width;
    lv_coord_t total_width;
    lv_coord_t start;

    lv_label_set_text(wifi_state, text);
    lv_obj_update_layout(wifi_state);
    text_width = lv_obj_get_width(wifi_state);
    total_width = 5 + 4 + text_width;
    start = total_width < 240 ? (240 - total_width) / 2 : 0;
    lv_obj_set_pos(wifi_state_dot, start, 141);
    lv_obj_set_pos(wifi_state, start + 9, 136);
}

static void create_wifi_status_icon(void)
{
    status_wifi = plain(status_root, 0, 0, 15, 14);
    (void)line_make(status_wifi, wifi_outer_points, 7, 0, 0, C_CYAN, 2);
    (void)line_make(status_wifi, wifi_inner_points, 5, 0, 0, C_CYAN, 2);
    lv_obj_t *dot = plain(status_wifi, 6, 10, 3, 3);
    panel_style(dot, C_CYAN, LV_OPA_COVER, C_CYAN, LV_OPA_TRANSP, LV_RADIUS_CIRCLE);
}

static void create_usb_status_icon(void)
{
    static lv_point_t main_points[] = {{7, 1}, {7, 12}, {3, 8}};
    static lv_point_t branch_points[] = {{7, 7}, {12, 4}};
    status_usb = plain(status_root, 0, 0, 14, 14);
    (void)line_make(status_usb, main_points, 3, 0, 0, C_CYAN, 1);
    (void)line_make(status_usb, branch_points, 2, 0, 0, C_CYAN, 1);
    lv_obj_t *circle = plain(status_usb, 1, 7, 4, 4);
    panel_style(circle, C_CYAN, LV_OPA_COVER, C_CYAN, LV_OPA_TRANSP, LV_RADIUS_CIRCLE);
    lv_obj_t *square = plain(status_usb, 10, 2, 4, 4);
    panel_style(square, C_CYAN, LV_OPA_COVER, C_CYAN, LV_OPA_TRANSP, 1);
}

static void create_status(void)
{
    status_root = plain(screen, 57, 29, 126, 16);
    create_wifi_status_icon();
    create_usb_status_icon();
    status_mode = label_make(status_root, "--", &airlink_font_small_13,
                             0xc9ddefU, 20, 0, 48, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *battery = plain(status_root, 82, 3, 29, 10);
    panel_style(battery, C_SCREEN, LV_OPA_TRANSP, 0xc9ddefU, LV_OPA_COVER, 2);
    for (uint32_t index = 0U; index < 4U; ++index) {
        status_battery_cells[index] =
            plain(battery, (lv_coord_t)(2U + index * 6U), 2, 4, 6);
        panel_style(status_battery_cells[index], C_DIM, LV_OPA_40,
                    C_DIM, LV_OPA_TRANSP, 1);
    }
    lv_obj_t *terminal = plain(status_root, 111, 6, 2, 4);
    panel_style(terminal, 0xc9ddefU, LV_OPA_COVER,
                0xc9ddefU, LV_OPA_TRANSP, 1);
}

static void create_ready_mark(lv_obj_t *parent)
{
    lv_obj_t *circle = plain(parent, 96, 87, 48, 48);
    panel_style(circle, C_GREEN, LV_OPA_20, C_GREEN, LV_OPA_40, LV_RADIUS_CIRCLE);
    (void)line_make(circle, check_points, 3, 15, 13, C_GREEN, 2);
}

static void create_home_page(void)
{
    lv_obj_t *badge = plain(pages[0], 82, 57, 76, 22);
    panel_style(badge, 0x2b84cfU, LV_OPA_20, C_CYAN, LV_OPA_30, 13);
    home_badge_text = label_make(badge, "无线模式", &airlink_font_small_13,
                                 0xd9f4ffU, 0, 3, 76, LV_TEXT_ALIGN_CENTER);
    home_transition_arc = arc_make(pages[0], 88, 79, 64, C_CYAN, 2, 0, 72);
    lv_obj_set_style_arc_opa(home_transition_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(home_transition_arc, LV_OBJ_FLAG_HIDDEN);
    create_ready_mark(pages[0]);
    home_title = label_make(pages[0], "USB共享已就绪", &airlink_font_medium_18,
                            C_TEXT, 37, 141, 166, LV_TEXT_ALIGN_CENTER);
    home_subtitle = label_make(pages[0], "电脑已连接", &airlink_font_small_13,
                               C_MUTED, 38, 168, 164, LV_TEXT_ALIGN_CENTER);
}

static void create_ch347_page(void)
{
    lv_obj_t *chip = plain(pages[1], 92, 62, 56, 43);
    panel_style(chip, 0x23659eU, LV_OPA_20, C_CYAN, LV_OPA_40, 9);
    (void)label_make(chip, "CH347", &airlink_font_small_13, C_CYAN,
                     0, 13, 56, LV_TEXT_ALIGN_CENTER);
    for (uint32_t row = 0U; row < 4U; ++row) {
        lv_obj_t *left = plain(pages[1], 86, 68 + (lv_coord_t)row * 9, 4, 2);
        lv_obj_t *right = plain(pages[1], 150, 68 + (lv_coord_t)row * 9, 4, 2);
        panel_style(left, C_CYAN, LV_OPA_30, C_CYAN, LV_OPA_TRANSP, 1);
        panel_style(right, C_CYAN, LV_OPA_30, C_CYAN, LV_OPA_TRANSP, 1);
    }
    (void)label_make(pages[1], "当前模式", &airlink_font_small_13,
                     C_MUTED, 75, 111, 90, LV_TEXT_ALIGN_CENTER);
    ch347_mode = label_make(pages[1], "SPI / I²C", &airlink_font_medium_18,
                            C_TEXT, 37, 128, 166, LV_TEXT_ALIGN_CENTER);
    ch347_pin_button = plain(pages[1], 35, 163, 80, 30);
    panel_style(ch347_pin_button, C_PANEL, LV_OPA_30, C_CYAN, LV_OPA_40, 16);
    ch347_pin_button_text = label_make(ch347_pin_button, "引脚对照",
                                       &airlink_font_small_13,
                                       0xdcf4ffU, 0, 7, 80,
                                       LV_TEXT_ALIGN_CENTER);
    ch347_button = plain(pages[1], 125, 163, 80, 30);
    panel_style(ch347_button, 0x2576b5U, LV_OPA_30, C_CYAN, LV_OPA_40, 16);
    ch347_button_text = label_make(ch347_button, "切换模式",
                                   &airlink_font_small_13,
                                   0xdcf4ffU, 0, 7, 80,
                                   LV_TEXT_ALIGN_CENTER);
}

static void create_signal_bars(lv_obj_t *parent)
{
    static const uint8_t heights[4] = {8, 14, 21, 28};
    for (uint32_t index = 0U; index < 4U; ++index) {
        wifi_bars[index] = plain(parent, 101 + (lv_coord_t)index * 9,
                                 87 - heights[index], 6, heights[index]);
        panel_style(wifi_bars[index], C_CYAN, LV_OPA_COVER, C_CYAN,
                    LV_OPA_TRANSP, 3);
    }
}

static void create_hub_icon(lv_obj_t *parent)
{
    lv_obj_t *hub = plain(parent, 82, 58, 76, 62);
    (void)line_make(hub, hub_input_points, 3, 34, 0, C_CYAN, 1);
    lv_obj_t *shell = plain(hub, 14, 13, 48, 29);
    panel_style(shell, 0x2576b5U, LV_OPA_30, C_CYAN, LV_OPA_50, 7);
    (void)label_make(shell, "HUB", &airlink_font_small_13,
                     C_CYAN, 0, 6, 48, LV_TEXT_ALIGN_CENTER);
    for (uint32_t index = 0U; index < 3U; ++index) {
        (void)line_make(hub, hub_port_points, 2, 25 + (lv_coord_t)index * 13,
                        42, C_CYAN, 1);
        lv_obj_t *port = plain(hub, 22 + (lv_coord_t)index * 13, 49, 7, 3);
        panel_style(port, C_CYAN, LV_OPA_COVER, C_CYAN, LV_OPA_TRANSP, 1);
    }
}

static void create_network_page(void)
{
    wifi_group = plain(pages[2], 0, 0, 240, 240);
    create_signal_bars(wifi_group);
    (void)label_make(wifi_group, "WI-FI", &airlink_font_small_13,
                     C_MUTED, 75, 94, 90, LV_TEXT_ALIGN_CENTER);
    wifi_ssid = label_make(wifi_group, "未连接", &airlink_font_medium_18,
                           C_TEXT, 37, 110, 166, LV_TEXT_ALIGN_CENTER);
    wifi_state_dot = plain(wifi_group, 0, 141, 5, 5);
    panel_style(wifi_state_dot, C_GREEN, LV_OPA_COVER, C_GREEN,
                LV_OPA_TRANSP, LV_RADIUS_CIRCLE);
    wifi_state = label_make(wifi_group, "Wi-Fi未连接",
                            &airlink_font_small_13, C_MUTED,
                            0, 136, LV_SIZE_CONTENT, LV_TEXT_ALIGN_LEFT);
    wifi_state_set_text("Wi-Fi未连接");
    wifi_ip = label_make(wifi_group, "本机 IP --", &airlink_font_small_13,
                         0xcfe4f7U, 43, 153, 154, LV_TEXT_ALIGN_CENTER);
    wifi_button = plain(wifi_group, 73, 173, 94, 30);
    panel_style(wifi_button, 0x2576b5U, LV_OPA_30, C_CYAN, LV_OPA_40, 16);
    wifi_button_text = label_make(wifi_button, "重新配置",
                                  &airlink_font_small_13,
                                  0xdcf4ffU, 0, 7, 94,
                                  LV_TEXT_ALIGN_CENTER);

    wired_group = plain(pages[2], 0, 0, 240, 240);
    create_hub_icon(wired_group);
    (void)label_make(wired_group, "HDMI·网口·耳机口", &airlink_font_medium_17,
                     C_TEXT, 25, 133, 190, LV_TEXT_ALIGN_CENTER);
    (void)label_make(wired_group, "已打开", &airlink_font_medium_17,
                     C_TEXT, 25, 160, 190, LV_TEXT_ALIGN_CENTER);
}

static void create_battery_page(void)
{
    lv_obj_t *battery = plain(pages[3], 51, 78, 132, 62);
    panel_style(battery, C_SCREEN, LV_OPA_TRANSP,
                0x92abc2U, LV_OPA_COVER, 12);
    for (uint32_t index = 0U; index < 4U; ++index) {
        battery_cells[index] =
            plain(battery, (lv_coord_t)(8U + index * 29U), 10, 23, 42);
        panel_style(battery_cells[index], C_DIM, LV_OPA_40,
                    C_DIM, LV_OPA_TRANSP, 6);
    }
    lv_obj_t *terminal = plain(pages[3], 183, 97, 8, 24);
    panel_style(terminal, 0x92abc2U, LV_OPA_COVER,
                0x92abc2U, LV_OPA_TRANSP, 3);
    battery_voltage = label_make(pages[3], "3.98 V",
                                 &airlink_font_medium_18,
                                 C_TEXT, 37, 158, 166,
                                 LV_TEXT_ALIGN_CENTER);
}

static void create_dots(void)
{
    for (uint32_t index = 0U; index < 4U; ++index) {
        dots[index] = plain(screen, 0, 212, 5, 5);
        panel_style(dots[index], C_DIM, LV_OPA_COVER, C_DIM,
                    LV_OPA_TRANSP, LV_RADIUS_CIRCLE);
    }
}



static void create_overlays(void)
{
    overlay_root = plain(screen, 0, 0, 240, 240);
    panel_style(overlay_root, C_SCREEN, LV_OPA_COVER, C_SCREEN, LV_OPA_TRANSP, 0);

    ch_overlay = plain(overlay_root, 0, 0, 240, 240);
    (void)label_make(ch_overlay, "<", &airlink_font_medium_21,
                     0xb2c9dcU, 44, 29, 24, LV_TEXT_ALIGN_CENTER);
    (void)label_make(ch_overlay, "选择CH347模式", &airlink_font_small_13,
                     C_MUTED, 74, 34, 120, LV_TEXT_ALIGN_CENTER);
    mode_number = label_make(ch_overlay, "2 / 4", &airlink_font_small_13,
                             C_DIM, 75, 63, 90, LV_TEXT_ALIGN_CENTER);
    (void)label_make(ch_overlay, "<", &airlink_font_medium_21,
                     C_DIM, 23, 96, 30, LV_TEXT_ALIGN_CENTER);
    mode_name_label = label_make(ch_overlay, "SPI / I²C", &airlink_font_medium_20,
                                 C_TEXT, 42, 88, 156, LV_TEXT_ALIGN_CENTER);
    (void)label_make(ch_overlay, ">", &airlink_font_medium_21,
                     C_DIM, 187, 96, 30, LV_TEXT_ALIGN_CENTER);
    mode_note = label_make(ch_overlay, "UART1 + SPI、I²C及GPIO",
                           &airlink_font_small_13, C_MUTED,
                           42, 124, 156, LV_TEXT_ALIGN_CENTER);
    mode_warning = label_make(ch_overlay, "切换会中断远程CH347",
                              &airlink_font_small_13, C_AMBER,
                              40, 151, 160, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *hold_button = plain(ch_overlay, 62, 179, 116, 31);
    panel_style(hold_button, 0x236fabU, LV_OPA_30, C_CYAN, LV_OPA_40, 17);
    hold_progress = plain(hold_button, 0, 0, 0, 31);
    panel_style(hold_progress, C_BLUE, LV_OPA_40, C_BLUE, LV_OPA_TRANSP, 17);
    hold_text = label_make(hold_button, "按住1秒确认", &airlink_font_small_13,
                           0xddf3ffU, 0, 7, 116, LV_TEXT_ALIGN_CENTER);


    pinout_overlay = plain(overlay_root, 0, 0, 240, 240);
    lv_obj_t *screen_direction = plain(pinout_overlay, 78, 48, 84, 30);
    panel_style(screen_direction, C_PANEL, LV_OPA_20,
                C_CYAN, LV_OPA_80, 8);
    (void)label_make(screen_direction, "屏幕方向",
                     &airlink_font_small_13, C_CYAN,
                     0, 7, 84, LV_TEXT_ALIGN_CENTER);
    for (uint32_t row = 0U; row < 2U; ++row) {
        for (uint32_t column = 0U; column < 6U; ++column) {
            uint32_t index = row * 6U + column;
            lv_coord_t x = 7 + (lv_coord_t)column * 38;
            lv_coord_t y = row == 0U ? 94 : 130;
            pinout_cells[index] = plain(pinout_overlay, x, y, 35, 28);
            panel_style(pinout_cells[index], C_BLUE, LV_OPA_20,
                        C_BLUE, LV_OPA_50, 7);
            pinout_labels[index] = label_make(pinout_cells[index], "",
                                               &airlink_font_small_13,
                                               C_BLUE, 0, 6, 35,
                                               LV_TEXT_ALIGN_CENTER);
        }
    }

    result_overlay = plain(overlay_root, 0, 0, 240, 240);
    result_circle = plain(result_overlay, 89, 65, 62, 62);
    panel_style(result_circle, C_GREEN, LV_OPA_20, C_GREEN,
                LV_OPA_TRANSP, LV_RADIUS_CIRCLE);
    (void)line_make(result_circle, check_points, 3, 21, 19, C_GREEN, 3);
    result_name = label_make(result_overlay, "已切换", &airlink_font_medium_18,
                             C_TEXT, 30, 139, 180, LV_TEXT_ALIGN_CENTER);
    result_note = label_make(result_overlay, "远程CH347将重新识别",
                             &airlink_font_small_13, C_MUTED,
                             40, 167, 160, LV_TEXT_ALIGN_CENTER);

    provision_overlay = plain(overlay_root, 0, 0, 240, 240);
    (void)label_make(provision_overlay, "配置Wi-Fi", &airlink_font_small_13,
                     C_MUTED, 55, 28, 130, LV_TEXT_ALIGN_CENTER);
    provision_state = label_make(provision_overlay, "正在启动配网热点",
                                 &airlink_font_medium_18, C_TEXT,
                                 25, 61, 190, LV_TEXT_ALIGN_CENTER);
    provision_line1 = label_make(provision_overlay, "",
                                 &airlink_font_small_13, C_TEXT,
                                 25, 103, 190, LV_TEXT_ALIGN_CENTER);
    provision_line2 = label_make(provision_overlay, "",
                                 &airlink_font_small_13, C_TEXT,
                                 25, 130, 190, LV_TEXT_ALIGN_CENTER);
    provision_line3 = label_make(provision_overlay, "",
                                 &airlink_font_small_13, C_MUTED,
                                 25, 157, 190, LV_TEXT_ALIGN_CENTER);
    provision_line4 = label_make(provision_overlay, "点击屏幕返回",
                                 &airlink_font_small_13, C_CYAN,
                                 40, 190, 160, LV_TEXT_ALIGN_CENTER);

    lv_obj_add_flag(ch_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pinout_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(result_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(provision_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay_root, LV_OBJ_FLAG_HIDDEN);
}

static void create_saver(void)
{
    saver_root = plain(screen, 0, 0, 240, 240);
    panel_style(saver_root, C_SCREEN, LV_OPA_COVER, C_SCREEN, LV_OPA_TRANSP, 0);
    saver_dot = plain(saver_root, 171, 117, 7, 7);
    panel_style(saver_dot, C_CYAN, LV_OPA_COVER, C_CYAN,
                LV_OPA_TRANSP, LV_RADIUS_CIRCLE);
    saver_word = label_make(saver_root, "AirLink", &airlink_font_medium_18,
                            C_TEXT, 60, 108, 120, LV_TEXT_ALIGN_CENTER);

    saver_usb = plain(saver_root, 94, 82, 52, 76);
    (void)line_make(saver_usb, saver_usb_stem_points, 2, 0, 0, C_CYAN, 2);
    (void)line_make(saver_usb, saver_usb_arrow_points, 3, 0, 0, C_CYAN, 2);
    (void)line_make(saver_usb, saver_usb_branch_points, 2, 0, 0, C_CYAN, 2);
    lv_obj_t *usb_circle = plain(saver_usb, 3, 33, 8, 8);
    panel_style(usb_circle, C_CYAN, LV_OPA_COVER, C_CYAN,
                LV_OPA_TRANSP, LV_RADIUS_CIRCLE);
    saver_usb_pulse = plain(saver_usb, 42, 52, 7, 7);
    panel_style(saver_usb_pulse, C_CYAN, LV_OPA_COVER, C_CYAN,
                LV_OPA_TRANSP, 2);

    saver_state = label_make(saver_root, "系统正在启动",
                             &airlink_font_small_13, C_MUTED,
                             50, 184, 140, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(saver_usb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(saver_root, LV_OBJ_FLAG_HIDDEN);
}

static void create_ui(void)
{
    screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    pages_view = plain(screen, 0, 0, 240, 240);
    pages_strip = plain(pages_view, 0, 0, 960, 240);
    for (uint32_t index = 0U; index < 4U; ++index)
        pages[index] = plain(pages_strip, (lv_coord_t)index * 240, 0, 240, 240);
    create_home_page();
    create_ch347_page();
    create_network_page();
    create_battery_page();
    create_status();
    create_dots();
    create_overlays();
    create_saver();
}

static void update_dots(void)
{
    lv_coord_t cursor = 95;
    for (uint32_t index = 0U; index < 4U; ++index) {
        lv_coord_t width = index == page_index ? 15 : 5;
        lv_obj_set_pos(dots[index], cursor, 212);
        lv_obj_set_size(dots[index], width, 5);
        lv_obj_set_style_bg_color(dots[index],
            lv_color_hex(index == page_index ? C_BLUE : C_DIM), 0);
        cursor += width + 7;
    }
}

static uint32_t battery_level_direct(uint32_t millivolts)
{
    if (millivolts >= 3950U) return 4U;
    if (millivolts >= 3750U) return 3U;
    if (millivolts >= 3550U) return 2U;
    if (millivolts >= 3350U) return 1U;
    return 0U;
}

static void battery_level_sync(void)
{
    static const uint32_t thresholds[4] = {3350U, 3550U, 3750U, 3950U};

    if (!current.battery_valid)
        return;
    if (!battery_level_initialized) {
        battery_level = battery_level_direct(current.battery_mv);
        battery_level_initialized = 1U;
        return;
    }
    while (battery_level < 4U &&
           current.battery_mv >= thresholds[battery_level] + 20U)
        battery_level++;
    while (battery_level > 0U &&
           current.battery_mv + 20U < thresholds[battery_level - 1U])
        battery_level--;
}

static void battery_cells_update(void)
{
    uint32_t filled_color =
        battery_level <= 1U ? C_AMBER : C_GREEN;

    for (uint32_t index = 0U; index < 4U; ++index) {
        uint32_t filled =
            current.battery_valid && index < battery_level;
        uint32_t color = filled ? filled_color : C_DIM;
        lv_opa_t opacity = filled ? LV_OPA_COVER : LV_OPA_40;

        lv_obj_set_style_bg_color(status_battery_cells[index],
                                  lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(status_battery_cells[index], opacity, 0);
        lv_obj_set_style_bg_color(battery_cells[index],
                                  lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(battery_cells[index], opacity, 0);
    }
}

static void status_update(void)
{
    uint32_t connected =
        (current.network.flags & AIRLINK_UI_STATUS_WIFI_CONNECTED) != 0U;

    if (current.wired) {
        lv_obj_add_flag(status_wifi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(status_usb, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(status_mode, "有线");
    } else {
        lv_obj_clear_flag(status_wifi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_usb, LV_OBJ_FLAG_HIDDEN);
        if (!connected) lv_label_set_text(status_mode, "--");
        else if (current.network.flags & AIRLINK_UI_STATUS_WIFI_5GHZ)
            lv_label_set_text(status_mode, "5G");
        else lv_label_set_text(status_mode, "2.4G");
    }
    battery_level_sync();
    battery_cells_update();
}

static void provision_view_reset(void)
{
    provision_last_session = 0U;
    provision_last_phase = AIRLINK_PROVISION_IDLE;
    provision_dismissed_session = 0U;
    provision_dismiss_pending = 0U;
}

static uint32_t provision_overlay_should_close(void)
{
    uint32_t active =
        (current.provision.flags & AIRLINK_PROVISION_FLAG_ACTIVE) != 0U;

    return current.wired ||
           (!active && current.provision.phase == AIRLINK_PROVISION_IDLE) ||
           current.provision.phase == AIRLINK_PROVISION_SUCCESS;
}

static void provision_pending_clear(void)
{
    if (pending_event == AIRLINK_UI_EVENT_PROVISION_REQUEST ||
        pending_event == AIRLINK_UI_EVENT_PROVISION_CANCEL) {
        pending_event = AIRLINK_UI_EVENT_NONE;
        pending_arg = 0U;
    }
}

static void provision_mark_dismissed(void)
{
    uint32_t active =
        (current.provision.flags & AIRLINK_PROVISION_FLAG_ACTIVE) != 0U;

    if (active && current.provision.session_id != 0U) {
        provision_dismissed_session = current.provision.session_id;
        provision_dismiss_pending = 0U;
    } else {
        provision_dismiss_pending = 1U;
    }
}

static void provision_update(uint64_t now)
{
    char line[64];
    char *pointer;
    uint32_t phase = current.provision.phase;
    uint32_t flags = current.provision.flags;
    uint32_t active = (flags & AIRLINK_PROVISION_FLAG_ACTIVE) != 0U;
    uint32_t ap_ready = (flags & AIRLINK_PROVISION_FLAG_AP_READY) != 0U;
    uint32_t session = current.provision.session_id;
    uint32_t new_session =
        session != 0U && session != provision_last_session;
    uint32_t failed_edge =
        phase == AIRLINK_PROVISION_FAILED &&
        provision_last_phase != AIRLINK_PROVISION_FAILED;
    uint32_t success =
        phase == AIRLINK_PROVISION_SUCCESS ||
        (flags & AIRLINK_PROVISION_FLAG_SUCCESS) != 0U;
    uint32_t success_activity = 0U;

    if (current.wired)
        return;

    if (!active && phase == AIRLINK_PROVISION_IDLE) {
        provision_last_session = 0U;
        provision_dismissed_session = 0U;
        provision_dismiss_pending = 0U;
    } else if (session != provision_last_session) {
        provision_last_session = session;
        if (provision_dismiss_pending && session != 0U) {
            provision_dismissed_session = session;
            provision_dismiss_pending = 0U;
        } else {
            provision_dismissed_session = 0U;
        }
    }

    if (failed_edge && ap_ready)
        provision_dismissed_session = 0U;
    if (success && session != 0U && session != provision_success_session) {
        provision_success_session = session;
        success_activity = 1U;
    } else if (success && session == 0U &&
               !provision_success_without_session) {
        provision_success_without_session = 1U;
        success_activity = 1U;
    } else if (!success) {
        provision_success_without_session = 0U;
    }
    if (success_activity) {
        if (ui_stats.saver)
            leave_saver(now);
        saver_reset(now, AIRLINK_UI_SAVER_RESET_PROVISION_SUCCESS, 1U);
    }
    provision_last_phase = phase;

    if (active && ap_ready && !mode_controls_locked() &&
        overlay == OVERLAY_NONE &&
        provision_dismissed_session != session &&
        ((flags & AIRLINK_PROVISION_FLAG_MANDATORY) ||
         failed_edge || new_session))
        show_provision(now);

    if (phase == AIRLINK_PROVISION_SCANNING) {
        lv_label_set_text(provision_state, "正在扫描周边Wi-Fi");
    } else if (phase == AIRLINK_PROVISION_AP_STARTING) {
        lv_label_set_text(provision_state, "正在启动配网热点");
    } else if (phase == AIRLINK_PROVISION_SUBMITTED ||
               phase == AIRLINK_PROVISION_STA_TESTING) {
        lv_label_set_text(provision_state, "正在连接Wi-Fi");
    } else if (phase == AIRLINK_PROVISION_SUCCESS) {
        lv_label_set_text(provision_state, "Wi-Fi配置成功");
    } else if (phase == AIRLINK_PROVISION_FAILED) {
        lv_label_set_text(provision_state, "连接失败，热点已恢复");
    } else if (ap_ready) {
        lv_label_set_text(provision_state, "连接配网热点");
    } else {
        lv_label_set_text(provision_state, "等待配网服务");
    }

    if (phase == AIRLINK_PROVISION_SUBMITTED ||
        phase == AIRLINK_PROVISION_STA_TESTING) {
        lv_label_set_text(provision_line1,
            current.provision.target_ssid[0] ?
            current.provision.target_ssid : "正在应用新配置");
        lv_label_set_text(provision_line2, "热点暂时断开");
        lv_label_set_text(provision_line3, "请等待圆屏显示结果");
    } else if (phase == AIRLINK_PROVISION_SUCCESS) {
        lv_label_set_text(provision_line1, "网络连接与保存完成");
        lv_label_set_text(provision_line2, "");
        lv_label_set_text(provision_line3, "");
    } else if (ap_ready) {
        pointer = line;
        pointer = append(pointer, "热点名称 ");
        (void)append(pointer, current.provision.ap_ssid);
        lv_label_set_text(provision_line1, line);
        pointer = line;
        pointer = append(pointer, "密码 ");
        (void)append(pointer, current.provision.ap_password);
        lv_label_set_text(provision_line2, line);
        lv_label_set_text(provision_line3, "地址 192.168.4.1");
    } else {
        lv_label_set_text(provision_line1, "配网服务准备中");
        lv_label_set_text(provision_line2, "");
        lv_label_set_text(provision_line3, "");
    }
    lv_label_set_text(provision_line4, "点击屏幕返回");
}

static uint32_t mode_controls_locked(void)
{
    return mode_transition_active || mode_transition_slow;
}

static uint32_t ch347_controls_locked(void)
{
    return current.ch347_state == AIRLINK_UI_CH347_PREPARING ||
           current.ch347_state == AIRLINK_UI_CH347_SWITCHING ||
           current.ch347_state == AIRLINK_UI_CH347_ENUMERATING;
}

static uint32_t mode_phase_reached(uint32_t phase)
{
    if (mode_transition_wired)
        return 1U;
    return phase == AIRLINK_SYSTEM_WIRELESS_WAIT_LINK ||
           phase == AIRLINK_SYSTEM_WIRELESS_PROVISIONING ||
           phase == AIRLINK_SYSTEM_WIRELESS_READY;
}

static uint32_t current_virtualhere_state(void)
{
    uint32_t state = current.network.virtualhere_state;
    if ((current.network.flags &
         AIRLINK_UI_STATUS_VIRTUALHERE_RUNNING) == 0U)
        return AIRLINK_VIRTUALHERE_STOPPED;
    if (state == AIRLINK_VIRTUALHERE_CLIENT_CONNECTED)
        return state;
    return AIRLINK_VIRTUALHERE_LISTENING;
}

static uint32_t wireless_share_ready(void)
{
    uint32_t required = AIRLINK_UI_STATUS_VALID |
                        AIRLINK_UI_STATUS_WIFI_CONNECTED |
                        AIRLINK_UI_STATUS_VIRTUALHERE_RUNNING;

    return !current.wired &&
           (current.network.flags & required) == required;
}

static uint32_t home_spinner_should_run(void)
{
    if (current.wired)
        return 0U;
    return !wireless_share_ready();
}

static void home_spinner_sync(void)
{
    if (home_spinner_should_run())
        lv_obj_clear_flag(home_transition_arc, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(home_transition_arc, LV_OBJ_FLAG_HIDDEN);
}

static void mode_transition_update(uint64_t now)
{
    uint32_t phase = current.network.system_phase;
    uint32_t valid = (current.network.flags &
                      AIRLINK_UI_STATUS_VALID) != 0U;
    uint32_t fault = (current.network.flags &
                      AIRLINK_UI_STATUS_SYSTEM_FAULT) != 0U;

    if (!mode_transition_active && !mode_transition_slow)
        return;
    if (mode_phase_reached(phase) ||
        (fault && phase == AIRLINK_SYSTEM_DEGRADED)) {
        mode_transition_active = 0U;
        mode_transition_slow = 0U;
        home_spinner_sync();
        return;
    }
    /*
     * During cold boot, keep the home arc rotating until Linux publishes a
     * valid UI status.  The 3 s non-blocking timeout still applies to a mode
     * transition after Linux is already online.
     */
    if (mode_transition_active && valid &&
        elapsed_ms(mode_transition_started, now) >= 3000U) {
        mode_transition_active = 0U;
        mode_transition_slow = 1U;
        home_spinner_sync();
    }
}

static void mode_controls_update(void)
{
    uint32_t system_locked = mode_controls_locked();
    uint32_t ch347_locked = ch347_controls_locked();
    uint32_t fill = ch347_locked ? C_DIM : 0x2576b5U;
    uint32_t text = ch347_locked ? C_MUTED : 0xdcf4ffU;

    lv_obj_set_style_bg_color(ch347_button, lv_color_hex(fill), 0);
    lv_obj_set_style_border_color(ch347_button,
        lv_color_hex(ch347_locked ? C_DIM : C_CYAN), 0);
    lv_obj_set_style_text_color(ch347_button_text, lv_color_hex(text), 0);
    lv_label_set_text(ch347_button_text,
                      ch347_locked ? "切换中" : "切换模式");

    fill = system_locked ? C_DIM : 0x2576b5U;
    text = system_locked ? C_MUTED : 0xdcf4ffU;
    lv_obj_set_style_bg_color(wifi_button, lv_color_hex(fill), 0);
    lv_obj_set_style_border_color(wifi_button,
        lv_color_hex(system_locked ? C_DIM : C_CYAN), 0);
    lv_obj_set_style_text_color(wifi_button_text, lv_color_hex(text), 0);
    lv_label_set_text(wifi_button_text,
                      system_locked ? "暂不可用" : "重新配置");
}

static void page_update(uint64_t now)
{
    char line[96];
    char address[20];
    char *pointer;
    uint32_t valid = (current.network.flags & AIRLINK_UI_STATUS_VALID) != 0U;
    uint32_t connected = (current.network.flags & AIRLINK_UI_STATUS_WIFI_CONNECTED) != 0U;
    uint32_t vh_state = current_virtualhere_state();
    uint32_t connecting = (current.network.flags & AIRLINK_UI_STATUS_WIFI_CONNECTING) != 0U;
    uint32_t fault = (current.network.flags & AIRLINK_UI_STATUS_SYSTEM_FAULT) != 0U;
    uint32_t unconfigured = (current.network.flags & AIRLINK_UI_STATUS_WIFI_UNCONFIGURED) != 0U;
    status_update();

    lv_label_set_text(home_badge_text, current.wired ? "有线模式" : "无线模式");
    lv_obj_set_style_text_color(home_title, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_text_color(home_subtitle, lv_color_hex(C_MUTED), 0);
    if (fault) {
        lv_label_set_text(home_title, current.wired ?
            "有线服务异常" : "无线服务异常");
        lv_label_set_text(home_subtitle, "请检查系统日志");
        lv_obj_set_style_text_color(home_title, lv_color_hex(C_ERROR), 0);
        lv_obj_set_style_text_color(home_subtitle, lv_color_hex(C_ERROR), 0);
    } else if (current.wired) {
        lv_label_set_text(home_title, "USB HUB已就绪");
        lv_label_set_text(home_subtitle, "HDMI · 网口 · 耳机口可用");
    } else if (mode_transition_active) {
        if (!valid) {
            lv_label_set_text(home_title, "系统正在启动");
            lv_label_set_text(home_subtitle, "正在等待Linux服务");
        } else {
            lv_label_set_text(home_title, "正在启动无线服务");
            lv_label_set_text(home_subtitle, "正在准备网络");
        }
        lv_obj_set_style_text_color(home_title, lv_color_hex(C_CYAN), 0);
    } else if (mode_transition_slow) {
        if (!valid) {
            lv_label_set_text(home_title, "系统正在启动");
            lv_label_set_text(home_subtitle, "正在等待Linux服务");
        } else {
            lv_label_set_text(home_title, "正在启动无线服务");
            lv_label_set_text(home_subtitle, "正在准备网络");
        }
        lv_obj_set_style_text_color(home_title, lv_color_hex(C_CYAN), 0);
    } else if (!valid) {
        lv_label_set_text(home_title, "系统正在启动");
        lv_label_set_text(home_subtitle, "正在等待Linux服务");
    } else if (current.network.system_phase == AIRLINK_SYSTEM_WIRELESS_STARTING) {
        lv_label_set_text(home_title, "正在启动无线服务");
        lv_label_set_text(home_subtitle, "正在准备网络");
    } else if (current.network.system_phase == AIRLINK_SYSTEM_WIRELESS_WAIT_LINK ||
               connecting) {
        lv_label_set_text(home_title, "正在连接Wi-Fi");
        lv_label_set_text(home_subtitle, "请稍候");
    } else if (current.network.system_phase ==
                   AIRLINK_SYSTEM_WIRELESS_PROVISIONING ||
               unconfigured) {
        lv_label_set_text(home_title, "Wi-Fi未配置");
        lv_label_set_text(home_subtitle, "请使用手机配置");
    } else if (connected) {
        if (vh_state == AIRLINK_VIRTUALHERE_CLIENT_CONNECTED) {
            lv_label_set_text(home_title, "电脑已连接");
            lv_label_set_text(home_subtitle, "无线共享已就绪");
        } else if (vh_state == AIRLINK_VIRTUALHERE_LISTENING) {
            lv_label_set_text(home_title, "等待电脑连接");
            lv_label_set_text(home_subtitle, "无线共享已启动");
        } else {
            lv_label_set_text(home_title, "无线服务启动中");
            lv_label_set_text(home_subtitle, "Wi-Fi已连接");
        }
    } else {
        lv_label_set_text(home_title, "Wi-Fi未连接");
        lv_label_set_text(home_subtitle, "正在等待网络");
    }
    mode_controls_update();

    lv_label_set_text(ch347_mode, mode_name(current.ch347_current));
    if (current.wired) {
        lv_obj_add_flag(wifi_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wired_group, LV_OBJ_FLAG_HIDDEN);
    } else {
        int32_t rssi = current.network.wifi_rssi_dbm;
        uint32_t strength = !connected ? 0U :
            (rssi >= -55 ? 4U : rssi >= -65 ? 3U : rssi >= -75 ? 2U : 1U);
        lv_obj_clear_flag(wifi_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wired_group, LV_OBJ_FLAG_HIDDEN);
        for (uint32_t index = 0U; index < 4U; ++index)
            lv_obj_set_style_bg_color(wifi_bars[index],
                lv_color_hex(index < strength ? C_CYAN : C_DIM), 0);
        lv_label_set_text(wifi_ssid, connected ?
            (current.network.ssid[0] ? current.network.ssid : "已连接") : "未连接");
        if (!valid) wifi_state_set_text("等待网络状态");
        else if (fault) wifi_state_set_text("无线服务异常");
        else if (connecting) wifi_state_set_text("正在连接Wi-Fi");
        else if (unconfigured) wifi_state_set_text("Wi-Fi未配置");
        else if (!connected) wifi_state_set_text("Wi-Fi未连接");
        else {
            pointer = line;
            pointer = append(pointer,
                (current.network.flags & AIRLINK_UI_STATUS_WIFI_5GHZ) ?
                    "5 GHz · " : "2.4 GHz · ");
            (void)append(pointer, strength >= 3U ? "信号良好" :
                         strength == 2U ? "信号一般" : "信号较弱");
            wifi_state_set_text(line);
        }
        if (connected) {
            ipv4_text(address, current.network.ipv4_address);
            pointer = line;
            pointer = append(pointer, "本机 IP ");
            (void)append(pointer, address);
            lv_label_set_text(wifi_ip, line);
        } else lv_label_set_text(wifi_ip, "本机 IP --");
    }

    if (current.battery_valid) {
        uint32_t fraction = (current.battery_mv % 1000U) / 10U;
        pointer = line;
        pointer = append_u32(pointer, current.battery_mv / 1000U);
        *pointer++ = '.';
        *pointer++ = (char)('0' + fraction / 10U);
        *pointer++ = (char)('0' + fraction % 10U);
        (void)append(pointer, " V");
        lv_label_set_text(battery_voltage, line);
    } else {
        lv_label_set_text(battery_voltage, "-- V");
    }
    provision_update(now);
    home_spinner_sync();
    update_dots();
}

static void page_update_or_defer(uint64_t now)
{
    if (page_slide_active) {
        if (!page_refresh_pending)
            page_refresh_pending_since = now;
        page_refresh_pending = 1U;
        return;
    }
    page_update(now);
}

static int32_t page_ease_out(lv_coord_t start, lv_coord_t target,
                             uint32_t elapsed)
{
    uint32_t t;
    uint32_t step;
    int32_t delta = (int32_t)target - (int32_t)start;

    if (elapsed >= PAGE_ANIM_MS)
        return target;
    t = elapsed * LV_BEZIER_VAL_MAX / PAGE_ANIM_MS;
    step = lv_bezier3(t, 0U, 900U, 950U, LV_BEZIER_VAL_MAX);
    return (int32_t)start +
        (int32_t)(((int64_t)delta * (int32_t)step) >>
                  LV_BEZIER_VAL_SHIFT);
}

static void page_slide_step(uint64_t now)
{
    uint32_t elapsed;
    uint32_t step_gap;
    lv_coord_t value;

    if (!page_slide_active)
        return;
    if (page_refresh_pending &&
        elapsed_ms(page_refresh_pending_since, now) >= PAGE_ANIM_MS) {
        page_refresh_pending = 0U;
        page_update(now);
    }
    elapsed = elapsed_ms(page_slide_started, now);
    step_gap = elapsed_ms(page_slide_last_step, now);
    if (elapsed < PAGE_ANIM_MS && step_gap < PAGE_FRAME_MS)
        return;
    if (step_gap >= PAGE_FRAME_MS) {
        page_slide_last_step +=
            (uint64_t)(step_gap / PAGE_FRAME_MS) * PAGE_FRAME_MS *
            TICKS_PER_MS;
    }
    value = (lv_coord_t)page_ease_out(page_slide_start_x,
                                      page_slide_target_x, elapsed);
    if (lv_obj_get_x(pages_strip) != value) {
        lv_obj_set_x(pages_strip, value);
        page_slide_frame_pending = 1U;
    }
    if (elapsed >= PAGE_ANIM_MS) {
        page_slide_active = 0U;
        if (lv_obj_get_x(pages_strip) != page_slide_target_x) {
            lv_obj_set_x(pages_strip, page_slide_target_x);
            page_slide_frame_pending = 1U;
        }
        if (page_refresh_pending) {
            page_refresh_pending = 0U;
            page_update(now);
        }
    }
}

static void go_page(uint32_t next, uint32_t animated)
{
    lv_coord_t target;

    if (next > 3U) next = 3U;
    page_index = next;
    ui_stats.page = next;
    target = -(lv_coord_t)(next * 240U);
    if (animated && lv_obj_get_x(pages_strip) != target) {
        uint64_t now = ui_read_time();
        page_slide_active = 1U;
        page_slide_start_x = lv_obj_get_x(pages_strip);
        page_slide_target_x = target;
        page_slide_started = now;
        page_slide_last_step = now;
        page_slide_frame_pending = 0U;
        ui_stats.transition_count++;
    } else {
        uint32_t apply_pending = page_refresh_pending;
        page_slide_active = 0U;
        page_slide_frame_pending = 0U;
        page_refresh_pending = 0U;
        lv_obj_set_x(pages_strip, target);
        if (apply_pending)
            page_update(ui_read_time());
    }
    update_dots();
}

static void chrome_visible(uint32_t visible)
{
    if (visible) {
        lv_obj_clear_flag(status_root, LV_OBJ_FLAG_HIDDEN);
        for (uint32_t index = 0U; index < 4U; ++index)
            lv_obj_clear_flag(dots[index], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(status_root, LV_OBJ_FLAG_HIDDEN);
        for (uint32_t index = 0U; index < 4U; ++index)
            lv_obj_add_flag(dots[index], LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_overlay_children(void)
{
    lv_obj_add_flag(ch_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pinout_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(result_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(provision_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void pinout_update(uint32_t mode)
{
    mode &= 3U;
    for (uint32_t index = 0U; index < 12U; ++index) {
        uint32_t kind = pinout_kinds[mode][index];
        uint32_t colour = pinout_colour(kind);
        lv_opa_t background = kind == PINOUT_NC ? LV_OPA_10 : LV_OPA_20;
        lv_opa_t border = kind == PINOUT_NC ? LV_OPA_20 : LV_OPA_60;

        lv_label_set_text(pinout_labels[index], pinout_text[mode][index]);
        lv_obj_set_style_text_color(pinout_labels[index],
            lv_color_hex(colour), 0);
        lv_obj_set_style_bg_color(pinout_cells[index],
            lv_color_hex(colour), 0);
        lv_obj_set_style_bg_opa(pinout_cells[index], background, 0);
        lv_obj_set_style_border_color(pinout_cells[index],
            lv_color_hex(colour), 0);
        lv_obj_set_style_border_opa(pinout_cells[index], border, 0);
    }
}

static void show_pinout(void)
{
    overlay = OVERLAY_PINOUT;
    hide_overlay_children();
    pinout_update(current.ch347_current);
    lv_obj_clear_flag(pinout_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(overlay_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_root);
    chrome_visible(0U);
}

static void mode_picker_update(void)
{
    char line[12];
    char *pointer = line;
    pointer = append_u32(pointer, (current.ch347_selected & 3U) + 1U);
    (void)append(pointer, " / 4");
    lv_label_set_text(mode_number, line);
    lv_label_set_text(mode_name_label, mode_name(current.ch347_selected));
    lv_label_set_text(mode_note, mode_note_text(current.ch347_selected));
    lv_label_set_text(mode_warning, current.wired ?
        "切换会使USB设备重新枚举" : "切换会中断远程CH347");
}

static void show_mode_picker(void)
{
    overlay = OVERLAY_CH347;
    hide_overlay_children();
    lv_obj_clear_flag(ch_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(hold_progress, 0);
    lv_label_set_text(hold_text, "按住1秒确认");
    mode_picker_update();
    lv_obj_clear_flag(overlay_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_root);
    chrome_visible(0U);
}

static void show_result(uint32_t success, uint64_t now)
{
    char line[64];
    char *pointer = line;
    overlay = OVERLAY_RESULT;
    hide_overlay_children();
    lv_obj_clear_flag(result_overlay, LV_OBJ_FLAG_HIDDEN);
    pointer = append(pointer, success ? "已切换到" : "切换失败：");
    (void)append(pointer, mode_name(current.ch347_selected));
    lv_label_set_text(result_name, line);
    lv_obj_set_style_bg_color(result_circle,
        lv_color_hex(success ? C_GREEN : C_ERROR), 0);
    lv_label_set_text(result_note, success ?
        (current.wired ? "USB设备将重新枚举" : "远程CH347将重新识别") :
        "请稍后重新尝试");
    lv_obj_clear_flag(overlay_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_root);
    chrome_visible(0U);
    overlay_until = now + 1450ULL * TICKS_PER_MS;
}

static void show_provision(uint64_t now)
{
    if (current.wired || mode_controls_locked())
        return;
    overlay = OVERLAY_PROVISION;
    hide_overlay_children();
    if (!(current.provision.flags & AIRLINK_PROVISION_FLAG_ACTIVE)) {
        provision_dismiss_pending = 0U;
        provision_dismissed_session = 0U;
        pending_event = AIRLINK_UI_EVENT_PROVISION_REQUEST;
        pending_arg = 0U;
        lv_label_set_text(provision_state, "正在启动配网热点");
    }
    lv_obj_clear_flag(provision_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(overlay_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_root);
    chrome_visible(0U);
    provision_update(now);
}


static void close_overlay(void)
{
    overlay = OVERLAY_NONE;
    overlay_until = 0U;
    lv_obj_add_flag(overlay_root, LV_OBJ_FLAG_HIDDEN);
    chrome_visible(1U);
}

static void request_full_redraw(uint32_t frames)
{
    if (full_redraw_frames < frames)
        full_redraw_frames = frames;
}

static void saver_reset_animation(uint64_t now)
{
    saver_orbit_phase = 0U;
    saver_usb_phase = 0U;
    saver_orbit_time = now;
    saver_usb_time = now;
    lv_obj_set_pos(saver_dot, 171, 117);
    lv_obj_set_size(saver_usb_pulse, 7, 7);
    lv_obj_set_pos(saver_usb_pulse, 42, 52);
    lv_obj_set_style_opa(saver_usb_pulse, 102, 0);
}

static void saver_update_mode(void)
{
    uint32_t flags = current.network.flags;
    uint32_t valid = (flags & AIRLINK_UI_STATUS_VALID) != 0U;
    uint32_t fault = (flags & AIRLINK_UI_STATUS_SYSTEM_FAULT) != 0U;
    uint32_t unconfigured =
        (flags & AIRLINK_UI_STATUS_WIFI_UNCONFIGURED) != 0U;
    uint32_t connected =
        (flags & AIRLINK_UI_STATUS_WIFI_CONNECTED) != 0U;
    uint32_t vh_state = current_virtualhere_state();
    uint32_t connecting =
        (flags & AIRLINK_UI_STATUS_WIFI_CONNECTING) != 0U;
    uint32_t provision_active =
        (current.provision.flags & AIRLINK_PROVISION_FLAG_ACTIVE) != 0U;
    uint32_t provision_wait =
        unconfigured ||
        current.network.system_phase == AIRLINK_SYSTEM_WIRELESS_PROVISIONING ||
        (provision_active &&
         current.provision.phase != AIRLINK_PROVISION_SUCCESS);

    lv_obj_set_style_text_color(saver_state, lv_color_hex(C_MUTED), 0);
    if (current.wired) {
        lv_obj_add_flag(saver_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(saver_word, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(saver_usb, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(saver_state, "USB HUB已就绪");
        return;
    }

    lv_obj_clear_flag(saver_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(saver_word, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(saver_usb, LV_OBJ_FLAG_HIDDEN);
    if (!valid) {
        lv_label_set_text(saver_state, "系统正在启动");
    } else if (fault) {
        lv_label_set_text(saver_state, "无线服务异常");
        lv_obj_set_style_text_color(saver_state, lv_color_hex(C_ERROR), 0);
    } else if (provision_wait) {
        lv_label_set_text(saver_state, "等待手机配网");
    } else if (connected &&
               vh_state == AIRLINK_VIRTUALHERE_CLIENT_CONNECTED) {
        lv_label_set_text(saver_state, "电脑已连接");
    } else if (connected &&
               vh_state == AIRLINK_VIRTUALHERE_LISTENING) {
        lv_label_set_text(saver_state, "等待电脑连接");
    } else if (connected) {
        lv_label_set_text(saver_state, "无线服务启动中");
    } else if (connecting ||
               current.network.system_phase ==
                   AIRLINK_SYSTEM_WIRELESS_STARTING ||
               current.network.system_phase ==
                   AIRLINK_SYSTEM_WIRELESS_WAIT_LINK) {
        lv_label_set_text(saver_state, "正在连接Wi-Fi");
    } else {
        lv_label_set_text(saver_state, "正在连接Wi-Fi");
    }
}

static void enter_saver(uint64_t now)
{
    if (ui_stats.saver) return;
    close_overlay();
    ui_stats.saver = 1U;
    chrome_visible(0U);
    lv_obj_add_flag(pages_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(saver_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(saver_root);
    saver_reset_animation(now);
    saver_update_mode();
    request_full_redraw(2U);
}

static void leave_saver(uint64_t now)
{
    (void)now;
    if (!ui_stats.saver) return;
    ui_stats.saver = 0U;
    lv_obj_add_flag(saver_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(pages_view, LV_OBJ_FLAG_HIDDEN);
    chrome_visible(1U);
    request_full_redraw(2U);
}

static void animate_saver(uint64_t now)
{
    if (!ui_stats.saver) return;
    if (current.wired) {
        int32_t wave;
        uint8_t size;
        uint8_t opacity;
        if (elapsed_ms(saver_usb_time, now) < SAVER_ANIM_MS) return;
        saver_usb_time = now;
        saver_usb_phase = (saver_usb_phase + 1U) % 36U;
        wave = lv_trigo_sin((int16_t)(saver_usb_phase * 10U));
        size = (uint8_t)(7U + (uint32_t)(wave + 32767) / 32768U);
        opacity = (uint8_t)(102U +
            (uint32_t)(wave + 32767) * 153U / 65534U);
        lv_obj_set_size(saver_usb_pulse, size, size);
        lv_obj_set_pos(saver_usb_pulse, 45 - size / 2, 55 - size / 2);
        lv_obj_set_style_opa(saver_usb_pulse, opacity, 0);
        return;
    }
    if (elapsed_ms(saver_orbit_time, now) < SAVER_ANIM_MS) return;
    saver_orbit_time = now;
    saver_orbit_phase = (saver_orbit_phase + 1U) % 120U;
    {
        int16_t angle = (int16_t)(saver_orbit_phase * 3U);
        int32_t x = (54 * (int32_t)lv_trigo_sin((int16_t)(angle + 90))) /
            32767;
        int32_t y = (54 * (int32_t)lv_trigo_sin(angle)) / 32767;
        lv_obj_set_pos(saver_dot,
                       (lv_coord_t)(117 + x), (lv_coord_t)(117 + y));
    }
}

enum performance_kind {
    PERF_NONE,
    PERF_LOCAL,
    PERF_PAGE,
    PERF_SAVER,
};

static uint32_t animation_active(void)
{
    return ui_stats.saver || page_slide_active ||
        page_slide_frame_pending || home_spinner_should_run() ||
        lv_anim_count_running() != 0U;
}

static uint32_t animation_kind(void)
{
    if (!animation_active())
        return PERF_NONE;
    if (ui_stats.saver)
        return PERF_SAVER;
    if (page_slide_active || page_slide_frame_pending)
        return PERF_PAGE;
    return PERF_LOCAL;
}

static uint32_t animation_period_ms(uint32_t kind)
{
    if (kind == PERF_SAVER)
        return SAVER_ANIM_MS;
    if (kind == PERF_PAGE)
        return PAGE_FRAME_MS;
    return LOCAL_ANIM_MS;
}

static void performance_sample(uint64_t now, uint32_t frames,
                               uint32_t kind)
{
    uint32_t elapsed;
    uint32_t active = kind != PERF_NONE;

    if (active && perf_active && perf_kind != kind) {
        elapsed = elapsed_ms(perf_window_start, now);
        if (elapsed >= 200U && perf_window_frames >= 2U) {
            uint32_t fps = perf_window_frames * 1000U / elapsed;
            ui_stats.fps_current = fps;
            if (fps != 0U &&
                (ui_stats.fps_min == 0U || fps < ui_stats.fps_min))
                ui_stats.fps_min = fps;
        }
        perf_active = 0U;
        perf_last_frame_time = 0U;
    }
    if (active && !perf_active) {
        perf_active = 1U;
        perf_kind = kind;
        perf_window_frames = 0U;
        perf_window_start = now;
        perf_last_frame_time = 0U;
    }
    if (active && frames != 0U) {
        if (perf_last_frame_time != 0U) {
            uint32_t gap = elapsed_ms(perf_last_frame_time, now);
            uint32_t period = animation_period_ms(kind);
            if (gap > period * 2U)
                ui_stats.missed_refresh += gap / period - 1U;
        }
        perf_last_frame_time = now;
        perf_window_frames += frames;
    }
    if (!perf_active)
        return;

    elapsed = elapsed_ms(perf_window_start, now);
    if ((active && elapsed >= 1000U) ||
        (!active && elapsed >= 200U && perf_window_frames >= 2U)) {
        uint32_t fps = elapsed != 0U ?
            perf_window_frames * 1000U / elapsed : 0U;
        ui_stats.fps_current = fps;
        if (fps != 0U &&
            (ui_stats.fps_min == 0U || fps < ui_stats.fps_min))
            ui_stats.fps_min = fps;
        perf_window_frames = 0U;
        perf_window_start = now;
    }
    if (!active) {
        perf_active = 0U;
        perf_kind = PERF_NONE;
        perf_last_frame_time = 0U;
    }
}

static uint32_t in_rect(uint32_t x, uint32_t y,
                        uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
    return x >= x1 && x <= x2 && y >= y1 && y <= y2;
}

int airlink_ui_init(struct airlink_display_status *display,
                    struct airlink_touch_status *touch,
                    const struct airlink_ui_model *model,
                    uint64_t now)
{
    lv_mem_monitor_t monitor;
    hw_display = display;
    current = *model;
    page_index = 0U;
    overlay = OVERLAY_NONE;
    saver_reset(now, AIRLINK_UI_SAVER_RESET_NONE, 0U);
    saver_reset_reason_pending = AIRLINK_UI_SAVER_RESET_NONE;
    full_redraw_frames = 0U;
    page_slide_active = 0U;
    page_slide_frame_pending = 0U;
    page_refresh_pending = 0U;
    page_refresh_pending_since = 0U;
    page_frame_total_us = 0U;
    perf_kind = PERF_NONE;
    last_model_refresh = now;
    lv_init();
    airlink_lv_port_init(display, touch);
    create_ui();
    page_update(now);
    go_page(0U, 0U);
    lv_timer_handler();
    lv_refr_now(NULL);
    if (airlink_display_make_visible(display) != 0) return -1;
    lv_mem_monitor(&monitor);
    ui_stats.ready = 1U;
    ui_stats.page = 0U;
    ui_stats.heap_free = monitor.free_size;
    ui_stats.heap_used_pct = monitor.used_pct;
    ui_stats.first_frame_ms = display->gram_ready_ms;
    ui_stats.visible_ms = display->visible_ms;
    ui_stats.frame_count = display->completed_frame_count;
    ui_stats.frame_bytes = display->last_frame_bytes;
    if (display->completed_frame_count != 0U) {
        if (display->last_frame_bytes >= FULL_FRAME_BYTES)
            ui_stats.full_frame_count = display->completed_frame_count;
        else
            ui_stats.partial_frame_count = display->completed_frame_count;
    }
    return 0;
}

int airlink_ui_tick(const struct airlink_ui_model *model,
                    const struct airlink_touch_status *touch,
                    uint64_t now, uint32_t *event_arg)
{
    uint32_t pressed = touch->ready && touch->points != 0U;
    int event = AIRLINK_UI_EVENT_NONE;
    *event_arg = 0U;

    page_slide_step(now);
    if (elapsed_ms(last_model_refresh, now) >= MODEL_REFRESH_MS) {
        uint32_t selected = current.ch347_selected;
        uint32_t visual_changed = model_visual_changed(model);
        current = *model;
        current.ch347_selected = selected;
        mode_transition_update(now);
        if (overlay == OVERLAY_PROVISION &&
            provision_overlay_should_close()) {
            provision_pending_clear();
            provision_view_reset();
            close_overlay();
            go_page(0U, 0U);
            visual_changed = 1U;
        }
        if (visual_changed) {
            page_update_or_defer(now);
            if (ui_stats.saver)
                saver_update_mode();
            if (overlay == OVERLAY_CH347)
                mode_picker_update();
        }
        last_model_refresh = now;
    }

    if (home_spinner_should_run() &&
        elapsed_ms(mode_transition_anim_time, now) >= LOCAL_ANIM_MS) {
        uint16_t start_angle =
            (uint16_t)((mode_transition_phase * 5U) % 360U);
        mode_transition_phase = (mode_transition_phase + 1U) % 72U;
        mode_transition_anim_time = now;
        lv_arc_set_angles(home_transition_arc, start_angle,
                          (uint16_t)((start_angle + 72U) % 360U));
    }
    {
        uint32_t was_active = mode_transition_active;
        uint32_t was_slow = mode_transition_slow;
        mode_transition_update(now);
        if (was_active != mode_transition_active ||
            was_slow != mode_transition_slow)
            page_update_or_defer(now);
    }
    if (overlay_until != 0U && (int64_t)(now - overlay_until) >= 0 &&
        overlay == OVERLAY_RESULT)
        close_overlay();

    if (!ui_stats.saver &&
        deadline_reached(now, saver_deadline))
        enter_saver(now);
    animate_saver(now);

    if (pressed && !touch_down) {
        touch_down = 1U;
        down_x = touch->x;
        down_y = touch->y;
        last_x = touch->x;
        last_y = touch->y;
        down_time = now;
        hold_sent = 0U;
        saver_reset(now, AIRLINK_UI_SAVER_RESET_TOUCH, 1U);
        if (ui_stats.saver) {
            leave_saver(now);
            touch_ignored = 1U;
        }
    } else if (pressed && touch_down) {
        last_x = touch->x;
        last_y = touch->y;
        saver_reset(now, AIRLINK_UI_SAVER_RESET_TOUCH, 0U);
        if (!touch_ignored && overlay == OVERLAY_CH347 &&
            in_rect(down_x, down_y, 62U, 179U, 178U, 214U)) {
            uint32_t held = elapsed_ms(down_time, now);
            lv_obj_set_width(hold_progress,
                (lv_coord_t)(held >= HOLD_MS ? 116U : 116U * held / HOLD_MS));
            if (!hold_sent && held >= HOLD_MS) {
                hold_sent = 1U;
                pending_event = AIRLINK_UI_EVENT_CH347_REQUEST;
                pending_arg = current.ch347_selected;
                lv_label_set_text(hold_text, "切换中");
            }
        }
    } else if (!pressed && touch_down) {
        int32_t dx = (int32_t)last_x - (int32_t)down_x;
        int32_t dy = (int32_t)last_y - (int32_t)down_y;
        int32_t ax = dx < 0 ? -dx : dx;
        int32_t ay = dy < 0 ? -dy : dy;
        touch_down = 0U;
        if (touch_ignored) {
            touch_ignored = 0U;
        } else if (overlay == OVERLAY_CH347) {
            if (!hold_sent && in_rect(last_x, last_y, 45U, 18U, 92U, 58U)) {
                close_overlay();
            } else if (!hold_sent && in_rect(last_x, last_y, 15U, 80U, 65U, 145U)) {
                current.ch347_selected = (current.ch347_selected + 3U) % 4U;
                mode_picker_update();
            } else if (!hold_sent && in_rect(last_x, last_y, 175U, 80U, 225U, 145U)) {
                current.ch347_selected = (current.ch347_selected + 1U) % 4U;
                mode_picker_update();
            }
            if (!hold_sent) lv_obj_set_width(hold_progress, 0);
        } else if (overlay == OVERLAY_PINOUT) {
            if (ax < 18 && ay < 18)
                close_overlay();
        } else if (overlay == OVERLAY_PROVISION) {
            provision_mark_dismissed();
            close_overlay();
            go_page(2U, 0U);
        } else if (overlay == OVERLAY_NONE && !hold_sent &&
                   ax >= SWIPE_MIN && ax > ay) {
            if (dx < 0 && page_index < 3U) go_page(page_index + 1U, 1U);
            else if (dx > 0 && page_index > 0U) go_page(page_index - 1U, 1U);
        } else if (overlay == OVERLAY_NONE && !hold_sent && ax < 18 && ay < 18) {
            if (page_index == 1U &&
                in_rect(last_x, last_y, 27U, 150U, 120U, 205U)) {
                show_pinout();
            } else if (page_index == 1U && !ch347_controls_locked() &&
                in_rect(last_x, last_y, 121U, 150U, 213U, 205U)) {
                show_mode_picker();
            } else if (page_index == 2U && !current.wired &&
                       !mode_controls_locked() &&
                       in_rect(last_x, last_y, 65U, 165U, 175U, 210U)) {
                show_provision(now);
            }
        }
    }

    if (pending_event != 0U) {
        event = (int)pending_event;
        *event_arg = pending_arg;
        pending_event = 0U;
    }

    if (airlink_lv_port_take_full_redraw_request() != 0U)
        request_full_redraw(2U);
    if (full_redraw_frames != 0U) {
        lv_obj_invalidate(screen);
        full_redraw_frames--;
    }

    {
        uint32_t before = hw_display->flush_count;
        uint32_t frames_before = hw_display->completed_frame_count;
        uint32_t kind = animation_kind();
        uint64_t started = ui_read_time();
        uint64_t finished;
        uint32_t loop_us;
        uint32_t frames;

        lv_timer_handler();
        finished = ui_read_time();
        loop_us = (uint32_t)((finished - started) /
                             (TIMEBASE_HZ / 1000000ULL));
        frames = hw_display->completed_frame_count - frames_before;
        if (loop_us > ui_stats.loop_max_us)
            ui_stats.loop_max_us = loop_us;
        if (hw_display->flush_count != before) {
            ui_stats.flush_count = hw_display->flush_count;
            ui_stats.flush_bytes = hw_display->flush_bytes;
            ui_stats.flush_avg_us = hw_display->flush_count != 0U ?
                (uint32_t)(hw_display->flush_total_cycles /
                           hw_display->flush_count / 25U) : 0U;
            ui_stats.flush_max_us = hw_display->flush_max_cycles / 25U;
        }
        if (frames != 0U) {
            if (hw_display->last_frame_bytes >= FULL_FRAME_BYTES)
                ui_stats.full_frame_count += frames;
            else
                ui_stats.partial_frame_count += frames;
            if (page_slide_frame_pending) {
                ui_stats.page_frame_count += frames;
                page_frame_total_us += loop_us;
                ui_stats.page_frame_avg_us =
                    (uint32_t)(page_frame_total_us /
                               ui_stats.page_frame_count);
                if (loop_us > ui_stats.page_frame_max_us)
                    ui_stats.page_frame_max_us = loop_us;
                page_slide_frame_pending = 0U;
            }
        }
        ui_stats.frame_count = hw_display->completed_frame_count;
        ui_stats.frame_bytes = hw_display->last_frame_bytes;
        ui_stats.spi_parent_hz = hw_display->spi_parent_hz;
        ui_stats.spi_sclk_hz = hw_display->spi_sclk_hz;
        performance_sample(finished, frames, kind);
    }
    return event;
}

void airlink_ui_set_ch347_result(uint32_t state)
{
    current.ch347_state = state;
    if (overlay != OVERLAY_CH347) return;
    switch (state) {
    case AIRLINK_UI_CH347_PREPARING:
        lv_label_set_text(mode_warning, "准备无线共享服务");
        break;
    case AIRLINK_UI_CH347_SWITCHING:
        lv_label_set_text(mode_warning, "正在切换");
        break;
    case AIRLINK_UI_CH347_ENUMERATING:
        lv_label_set_text(mode_warning, "等待重新枚举");
        break;
    case AIRLINK_UI_CH347_SUCCESS:
        show_result(1U, ui_read_time());
        break;
    case AIRLINK_UI_CH347_ERROR:
        show_result(0U, ui_read_time());
        break;
    default:
        break;
    }
}

void airlink_ui_show_mode_transition(uint32_t wired)
{
    uint64_t now = ui_read_time();

    if (ui_stats.saver)
        leave_saver(now);
    saver_reset(now, AIRLINK_UI_SAVER_RESET_GPIOA29, 1U);
    if (wired) {
        provision_pending_clear();
        provision_view_reset();
    }
    close_overlay();
    current.wired = wired;
    mode_transition_wired = wired ? 1U : 0U;
    mode_transition_active = 1U;
    mode_transition_slow = 0U;
    mode_transition_phase = 0U;
    mode_transition_started = now;
    mode_transition_anim_time = now;
    lv_arc_set_angles(home_transition_arc, 0, 72);
    lv_obj_set_style_arc_color(home_transition_arc,
        lv_color_hex(C_CYAN), LV_PART_INDICATOR);
    lv_obj_clear_flag(home_transition_arc, LV_OBJ_FLAG_HIDDEN);
    saver_reset_animation(now);
    saver_update_mode();
    go_page(0U, 0U);
    page_update(now);
}

void airlink_ui_set_mode_result(uint32_t success, uint32_t wired,
                                uint32_t phase, uint32_t error)
{
    uint64_t now = ui_read_time();

    current.wired = wired ? 1U : 0U;
    current.network.system_mode = current.wired;
    current.network.system_phase = phase;
    current.network.system_error = error;
    if (success)
        current.network.flags &= ~AIRLINK_UI_STATUS_SYSTEM_FAULT;
    else
        current.network.flags |= AIRLINK_UI_STATUS_SYSTEM_FAULT;

    if (!success) {
        mode_transition_active = 0U;
        mode_transition_slow = 0U;
        home_spinner_sync();
    } else {
        mode_transition_update(now);
    }
    page_update_or_defer(now);
}

void airlink_ui_note_loop_us(uint32_t loop_us)
{
    if (loop_us > ui_stats.loop_max_us)
        ui_stats.loop_max_us = loop_us;
}

void airlink_ui_get_stats(struct airlink_ui_stats *stats)
{
    lv_mem_monitor_t monitor;
    lv_mem_monitor(&monitor);
    ui_stats.heap_free = monitor.free_size;
    ui_stats.heap_used_pct = monitor.used_pct;
    *stats = ui_stats;
}

uint32_t airlink_ui_take_saver_reset_reason(void)
{
    uint32_t reason = saver_reset_reason_pending;
    saver_reset_reason_pending = AIRLINK_UI_SAVER_RESET_NONE;
    return reason;
}
