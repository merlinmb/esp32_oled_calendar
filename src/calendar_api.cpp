#include "calendar_api.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Parse "2026-04-16T18:00:00" or "2026-04-16T18:00:00+01:00" into a Unix timestamp.
// Returns 0 on failure.
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

static void extract_hhmm(const char *iso, char *out6) {
    int hr = 0, mn = 0;
    sscanf(iso, "%*4d-%*2d-%*2dT%2d:%2d", &hr, &mn);
    snprintf(out6, 6, "%02d:%02d", hr, mn);
}

bool calendar_fetch(const AppConfig &cfg, CalEvent &ev) {
    memset(&ev, 0, sizeof(ev));

    WiFiClientSecure client;
    client.setInsecure();  // Home LAN device — skip CA verification

    HTTPClient http;
    http.setTimeout(10000);

    if (!http.begin(client, cfg.server_url)) {
        return false;
    }

    if (cfg.read_token[0] != '\0') {
        http.addHeader("X-Read-Token", cfg.read_token);
    }

    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) return false;

    JsonArray events = doc["events"].as<JsonArray>();
    if (events.isNull() || events.size() == 0) {
        ev.has_event = false;
        return true;  // Successful fetch, just no events
    }

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

    ev.ts_start = parse_iso8601(start_str);
    ev.ts_end   = parse_iso8601(end_str);

    extract_hhmm(start_str, ev.time_start);
    extract_hhmm(end_str,   ev.time_end);

    return true;
}
