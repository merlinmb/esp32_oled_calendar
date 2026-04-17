#pragma once
#include <stdint.h>

struct CalEvent {
    char title[41];
    char location[31];
    char time_start[6];  // "HH:MM"
    char time_end[6];    // "HH:MM"
    bool is_all_day;
    bool has_event;
    // Unix timestamps for progress bar
    int32_t ts_start;
    int32_t ts_end;

    // Second upcoming event (may be empty)
    char next_title[41];
    char next_location[31];
    char next_time_start[6];
    char next_time_end[6];
    bool next_is_all_day;
    bool next_has_event;
    int32_t next_ts_start;
    int32_t next_ts_end;
};

void display_init();
void display_show_setup();
void display_show_connecting();
void display_render(const CalEvent &ev, bool offline);
void display_update_clock(bool offline);
// Fade out current content, switch to alternate event slot, fade back in.
// Call every ~30 s from the main loop.
void display_breathe(const CalEvent &ev, bool offline);
// Set backlight brightness 0-255. Called from MQTT callback.
void display_set_brightness(uint8_t level);
