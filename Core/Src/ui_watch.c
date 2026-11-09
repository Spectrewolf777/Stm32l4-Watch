#include "ui_watch.h"
#include "ui.h"
#include "sensors.h"
#include "drv2605l.h"
#include "lis3mdl.h"
#include "rtc_tasks.h"
#include "recorder.h"
#include "sound_detector.h"
#include "mic.h"
#include "cst816t.h"
#include "power_mon.h"
#include "weather.h"
#include "uart_cmd.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define COL_ACCENT   0x1190F4
#define COL_DIM      0x5A6B7A
#define COL_ALERT    0xF45B11
#define COL_LIGHT    0xFFC83D
#define LIGHT_ACK_TIMEOUT_MS 1500U
#define UI_REFRESH_MS 200U

/* ---- Pages -------------------------------------------------------------- */
enum { PAGE_FACE, PAGE_SENSORS, PAGE_COMPASS, PAGE_STOPWATCH, PAGE_TIMER, PAGE_VOICE, PAGE_SOUND, PAGE_LIGHT, PAGE_COUNT };
static lv_obj_t *s_pages[PAGE_COUNT];
static uint8_t   s_page;

/* sensors */
static lv_obj_t *s_sens_label, *s_weather_label;
static char      s_weather[48] = "";
/* compass */
static lv_obj_t *s_cmp_deg, *s_cmp_dir, *s_cmp_needle, *s_cmp_cal_btn_lbl, *s_cmp_north_btn_lbl;
static uint8_t   s_align_ui;      /* 0 idle, 1 confirm north, 2 confirm east */
static lv_point_precise_t s_needle_pts[2];
/* stopwatch */
static lv_obj_t *s_sw_label, *s_sw_btn_lbl;
static bool      s_sw_running;
static uint32_t  s_sw_start, s_sw_accum;
/* timer */
static lv_obj_t *s_tm_label, *s_tm_btn_lbl;
static bool      s_tm_running;
static uint32_t  s_tm_set_s = 300, s_tm_end, s_tm_left_s = 300;
/* voice recorder */
static lv_obj_t *s_rec_btn, *s_rec_btn_lbl, *s_rec_time, *s_rec_info, *s_rec_meter;
static lv_obj_t *s_rec_badge, *s_rec_lbl_btn_lbl;
/* sound alerts */
static lv_obj_t *s_sa_btn, *s_sa_btn_lbl, *s_sa_state, *s_sa_info;
static char      s_sa_status[40] = "not started";
/* lights (server Tapo bulb via nRF52 BLE broadcast) */
static lv_obj_t *s_lt_btn, *s_lt_btn_lbl, *s_lt_state, *s_lt_info;
static bool      s_lt_on;               /* last state we asked for */
static bool      s_lt_waiting;          /* sent, waiting for the nRF52 ack */
static uint32_t  s_lt_sent_at;
/* timer alarm vibration */
static uint8_t   s_alarm_left;
static uint32_t  s_alarm_next;
/* notification */
static lv_obj_t *s_notif;
static uint32_t  s_notif_until;

static bool      s_was_on;
static uint32_t  s_last_refresh;

/* ---- Helpers ------------------------------------------------------------ */
static void goto_page(int idx, lv_screen_load_anim_t anim)
{
    if (idx < 0) idx = PAGE_COUNT - 1;
    if (idx >= PAGE_COUNT) idx = 0;
    if (s_page == PAGE_COMPASS && idx != PAGE_COMPASS) {
        /* Leaving the compass page cancels any half-done procedure */
        if (Compass_CalRunning()) Compass_CalStop(false);
        Compass_AlignCancel();
        s_align_ui = 0;
        if (s_cmp_cal_btn_lbl)   lv_label_set_text(s_cmp_cal_btn_lbl, "Calibrate");
        if (s_cmp_north_btn_lbl) lv_label_set_text(s_cmp_north_btn_lbl, "Set N");
    }
    s_page = (uint8_t)idx;
    Sensors_SetFastCompass(s_page == PAGE_COMPASS);
    lv_screen_load_anim(s_pages[s_page], anim, anim == LV_SCR_LOAD_ANIM_NONE ? 0 : 180, 0, false);
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);

    if (dir == LV_DIR_LEFT) {
        lv_indev_wait_release(indev);
        goto_page(s_page + 1, LV_SCR_LOAD_ANIM_MOVE_LEFT);
    } else if (dir == LV_DIR_RIGHT) {
        lv_indev_wait_release(indev);
        goto_page(s_page - 1, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    } else if (dir == LV_DIR_BOTTOM && s_page != PAGE_FACE) {
        lv_indev_wait_release(indev);                 /* swipe down = home */
        goto_page(PAGE_FACE, LV_SCR_LOAD_ANIM_MOVE_BOTTOM);
    }
}

static lv_obj_t *make_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s, gesture_cb, LV_EVENT_GESTURE, NULL);
    return s;
}

static lv_obj_t *make_label(lv_obj_t *p, const lv_font_t *f, uint32_t col,
                            int32_t x, int32_t y, const char *txt)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, x, y);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *make_button(lv_obj_t *p, const char *txt, int32_t x, int32_t y,
                             int32_t w, lv_event_cb_t cb, lv_obj_t **lbl_out)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, w, 40);
    lv_obj_align(b, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_radius(b, 20, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    if (lbl_out) *lbl_out = l;
    return b;
}

static void fmt_x10(char *buf, int n, int32_t v)
{
    snprintf(buf, n, "%s%ld.%ld", v < 0 ? "-" : "", labs(v) / 10, labs(v) % 10);
}

static void page_dots(lv_obj_t *scr, int idx)
{
    char dots[2 * PAGE_COUNT];
    for (int i = 0; i < PAGE_COUNT; i++) {
        dots[2 * i]     = (i == idx) ? 'o' : '.';
        dots[2 * i + 1] = (i == PAGE_COUNT - 1) ? '\0' : ' ';
    }
    make_label(scr, &lv_font_montserrat_12, COL_DIM, 0, 104, dots);
}

/* ---- Sensors page ------------------------------------------------------- */
static void build_sensors(void)
{
    lv_obj_t *s = s_pages[PAGE_SENSORS] = make_screen();
    make_label(s, &lv_font_montserrat_14, COL_ACCENT, 0, -88, "Sensors");
    s_sens_label = make_label(s, &lv_font_montserrat_14, 0xFFFFFF, 0, 0, "...");
    lv_obj_set_style_text_line_space(s_sens_label, 4, 0);
    s_weather_label = make_label(s, &lv_font_montserrat_12, COL_ACCENT, 0, 78, "");
    page_dots(s, PAGE_SENSORS);
}

static void update_sensors(void)
{
    const sensors_data_t *d = Sensors_Get();
    char t[10], h[10], buf[200];
    fmt_x10(t, sizeof t, d->temp_x10);
    fmt_x10(h, sizeof h, d->humidity_x10);

    char bat[32];
    if (d->bat_ok && !d->bat_present)
        snprintf(bat, sizeof bat, "Battery  none (USB)");
    else
        snprintf(bat, sizeof bat, "Battery  %u%%  %u.%02uV%s", d->bat_percent,
                 d->bat_mv / 1000, (d->bat_mv % 1000) / 10, Power_OnUsb() ? " +" : "");

    const weather_t *wx = Weather_Get();
    char fc[40];
    if (wx->valid)
        snprintf(fc, sizeof fc, "%s %s", wx->arrow < 0 ? LV_SYMBOL_DOWN : wx->arrow > 0 ? LV_SYMBOL_UP : "-",
                 wx->text);
    else
        snprintf(fc, sizeof fc, "Forecast in %u min",
                 (unsigned)(wx->hours_x10 >= 10 ? 0 : 60 - wx->hours_x10 * 6));

    snprintf(buf, sizeof buf,
             "%s\n"
             "Temp  %s C\n"
             "Humidity  %s %%\n"
             "Pressure  %lu.%lu hPa\n"
             "Altitude  %ld m\n"
             "%s",
             fc,
             d->env_ok || d->baro_ok ? t : "--",
             d->env_ok ? h : "--",
             (unsigned long)(d->pressure_pa / 100), (unsigned long)((d->pressure_pa % 100) / 10),
             (long)d->altitude_m,
             bat);
    lv_label_set_text(s_sens_label, buf);
    lv_label_set_text(s_weather_label, s_weather);
}

/* ---- Compass page ------------------------------------------------------- */
/* No haptic on the compass buttons: the vibration motor's magnet spinning
 * next to the magnetometer would corrupt the readings taken right after. */
static void cal_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_align_ui) return;                 /* finish Set North first */
    if (Compass_CalRunning()) {
        compass_cal_result_t r = Compass_CalStop(true);
        lv_label_set_text(s_cmp_cal_btn_lbl, "Calibrate");
        UI_ShowNotification(r == CAL_OK_TILT ? "Calibrated + tilt fix.\nPoint 12 north,\ntap Set N" :
                            r == CAL_OK_FLAT ? "Calibrated (flat).\nTilt/flip too for\ntilt fix. Tap Set N" :
                                               "Spin it flat a\nfull turn first");
    } else {
        Compass_CalStart();
        lv_label_set_text(s_cmp_cal_btn_lbl, "Save cal");
    }
}

/* Set North: 3 taps. Tap -> instructions, tap (facing N) -> tap (facing E) */
static void north_btn_cb(lv_event_t *e)
{
    (void)e;
    if (Compass_CalRunning()) return;       /* finish calibration first */

    switch (s_align_ui) {
    case 0:
    case 1:
        /* Tap Set N while 12 o'clock points north */
        lv_label_set_text(s_cmp_dir, "hold still...");
        lv_refr_now(NULL);
        if (!Compass_AlignNorth()) {
            s_align_ui = 0;
            lv_label_set_text(s_cmp_north_btn_lbl, "Set N");
            UI_ShowNotification("Compass read failed");
        } else if (Compass_AlignNeedsEast()) {
            s_align_ui = 2;                       /* flat mode: need east too */
            lv_label_set_text(s_cmp_north_btn_lbl, "E  OK");
        } else {
            s_align_ui = 0;
            UI_ShowNotification("North set!");
        }
        break;
    default: {
        lv_label_set_text(s_cmp_dir, "hold still...");
        lv_refr_now(NULL);
        compass_align_result_t r = Compass_AlignEast();
        s_align_ui = 0;
        lv_label_set_text(s_cmp_north_btn_lbl, "Set N");
        UI_ShowNotification(r == ALIGN_OK     ? "North set!" :
                            r == ALIGN_NOT_90 ? "Turn exactly 90\xC2\xB0 right - try again" :
                                                "Compass read failed");
        break;
    }
    }
}

bool UI_KeepAwake(void)
{
    /* Keep the backlight on during compass procedures */
    return Compass_CalRunning() || s_align_ui != 0;
}

static void build_compass(void)
{
    lv_obj_t *s = s_pages[PAGE_COMPASS] = make_screen();

    lv_obj_t *ring = lv_arc_create(s);
    lv_obj_set_size(ring, 200, 200);
    lv_obj_center(ring);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_value(ring, 0);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, lv_color_hex(COL_DIM), LV_PART_MAIN);

    s_cmp_needle = lv_line_create(s);
    lv_obj_set_style_line_width(s_cmp_needle, 4, 0);
    lv_obj_set_style_line_color(s_cmp_needle, lv_color_hex(COL_ALERT), 0);
    lv_obj_set_style_line_rounded(s_cmp_needle, true, 0);
    lv_obj_set_pos(s_cmp_needle, 0, 0);
    lv_obj_set_size(s_cmp_needle, 240, 240);

    s_cmp_deg = make_label(s, &lv_font_montserrat_48, 0xFFFFFF, 0, -10, "--");
    s_cmp_dir = make_label(s, &lv_font_montserrat_14, COL_ACCENT, 0, 28, "");
    make_button(s, "Calibrate", -50, 70, 92, cal_btn_cb, &s_cmp_cal_btn_lbl);
    make_button(s, "Set N",      50, 70, 92, north_btn_cb, &s_cmp_north_btn_lbl);
    page_dots(s, PAGE_COMPASS);
}

static void update_compass(void)
{
    static const char *names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    const sensors_data_t *d = Sensors_Get();
    int16_t h = d->heading_deg;

    if (!d->mag_ok || h < 0) {
        lv_label_set_text(s_cmp_deg, "--");
        if (d->mag_ok && Compass_CalRunning()) {
            lv_label_set_text_fmt(s_cmp_dir, "spin flat %u%%", Compass_CalProgress());
            return;
        }
        lv_label_set_text(s_cmp_dir, d->mag_ok ? "" : "no compass");
        return;
    }
    lv_label_set_text_fmt(s_cmp_deg, "%d\xC2\xB0", h);      /* degree sign */
    if (Compass_CalRunning()) {
        uint8_t flat = Compass_CalProgress();
        if (flat < 100) lv_label_set_text_fmt(s_cmp_dir, "spin flat %u%%", flat);
        else lv_label_set_text_fmt(s_cmp_dir, "tilt + flip %u%% (opt)", Compass_CalTiltProgress());
    }
    else if (s_align_ui == 2)
        lv_label_set_text(s_cmp_dir, "turn right to EAST");
    else if (!Compass_IsAligned())
        lv_label_set_text(s_cmp_dir, "12 to north, tap Set N");
    else
        lv_label_set_text(s_cmp_dir, names[((h + 22) / 45) % 8]);

    /* Needle points at north: rotate opposite to heading */
    float a = -(float)h * 0.0174533f;
    s_needle_pts[0].x = 120; s_needle_pts[0].y = 120;
    s_needle_pts[1].x = (lv_value_precise_t)(120 + 95 * sinf(a));
    s_needle_pts[1].y = (lv_value_precise_t)(120 - 95 * cosf(a));
    lv_line_set_points(s_cmp_needle, s_needle_pts, 2);
}

/* ---- Stopwatch page ----------------------------------------------------- */
static uint32_t sw_elapsed(void)
{
    return s_sw_accum + (s_sw_running ? HAL_GetTick() - s_sw_start : 0);
}

static void sw_start_cb(lv_event_t *e)
{
    (void)e;
    Haptic(HAPTIC_TICK);
    if (s_sw_running) { s_sw_accum = sw_elapsed(); s_sw_running = false; }
    else              { s_sw_start = HAL_GetTick(); s_sw_running = true; }
    lv_label_set_text(s_sw_btn_lbl, s_sw_running ? "Stop" : "Start");
}

static void sw_reset_cb(lv_event_t *e)
{
    (void)e;
    Haptic(HAPTIC_TICK);
    s_sw_accum = 0;
    s_sw_start = HAL_GetTick();
}

static void build_stopwatch(void)
{
    lv_obj_t *s = s_pages[PAGE_STOPWATCH] = make_screen();
    make_label(s, &lv_font_montserrat_14, COL_ACCENT, 0, -80, "Stopwatch");
    s_sw_label = make_label(s, &lv_font_montserrat_48, 0xFFFFFF, 0, -20, "00:00.0");
    make_button(s, "Start", -50, 50, 84, sw_start_cb, &s_sw_btn_lbl);
    make_button(s, "Reset",  50, 50, 84, sw_reset_cb, NULL);
    page_dots(s, PAGE_STOPWATCH);
}

static void update_stopwatch(void)
{
    uint32_t ms = sw_elapsed();
    uint32_t m = ms / 60000, sec = (ms / 1000) % 60, ds = (ms / 100) % 10;
    if (m > 99) m = 99;
    lv_label_set_text_fmt(s_sw_label, "%02lu:%02lu.%lu", (unsigned long)m,
                          (unsigned long)sec, (unsigned long)ds);
}

/* ---- Countdown timer page ----------------------------------------------- */
static void tm_show(void)
{
    lv_label_set_text_fmt(s_tm_label, "%02lu:%02lu", (unsigned long)(s_tm_left_s / 60),
                          (unsigned long)(s_tm_left_s % 60));
    lv_label_set_text(s_tm_btn_lbl, s_tm_running ? "Stop" : "Start");
}

static void tm_adjust(int32_t delta_s)
{
    if (s_tm_running) return;
    int32_t v = (int32_t)s_tm_set_s + delta_s;
    if (v < 60) v = 60;
    if (v > 99 * 60) v = 99 * 60;
    s_tm_set_s = s_tm_left_s = (uint32_t)v;
    tm_show();
}

static void tm_minus_cb(lv_event_t *e) { (void)e; Haptic(HAPTIC_TICK); tm_adjust(-60); }
static void tm_plus_cb(lv_event_t *e)  { (void)e; Haptic(HAPTIC_TICK); tm_adjust(60); }

static void tm_start_cb(lv_event_t *e)
{
    (void)e;
    Haptic(HAPTIC_TICK);
    if (s_tm_running) {
        s_tm_running = false;
        s_tm_left_s  = s_tm_set_s;
    } else {
        s_tm_end     = HAL_GetTick() + s_tm_set_s * 1000U;
        s_tm_running = true;
    }
    tm_show();
}

static void build_timer(void)
{
    lv_obj_t *s = s_pages[PAGE_TIMER] = make_screen();
    make_label(s, &lv_font_montserrat_14, COL_ACCENT, 0, -80, "Timer");
    s_tm_label = make_label(s, &lv_font_montserrat_48, 0xFFFFFF, 0, -25, "05:00");
    make_button(s, "-1m", -70, 30, 56, tm_minus_cb, NULL);
    make_button(s, "+1m",  70, 30, 56, tm_plus_cb, NULL);
    make_button(s, "Start", 0, 75, 90, tm_start_cb, &s_tm_btn_lbl);
    page_dots(s, PAGE_TIMER);
    tm_show();
}

/* Tap the timer icon / number on the watchface: start a 60 s countdown,
 * tap again to cancel. */
static uint32_t s_quick_tap_ms;

static void quick_timer_cb(lv_event_t *e)
{
    (void)e;
    s_quick_tap_ms = HAL_GetTick();
    Haptic(HAPTIC_TICK);
    if (s_tm_running) {
        s_tm_running = false;
        s_tm_left_s  = s_tm_set_s;
    } else {
        s_tm_set_s = s_tm_left_s = 60;
        s_tm_end     = HAL_GetTick() + 60U * 1000U;
        s_tm_running = true;
    }
    tm_show();
}

/* Runs even with the screen off */
static void timer_service(void)
{
    if (!s_tm_running) return;
    int32_t left_ms = (int32_t)(s_tm_end - HAL_GetTick());
    uint32_t left_s = left_ms > 0 ? ((uint32_t)left_ms + 999U) / 1000U : 0;

    if (left_s != s_tm_left_s) {
        s_tm_left_s = left_s;
        if (s_page == PAGE_TIMER) tm_show();
    }
    if (left_ms <= 0) {
        s_tm_running = false;
        s_tm_left_s  = s_tm_set_s;
        tm_show();
        UI_ShowNotification("Timer done!\n(tap to stop)");
        /* Alarm: always vibrates, even if button haptics are turned off */
        s_alarm_left = 3;
        s_alarm_next = HAL_GetTick();
    }
}

/* ---- Voice recorder page ------------------------------------------------ */
static void rec_refresh_button(void)
{
    bool on = Recorder_IsRunning();
    lv_obj_set_style_bg_color(s_rec_btn, lv_color_hex(on ? COL_ALERT : COL_ACCENT), 0);
    lv_label_set_text(s_rec_btn_lbl, on ? LV_SYMBOL_STOP : LV_SYMBOL_AUDIO);
    if (on) lv_obj_remove_flag(s_rec_badge, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(s_rec_badge, LV_OBJ_FLAG_HIDDEN);
}

static void rec_toggle(void)
{
    bool was = Recorder_IsRunning();
    bool now = Recorder_Toggle();
    if (!was && !now) {
        UI_ShowNotification(Recorder_Status());     /* start failed: say why */
    } else {
        Haptic(now ? HAPTIC_CLICK : HAPTIC_DOUBLE_CLICK);
    }
    rec_refresh_button();
}

static void rec_btn_cb(lv_event_t *e) { (void)e; rec_toggle(); }

/* What is being recorded, for training the sound AI. The label becomes the start of the
 * file name (KNOCK_261108_113000.WAV) and import_watch_recordings.py sorts by it. */
static void rec_label_show(void)
{
    const char *l = Recorder_GetLabel();
    lv_label_set_text_fmt(s_rec_lbl_btn_lbl, "%s " LV_SYMBOL_RIGHT, l[0] ? l : "Voice");
}

static void rec_label_cb(lv_event_t *e)
{
    (void)e;
    if (Recorder_IsRunning()) return;                 /* label is fixed while recording */
    Haptic(HAPTIC_TICK);
    Recorder_NextLabel();
    rec_label_show();
}

static void build_voice(void)
{
    lv_obj_t *s = s_pages[PAGE_VOICE] = make_screen();
    lv_obj_t *lb = make_button(s, "Voice", 0, -84, 110, rec_label_cb, &s_rec_lbl_btn_lbl);
    lv_obj_set_height(lb, 28);
    rec_label_show();

    s_rec_btn = lv_button_create(s);
    lv_obj_set_size(s_rec_btn, 84, 84);
    lv_obj_align(s_rec_btn, LV_ALIGN_CENTER, 0, -18);
    lv_obj_set_style_radius(s_rec_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(s_rec_btn, rec_btn_cb, LV_EVENT_CLICKED, NULL);
    s_rec_btn_lbl = lv_label_create(s_rec_btn);
    lv_obj_set_style_text_font(s_rec_btn_lbl, &lv_font_montserrat_48, 0);
    lv_obj_center(s_rec_btn_lbl);

    s_rec_meter = lv_bar_create(s);
    lv_obj_set_size(s_rec_meter, 120, 6);
    lv_obj_align(s_rec_meter, LV_ALIGN_CENTER, 0, 36);
    lv_bar_set_range(s_rec_meter, 0, 100);

    s_rec_time = make_label(s, &lv_font_montserrat_14, 0xFFFFFF, 0, 56, "00:00");
    s_rec_info = make_label(s, &lv_font_montserrat_12, COL_DIM, 0, 78, "tap to record");
    page_dots(s, PAGE_VOICE);

    /* Red "on air" symbol fixed on the watchface, left of the Bluetooth
     * icon (ui_Image1 sits at -23,-98), shown while recording */
    s_rec_badge = lv_label_create(ui_Screen1);
    lv_obj_set_style_text_font(s_rec_badge, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_rec_badge, lv_color_hex(COL_ALERT), 0);
    lv_label_set_text(s_rec_badge, LV_SYMBOL_WIFI);
    lv_obj_align(s_rec_badge, LV_ALIGN_CENTER, -50, -98);
    lv_obj_add_flag(s_rec_badge, LV_OBJ_FLAG_HIDDEN);

    rec_refresh_button();
}

static void update_voice(void)
{
    uint32_t sec = Recorder_Seconds();
    lv_label_set_text_fmt(s_rec_time, "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));

    if (Recorder_IsRunning()) {
        const char *n = Recorder_FileName();
        const char *slash = strrchr(n, '/');
        lv_label_set_text(s_rec_info, slash ? slash + 1 : n);
        uint32_t lvl = (uint32_t)Mic_Level() * 100U / 8192U;   /* ~-12 dBFS = full */
        lv_bar_set_value(s_rec_meter, lvl > 100 ? 100 : (int32_t)lvl, LV_ANIM_OFF);
    } else {
        lv_label_set_text(s_rec_info, strcmp(Recorder_Status(), "ok") == 0 ? "tap to record"
                                                                           : Recorder_Status());
        lv_bar_set_value(s_rec_meter, 0, LV_ANIM_OFF);
    }
    rec_refresh_button();
}

void UI_RecorderChanged(void)
{
    if (s_rec_btn) rec_refresh_button();
}

/* ---- Sound alerts page -------------------------------------------------- */
static const char *sa_pretty(const char *name)
{
    if (strcmp(name, "alarm") == 0)      return "SMOKE / FIRE\nALARM";
    if (strcmp(name, "smoke_alarm") == 0) return "SMOKE / FIRE\nALARM";
    if (strcmp(name, "co_alarm") == 0)   return "CARBON\nMONOXIDE ALARM";
    if (strcmp(name, "door_knock") == 0) return "DOOR KNOCK";
    if (strcmp(name, "doorbell") == 0)   return "DOORBELL";
    if (strcmp(name, "siren") == 0)      return "SIREN";
    return name;
}

static void sa_show(void)
{
    if (!s_sa_btn) return;
    bool on = SD_IsRunning();
    lv_obj_set_style_bg_color(s_sa_btn, lv_color_hex(on ? COL_ACCENT : 0x26323D), 0);
    lv_label_set_text(s_sa_state, on ? "listening" : (SD_IsReady() ? "OFF" : s_sa_status));
    lv_obj_set_style_text_color(s_sa_state, lv_color_hex(on ? COL_ACCENT : COL_DIM), 0);
}

static void sa_toggle_cb(lv_event_t *e)
{
    (void)e;
    Haptic(HAPTIC_TICK);
    if (SD_IsRunning()) SD_Stop();
    else if (!SD_Start()) UI_ShowNotification(SD_IsReady() ? "Mic start failed" : s_sa_status);
    sa_show();
}

static void build_sound(void)
{
    lv_obj_t *s = s_pages[PAGE_SOUND] = make_screen();
    make_label(s, &lv_font_montserrat_14, COL_ACCENT, 0, -88, "Sound alerts");

    s_sa_btn = lv_button_create(s);
    lv_obj_set_size(s_sa_btn, 80, 80);
    lv_obj_align(s_sa_btn, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_radius(s_sa_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(s_sa_btn, sa_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_sa_btn_lbl = lv_label_create(s_sa_btn);
    lv_obj_set_style_text_font(s_sa_btn_lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text(s_sa_btn_lbl, LV_SYMBOL_BELL);
    lv_obj_center(s_sa_btn_lbl);

    s_sa_state = make_label(s, &lv_font_montserrat_14, COL_DIM, 0, 26, "OFF");
    s_sa_info  = make_label(s, &lv_font_montserrat_12, COL_DIM, 0, 60, "no alerts yet");
    page_dots(s, PAGE_SOUND);
    sa_show();
}

static void update_sound(void)
{
    const sd_stats_t *st = SD_Stats();
    sa_show();
    if (st->alerts == 0) return;
    uint32_t ago = (HAL_GetTick() - st->last_alert_tick) / 60000U;
    char n[24];
    strncpy(n, st->last_alert_conf >= 1.0f ? "alarm (beeps)" : SD_ClassName(st->last_alert_class), sizeof n - 1);
    n[sizeof n - 1] = '\0';
    lv_label_set_text_fmt(s_sa_info, "last: %s %d%%\n%lu min ago", n,
                          (int)(st->last_alert_conf * 100.0f), (unsigned long)ago);
}

/* Called by sound_detector.c when a sound is recognised */
void sd_on_detect(int class_id, const char *name, float confidence)
{
    (void)class_id;
    char txt[64];
    if (confidence >= 1.0f)          /* beep-pattern detector: no percentage, it matched the rule */
        snprintf(txt, sizeof txt, "%s\n(tap to stop)", sa_pretty(name));
    else
        snprintf(txt, sizeof txt, "%s\n%d%%  (tap to stop)", sa_pretty(name), (int)(confidence * 100.0f));
    UI_ShowNotification(txt);
    s_notif_until = HAL_GetTick() + 20000U;          /* keep it up longer than a normal popup */
    /* Always vibrates, even if button haptics are off. Alarms and sirens buzz longer. */
    s_alarm_left = (strstr(name, "alarm") || strcmp(name, "siren") == 0) ? 5 : 3;
    s_alarm_next = HAL_GetTick();
}

/* Boot: load the network, check it computes what the PC computed, start listening */
void UI_SoundAI_Boot(void)
{
    if (SD_Init() != 0) {
        snprintf(s_sa_status, sizeof s_sa_status, "AI load failed");
    } else {
        float fe, pe;
        if (SD_SelfTest(&fe, &pe)) {
            snprintf(s_sa_status, sizeof s_sa_status, "ok");
            SD_Start();
        } else {
            snprintf(s_sa_status, sizeof s_sa_status, "self-test FAILED");
            UI_ShowNotification("Sound alerts:\nself-test FAILED\n(-ai test)");
        }
    }
    sa_show();
}

/* ---- Lights page -------------------------------------------------------- */
static void light_show(void)
{
    if (!s_lt_btn) return;
    lv_obj_set_style_bg_color(s_lt_btn, lv_color_hex(s_lt_on ? COL_LIGHT : 0x26323D), 0);
    lv_obj_set_style_shadow_width(s_lt_btn, s_lt_on ? 30 : 0, 0);
    lv_obj_set_style_shadow_color(s_lt_btn, lv_color_hex(COL_LIGHT), 0);
    lv_obj_set_style_text_color(s_lt_btn_lbl, lv_color_hex(s_lt_on ? 0x000000 : 0xFFFFFF), 0);
    lv_label_set_text(s_lt_state, s_lt_on ? "ON" : "OFF");
    lv_obj_set_style_text_color(s_lt_state, lv_color_hex(s_lt_on ? COL_LIGHT : COL_DIM), 0);
}

/* cmd: 0 = off, 1 = on, 2 = toggle (sent as the command byte to the nRF52) */
static void light_send(uint8_t cmd)
{
    Haptic(HAPTIC_TICK);
    s_lt_on = (cmd == 2) ? !s_lt_on : (cmd == 1);
    light_show();
    if (UART_Cmd_SendLight(cmd)) {
        s_lt_waiting = true;
        s_lt_sent_at = HAL_GetTick();
        lv_label_set_text(s_lt_info, "sending...");
    } else {
        s_lt_waiting = false;
        lv_label_set_text(s_lt_info, "UART error");
    }
}

static void lt_toggle_cb(lv_event_t *e) { (void)e; light_send(2); }  /* toggle */
static void lt_on_cb(lv_event_t *e)     { (void)e; light_send(1); }
static void lt_off_cb(lv_event_t *e)    { (void)e; light_send(0); }

void UI_LightAck(bool ok)
{
    s_lt_waiting = false;
    if (s_lt_info) lv_label_set_text(s_lt_info, ok ? "sent " LV_SYMBOL_OK : "BLE error");
}

/* Runs even with the screen off */
static void light_service(void)
{
    if (s_lt_waiting && HAL_GetTick() - s_lt_sent_at > LIGHT_ACK_TIMEOUT_MS) {
        s_lt_waiting = false;
        lv_label_set_text(s_lt_info, "no reply from nRF52");
    }
}

static void build_light(void)
{
    lv_obj_t *s = s_pages[PAGE_LIGHT] = make_screen();
    make_label(s, &lv_font_montserrat_14, COL_ACCENT, 0, -88, "Lights");

    s_lt_btn = lv_button_create(s);
    lv_obj_set_size(s_lt_btn, 80, 80);
    lv_obj_align(s_lt_btn, LV_ALIGN_CENTER, 0, -34);
    lv_obj_set_style_radius(s_lt_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(s_lt_btn, lt_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_lt_btn_lbl = lv_label_create(s_lt_btn);
    lv_obj_set_style_text_font(s_lt_btn_lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text(s_lt_btn_lbl, LV_SYMBOL_POWER);
    lv_obj_center(s_lt_btn_lbl);

    s_lt_state = make_label(s, &lv_font_montserrat_14, COL_DIM, 0, 18, "OFF");
    make_button(s, "Off", -46, 52, 72, lt_off_cb, NULL);
    make_button(s, "On",   46, 52, 72, lt_on_cb, NULL);
    s_lt_info = make_label(s, &lv_font_montserrat_12, COL_DIM, 0, 86, "tap to switch");
    page_dots(s, PAGE_LIGHT);
    light_show();
}

/* ---- Notification popup ------------------------------------------------- */
static void notif_close(void)
{
    if (s_notif) { lv_obj_delete(s_notif); s_notif = NULL; }
}

static void alarm_stop(void)
{
    if (s_alarm_left) { s_alarm_left = 0; DRV2605L_Stop(); }
}

static void notif_click_cb(lv_event_t *e) { (void)e; alarm_stop(); notif_close(); }

/* Long buzz, pause, long buzz (~1.75 s), repeated s_alarm_left times */
static void alarm_service(void)
{
    static const uint8_t pattern[] = { HAPTIC_ALERT_750MS, HAPTIC_WAIT(250), HAPTIC_ALERT_750MS };
    if (!s_alarm_left || (int32_t)(HAL_GetTick() - s_alarm_next) < 0) return;
    DRV2605L_PlaySequence(pattern, sizeof(pattern));
    s_alarm_left--;
    s_alarm_next = HAL_GetTick() + 2400U;
}

void UI_ShowNotification(const char *text)
{
    notif_close();
    s_notif = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_notif, 180, 110);
    lv_obj_center(s_notif);
    lv_obj_set_style_bg_color(s_notif, lv_color_hex(0x10202E), 0);
    lv_obj_set_style_border_color(s_notif, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_border_width(s_notif, 2, 0);
    lv_obj_set_style_radius(s_notif, 16, 0);
    lv_obj_remove_flag(s_notif, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_notif, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_notif, notif_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l = lv_label_create(s_notif);
    lv_obj_set_width(l, 160);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);

    s_notif_until = HAL_GetTick() + 6000U;
    Display_Wake();
    Haptic(HAPTIC_DOUBLE_CLICK);
}

void UI_SetWeather(const char *text)
{
    strncpy(s_weather, text, sizeof(s_weather) - 1);
    s_weather[sizeof(s_weather) - 1] = '\0';
}

void UI_SetBleConnected(bool connected)
{
    /* ui_Image1 is the "bluetooth disconnected" icon: hide it when connected */
    if (!ui_Image1) return;
    if (connected) lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_remove_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN);
}

/* ---- Watchface (SquareLine screen) live values -------------------------- */
static void update_face(void)
{
    const sensors_data_t *d = Sensors_Get();
    if (d->bat_ok && !d->bat_present) {
        /* No battery fitted: running from USB, a percentage would be fake */
        lv_label_set_text(ui_batPercent, LV_SYMBOL_USB);
        lv_bar_set_value(ui_Bar, 0, LV_ANIM_OFF);
    } else if (d->bat_ok) {
        if (Power_OnUsb()) lv_label_set_text_fmt(ui_batPercent, "%u%%" LV_SYMBOL_CHARGE, d->bat_percent);
        else               lv_label_set_text_fmt(ui_batPercent, "%u%%", d->bat_percent);
        lv_bar_set_value(ui_Bar, d->bat_percent, LV_ANIM_OFF);
    }

    /* Timer: seconds left (minutes once over 99 s), "60" when idle = tap to start */
    if (!s_tm_running)            lv_label_set_text(ui_Timer, "60");
    else if (s_tm_left_s < 100)   lv_label_set_text_fmt(ui_Timer, "%lu", (unsigned long)s_tm_left_s);
    else                          lv_label_set_text_fmt(ui_Timer, "%lum", (unsigned long)((s_tm_left_s + 59) / 60));
}

/* ---- Public ------------------------------------------------------------- */
void UI_Watch_Init(void)
{
    s_pages[PAGE_FACE] = ui_Screen1;
    lv_obj_add_event_cb(ui_Screen1, gesture_cb, LV_EVENT_GESTURE, NULL);

    /* Timer icon + number on the watchface start a 60 s countdown. The
     * targets are small, so grow their touch area. */
    lv_obj_add_flag(ui_Timer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ui_Timer, 18);
    lv_obj_set_ext_click_area(ui_clock, 18);
    lv_obj_add_event_cb(ui_Timer, quick_timer_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_clock, quick_timer_cb, LV_EVENT_CLICKED, NULL);

    build_sensors();
    build_compass();
    build_stopwatch();
    build_timer();
    build_voice();
    build_sound();
    build_light();

    s_page = PAGE_FACE;
    UI_SetBleConnected(false);
}

void UI_Watch_Task(void)
{
    bool on = Display_IsOn();

    timer_service();
    alarm_service();
    light_service();

    /* Screen turned off: stay on the current page so waking returns you
     * to it. Only close popups and cancel half-done compass steps. */
    if (s_was_on && !on) {
        notif_close();
        if (s_page == PAGE_COMPASS) {
            if (Compass_CalRunning()) Compass_CalStop(false);
            Compass_AlignCancel();
            s_align_ui = 0;
            lv_label_set_text(s_cmp_cal_btn_lbl, "Calibrate");
            lv_label_set_text(s_cmp_north_btn_lbl, "Set N");
        }
    }
    s_was_on = on;

    if (s_notif && (int32_t)(HAL_GetTick() - s_notif_until) >= 0) notif_close();

    /* Software gestures from the touch driver: double tap on the watchface
     * starts / stops a voice recording */
    cst816t_gesture_t g = CST816T_GetGesture();
    if (on && g == CST816T_GESTURE_DOUBLE_TAP && s_page == PAGE_FACE &&
        HAL_GetTick() - s_quick_tap_ms > 600U)          /* not a timer double-tap */
        rec_toggle();

    /* Recorder may stop by itself (error / flash full) */
    static bool rec_was;
    if (Recorder_IsRunning() != rec_was) {
        rec_was = Recorder_IsRunning();
        rec_refresh_button();
        if (!rec_was && strncmp(Recorder_Status(), "ok", 2) != 0) UI_ShowNotification(Recorder_Status());
    }

    uint32_t period = (s_page == PAGE_COMPASS) ? 50U : UI_REFRESH_MS;   /* 20 fps needle */
    if (!on || HAL_GetTick() - s_last_refresh < period) return;
    s_last_refresh = HAL_GetTick();

    switch (s_page) {
    case PAGE_FACE:      update_face();      break;
    case PAGE_SENSORS:   update_sensors();   break;
    case PAGE_COMPASS:   update_compass();   break;
    case PAGE_STOPWATCH: update_stopwatch(); break;
    case PAGE_TIMER:     break;              /* updated by timer_service */
    case PAGE_VOICE:     update_voice();     break;
    case PAGE_SOUND:     update_sound();     break;
    case PAGE_LIGHT:     break;              /* event driven */
    }
}
