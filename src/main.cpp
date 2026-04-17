#include <Arduino.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <string.h>
#include "config.h"
#include "web_server.h"
#include "display.h"
#include "calendar_api.h"
#include "timezone_db.h"
#include "mqtt_client.h"

static AppConfig     g_cfg;
static CalEvent      g_event;
static bool          g_offline      = false;
static WiFiUDP       g_udp;
static NTPClient     g_ntp(g_udp, "pool.ntp.org", 0, 60000);
static Timezone     *g_tz   = nullptr;

static unsigned long g_last_clock   = 0;
static unsigned long g_last_breathe = 0;
static unsigned long g_last_fetch   = 0;
static bool          g_first_fetch  = true;  // Force fetch immediately on first loop

static void sync_time() {
    g_tz = tz_lookup(g_cfg.timezone);

    // Force an immediate update; retry up to 10 times (~5 s)
    bool ok = false;
    for (int i = 0; i < 10 && !ok; i++) {
        ok = g_ntp.forceUpdate();
        if (!ok) delay(500);
    }

    time_t utc = (time_t)g_ntp.getEpochTime();
    time_t local = g_tz->toLocal(utc);
    setTime(local);
    Serial.printf("[ntp] UTC=%ld  local=%ld  tz=%s\n",
                  (long)utc, (long)local, g_cfg.timezone);
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
    g_ntp.begin();
    sync_time();
    ws_start_sta(g_cfg);
    mqtt_client_init(g_cfg);

    memset(&g_event, 0, sizeof(g_event));
}

void loop() {
    ws_handle();

    if (g_ap_mode) {
        return;
    }

    unsigned long now = millis();

    mqtt_client_tick();

    // NTP resync — check once per minute; NTPClient will only hit the network
    // every 60 s (its configured interval), avoiding constant parsePacket() errors.
    static unsigned long s_last_ntp = 0;
    if (now - s_last_ntp >= 60000UL) {
        s_last_ntp = now;
        if (g_ntp.update()) {
            time_t utc   = (time_t)g_ntp.getEpochTime();
            time_t local = g_tz->toLocal(utc);
            setTime(local);
        }
    }

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
        g_last_clock   = now;
        g_last_breathe = now;
    }

    // Clock update every second
    if (now - g_last_clock >= 1000UL) {
        g_last_clock = now;
        display_update_clock(g_offline);
    }

    // Breathe (fade out/in + alternate event slot) every 30 seconds
    if (now - g_last_breathe >= 30000UL) {
        g_last_breathe = now;
        display_breathe(g_event, g_offline);
    }
}
