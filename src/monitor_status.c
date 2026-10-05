/* SPDX-License-Identifier: MIT
 *
 * Charybdis Dongle 模式：本地键盘状态聚合。
 *
 * 本文件运行在 dongle（= split central，带 GC9A01 屏 + USB）上，
 * 不再监听键盘 BLE 广播（广播功能已移除）。所有屏幕数据直接从
 * ZMK 本地 API 与 split 链路获取：
 *   - 层名 / WPM / 修饰键 / 输入字符：dongle 本地（按键在 central 处理）
 *   - 左右手电量：CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING
 *   - dongle 自身电量：UI 直接读 zmk_battery_state_of_charge()
 *
 * typed_keys 跟踪逻辑移植自 prospector-zmk-module main 分支。
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/monitor_status.h>
#include <zmk/keymap.h>
#include <zmk/wpm.h>
#include <zmk/ble.h>
#include <zmk/usb.h>
#include <zmk/hid.h>
#include <zmk/keys.h>
#include <zmk/split/central.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/battery_state_changed.h>

LOG_MODULE_REGISTER(dongle_monitor, CONFIG_ZMK_LOG_LEVEL);

/* ========================================================================
 * typed_keys：最近键入字符（最多 5 个）
 * ======================================================================== */
#define TYPED_KEYS_MAX 5
#define HID_KEY_A 0x04
#define HID_KEY_Z 0x1D
#define HID_KEY_1 0x1E
#define HID_KEY_0 0x27
#define HID_KEY_BACKSPACE 0x2A
#define HID_KEY_SPACE 0x2C
#define HID_KEY_MINUS 0x2D
#define HID_KEY_EQUAL 0x2E
#define HID_KEY_LEFT_BRACE 0x2F
#define HID_KEY_RIGHT_BRACE 0x30
#define HID_KEY_BACKSLASH 0x31
#define HID_KEY_SEMICOLON 0x33
#define HID_KEY_QUOTE 0x34
#define HID_KEY_GRAVE 0x35
#define HID_KEY_COMMA 0x36
#define HID_KEY_PERIOD 0x37
#define HID_KEY_SLASH 0x38
#define HID_KEY_LEFT_SHIFT 0xE1
#define HID_KEY_RIGHT_SHIFT 0xE5
#define TYPED_KEYS_IDLE_TIMEOUT K_SECONDS(5)

static char typed_keys[TYPED_KEYS_MAX + 1];
static size_t typed_keys_len;
static uint8_t pressed_modifiers_count;
static bool shortcut_key_down;
static bool shift_pressed;
static struct k_work_delayable typed_keys_idle_work;

/* US ANSI 布局：可打印 HID 键码 -> 字符。无可打印表示返回 '\0'。 */
static char keycode_to_char(uint8_t keycode, bool shifted) {
    static const char digits_low[] = "1234567890";   /* 0x1E-0x27 */
    static const char digits_high[] = "!@#$%^&*()";
    static const char punct_low[] = "-=[]\\;'`,./"; /* 0x2D..0x38 */
    static const char punct_high[] = "_+{}|:\"~<>?";
    static const uint8_t punct_keys[] = {
        HID_KEY_MINUS,      HID_KEY_EQUAL,      HID_KEY_LEFT_BRACE,
        HID_KEY_RIGHT_BRACE, HID_KEY_BACKSLASH, HID_KEY_SEMICOLON,
        HID_KEY_QUOTE,      HID_KEY_GRAVE,      HID_KEY_COMMA,
        HID_KEY_PERIOD,     HID_KEY_SLASH,
    };

    if (keycode >= HID_KEY_1 && keycode <= HID_KEY_0) {
        return shifted ? digits_high[keycode - HID_KEY_1] : digits_low[keycode - HID_KEY_1];
    }
    if (keycode == HID_KEY_SPACE) {
        return ' ';
    }
    for (size_t i = 0; i < sizeof(punct_keys); i++) {
        if (keycode == punct_keys[i]) {
            return shifted ? punct_high[i] : punct_low[i];
        }
    }
    return '\0';
}

static void typed_keys_clear(void) {
    typed_keys_len = 0;
    typed_keys[0] = '\0';
}

static void typed_keys_append(char letter) {
    if (typed_keys_len == TYPED_KEYS_MAX) {
        memmove(typed_keys, typed_keys + 1, TYPED_KEYS_MAX - 1);
        typed_keys_len--;
    }

    typed_keys[typed_keys_len++] = letter;
    typed_keys[typed_keys_len] = '\0';
}

static void typed_keys_idle_handler(struct k_work *work) {
    ARG_UNUSED(work);

    typed_keys_clear();
    zmk_monitor_status_changed();
}

static int keycode_state_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || ev->usage_page != HID_USAGE_KEY) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool keys_changed = false;

    if (ev->state) {
        k_work_reschedule(&typed_keys_idle_work, TYPED_KEYS_IDLE_TIMEOUT);
    }

    if (is_mod(ev->usage_page, ev->keycode)) {
        if (ev->keycode == HID_KEY_LEFT_SHIFT || ev->keycode == HID_KEY_RIGHT_SHIFT) {
            /* Shift 只改变符号映射，不清空缓冲。 */
            shift_pressed = ev->state;
        } else if (ev->state) {
            /* Ctrl/Alt/GUI 开始快捷键序列：清空显示。 */
            typed_keys_clear();
            pressed_modifiers_count++;
        } else if (pressed_modifiers_count > 0) {
            pressed_modifiers_count--;
        }
        keys_changed = true;
    } else if (!ev->state && shortcut_key_down) {
        /* 释放快捷键的字母/数字，动作完成。 */
        typed_keys_clear();
        shortcut_key_down = false;
        keys_changed = true;
    } else if (ev->state && ev->keycode == HID_KEY_BACKSPACE && typed_keys_len > 0) {
        typed_keys[--typed_keys_len] = '\0';
        keys_changed = true;
    } else if (ev->state) {
        char ch;
        if (ev->keycode >= HID_KEY_A && ev->keycode <= HID_KEY_Z) {
            ch = 'A' + (ev->keycode - HID_KEY_A);
        } else {
            ch = keycode_to_char(ev->keycode, shift_pressed);
        }
        if (ch != '\0') {
            typed_keys_append(ch);
            shortcut_key_down = pressed_modifiers_count > 0;
            keys_changed = true;
        }
    }

    if (keys_changed) {
        zmk_monitor_status_changed();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_keycode_listener, keycode_state_listener);
ZMK_SUBSCRIPTION(dongle_keycode_listener, zmk_keycode_state_changed);

/* ========================================================================
 * 层切换：layer 行为不产生 keycode 事件，需单独刷新（层名显示）。
 * ======================================================================== */
static int layer_state_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    zmk_monitor_status_changed();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_layer_listener, layer_state_listener);
ZMK_SUBSCRIPTION(dongle_layer_listener, zmk_layer_state_changed);

/* ========================================================================
 * 左右手电量：split central 拉取到新电量时刷新屏幕（L/R 弧）。
 * 注：dongle 自身电量（M 弧）由 UI 的 battery_event_listener 刷新。
 * ======================================================================== */
static int peripheral_battery_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    zmk_monitor_status_changed();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_peripheral_battery_listener, peripheral_battery_listener);
ZMK_SUBSCRIPTION(dongle_peripheral_battery_listener, zmk_peripheral_battery_state_changed);

/* ========================================================================
 * 状态快照：UI 每次刷新调用。
 * ======================================================================== */
__attribute__((weak)) void zmk_monitor_status_changed(void) {}

void zmk_monitor_status_snapshot(struct zmk_monitor_status *status) {
    memset(status, 0, sizeof(*status));

    status->present = true;
    status->last_seen_ms = k_uptime_get_32();

    /* 层名（display-name）。 */
    uint8_t layer_index = zmk_keymap_highest_layer_active();
    status->layer = layer_index;
    zmk_keymap_layer_id_t layer_id = zmk_keymap_layer_index_to_id(layer_index);
    const char *name = zmk_keymap_layer_name(layer_id);
    if (name != NULL && name[0] != '\0') {
        strncpy(status->layer_name, name, sizeof(status->layer_name) - 1);
        status->layer_name[sizeof(status->layer_name) - 1] = '\0';
    }

    /* 最近输入字符。 */
    memcpy(status->typed_keys, typed_keys, sizeof(status->typed_keys));
    status->typed_keys[sizeof(status->typed_keys) - 1] = '\0';

    /* 修饰键（HID 报告位：0x01|0x10=Ctrl、0x02|0x20=Shift、0x04|0x40=Alt、0x08|0x80=GUI）。 */
    struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
    status->modifiers = (report != NULL) ? report->body.modifiers : 0;

    /* WPM / BLE profile。 */
    status->wpm = zmk_wpm_get_state();
    int profile = zmk_ble_active_profile_index();
    status->profile = (profile < 0) ? 0 : (uint8_t)profile;

    /* 电量：L=左半（peripheral 0）、R=右半（peripheral 1）；M=dongle 自身由 UI 直读。 */
    uint8_t level = 0;
    if (zmk_split_central_get_peripheral_battery_level(0, &level) == 0) {
        status->left_battery = level;
    }
    if (zmk_split_central_get_peripheral_battery_level(1, &level) == 0) {
        status->right_battery = level;
    }

    /* USB / BLE 连接状态。 */
    status->usb_ready = zmk_usb_is_hid_ready();
    status->ble_connected = zmk_ble_active_profile_is_connected();

    /* dongle 模式无 RSSI 概念。 */
    status->rssi = 0;
}
