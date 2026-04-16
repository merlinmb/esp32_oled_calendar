#include "display.h"
#include <TFT_eSPI.h>
#include <TimeLib.h>
#include <stdio.h>
#include <string.h>
#include "fonts/Orbitron_Medium_20.h"
#include "fonts/Monospaced_plain_16.h"
#include "fonts/Orbitron_Bold_16.h"

#define LINEFONT    &Orbitron_Bold_16
#define TIMEFONT    &Monospaced_plain_16
#define HEADINGFONT &Orbitron_Bold_16

// ── Colours ────────────────────────────────────────────────────
static const uint16_t C_BG_BASE    = TFT_BLACK;
static const uint16_t C_BG_SURFACE = TFT_BLACK;
static const uint16_t C_BG_ACTIVE  = TFT_BLACK;
static const uint16_t C_TEXT_1     = TFT_WHITE;
static const uint16_t C_TEXT_2     = TFT_YELLOW;
static const uint16_t C_ACCENT     = TFT_MAGENTA;
static const uint16_t C_RED        = TFT_YELLOW;
static const uint16_t C_BORDER     = TFT_WHITE;

// ── Layout constants (landscape 320×170) ──────────────────────
static const int W        = 320;
static const int H        = 170;
static const int HDR_H    =  26;  // header bar height px
static const int PAD      =   6;  // card padding
static const int BAR_H    =   6;  // progress bar height
static const int BAR_Y    = H - PAD - BAR_H;
static const int DATE_Y   = HDR_H + PAD;
static const int TITLE_Y  = DATE_Y + 20;   // NotoSansBold15 date row
static const int TIME_Y   = TITLE_Y + 58;  // 2 title lines × 22px + gap
static const int LOC_Y    = TIME_Y + 22;

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
    // "nextUp" wordmark
    s_spr.setFreeFont(HEADINGFONT);
    s_spr.setTextColor(C_ACCENT, C_BG_BASE);
    s_spr.setTextSize(1);
    s_spr.setCursor(6, 4);
    s_spr.print("nextUp");

    // Current time HH:MM
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour(), minute());

    s_spr.setFreeFont(TIMEFONT);
    s_spr.setTextColor(C_TEXT_2, C_BG_BASE);
    int time_x = offline ? W - 130 : W - (int)s_spr.textWidth(buf) - 8;
    s_spr.setCursor(time_x, 4);
    s_spr.print(buf);

    // Offline indicator
    if (offline) {
        s_spr.setFreeFont(TIMEFONT);
        s_spr.setTextColor(C_RED, C_BG_BASE);
        s_spr.setCursor(W - 70, 4);
        s_spr.print("OFFLINE");
    }

    // Bottom border line
    s_spr.drawFastHLine(0, HDR_H - 1, W, C_BORDER);
}

// ── Public API ─────────────────────────────────────────────────

void display_init() {
    // GPIO15 = PIN_POWER_ON: must be HIGH or the display stays dark
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);

    // GPIO38 = TFT_BL backlight: drive HIGH explicitly before init
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);

    s_tft.init();
    s_tft.setRotation(1);       // Landscape, USB connector on left
    s_tft.fillScreen(C_BG_BASE);
    s_tft.setSwapBytes(true);

    s_spr.createSprite(W, H);
    s_spr.setSwapBytes(true);
}

void display_show_setup() {
    s_spr.fillSprite(C_BG_BASE);
    draw_header(false);

    // Title
    s_spr.setFreeFont(HEADINGFONT);
    s_spr.setTextSize(1);
    s_spr.setTextColor(C_ACCENT, C_BG_BASE);
    const char *title = "Setup Mode";
    int tw = s_spr.textWidth(title);
    s_spr.setCursor((W - tw) / 2, HDR_H + 16);
    s_spr.print(title);

    // Instructions
    s_spr.setFreeFont(LINEFONT);
    s_spr.setTextColor(C_TEXT_2, C_BG_BASE);

    const char *l1 = "WiFi: CalendarSetup";
    tw = s_spr.textWidth(l1);
    s_spr.setCursor((W - tw) / 2, HDR_H + 56);
    s_spr.print(l1);

    const char *l2 = "http://192.168.4.1";
    tw = s_spr.textWidth(l2);
    s_spr.setCursor((W - tw) / 2, HDR_H + 78);
    s_spr.print(l2);

    push_sprite();
}

void display_show_connecting() {
    s_spr.fillSprite(C_BG_BASE);
    draw_header(false);

    s_spr.setFreeFont(HEADINGFONT);
    s_spr.setTextColor(C_TEXT_2, C_BG_BASE);
    const char *msg = "Connecting...";
    int tw = s_spr.textWidth(msg);
    s_spr.setCursor((W - tw) / 2, HDR_H + (H - HDR_H) / 2 - 8);
    s_spr.print(msg);

    push_sprite();
}

void display_render(const CalEvent &ev, bool offline) {
    s_spr.fillSprite(C_BG_BASE);
    draw_header(offline);

    if (!ev.has_event) {
        s_spr.setFreeFont(LINEFONT);
        s_spr.setTextColor(C_TEXT_2, C_BG_BASE);
        const char *msg = "No upcoming events";
        int tw = s_spr.textWidth(msg);
        s_spr.setCursor((W - tw) / 2, HDR_H + (H - HDR_H) / 2 - 8);
        s_spr.print(msg);
        push_sprite();
        return;
    }

    // Date row: "WED 16 APR"
    static const char *DAYS[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    static const char *MONS[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                  "JUL","AUG","SEP","OCT","NOV","DEC"};
    // weekday(): 1=Sun..7=Sat; month(): 1..12; day(): 1..31
    char date_buf[20];
    snprintf(date_buf, sizeof(date_buf), "%s %02d %s",
             DAYS[weekday() - 1], day(), MONS[month() - 1]);
    s_spr.setFreeFont(LINEFONT);
    s_spr.setTextColor(C_ACCENT, C_BG_BASE);
    s_spr.setCursor(PAD + 4, DATE_Y);
    s_spr.print(date_buf);

    // Title — up to 2 lines, Orbitron_Medium_20
    s_spr.setFreeFont(&Orbitron_Medium_20);
    s_spr.setTextColor(C_TEXT_1, C_BG_BASE);

    int tlen = (int)strlen(ev.title);
    if (tlen <= 18) {
        s_spr.setCursor(PAD + 4, TITLE_Y);
        s_spr.print(ev.title);
    } else {
        // Find word-break point
        char line1[22];
        strncpy(line1, ev.title, 18);
        line1[18] = '\0';
        int break_pos = 17;
        for (int i = 17; i > 6; i--) {
            if (line1[i] == ' ') { break_pos = i; break; }
        }
        line1[break_pos] = '\0';

        const char *rest = ev.title + break_pos + (ev.title[break_pos] == ' ' ? 1 : 0);
        char line2[22];
        strncpy(line2, rest, 18);
        line2[18] = '\0';
        if (strlen(rest) > 18) {
            line2[15] = '.'; line2[16] = '.'; line2[17] = '.'; line2[18] = '\0';
        }

        s_spr.setCursor(PAD + 4, TITLE_Y);
        s_spr.print(line1);
        s_spr.setCursor(PAD + 4, TITLE_Y + 28);
        s_spr.print(line2);
    }

    // Time row: "18:00 -> 19:30"
    if (!ev.is_all_day && ev.time_start[0]) {
        s_spr.setFreeFont(TIMEFONT);
        s_spr.setTextColor(C_TEXT_2, C_BG_BASE);
        char time_buf[14];
        snprintf(time_buf, sizeof(time_buf), "%s -> %s", ev.time_start, ev.time_end);
        s_spr.setCursor(PAD + 4, TIME_Y);
        s_spr.print(time_buf);
    }

    // Location row: "@ Venue Name"
    if (ev.location[0] != '\0') {
        s_spr.setFreeFont(LINEFONT);
        s_spr.setTextColor(C_TEXT_2, C_BG_BASE);
        char loc_buf[35];
        snprintf(loc_buf, sizeof(loc_buf), "@ %.30s", ev.location);
        s_spr.setCursor(PAD + 4, LOC_Y);
        s_spr.print(loc_buf);
    }

    // Progress bar — only during event (between start and end)
    if (!ev.is_all_day && ev.ts_start && ev.ts_end) {
        int32_t now32 = (int32_t)now();
        if (now32 >= ev.ts_start && now32 <= ev.ts_end) {
            int bar_x = PAD + 4;
            int bar_w = W - PAD * 2 - 8;
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

    int bar_x = PAD + 4;
    int bar_w = W - PAD * 2 - 8;

    // Clear bar area
    s_spr.fillRoundRect(bar_x, BAR_Y, bar_w, BAR_H, 3, C_BG_ACTIVE);

    int32_t now32 = (int32_t)now();
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
