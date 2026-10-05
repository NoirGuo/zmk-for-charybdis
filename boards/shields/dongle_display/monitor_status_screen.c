/* SPDX-License-Identifier: MIT */

#include <math.h>
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
 * GC9A01 240x240 round panel layout (1.28", v6 mock-up, 参考图坐标):
 *
 *        [ USB ]   [ BLE 1 ]         <- 顶部连接状态芯片（暗底灰框灰字）
 *       L                     R      <- L 蓝 / R 橙（电量标签，无数字）
 *             WPM 42                  <- WPM（灰标签 + 白数字，BASE 上方）
 *             BASE                    <- 层名（20px 白）
 *             QWERT                   <- 输入字符（20px 白）
 *        [CTRL][ALT][GUI][SHIFT]      <- 修饰键芯片 y=157（14px 文字框，
 *                                        按下=蓝调底 0x14242E+亮蓝框 0x4FC3F7+白字，
 *                                        未按=暗底 0x141414+灰框 0x555B66+灰字 0x9AA0A6，
 *                                        xiaozhi-esp32 颜色规则）
 *             M                       <- M 标签（16px 白）
 *   ◜ 左弧    ◟ M 弧    ◝ 右弧          <- 三条弧形电量条（圆心 120,120，
 *                                        半径 95-105，轨道深灰 0x2A2A2A）
 *
 * 弧填充方向（与模拟图一致，LVGL 屏幕坐标 y 向下）：
 *   左弧 [150°,205°] 自底端向上填充（L = 左手电量）
 *   右弧 [335°,30°]  自底端向上填充（R = 右手电量）
 *   M 弧 [55°,125°]  自左端向右填充（M = Monitor 自身电量）
 *
 * 弧色沿用 st7789v 分支规则：>30% 绿 0x4CAF50 / 10~30% 黄 0xFFD600 /
 * <=10% 红 0xFF1744。弧用 lv_line 逐点渲染（每 ~1.5° 一点），避免
 * lv_arc 对跨 0 弧与反向填充的限制。
 *
 * 失联 15 秒：层名行显示 WAITING，顶部芯片显示 "--"。
 */

#define ARC_CX 120
#define ARC_CY 120
#define ARC_R  100
#define ARC_W  10
#define ARC_N  60
#define ARC_PI 3.14159265f

#define CHIP_BG    0x141414 /* 未点亮芯片底色（xiaozhi kChipBg） */
#define CHIP_BG_ON 0x14242E /* 点亮芯片底色（xiaozhi kChipOnBg） */
#define CHIP_EDGE  0x555B66 /* 未点亮芯片边框（xiaozhi kChipBorder） */
#define CHIP_ON    0x4FC3F7 /* 点亮芯片边框（xiaozhi kChipOnBorder） */
#define CHIP_TEXT  0x9AA0A6 /* 未点亮文字（xiaozhi kDim） */

#define TRACK_GRAY 0x2A2A2A
#define BAR_GREEN  0x4CAF50
#define BAR_YELLOW 0xFFD600
#define BAR_RED    0xFF1744

#define L_COLOR 0x2196F3
#define R_COLOR 0xFF9800

static lv_obj_t *screen;

static lv_obj_t *usb_label;
static lv_obj_t *ble_label;

static lv_obj_t *wpm_lbl;  /* "WPM" 标签（灰字） */
static lv_obj_t *wpm_num;  /* WPM 数字（白字） */

static lv_obj_t *layer; /* 层名（失联时 WAITING） */
static lv_obj_t *typed; /* 最近输入字符 */

static lv_obj_t *chip_ctrl;
static lv_obj_t *chip_alt;
static lv_obj_t *chip_gui;
static lv_obj_t *chip_shift;

struct arc_def {
    lv_obj_t *track;
    lv_obj_t *fill;
    lv_point_t track_pts[ARC_N];
    lv_point_t fill_pts[ARC_N];
    float a0;        /* 轨道起始角 */
    float total;     /* 轨道角长（度，跨 0 自动） */
    float fill_from; /* 填充起点角 */
    float fill_dir;  /* 填充方向：+1 角度递增 / -1 递减 */
};

static struct arc_def arc_l;
static struct arc_def arc_m;
static struct arc_def arc_r;

static bool ready;

/* Modifier mask bits used by the status advertisement. */
#define MOD_CTRL 0x11
#define MOD_SHIFT 0x22
#define MOD_ALT 0x44
#define MOD_GUI 0x88

static void clean_obj(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static lv_color_t arc_color(uint8_t level)
{
    if (level > 30) {
        return lv_color_hex(BAR_GREEN);
    }
    if (level > 10) {
        return lv_color_hex(BAR_YELLOW);
    }
    return lv_color_hex(BAR_RED);
}

static lv_obj_t *make_chip(int cx, int cy, int w, int h, const char *text,
                           lv_obj_t **label_out)
{
    lv_obj_t *chip = lv_obj_create(screen);
    clean_obj(chip);
    lv_obj_set_size(chip, w, h);
    lv_obj_set_style_bg_color(chip, lv_color_hex(CHIP_BG), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 2, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(CHIP_EDGE), 0);
    lv_obj_set_style_radius(chip, 4, 0);
    lv_obj_align(chip, LV_ALIGN_CENTER, cx - 120, cy - 120);

    lv_obj_t *label = lv_label_create(chip);
    clean_obj(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(CHIP_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    if (label_out != NULL) {
        *label_out = label;
    }
    return chip;
}

static lv_obj_t *make_modifier_chip(int cx, int cy, int w, const char *text)
{
    lv_obj_t *chip = lv_obj_create(screen);
    clean_obj(chip);
    lv_obj_set_size(chip, w, 22);
    lv_obj_set_style_bg_color(chip, lv_color_hex(CHIP_BG), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 2, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(CHIP_EDGE), 0);
    lv_obj_set_style_radius(chip, 4, 0);
    lv_obj_align(chip, LV_ALIGN_CENTER, cx - 120, cy - 120);

    lv_obj_t *label = lv_label_create(chip);
    clean_obj(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(CHIP_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return chip;
}

static void set_modifier_chip(lv_obj_t *chip, bool on)
{
    lv_obj_t *label = lv_obj_get_child(chip, 0);

    lv_obj_set_style_bg_color(chip,
                              on ? lv_color_hex(CHIP_BG_ON) : lv_color_hex(CHIP_BG), 0);
    lv_obj_set_style_border_color(chip,
                                  on ? lv_color_hex(CHIP_ON) : lv_color_hex(CHIP_EDGE), 0);
    lv_obj_set_style_text_color(label,
                                on ? lv_color_white() : lv_color_hex(CHIP_TEXT), 0);
}

static float arc_len(float a0, float a1)
{
    return (a1 >= a0) ? (a1 - a0) : (360.0f - a0 + a1);
}

static void arc_geometry(lv_point_t *pts, float a0, float total, int n)
{
    for (int i = 0; i < n; i++) {
        float a = (a0 + total * i / (n - 1)) * ARC_PI / 180.0f;
        pts[i].x = ARC_CX + ARC_R * (lv_coord_t)cosf(a);
        pts[i].y = ARC_CY + ARC_R * (lv_coord_t)sinf(a);
    }
}

static void init_arc(struct arc_def *arc, float a0, float a1, float fill_from,
                     float fill_dir)
{
    arc->a0 = a0;
    arc->total = arc_len(a0, a1);
    arc->fill_from = fill_from;
    arc->fill_dir = fill_dir;

    arc_geometry(arc->track_pts, a0, arc->total, ARC_N);
    arc->track = lv_line_create(screen);
    lv_line_set_points(arc->track, arc->track_pts, ARC_N);
    lv_obj_set_style_line_width(arc->track, ARC_W, 0);
    lv_obj_set_style_line_color(arc->track, lv_color_hex(TRACK_GRAY), 0);
    lv_obj_set_style_line_rounded(arc->track, false, 0);

    arc->fill = lv_line_create(screen);
    lv_obj_set_style_line_width(arc->fill, ARC_W, 0);
    lv_obj_set_style_line_rounded(arc->fill, false, 0);
}

static void set_arc(struct arc_def *arc, uint8_t level)
{
    level = MIN(level, 100);
    float flen = arc->total * level / 100.0f;
    int n = MAX(2, (int)(flen / 1.5f) + 1);

    for (int i = 0; i < n; i++) {
        float a = (arc->fill_from + arc->fill_dir * flen * i / (n - 1)) *
                  ARC_PI / 180.0f;
        arc->fill_pts[i].x = ARC_CX + ARC_R * (lv_coord_t)cosf(a);
        arc->fill_pts[i].y = ARC_CY + ARC_R * (lv_coord_t)sinf(a);
    }

    lv_line_set_points(arc->fill, arc->fill_pts, n);
    lv_obj_set_style_line_color(arc->fill, arc_color(level), 0);
}

static void update_screen(struct k_work *work)
{
    ARG_UNUSED(work);
    if (!ready) {
        return;
    }

    struct zmk_monitor_status status;
    zmk_monitor_status_snapshot(&status);
    bool alive = status.present && (k_uptime_get_32() - status.last_seen_ms) < 15000U;
    char text[24];

    if (alive) {
        /* Top chips: USB / BLE + profile. */
        if (status.usb_ready) {
            lv_label_set_text(usb_label, "USB");
        } else {
            lv_label_set_text(usb_label, "--");
        }

        if (status.ble_connected) {
            snprintf(text, sizeof(text), "BLE %u", status.profile);
        } else {
            snprintf(text, sizeof(text), "--");
        }
        lv_label_set_text(ble_label, text);

        /* WPM (above layer name). */
        snprintf(text, sizeof(text), "%u", status.wpm);
        lv_label_set_text(wpm_num, text);
        lv_obj_set_style_text_color(wpm_num, lv_color_white(), 0);

        /* Row: layer name. */
        if (status.layer_name[0] != '\0') {
            snprintf(text, sizeof(text), "%s", status.layer_name);
        } else {
            snprintf(text, sizeof(text), "LAYER %u", status.layer);
        }
        lv_label_set_text(layer, text);

        /* Row: recently typed chars. */
        lv_label_set_text(typed, status.typed_keys);

        /* Modifier chips: white text always on, border = orange when
         * pressed, gray when released (参考图规则). */
        set_modifier_chip(chip_ctrl, (status.modifiers & MOD_CTRL) != 0);
        set_modifier_chip(chip_alt, (status.modifiers & MOD_ALT) != 0);
        set_modifier_chip(chip_gui, (status.modifiers & MOD_GUI) != 0);
        set_modifier_chip(chip_shift, (status.modifiers & MOD_SHIFT) != 0);

        /* Battery arcs: L = left half, M = monitor own, R = right half. */
        set_arc(&arc_l, status.left_battery);
        set_arc(&arc_m, zmk_battery_state_of_charge());
        set_arc(&arc_r, status.right_battery);
    } else {
        /* Link loss: WAITING on the layer row (unchanged behavior). */
        lv_label_set_text(usb_label, "--");
        lv_label_set_text(ble_label, "--");
        lv_label_set_text(wpm_num, "--");
        lv_obj_set_style_text_color(wpm_num, lv_color_hex(CHIP_TEXT), 0);
        lv_label_set_text(layer, "WAITING");
        lv_label_set_text(typed, "");
        set_modifier_chip(chip_ctrl, false);
        set_modifier_chip(chip_alt, false);
        set_modifier_chip(chip_gui, false);
        set_modifier_chip(chip_shift, false);
        set_arc(&arc_l, 0);
        set_arc(&arc_m, zmk_battery_state_of_charge());
        set_arc(&arc_r, 0);
    }
}

K_WORK_DEFINE(screen_update_work, update_screen);

void zmk_monitor_status_changed(void)
{
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &screen_update_work);
    }
}

void zmk_display_settings_runtime_changed(void)
{
    zmk_monitor_status_changed();
}

/* Keep the monitor-own (M) arc live: refresh when the battery module
 * publishes a new state-of-charge. */
static int battery_event_listener(const zmk_event_t *eh)
{
    ARG_UNUSED(eh);
    zmk_monitor_status_changed();
    return 0;
}
ZMK_LISTENER(monitor_battery, battery_event_listener);
ZMK_SUBSCRIPTION(monitor_battery, zmk_battery_state_changed);

lv_obj_t *zmk_display_status_screen(void)
{
    lv_coord_t hor = lv_disp_get_hor_res(NULL);
    lv_coord_t ver = lv_disp_get_ver_res(NULL);

    screen = lv_obj_create(NULL);
    clean_obj(screen);
    lv_obj_set_size(screen, hor, ver);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(screen, 1, 0);

    /* Top connection chips: USB (86,40) 38x22 / BLE 1 (152,40) 54x22. */
    make_chip(86, 40, 38, 22, "USB", &usb_label);
    make_chip(152, 40, 54, 22, "BLE 1", &ble_label);

    /* L / R battery labels (no percentage digits, v6). */
    lv_obj_t *l_lbl = lv_label_create(screen);
    clean_obj(l_lbl);
    lv_obj_set_style_text_font(l_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l_lbl, lv_color_hex(L_COLOR), 0);
    lv_label_set_text(l_lbl, "L");
    lv_obj_align(l_lbl, LV_ALIGN_CENTER, 55 - 120, 86 - 120);

    lv_obj_t *r_lbl = lv_label_create(screen);
    clean_obj(r_lbl);
    lv_obj_set_style_text_font(r_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(r_lbl, lv_color_hex(R_COLOR), 0);
    lv_label_set_text(r_lbl, "R");
    lv_obj_align(r_lbl, LV_ALIGN_CENTER, 184 - 120, 86 - 120);

    /* WPM above the layer name: "WPM" label (gray) + number (white).
     * Number occupies a fixed 26px slot (up to 3 digits) right-aligned at
     * x = 153; "WPM" centered at x = 100 keeps the whole row centered on
     * x = 120 (row spans 87..153). */
    wpm_lbl = lv_label_create(screen);
    clean_obj(wpm_lbl);
    lv_obj_set_style_text_font(wpm_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wpm_lbl, lv_color_hex(CHIP_TEXT), 0);
    lv_label_set_text(wpm_lbl, "WPM");
    lv_obj_align(wpm_lbl, LV_ALIGN_CENTER, 100 - 120, 76 - 120);

    wpm_num = lv_label_create(screen);
    clean_obj(wpm_num);
    lv_obj_set_style_text_font(wpm_num, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wpm_num, lv_color_white(), 0);
    lv_obj_set_width(wpm_num, 26);
    lv_obj_set_style_text_align(wpm_num, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(wpm_num, "--");
    lv_obj_align(wpm_num, LV_ALIGN_CENTER, 140 - 120, 76 - 120);

    /* Layer name / typed chars. */
    layer = lv_label_create(screen);
    typed = lv_label_create(screen);
    clean_obj(layer);
    clean_obj(typed);
    lv_obj_set_style_text_font(layer, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_font(typed, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(layer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(typed, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(layer, LV_ALIGN_CENTER, 0, 102 - 120);
    lv_obj_align(typed, LV_ALIGN_CENTER, 0, 130 - 120);

    /* Modifier chips, y = 157. */
    chip_ctrl = make_modifier_chip(63, 157, 38, "CTRL");
    chip_alt = make_modifier_chip(103, 157, 36, "ALT");
    chip_gui = make_modifier_chip(139, 157, 34, "GUI");
    chip_shift = make_modifier_chip(176, 157, 42, "SHIFT");

    /* M label. */
    lv_obj_t *m_lbl = lv_label_create(screen);
    clean_obj(m_lbl);
    lv_obj_set_style_text_font(m_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(m_lbl, lv_color_white(), 0);
    lv_label_set_text(m_lbl, "M");
    lv_obj_align(m_lbl, LV_ALIGN_CENTER, 0, 184 - 120);

    /* Battery arcs (track + fill), center 120,120. */
    init_arc(&arc_l, 150.0f, 205.0f, 150.0f, 1.0f);   /* 左弧：自底端 150° 向上 */
    init_arc(&arc_m, 55.0f, 125.0f, 125.0f, -1.0f);   /* M 弧：自左端 125° 向右 */
    init_arc(&arc_r, 335.0f, 30.0f, 30.0f, -1.0f);    /* 右弧：自底端 30° 向上 */

    ready = true;
    update_screen(NULL);
    return screen;
}
