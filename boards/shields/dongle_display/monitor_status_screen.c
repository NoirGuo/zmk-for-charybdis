/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <string.h>

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/monitor_status.h>

#include "custom_status_screen.h"

/*
 * Fixed 128x64 layout (ported from S7venYoung/zmk-sofle-dongle-dya, theme
 * switching removed):
 *
 *   WPM 42          U B2
 *        BASE
 *     CTRL SHIFT
 *  90%      80%      75%
 *  [====]   [====]   [====]
 *
 * Left bar   : left half keyboard battery
 * Middle bar : monitor (dongle) own battery
 * Right bar  : right half keyboard battery
 */

static lv_obj_t *screen;
static lv_obj_t *wpm;
static lv_obj_t *connection;
static lv_obj_t *layer;
static lv_obj_t *modifiers;
static lv_obj_t *left_battery;
static lv_obj_t *mid_battery;
static lv_obj_t *right_battery;
static lv_obj_t *left_bar;
static lv_obj_t *mid_bar;
static lv_obj_t *right_bar;
static lv_obj_t *left_fill;
static lv_obj_t *mid_fill;
static lv_obj_t *right_fill;
static bool ready;

static void clean_obj(lv_obj_t *obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static void configure_bar(lv_obj_t **track, lv_obj_t **fill) {
    *track = lv_obj_create(screen);
    clean_obj(*track);
    lv_obj_set_size(*track, 42, 5);
    lv_obj_set_style_border_width(*track, 1, 0);
    lv_obj_set_style_border_color(*track, lv_color_black(), 0);

    *fill = lv_obj_create(*track);
    clean_obj(*fill);
    lv_obj_set_style_bg_color(*fill, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(*fill, LV_OPA_COVER, 0);
    lv_obj_align(*fill, LV_ALIGN_LEFT_MID, 1, 0);
}

static void set_bar(lv_obj_t *fill, uint8_t level) {
    level = MIN(level, 100);
    lv_obj_set_size(fill, MAX(1, (40 * level) / 100), 3);
}

static void set_battery_text(lv_obj_t *label, uint8_t level) {
    char text[8];
    if (level > 100) {
        lv_label_set_text(label, "--%");
        return;
    }
    snprintf(text, sizeof(text), "%u%%", level);
    lv_label_set_text(label, text);
}

static void set_modifiers(uint8_t flags) {
    char text[24] = "";
    bool separator = false;

#define APPEND_MODIFIER(mask, name)                                                                \
    do {                                                                                           \
        if ((flags & (mask)) != 0) {                                                               \
            snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%s",                   \
                     separator ? " " : "", name);                                                \
            separator = true;                                                                      \
        }                                                                                          \
    } while (0)

    APPEND_MODIFIER(0x11, "CTRL");
    APPEND_MODIFIER(0x22, "SHIFT");
    APPEND_MODIFIER(0x44, "ALT");
    APPEND_MODIFIER(0x88, "GUI");

#undef APPEND_MODIFIER

    lv_label_set_text(modifiers, text);
}

static void update_screen(struct k_work *work) {
    ARG_UNUSED(work);
    if (!ready) {
        return;
    }

    struct zmk_monitor_status status;
    zmk_monitor_status_snapshot(&status);
    bool alive = status.present && (k_uptime_get_32() - status.last_seen_ms) < 15000U;
    char text[24];

    if (alive) {
        snprintf(text, sizeof(text), "WPM %u", status.wpm);
        lv_label_set_text(wpm, text);

        if (status.ble_connected) {
            snprintf(text, sizeof(text), "%c B%u", status.usb_ready ? 'U' : '-', status.profile);
        } else {
            snprintf(text, sizeof(text), "%c -", status.usb_ready ? 'U' : '-');
        }
        lv_label_set_text(connection, text);

        /* Central info: layer name only. */
        if (status.layer_name[0] != '\0') {
            snprintf(text, sizeof(text), "%s", status.layer_name);
        } else {
            snprintf(text, sizeof(text), "LAYER %u", status.layer);
        }
        lv_label_set_text(layer, text);

        set_modifiers(status.modifiers);
        set_battery_text(left_battery, status.left_battery);
        set_battery_text(mid_battery, zmk_battery_state_of_charge());
        set_battery_text(right_battery, status.right_battery);
        set_bar(left_fill, status.left_battery);
        set_bar(mid_fill, zmk_battery_state_of_charge());
        set_bar(right_fill, status.right_battery);
    } else {
        lv_label_set_text(wpm, "WPM --");
        lv_label_set_text(connection, "-- --");
        lv_label_set_text(layer, "WAITING");
        lv_label_set_text(modifiers, "");
        lv_label_set_text(left_battery, "--%");
        set_battery_text(mid_battery, zmk_battery_state_of_charge());
        lv_label_set_text(right_battery, "--%");
        set_bar(left_fill, 0);
        set_bar(mid_fill, zmk_battery_state_of_charge());
        set_bar(right_fill, 0);
    }
}

K_WORK_DEFINE(screen_update_work, update_screen);

void zmk_monitor_status_changed(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &screen_update_work);
    }
}

void zmk_display_settings_runtime_changed(void) {
    zmk_monitor_status_changed();
}

/* Keep the middle (monitor own battery) bar live: refresh when the battery
 * module publishes a new state-of-charge (every CONFIG_ZMK_BATTERY_REPORT_INTERVAL). */
static int battery_event_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    zmk_monitor_status_changed();
    return 0;
}
ZMK_LISTENER(monitor_battery, battery_event_listener);
ZMK_SUBSCRIPTION(monitor_battery, zmk_battery_state_changed);

lv_obj_t *zmk_display_status_screen(void) {
    screen = lv_obj_create(NULL);
    clean_obj(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);
    lv_obj_set_style_text_font(screen, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_letter_space(screen, 1, 0);
    lv_obj_set_style_text_line_space(screen, 1, 0);

    wpm = lv_label_create(screen);
    connection = lv_label_create(screen);
    layer = lv_label_create(screen);
    modifiers = lv_label_create(screen);
    left_battery = lv_label_create(screen);
    mid_battery = lv_label_create(screen);
    right_battery = lv_label_create(screen);

    clean_obj(wpm);
    clean_obj(connection);
    clean_obj(layer);
    clean_obj(modifiers);
    clean_obj(left_battery);
    clean_obj(mid_battery);
    clean_obj(right_battery);

    lv_obj_set_style_text_font(layer, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_align(layer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(modifiers, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_align(wpm, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(connection, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_align(layer, LV_ALIGN_CENTER, 0, -7);
    lv_obj_align(modifiers, LV_ALIGN_CENTER, 0, 13);
    lv_obj_align(left_battery, LV_ALIGN_BOTTOM_LEFT, 0, -6);
    lv_obj_align(mid_battery, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_align(right_battery, LV_ALIGN_BOTTOM_RIGHT, 0, -6);

    configure_bar(&left_bar, &left_fill);
    configure_bar(&mid_bar, &mid_fill);
    configure_bar(&right_bar, &right_fill);
    lv_obj_align(left_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_align(mid_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_align(right_bar, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    ready = true;
    update_screen(NULL);
    return screen;
}
