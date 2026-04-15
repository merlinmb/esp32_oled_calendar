#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <string.h>
#include "config.h"
#include "web_server.h"
#include "display.h"
#include "calendar_api.h"

static AppConfig     g_cfg;
static CalEvent      g_event;
static bool          g_offline      = false;

static unsigned long g_last_clock    = 0;
static unsigned long g_last_progress = 0;
static unsigned long g_last_fetch    = 0;
static bool          g_first_fetch   = true;  // Force fetch immediately on first loop

static void sync_time() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    // Wait up to 5 seconds for a valid timestamp
    time_t now = 0;
    int attempts = 0;
    while (now < 1000000000UL && attempts < 50) {
        delay(100);
        now = time(nullptr);
        attempts++;
    }
    Serial.printf("[ntp] Time synced: %ld\n", (long)now);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[boot] Starting esp32_oled_calendar");

    display_init();

    config_load(g_cfg);
    Serial.printf("[boot] Config loaded. SSID: '%s'\n", g_cfg.wifi_ssid);

    if (g_cfg.wifi_ssid[0] == '\0') {
        Serial.println("[boot] No WiFi credentials — entering AP setup mode");
        ws_start_ap(g_cfg);
        display_show_setup();
        return;
    }

    display_show_connecting();

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_password);
    Serial.printf("[boot] Connecting to '%s'", g_cfg.wifi_ssid);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[boot] WiFi failed — falling back to AP setup mode");
        ws_start_ap(g_cfg);
        display_show_setup();
        return;
    }

    Serial.printf("[boot] WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
    sync_time();

    memset(&g_event, 0, sizeof(g_event));
}

void loop() {
    if (g_ap_mode) {
        ws_handle();
        return;
    }

    unsigned long now = millis();

    // Calendar fetch — immediately on first loop, then every refresh_secs
    if (g_first_fetch || (now - g_last_fetch >= (unsigned long)g_cfg.refresh_secs * 1000UL)) {
        g_last_fetch  = now;
        g_first_fetch = false;

        Serial.println("[loop] Fetching calendar...");
        CalEvent tmp;
        bool ok = calendar_fetch(g_cfg, tmp);

        if (ok) {
            g_event   = tmp;
            g_offline = false;
            Serial.printf("[loop] Fetch OK. has_event=%d title='%s'\n",
                          g_event.has_event, g_event.title);
        } else {
            g_offline = true;
            Serial.println("[loop] Fetch failed — showing offline indicator");
        }

        display_render(g_event, g_offline);
        g_last_clock    = now;
        g_last_progress = now;
    }

    // Clock update every second
    if (now - g_last_clock >= 1000UL) {
        g_last_clock = now;
        display_update_clock(g_offline);
    }

    // Progress bar + pixel shift every 60 seconds
    if (now - g_last_progress >= 60000UL) {
        g_last_progress = now;
        display_update_progress(g_event);
        display_advance_pixel_shift();
    }
}
