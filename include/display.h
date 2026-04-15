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
};

void display_init();
void display_show_setup();
void display_show_connecting();
void display_render(const CalEvent &ev, bool offline);
void display_update_clock(bool offline);
void display_update_progress(const CalEvent &ev);
void display_advance_pixel_shift();
