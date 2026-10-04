/*
 * Copyright (c) 2026 S7venYoung
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/keys.h>

#include "typed_keys_status.h"

#define TYPED_KEYS_MAX 5
#define HID_KEY_A 0x04
#define HID_KEY_Z 0x1D
#define HID_KEY_BACKSPACE 0x2A
#define TYPED_KEYS_IDLE_TIMEOUT K_SECONDS(5)

struct typed_keys_state {
    char text[20];
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static char typed_keys[TYPED_KEYS_MAX + 1];
static size_t typed_length;
static char layer_letter;
static uint8_t pressed_modifiers;
static bool shortcut_key_down;
static bool idle_layer_visible = true;
static struct k_work_delayable typed_keys_idle_work;

static void clear_typed_keys(void) {
    typed_length = 0;
    typed_keys[0] = '\0';
    layer_letter = '\0';
}

static void append_letter(char letter) {
    if (typed_length == TYPED_KEYS_MAX) {
        memmove(typed_keys, typed_keys + 1, TYPED_KEYS_MAX - 1);
        typed_length--;
    }

    typed_keys[typed_length++] = letter;
    typed_keys[typed_length] = '\0';
}

static struct typed_keys_state typed_keys_get_state(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *layer_ev = as_zmk_layer_state_changed(eh);
    if (layer_ev != NULL && layer_ev->state) {
        layer_letter = '\0';
    }

    const struct zmk_keycode_state_changed *key_ev = as_zmk_keycode_state_changed(eh);
    if (key_ev != NULL && key_ev->usage_page == HID_USAGE_KEY) {
        if (key_ev->state) {
            idle_layer_visible = false;
            k_work_reschedule_for_queue(zmk_display_work_q(), &typed_keys_idle_work,
                                        TYPED_KEYS_IDLE_TIMEOUT);
        }

        if (is_mod(key_ev->usage_page, key_ev->keycode)) {
            if (key_ev->state) {
                /* Start a fresh display sequence for the shortcut being entered. */
                clear_typed_keys();
                pressed_modifiers++;
            } else if (pressed_modifiers > 0) {
                pressed_modifiers--;
            }
        } else if (!key_ev->state && shortcut_key_down) {
            /* Releasing the shortcut's letter or number completes the action. */
            clear_typed_keys();
            shortcut_key_down = false;
        } else if (key_ev->state && key_ev->keycode >= HID_KEY_A &&
                   key_ev->keycode <= HID_KEY_Z) {
            char letter = 'A' + (key_ev->keycode - HID_KEY_A);
            append_letter(letter);
            shortcut_key_down = pressed_modifiers > 0;
            if (zmk_keymap_highest_layer_active() != 0) {
                layer_letter = letter;
            }
        } else if (key_ev->state && pressed_modifiers > 0) {
            /* Digits and other shortcut keys are not rendered, but their
             * release must still clear the shortcut display. */
            shortcut_key_down = true;
        } else if (key_ev->state && key_ev->keycode == HID_KEY_BACKSPACE &&
                   typed_length > 0) {
            typed_keys[--typed_length] = '\0';
        }
    }

    struct typed_keys_state state = {};
    uint8_t layer = zmk_keymap_highest_layer_active();
    if (layer != 0 || idle_layer_visible) {
        const char *layer_name = zmk_keymap_layer_name(layer);
        if (layer_name == NULL) {
            if (layer_letter == '\0') {
                snprintf(state.text, sizeof(state.text), "L%u", layer);
            } else {
                snprintf(state.text, sizeof(state.text), "L%u %c", layer, layer_letter);
            }
        } else if (layer_letter == '\0') {
            snprintf(state.text, sizeof(state.text), "%s", layer_name);
        } else {
            snprintf(state.text, sizeof(state.text), "%s %c", layer_name, layer_letter);
        }
    } else if (typed_length == 0) {
        state.text[0] = '\0';
    } else {
        snprintf(state.text, sizeof(state.text), "%s", typed_keys);
    }

    return state;
}

static void set_typed_keys(struct zmk_widget_typed_keys_status *widget,
                           struct typed_keys_state state) {
    lv_label_set_text(widget->obj, state.text);
}

static void typed_keys_update_cb(struct typed_keys_state state) {
    struct zmk_widget_typed_keys_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_typed_keys(widget, state); }
}

static void typed_keys_idle_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    clear_typed_keys();
    idle_layer_visible = true;
    typed_keys_update_cb(typed_keys_get_state(NULL));
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_typed_keys_status, struct typed_keys_state,
                            typed_keys_update_cb, typed_keys_get_state)
ZMK_SUBSCRIPTION(widget_typed_keys_status, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(widget_typed_keys_status, zmk_layer_state_changed);

int zmk_widget_typed_keys_status_init(struct zmk_widget_typed_keys_status *widget,
                                      lv_obj_t *parent) {
    k_work_init_delayable(&typed_keys_idle_work, typed_keys_idle_work_handler);

    widget->obj = lv_label_create(parent);
    lv_obj_set_width(widget->obj, 126);
    lv_obj_set_style_text_align(widget->obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(widget->obj, &lv_font_unscii_16, 0);
    lv_label_set_long_mode(widget->obj, LV_LABEL_LONG_CLIP);

    sys_slist_append(&widgets, &widget->node);
    widget_typed_keys_status_init();
    return 0;
}

lv_obj_t *zmk_widget_typed_keys_status_obj(struct zmk_widget_typed_keys_status *widget) {
    return widget->obj;
}
