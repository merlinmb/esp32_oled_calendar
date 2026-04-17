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
static const int DATE_Y   = HDR_H + PAD + 20;
static const int TITLE_Y  = DATE_Y + 20;   // NotoSansBold15 date row
static const int TIME_Y   = TITLE_Y + 58;  // 2 title lines × 22px + gap
static const int LOC_Y    = TIME_Y + 22;

// ── Breathe / fade state ───────────────────────────────────────
static bool    s_show_next       = false;  // which slot is currently displayed
static uint8_t s_brightness      = 255;    // current target brightness (0-255)

// ── TFT and sprite ─────────────────────────────────────────────
static TFT_eSPI    s_tft;
static TFT_eSprite s_spr(&s_tft);

// ── Helpers ────────────────────────────────────────────────────

static void push_sprite() {
    s_spr.pushSprite(0, 0);
}

static void draw_header(bool offline) {
    // "nextUp" wordmark
    s_spr.setFreeFont(HEADINGFONT);
    s_spr.setTextColor(C_ACCENT, C_BG_BASE);
    s_spr.setTextSize(1);
    s_spr.setCursor(6, 20);
    s_spr.print("nextUp");

    // Current time HH:MM
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour(), minute());

    s_spr.setFreeFont(TIMEFONT);
    s_spr.setTextColor(C_TEXT_2, C_BG_BASE);
    int time_x = offline ? W - 140 : W - (int)s_spr.textWidth(buf) - 8;
    s_spr.setCursor(time_x, 20);
    s_spr.print(buf);

    // Offline indicator
    if (offline) {
        s_spr.setFreeFont(TIMEFONT);
        s_spr.setTextColor(C_RED, C_BG_BASE);
        s_spr.setCursor(W - 80, 20);
        s_spr.print("offline");
    }

    // Bottom border line
    s_spr.drawFastHLine(0, HDR_H - 1, W, C_BORDER);
}

// ── Public API ─────────────────────────────────────────────────

void display_init() {
    // GPIO15 = PIN_POWER_ON: must be HIGH or the display stays dark
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);

    s_tft.init();
    s_tft.setRotation(1);       // Landscape, USB connector on left
    s_tft.fillScreen(C_BG_BASE);
    s_tft.setSwapBytes(true);

    // GPIO38 = TFT_BL backlight — set up LEDC PWM after TFT init so we
    // take over the pin from TFT_eSPI's digitalWrite(HIGH).
    // ESP32 Arduino core 2.x channel-based API:
    ledcSetup(0, 10000, 8);     // channel 0, 10 kHz, 8-bit resolution
    ledcAttachPin(38, 0);       // attach GPIO38 to channel 0
    ledcWrite(0, 255);          // full brightness

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

// Draw a single event's content into the sprite (header already drawn).
// title/location/time_start/time_end/is_all_day/ts_start/ts_end are passed
// directly so this works for both the primary and secondary event slots.
static void draw_event_content(const char *title, const char *location,
                                const char *time_start, const char *time_end,
                                bool is_all_day, int32_t ts_start, int32_t ts_end,
                                bool has_event, bool show_progress) {
    if (!has_event) {
        s_spr.setFreeFont(LINEFONT);
        s_spr.setTextColor(C_TEXT_2, C_BG_BASE);
        const char *msg = "No upcoming events";
        int tw = s_spr.textWidth(msg);
        s_spr.setCursor((W - tw) / 2, HDR_H + (H - HDR_H) / 2 - 8);
        s_spr.print(msg);
        return;
    }

    // Date row: "WED 16 APR"
    static const char *DAYS[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    static const char *MONS[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                  "JUL","AUG","SEP","OCT","NOV","DEC"};
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

    int tlen = (int)strlen(title);
    if (tlen <= 26) {
        s_spr.setCursor(PAD + 4, TITLE_Y);
        s_spr.print(title);
    } else {
        char line1[30];
        strncpy(line1, title, 26);
        line1[26] = '\0';
        int break_pos = 25;
        for (int i = 25; i > 6; i--) {
            if (line1[i] == ' ') { break_pos = i; break; }
        }
        line1[break_pos] = '\0';

        const char *rest = title + break_pos + (title[break_pos] == ' ' ? 1 : 0);
        char line2[30];
        strncpy(line2, rest, 26);
        line2[26] = '\0';
        if (strlen(rest) > 26) {
            line2[23] = '.'; line2[24] = '.'; line2[25] = '.'; line2[26] = '\0';
        }

        s_spr.setCursor(PAD + 4, TITLE_Y);
        s_spr.print(line1);
        s_spr.setCursor(PAD + 4, TITLE_Y + 28);
        s_spr.print(line2);
    }

    // Time row: "18:00 -> 19:30"
    if (!is_all_day && time_start[0]) {
        s_spr.setFreeFont(TIMEFONT);
        s_spr.setTextColor(C_TEXT_2, C_BG_BASE);
        char time_buf[15];
        snprintf(time_buf, sizeof(time_buf), "%s -> %s", time_start, time_end);
        s_spr.setCursor(PAD + 4, TIME_Y);
        s_spr.print(time_buf);
    }

    // Location row: "@ Venue Name"
    if (location[0] != '\0') {
        s_spr.setFreeFont(LINEFONT);
        s_spr.setTextColor(C_TEXT_2, C_BG_BASE);
        char loc_buf[35];
        snprintf(loc_buf, sizeof(loc_buf), "@ %.30s", location);
        s_spr.setCursor(PAD + 4, LOC_Y);
        s_spr.print(loc_buf);
    }

    // Progress bar — only for the primary event slot during the event
    if (show_progress && !is_all_day && ts_start && ts_end) {
        int32_t now32 = (int32_t)now();
        if (now32 >= ts_start && now32 <= ts_end) {
            int bar_x = PAD + 4;
            int bar_w = W - PAD * 2 - 8;
            s_spr.fillRoundRect(bar_x, BAR_Y, bar_w, BAR_H, 3, C_BG_ACTIVE);
            int32_t duration = ts_end - ts_start;
            int32_t elapsed  = now32 - ts_start;
            int fill = (duration > 0) ? (int)((long)bar_w * elapsed / duration) : 0;
            if (fill > 0)
                s_spr.fillRoundRect(bar_x, BAR_Y, fill, BAR_H, 3, C_ACCENT);
        }
    }
}

void display_render(const CalEvent &ev, bool offline) {
    s_show_next = false;  // reset to primary slot on full re-render
    s_spr.fillSprite(C_BG_BASE);
    draw_header(offline);
    draw_event_content(ev.title, ev.location, ev.time_start, ev.time_end,
                       ev.is_all_day, ev.ts_start, ev.ts_end, ev.has_event, true);
    push_sprite();
}

void display_update_clock(bool offline) {
    // Clear header strip before redrawing to prevent ghost digits
    s_spr.fillRect(0, 0, W, HDR_H, C_BG_BASE);
    draw_header(offline);
    push_sprite();
}


// Fade out via backlight PWM → switch event slot → fade back in.
void display_breathe(const CalEvent &ev, bool offline) {
    // Decide which slot to show after the transition
    bool show_next_after = !s_show_next;
    if (show_next_after && !ev.next_has_event) {
        show_next_after = false;
    }

    // --- Fade out: dim backlight from s_brightness → 0 ---
    for (int b = (int)s_brightness; b >= 0; b -= 3) {
        ledcWrite(0, (uint8_t)b);
        delay(6);
    }
    ledcWrite(0, 0);

    // --- Switch slot and render new content while screen is dark ---
    s_show_next = show_next_after;

    s_spr.fillSprite(C_BG_BASE);
    draw_header(offline);
    if (s_show_next) {
        draw_event_content(ev.next_title, ev.next_location,
                           ev.next_time_start, ev.next_time_end,
                           ev.next_is_all_day,
                           ev.next_ts_start, ev.next_ts_end,
                           ev.next_has_event, false);
    } else {
        draw_event_content(ev.title, ev.location,
                           ev.time_start, ev.time_end,
                           ev.is_all_day,
                           ev.ts_start, ev.ts_end,
                           ev.has_event, true);
    }
    push_sprite();

    // --- Fade in: brighten backlight from 0 → s_brightness ---
    for (int b = 0; b <= (int)s_brightness; b += 3) {
        ledcWrite(0, (uint8_t)b);
        delay(6);
    }
    ledcWrite(0, s_brightness);
}

void display_set_brightness(uint8_t level) {
    s_brightness = level;
    ledcWrite(0, s_brightness);
}
