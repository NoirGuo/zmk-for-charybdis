/* SPDX-License-Identifier: MIT */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct zmk_monitor_status {
    bool present;
    uint8_t left_battery;
    uint8_t right_battery;
    uint8_t layer;
    char layer_name[5];
    uint8_t modifiers;
    uint8_t wpm;
    uint8_t profile;
    bool usb_ready;
    bool ble_connected;
    int8_t rssi;
    uint32_t last_seen_ms;
};

void zmk_monitor_status_snapshot(struct zmk_monitor_status *status);

/* Implemented by the active display screen. Runs through the display queue. */
void zmk_monitor_status_changed(void);
