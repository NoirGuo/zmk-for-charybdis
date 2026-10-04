/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/usb.h>

#include "battery_status.h"

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
#include <zmk/display_settings.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
    #define SOURCE_OFFSET 1
#else
    #define SOURCE_OFFSET 0
#endif

#ifndef ZMK_SPLIT_BLE_PERIPHERAL_COUNT
#  define ZMK_SPLIT_BLE_PERIPHERAL_COUNT 0
#endif

#define BUFFER_SIZE LV_CANVAS_BUF_SIZE(5, 8, LV_COLOR_FORMAT_GET_BPP(LV_COLOR_FORMAT_L8), LV_DRAW_BUF_STRIDE_ALIGN)
#define SPLIT_BATTERY_BAR_MAX_WIDTH 54
#define SPLIT_BATTERY_BAR_HEIGHT 4

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct battery_state {
    uint8_t source;
    uint8_t level;
    bool usb_present;
    bool central;
};

struct battery_object {
    lv_obj_t *symbol;
    lv_obj_t *label;
    lv_obj_t *bar_track;
    lv_obj_t *bar_fill;
} battery_objects[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET];

static struct battery_state central_state;
static bool central_state_valid;
static struct battery_state peripheral_states[MAX(1, ZMK_SPLIT_BLE_PERIPHERAL_COUNT)];
static bool peripheral_state_valid[MAX(1, ZMK_SPLIT_BLE_PERIPHERAL_COUNT)];
static bool split_layout;
    
static lv_color_t battery_image_buffer[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET][BUFFER_SIZE];

static void draw_battery(lv_obj_t *canvas, uint8_t level, bool usb_present) {
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_rect_dsc_t rect_fill_dsc;
    lv_draw_rect_dsc_init(&rect_fill_dsc);
    rect_fill_dsc.bg_color = lv_color_white();

    if (usb_present) {
        rect_fill_dsc.bg_opa = LV_OPA_TRANSP;
        rect_fill_dsc.border_color = lv_color_white();
        rect_fill_dsc.border_width = 1;
    }

    lv_canvas_set_px(canvas, 0, 0, lv_color_white(), LV_OPA_COVER);
    lv_canvas_set_px(canvas, 4, 0, lv_color_white(), LV_OPA_COVER);

    lv_area_t rect_coords;
    bool rect_draw = true;
    
    if (level <= 10 || usb_present) {
        rect_coords = (lv_area_t){1, 2, 3, 6};
    } else if (level <= 30) {
        rect_coords = (lv_area_t){1, 2, 3, 5};
    } else if (level <= 50) {
        rect_coords = (lv_area_t){1, 2, 3, 4};
    } else if (level <= 70) {
        rect_coords = (lv_area_t){1, 2, 3, 3};
    } else if (level <= 90) {
        rect_coords = (lv_area_t){1, 2, 3, 2};
    } else {
        rect_draw = false;
    }

    if (rect_draw) {
        lv_draw_rect(&layer, &rect_fill_dsc, &rect_coords);
    }

    lv_canvas_finish_layer(canvas, &layer);
}

static void set_battery_symbol(uint8_t object_index, struct battery_state state) {
    if (object_index >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET) {
        return;
    }
    LOG_DBG("source: %d, level: %d, usb: %d", object_index, state.level, state.usb_present);
    lv_obj_t *symbol = battery_objects[object_index].symbol;
    lv_obj_t *label = battery_objects[object_index].label;
    lv_obj_t *bar_track = battery_objects[object_index].bar_track;
    lv_obj_t *bar_fill = battery_objects[object_index].bar_fill;

    if (split_layout && object_index < 2) {
        if (state.level > 0) {
            lv_label_set_text_fmt(label, "%u", state.level);
        } else {
            lv_label_set_text(label, "X");
        }

        lv_obj_set_style_text_font(label, &lv_font_unscii_8, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(label, SPLIT_BATTERY_BAR_MAX_WIDTH);
        lv_obj_set_size(bar_fill,
                        MAX(1, DIV_ROUND_UP(SPLIT_BATTERY_BAR_MAX_WIDTH * state.level, 100)),
                        SPLIT_BATTERY_BAR_HEIGHT);
        /* Keep the filled side against the outer edge. As the level falls, the
         * hollow section therefore grows from the screen centre outwards. */
        lv_obj_align(bar_fill,
                     object_index == 0 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID,
                     0, 0);

        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
        lv_obj_clear_flag(bar_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(bar_track);
        if (state.level > 0) {
            lv_obj_clear_flag(bar_fill, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(bar_fill);
        } else {
            lv_obj_add_flag(bar_fill, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    lv_obj_add_flag(bar_track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bar_fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    draw_battery(symbol, state.level, state.usb_present);
    lv_label_set_text_fmt(label, "%4u%% ", state.level);

    if (state.level > 0 || state.usb_present) {
        lv_obj_clear_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(symbol);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    } else {
        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

void zmk_widget_dongle_battery_status_refresh(struct zmk_widget_dongle_battery_status *widget) {
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; i++) {
        lv_obj_add_flag(battery_objects[i].symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_objects[i].label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_objects[i].bar_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_objects[i].bar_fill, LV_OBJ_FLAG_HIDDEN);
    }

    if (split_layout) {
        /* Extra height keeps the 8 px font inside the parent while the bar
         * itself remains flush with the physical bottom row. */
        lv_obj_set_size(widget->obj, 128, 16);
        for (uint8_t i = 0; i < MIN(2, ZMK_SPLIT_BLE_PERIPHERAL_COUNT); i++) {
            struct battery_state state = {
                .source = i,
                .level = 0,
                .central = false,
            };
            if (peripheral_state_valid[i]) {
                state = peripheral_states[i];
            }
            set_battery_symbol(i, state);

            struct battery_object *object = &battery_objects[i];
            if (i == 0) {
                lv_obj_align(object->label, LV_ALIGN_BOTTOM_LEFT, 2, -5);
                lv_obj_align(object->bar_track, LV_ALIGN_BOTTOM_LEFT, 2, 0);
            } else {
                lv_obj_align(object->label, LV_ALIGN_BOTTOM_RIGHT, -2, -5);
                lv_obj_align(object->bar_track, LV_ALIGN_BOTTOM_RIGHT, -2, 0);
            }
        }
        return;
    }

    bool show_central = IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY);
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    show_central = show_central && zmk_display_settings_dongle_battery_enabled();
#endif

    uint8_t offset = show_central ? 1U : 0U;
    if (show_central && central_state_valid) {
        set_battery_symbol(0, central_state);
    }

    for (uint8_t i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        if (peripheral_state_valid[i]) {
            set_battery_symbol(i + offset, peripheral_states[i]);
        }
    }

    lv_obj_set_size(widget->obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; i++) {
        lv_obj_align(battery_objects[i].symbol, LV_ALIGN_TOP_RIGHT, 0, i * 10);
        lv_obj_align_to(battery_objects[i].label, battery_objects[i].symbol,
                        LV_ALIGN_OUT_LEFT_MID, 0, 0);
    }

    /* Preserve the first row's original position and align only the second
     * percentage label's right edge with it. */
    if (ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET >= 2) {
        lv_obj_set_x(battery_objects[1].label, lv_obj_get_x(battery_objects[0].label));
    }
}

void zmk_widget_dongle_battery_status_set_split_layout(
    struct zmk_widget_dongle_battery_status *widget, bool enabled) {
    split_layout = enabled;
    zmk_widget_dongle_battery_status_refresh(widget);
}

void battery_status_update_cb(struct battery_state state) {
    if (state.central) {
        central_state = state;
        central_state_valid = true;
    } else if (state.source < ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
        peripheral_states[state.source] = state;
        peripheral_state_valid[state.source] = true;
    }

    struct zmk_widget_dongle_battery_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        zmk_widget_dongle_battery_status_refresh(widget);
    }
}

static struct battery_state peripheral_battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev = as_zmk_peripheral_battery_state_changed(eh);
    return (struct battery_state){
        .source = ev->source,
        .level = ev->state_of_charge,
        .central = false,
    };
}

static struct battery_state central_battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_state) {
        .source = 0,
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
        .central = true,
    };
}

static struct battery_state battery_status_get_state(const zmk_event_t *eh) { 
    if (as_zmk_peripheral_battery_state_changed(eh) != NULL) {
        return peripheral_battery_status_get_state(eh);
    } else {
        return central_battery_status_get_state(eh);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_dongle_battery_status, struct battery_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_peripheral_battery_state_changed);

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
#endif /* !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
#endif /* IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY) */

int zmk_widget_dongle_battery_status_init(struct zmk_widget_dongle_battery_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);

    /* The YADS bars use the physical screen edges, so the container must not
     * contribute theme padding, borders, or an inset content area. */
    lv_obj_remove_style_all(widget->obj);
    lv_obj_set_size(widget->obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; i++) {
        lv_obj_t *image_canvas = lv_canvas_create(widget->obj);
        lv_obj_t *battery_label = lv_label_create(widget->obj);
        lv_obj_t *battery_bar_track = lv_obj_create(widget->obj);
        lv_obj_t *battery_bar_fill = lv_obj_create(battery_bar_track);

        lv_canvas_set_buffer(image_canvas, battery_image_buffer[i], 5, 8, LV_COLOR_FORMAT_L8);

        lv_obj_remove_style_all(battery_bar_track);
        lv_obj_set_style_bg_opa(battery_bar_track, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(battery_bar_track, lv_color_black(), 0);
        lv_obj_set_style_border_width(battery_bar_track, 1, 0);
        lv_obj_set_style_radius(battery_bar_track, 0, 0);
        lv_obj_set_size(battery_bar_track, SPLIT_BATTERY_BAR_MAX_WIDTH,
                        SPLIT_BATTERY_BAR_HEIGHT);

        lv_obj_remove_style_all(battery_bar_fill);
        lv_obj_set_style_bg_color(battery_bar_fill, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(battery_bar_fill, LV_OPA_COVER, 0);
        lv_obj_set_size(battery_bar_fill, 1, SPLIT_BATTERY_BAR_HEIGHT);

        lv_obj_align(image_canvas, LV_ALIGN_TOP_RIGHT, 0, i * 10);
        lv_obj_align_to(battery_label, image_canvas, LV_ALIGN_OUT_LEFT_MID, 0, 0);

        lv_obj_add_flag(image_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_bar_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_bar_fill, LV_OBJ_FLAG_HIDDEN);
        
        battery_objects[i] = (struct battery_object){
            .symbol = image_canvas,
            .label = battery_label,
            .bar_track = battery_bar_track,
            .bar_fill = battery_bar_fill,
        };
    }

    sys_slist_append(&widgets, &widget->node);

    widget_dongle_battery_status_init();

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget) {
    return widget->obj;
}
