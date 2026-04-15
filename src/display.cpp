#include "display.h"
#include <TFT_eSPI.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

// ── Colour helpers ─────────────────────────────────────────────
// Convert 24-bit RGB888 to 16-bit RGB565
#define RGB565(r, g, b) ((uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3)))

static const uint16_t C_BG_BASE    = RGB565(0x0b, 0x0c, 0x11);
static const uint16_t C_BG_SURFACE = RGB565(0x11, 0x13, 0x18);
static const uint16_t C_BG_ACTIVE  = RGB565(0x23, 0x26, 0x36);
static const uint16_t C_TEXT_1     = RGB565(0xe8, 0xea, 0xf2);
static const uint16_t C_TEXT_2     = RGB565(0x8b, 0x90, 0xaa);
static const uint16_t C_ACCENT     = RGB565(0x5b, 0x8e, 0xf0);
static const uint16_t C_RED        = RGB565(0xe0, 0x58, 0x58);
static const uint16_t C_BORDER     = RGB565(0x1e, 0x20, 0x2c);

// ── Layout constants (landscape 320×170) ──────────────────────
static const int W        = 320;
static const int H        = 170;
static const int HDR_H    =  24;  // header bar height px
static const int PAD      =  10;  // card padding
static const int BAR_H    =   6;  // progress bar height
static const int BAR_Y    = H - PAD - BAR_H;
static const int DATE_Y   = HDR_H + PAD + 2;
static const int TITLE_Y  = DATE_Y + 18;
static const int TIME_Y   = TITLE_Y + 54;  // 2 title lines × 26px + gap
static const int LOC_Y    = TIME_Y + 18;

// ── Anti-burn-in pixel shift ───────────────────────────────────
static const int8_t SHIFTS[4][2] = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
static int8_t s_shift_step = 0;
static int8_t s_shift_x    = 0;
static int8_t s_shift_y    = 0;

// ── TFT and sprite ─────────────────────────────────────────────
static TFT_eSPI    s_tft;
static TFT_eSprite s_spr(&s_tft);

// ── Helpers ────────────────────────────────────────────────────

static void push_sprite() {
    s_spr.pushSprite(s_shift_x, s_shift_y);
}

static void draw_header(bool offline) {
    s_spr.fillRect(0, 0, W, HDR_H, C_BG_SURFACE);

    // "nextUp" wordmark
    s_spr.setTextColor(C_ACCENT, C_BG_SURFACE);
    s_spr.setTextFont(2);
    s_spr.setTextSize(1);
    s_spr.setCursor(8, 5);
    s_spr.print("nextUp");

    // Current time HH:MM
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);

    int time_x = offline ? W - 115 : W - (int)s_spr.textWidth(buf) - 8;
    s_spr.setTextColor(C_TEXT_2, C_BG_SURFACE);
    s_spr.setCursor(time_x, 5);
    s_spr.print(buf);

    // Offline indicator
    if (offline) {
        s_spr.setTextColor(C_RED, C_BG_SURFACE);
        s_spr.setCursor(W - 56, 5);
        s_spr.print("! offline");
    }

    // Bottom border line
    s_spr.drawFastHLine(0, HDR_H - 1, W, C_BORDER);
}

// ── Public API ─────────────────────────────────────────────────

void display_init() {
    s_tft.init();
    s_tft.setRotation(1);       // Landscape, USB connector on left
    s_tft.fillScreen(C_BG_BASE);
    s_tft.setSwapBytes(true);

    // Backlight on (GPIO 38)
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);

    s_spr.createSprite(W, H);
    s_spr.setSwapBytes(true);
}

void display_show_setup() {
    s_spr.fillSprite(C_BG_BASE);
    draw_header(false);

    // Title
    s_spr.setTextFont(4);
    s_spr.setTextSize(1);
    s_spr.setTextColor(C_ACCENT, C_BG_BASE);
    const char *title = "Setup Mode";
    int tw = s_spr.textWidth(title);
    s_spr.setCursor((W - tw) / 2, 40);
    s_spr.print(title);

    // Instructions
    s_spr.setTextFont(2);
    s_spr.setTextColor(C_TEXT_2, C_BG_BASE);

    const char *l1 = "Connect to WiFi: CalendarSetup";
    tw = s_spr.textWidth(l1);
    s_spr.setCursor((W - tw) / 2, 90);
    s_spr.print(l1);

    const char *l2 = "Then open: http://192.168.4.1";
    tw = s_spr.textWidth(l2);
    s_spr.setCursor((W - tw) / 2, 112);
    s_spr.print(l2);

    push_sprite();
}

void display_show_connecting() {
    s_spr.fillSprite(C_BG_BASE);
    draw_header(false);

    s_spr.setTextFont(4);
    s_spr.setTextColor(C_TEXT_2, C_BG_BASE);
    const char *msg = "Connecting...";
    int tw = s_spr.textWidth(msg);
    s_spr.setCursor((W - tw) / 2, HDR_H + (H - HDR_H - 26) / 2);
    s_spr.print(msg);

    push_sprite();
}

void display_render(const CalEvent &ev, bool offline) {
    s_spr.fillSprite(C_BG_BASE);
    draw_header(offline);

    // Event card
    s_spr.fillRoundRect(PAD, HDR_H + PAD,
                        W - PAD * 2, H - HDR_H - PAD * 2,
                        6, C_BG_SURFACE);

    if (!ev.has_event) {
        s_spr.setTextFont(2);
        s_spr.setTextColor(C_TEXT_2, C_BG_SURFACE);
        const char *msg = "No upcoming events";
        int tw = s_spr.textWidth(msg);
        s_spr.setCursor((W - tw) / 2, HDR_H + (H - HDR_H) / 2 - 8);
        s_spr.print(msg);
        push_sprite();
        return;
    }

    // Date row: "WED 16 APR"
    time_t now_ts = time(nullptr);
    struct tm *now_tm = localtime(&now_ts);
    static const char *DAYS[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    static const char *MONS[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                  "JUL","AUG","SEP","OCT","NOV","DEC"};
    char date_buf[20];
    snprintf(date_buf, sizeof(date_buf), "%s %02d %s",
             DAYS[now_tm->tm_wday], now_tm->tm_mday, MONS[now_tm->tm_mon]);
    s_spr.setTextFont(2);
    s_spr.setTextColor(C_ACCENT, C_BG_SURFACE);
    s_spr.setCursor(PAD + 8, DATE_Y);
    s_spr.print(date_buf);

    // Title — up to 2 lines, font 4 (~21 chars wide at 320px)
    s_spr.setTextFont(4);
    s_spr.setTextColor(C_TEXT_1, C_BG_SURFACE);

    int tlen = (int)strlen(ev.title);
    if (tlen <= 20) {
        s_spr.setCursor(PAD + 8, TITLE_Y);
        s_spr.print(ev.title);
    } else {
        // Find word-break point
        char line1[22];
        strncpy(line1, ev.title, 21);
        line1[21] = '\0';
        int break_pos = 20;
        for (int i = 20; i > 8; i--) {
            if (line1[i] == ' ') { break_pos = i; break; }
        }
        line1[break_pos] = '\0';

        const char *rest = ev.title + break_pos + (ev.title[break_pos] == ' ' ? 1 : 0);
        char line2[22];
        strncpy(line2, rest, 21);
        line2[21] = '\0';
        // Truncate with ellipsis if still overflows
        if (strlen(rest) > 21) {
            line2[18] = '.'; line2[19] = '.'; line2[20] = '.'; line2[21] = '\0';
        }

        s_spr.setCursor(PAD + 8, TITLE_Y);
        s_spr.print(line1);
        s_spr.setCursor(PAD + 8, TITLE_Y + 26);
        s_spr.print(line2);
    }

    // Time row: "18:00 -> 19:30"
    if (!ev.is_all_day && ev.time_start[0]) {
        s_spr.setTextFont(2);
        s_spr.setTextColor(C_TEXT_2, C_BG_SURFACE);
        char time_buf[14];
        snprintf(time_buf, sizeof(time_buf), "%s -> %s", ev.time_start, ev.time_end);
        s_spr.setCursor(PAD + 8, TIME_Y);
        s_spr.print(time_buf);
    }

    // Location row: "@ Venue Name"
    if (ev.location[0] != '\0') {
        s_spr.setTextFont(2);
        s_spr.setTextColor(C_TEXT_2, C_BG_SURFACE);
        char loc_buf[35];
        snprintf(loc_buf, sizeof(loc_buf), "@ %.30s", ev.location);
        s_spr.setCursor(PAD + 8, LOC_Y);
        s_spr.print(loc_buf);
    }

    // Progress bar — only during event (between start and end)
    if (!ev.is_all_day && ev.ts_start && ev.ts_end) {
        int32_t now32 = (int32_t)now_ts;
        if (now32 >= ev.ts_start && now32 <= ev.ts_end) {
            int bar_x = PAD + 8;
            int bar_w = W - PAD * 2 - 16;
            s_spr.fillRoundRect(bar_x, BAR_Y, bar_w, BAR_H, 3, C_BG_ACTIVE);
            int32_t duration = ev.ts_end - ev.ts_start;
            int32_t elapsed  = now32 - ev.ts_start;
            int fill = (duration > 0) ? (int)((long)bar_w * elapsed / duration) : 0;
            if (fill > 0)
                s_spr.fillRoundRect(bar_x, BAR_Y, fill, BAR_H, 3, C_ACCENT);
        }
    }

    push_sprite();
}

void display_update_clock(bool offline) {
    // Redraw only the header strip, then push full sprite
    draw_header(offline);
    push_sprite();
}

void display_update_progress(const CalEvent &ev) {
    if (ev.is_all_day || !ev.ts_start || !ev.ts_end) return;

    int bar_x = PAD + 8;
    int bar_w = W - PAD * 2 - 16;

    // Clear bar area
    s_spr.fillRoundRect(bar_x, BAR_Y, bar_w, BAR_H, 3, C_BG_ACTIVE);

    int32_t now32 = (int32_t)time(nullptr);
    if (now32 >= ev.ts_start && now32 <= ev.ts_end) {
        int32_t duration = ev.ts_end - ev.ts_start;
        int32_t elapsed  = now32 - ev.ts_start;
        int fill = (duration > 0) ? (int)((long)bar_w * elapsed / duration) : 0;
        if (fill > 0)
            s_spr.fillRoundRect(bar_x, BAR_Y, fill, BAR_H, 3, C_ACCENT);
    }

    push_sprite();
}

void display_advance_pixel_shift() {
    s_shift_step = (s_shift_step + 1) % 4;
    s_shift_x = SHIFTS[s_shift_step][0];
    s_shift_y = SHIFTS[s_shift_step][1];
}
