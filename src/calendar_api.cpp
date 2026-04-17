#include "calendar_api.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <Timezone.h>

// Parse UTC ISO-8601 "2026-04-16T18:00:00.000Z" into a UTC Unix timestamp.
// Suffix characters (Z, fractional seconds, offsets) are ignored by sscanf — safe
// because the server always sends UTC (toISOString() output). Returns 0 on failure.
static int32_t parse_iso8601(const char *s) {
    int yr = 0, mo = 0, day = 0, hr = 0, mn = 0, sec = 0;
    int matched = sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &yr, &mo, &day, &hr, &mn, &sec);
    if (matched < 5) return 0;
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year  = yr - 1900;
    t.tm_mon   = mo - 1;
    t.tm_mday  = day;
    t.tm_hour  = hr;
    t.tm_min   = mn;
    t.tm_sec   = sec;
    t.tm_isdst = -1;
    time_t result = mktime(&t);
    return (result == (time_t)-1) ? 0 : (int32_t)result;
}

// Convert a UTC epoch to local HH:MM using the given Timezone.
// Falls back to raw UTC digits if tz is null.
static void epoch_to_local_hhmm(int32_t utc_epoch, Timezone *tz, char *out6) {
    time_t local = tz ? tz->toLocal((time_t)utc_epoch) : (time_t)utc_epoch;
    struct tm t;
    gmtime_r(&local, &t);
    snprintf(out6, 6, "%02d:%02d", t.tm_hour, t.tm_min);
}

bool calendar_fetch(const AppConfig &cfg, CalEvent &ev, Timezone *tz) {
    memset(&ev, 0, sizeof(ev));

    Serial.printf("[cal] Fetching URL: '%s'\n", cfg.server_url);
    Serial.printf("[cal] Token len=%d value='%s'\n", strlen(cfg.read_token), cfg.read_token);

    WiFiClientSecure client;
    client.setInsecure();  // Home LAN device — skip CA verification
    // Force HTTP/1.1 — ESP32 WiFiClientSecure doesn't support HTTP/2 (h2).
    // Without this, nginx servers with h2 enabled reject the ALPN negotiation.
    static const char *alpn_protos[] = {"http/1.1", nullptr};
    client.setAlpnProtocols(alpn_protos);

    HTTPClient http;
    http.setTimeout(10000);

    if (!http.begin(client, cfg.server_url)) {
        Serial.println("[cal] http.begin() failed — bad URL?");
        return false;
    }

    if (cfg.read_token[0] != '\0') {
        char bearer[140];
        snprintf(bearer, sizeof(bearer), "Bearer %s", cfg.read_token);
        http.addHeader("Authorization", bearer);
    }

    int code = http.GET();
    Serial.printf("[cal] HTTP response code: %d\n", code);
    if (code != 200) {
        if (code > 0) {
            String errBody = http.getString();
            Serial.printf("[cal] Response body: %s\n", errBody.c_str());
        } else {
            Serial.printf("[cal] HTTP error: %s\n", http.errorToString(code).c_str());
        }
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();
    Serial.printf("[cal] Body length: %d bytes\n", body.length());
    Serial.printf("[cal] Body preview: %.200s\n", body.c_str());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[cal] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonArray events = doc["events"].as<JsonArray>();
    Serial.printf("[cal] events array: %s, size=%d\n",
                  events.isNull() ? "null" : "ok",
                  events.isNull() ? 0 : (int)events.size());
    if (events.isNull() || events.size() == 0) {
        ev.has_event = false;
        return true;  // Successful fetch, just no events
    }

    // --- First event ---
    JsonObject e = events[0];

    const char *title = e["title"] | "";
    strncpy(ev.title, title, sizeof(ev.title) - 1);
    ev.title[sizeof(ev.title) - 1] = '\0';

    const char *loc = e["location"] | "";
    strncpy(ev.location, loc, sizeof(ev.location) - 1);
    ev.location[sizeof(ev.location) - 1] = '\0';

    ev.is_all_day = e["isAllDay"] | false;
    ev.has_event  = true;

    const char *start_str = e["start"] | "";
    const char *end_str   = e["end"]   | "";

    int32_t utc_start = parse_iso8601(start_str);
    int32_t utc_end   = parse_iso8601(end_str);
    ev.ts_start = tz ? (int32_t)tz->toLocal((time_t)utc_start) : utc_start;
    ev.ts_end   = tz ? (int32_t)tz->toLocal((time_t)utc_end)   : utc_end;

    epoch_to_local_hhmm(utc_start, tz, ev.time_start);
    epoch_to_local_hhmm(utc_end,   tz, ev.time_end);

    Serial.printf("[cal] Parsed event: title='%s' start='%s' end='%s' allDay=%d\n",
                  ev.title, start_str, end_str, ev.is_all_day);

    // --- Second event (optional) ---
    if (events.size() >= 2) {
        JsonObject e2 = events[1];

        const char *title2 = e2["title"] | "";
        strncpy(ev.next_title, title2, sizeof(ev.next_title) - 1);
        ev.next_title[sizeof(ev.next_title) - 1] = '\0';

        const char *loc2 = e2["location"] | "";
        strncpy(ev.next_location, loc2, sizeof(ev.next_location) - 1);
        ev.next_location[sizeof(ev.next_location) - 1] = '\0';

        ev.next_is_all_day = e2["isAllDay"] | false;
        ev.next_has_event  = true;

        const char *start2 = e2["start"] | "";
        const char *end2   = e2["end"]   | "";

        int32_t utc_start2 = parse_iso8601(start2);
        int32_t utc_end2   = parse_iso8601(end2);
        ev.next_ts_start = tz ? (int32_t)tz->toLocal((time_t)utc_start2) : utc_start2;
        ev.next_ts_end   = tz ? (int32_t)tz->toLocal((time_t)utc_end2)   : utc_end2;

        epoch_to_local_hhmm(utc_start2, tz, ev.next_time_start);
        epoch_to_local_hhmm(utc_end2,   tz, ev.next_time_end);

        Serial.printf("[cal] Parsed next event: title='%s' start='%s' end='%s' allDay=%d\n",
                      ev.next_title, start2, end2, ev.next_is_all_day);
    }

    return true;
}
