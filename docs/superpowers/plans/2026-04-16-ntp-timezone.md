# NTP + Timezone (BST/DST) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace esp_sntp with NTPClient (taranais fork) + PaulStoffregen/Time, add a configurable timezone field (NVS-backed, selectable in the web config form), and auto-handle DST transitions.

**Architecture:** NTPClient polls UDP NTP for a UTC epoch; a small lookup table maps timezone region names to PaulStoffregen `Timezone` + `TimeChangeRule` pairs; the converted local epoch is fed to PaulStoffregen `setTime()` so all display code reads local time via `now()`, `hour()`, `minute()`, `day()`, `weekday()`, `month()`. The NTP update runs in `loop()` every 60 s and re-applies the DST conversion each time.

**Tech Stack:** taranais/NTPClient, PaulStoffregen/Time, PlatformIO/espressif32, TFT_eSPI, ArduinoJson, ESP32 Preferences (NVS)

---

## File Map

| File | Change |
|---|---|
| `platformio.ini` | Add two new lib_deps |
| `include/config.h` | Add `timezone[48]` field to `AppConfig` |
| `src/config.cpp` | Save/load `timezone` NVS key; default `"Europe/London"` |
| `include/timezone_db.h` | **New** — declare `tz_lookup()` |
| `src/timezone_db.cpp` | **New** — lookup table + `tz_lookup()` returning a `Timezone` object |
| `src/main.cpp` | Replace `esp_sntp` block with NTPClient init + loop update |
| `src/display.cpp` | Replace `time(nullptr)` + `localtime()` with `now()` + PaulStoffregen accessors |
| `src/web_server.cpp` | Add timezone `<select>` to HTML form; parse + save `tz` field |

---

## Task 1: Add libraries to platformio.ini

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 1: Add lib_deps entries**

Open `platformio.ini`. The current `lib_deps` block ends at line 54. Replace it with:

```ini
lib_deps =
    bodmer/TFT_eSPI @ ^2.5.43
    bblanchon/ArduinoJson @ ^7.0.0
    arduino-libraries/NTPClient @ ^3.2.1
    PaulStoffregen/Time @ ^1.6.1
```

- [ ] **Step 2: Verify PlatformIO resolves the libraries**

```bash
cd e:/GoogleDrive/Arduino/esp32_oled_calendar
pio pkg install
```

Expected: no errors, both new packages downloaded/confirmed.

- [ ] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "chore: add NTPClient and PaulStoffregen/Time dependencies"
```

---

## Task 2: Add timezone field to AppConfig

**Files:**
- Modify: `include/config.h`
- Modify: `src/config.cpp`

- [ ] **Step 1: Add `timezone` field to struct in `include/config.h`**

Replace the struct body:

```cpp
struct AppConfig {
    char wifi_ssid[64];
    char wifi_password[64];
    char server_url[256];
    char read_token[128];
    char timezone[48];
    uint16_t refresh_secs;
};
```

- [ ] **Step 2: Set default in `config_defaults()` in `src/config.cpp`**

After the existing `cfg.refresh_secs = 300;` line in `config_defaults()`, add:

```cpp
    strncpy(cfg.timezone, "Europe/London", sizeof(cfg.timezone) - 1);
    cfg.timezone[sizeof(cfg.timezone) - 1] = '\0';
```

- [ ] **Step 3: Load timezone in `config_load()` in `src/config.cpp`**

After the existing `prefs.getString("read_token", ...)` call, add:

```cpp
    prefs.getString("timezone", cfg.timezone, sizeof(cfg.timezone));
```

- [ ] **Step 4: Save timezone in `config_save()` in `src/config.cpp`**

After the existing `prefs.putString("read_token", ...)` call, add:

```cpp
    prefs.putString("timezone",    cfg.timezone);
```

- [ ] **Step 5: Commit**

```bash
git add include/config.h src/config.cpp
git commit -m "feat: add timezone field to AppConfig with NVS persistence"
```

---

## Task 3: Create timezone lookup table

**Files:**
- Create: `include/timezone_db.h`
- Create: `src/timezone_db.cpp`

- [ ] **Step 1: Create `include/timezone_db.h`**

```cpp
#pragma once
#include <Timezone.h>   // PaulStoffregen/Time

// Returns a heap-allocated Timezone for the given region name string.
// Caller owns the pointer. Returns UTC if name is unrecognised.
Timezone* tz_lookup(const char *name);

// Returns a null-terminated list of supported region name strings.
// Used to populate the web config dropdown.
const char* const* tz_names();
int tz_count();
```

- [ ] **Step 2: Create `src/timezone_db.cpp`**

```cpp
#include "timezone_db.h"

static const char* const NAMES[] = {
    "Europe/London",
    "Europe/Paris",
    "Europe/Athens",
    "America/New_York",
    "America/Chicago",
    "America/Denver",
    "America/Los_Angeles",
    "UTC",
};

static const int TZ_COUNT = sizeof(NAMES) / sizeof(NAMES[0]);

const char* const* tz_names() { return NAMES; }
int tz_count() { return TZ_COUNT; }

Timezone* tz_lookup(const char *name) {
    // Europe/London: GMT/BST  (last Sun Mar 01:00 UTC -> BST, last Sun Oct 01:00 UTC -> GMT)
    if (strcmp(name, "Europe/London") == 0) {
        static TimeChangeRule bst = {"BST", Last, Sun, Mar, 1, 60};
        static TimeChangeRule gmt = {"GMT", Last, Sun, Oct, 2, 0};
        return new Timezone(bst, gmt);
    }
    // Europe/Paris: CET/CEST  (last Sun Mar 02:00 -> CEST +120, last Sun Oct 03:00 -> CET +60)
    if (strcmp(name, "Europe/Paris") == 0) {
        static TimeChangeRule cest = {"CEST", Last, Sun, Mar, 2, 120};
        static TimeChangeRule cet  = {"CET",  Last, Sun, Oct, 3, 60};
        return new Timezone(cest, cet);
    }
    // Europe/Athens: EET/EEST  (+120/+180)
    if (strcmp(name, "Europe/Athens") == 0) {
        static TimeChangeRule eest = {"EEST", Last, Sun, Mar, 3, 180};
        static TimeChangeRule eet  = {"EET",  Last, Sun, Oct, 4, 120};
        return new Timezone(eest, eet);
    }
    // America/New_York: EST/EDT  (2nd Sun Mar 02:00 -> EDT -240, 1st Sun Nov 02:00 -> EST -300)
    if (strcmp(name, "America/New_York") == 0) {
        static TimeChangeRule edt = {"EDT", Second, Sun, Mar, 2, -240};
        static TimeChangeRule est = {"EST", First,  Sun, Nov, 2, -300};
        return new Timezone(edt, est);
    }
    // America/Chicago: CST/CDT  (-360/-300)
    if (strcmp(name, "America/Chicago") == 0) {
        static TimeChangeRule cdt = {"CDT", Second, Sun, Mar, 2, -300};
        static TimeChangeRule cst = {"CST", First,  Sun, Nov, 2, -360};
        return new Timezone(cdt, cst);
    }
    // America/Denver: MST/MDT  (-420/-360)
    if (strcmp(name, "America/Denver") == 0) {
        static TimeChangeRule mdt = {"MDT", Second, Sun, Mar, 2, -360};
        static TimeChangeRule mst = {"MST", First,  Sun, Nov, 2, -420};
        return new Timezone(mdt, mst);
    }
    // America/Los_Angeles: PST/PDT  (-480/-420)
    if (strcmp(name, "America/Los_Angeles") == 0) {
        static TimeChangeRule pdt = {"PDT", Second, Sun, Mar, 2, -420};
        static TimeChangeRule pst = {"PST", First,  Sun, Nov, 2, -480};
        return new Timezone(pdt, pst);
    }
    // UTC fallback
    static TimeChangeRule utc = {"UTC", Last, Sun, Jan, 1, 0};
    return new Timezone(utc, utc);
}
```

- [ ] **Step 3: Commit**

```bash
git add include/timezone_db.h src/timezone_db.cpp
git commit -m "feat: add timezone lookup table with DST rules for 7 regions"
```

---

## Task 4: Replace esp_sntp with NTPClient in main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Replace includes and add globals**

Replace the top of `src/main.cpp` (lines 1–9) with:

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <string.h>
#include "config.h"
#include "web_server.h"
#include "display.h"
#include "calendar_api.h"
#include "timezone_db.h"
```

- [ ] **Step 2: Add NTP globals after the static AppConfig / CalEvent declarations**

After the `static bool g_offline = false;` line, add:

```cpp
static WiFiUDP       g_udp;
static NTPClient     g_ntp(g_udp, "pool.ntp.org", 0, 60000);
static Timezone     *g_tz   = nullptr;
```

- [ ] **Step 3: Replace `sync_time()` function**

Remove the entire existing `sync_time()` function (lines 20–40) and replace with:

```cpp
static void sync_time() {
    g_tz = tz_lookup(g_cfg.timezone);

    g_ntp.begin();
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
```

- [ ] **Step 4: Add NTP resync to `loop()` — keep local clock in sync**

Inside `loop()`, after the `ws_handle()` call and the `g_ap_mode` guard, add a resync block. Insert it just before the `// Calendar fetch` comment:

```cpp
    // NTP resync — NTPClient polls every 60 s internally; re-apply DST each time
    if (g_ntp.update()) {
        time_t utc   = (time_t)g_ntp.getEpochTime();
        time_t local = g_tz->toLocal(utc);
        setTime(local);
    }
```

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: replace esp_sntp with NTPClient + PaulStoffregen/Time for DST-aware timekeeping"
```

---

## Task 5: Update display.cpp to use PaulStoffregen/Time accessors

**Files:**
- Modify: `src/display.cpp`

The display currently uses `time(nullptr)` + `localtime()` + `struct tm` in three places. Replace all three with PaulStoffregen `now()` + accessor functions.

- [ ] **Step 1: Replace includes at top of `src/display.cpp`**

Remove `#include <time.h>` and add `#include <TimeLib.h>`:

```cpp
#include "display.h"
#include <TFT_eSPI.h>
#include <TimeLib.h>
#include <stdio.h>
#include <string.h>
#include "fonts/Orbitron_Medium_20.h"
#include "fonts/Monospaced_plain_16.h"
#include "fonts/Orbitron_Bold_16.h"
```

- [ ] **Step 2: Replace `draw_header()` time block (lines 61–64)**

Replace:
```cpp
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
```
With:
```cpp
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour(), minute());
```

- [ ] **Step 3: Replace `display_render()` date block (lines 164–171)**

Replace:
```cpp
    time_t now_ts = time(nullptr);
    struct tm *now_tm = localtime(&now_ts);
    static const char *DAYS[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    static const char *MONS[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                  "JUL","AUG","SEP","OCT","NOV","DEC"};
    char date_buf[20];
    snprintf(date_buf, sizeof(date_buf), "%s %02d %s",
             DAYS[now_tm->tm_wday], now_tm->tm_mday, MONS[now_tm->tm_mon]);
```
With:
```cpp
    static const char *DAYS[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    static const char *MONS[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                  "JUL","AUG","SEP","OCT","NOV","DEC"};
    // weekday(): 1=Sun..7=Sat; month(): 1..12; day(): 1..31
    char date_buf[20];
    snprintf(date_buf, sizeof(date_buf), "%s %02d %s",
             DAYS[weekday() - 1], day(), MONS[month() - 1]);
```

- [ ] **Step 4: Replace `now_ts` usage in progress bar (lines 232–233)**

The variable `now_ts` was used for the progress bar comparison. After the previous replacement it no longer exists. Replace the progress bar block:

```cpp
    // Progress bar — only during event (between start and end)
    if (!ev.is_all_day && ev.ts_start && ev.ts_end) {
        int32_t now32 = (int32_t)now();
        if (now32 >= ev.ts_start && now32 <= ev.ts_end) {
```

- [ ] **Step 5: Replace `time(nullptr)` in `display_update_progress()` (line 263)**

Replace:
```cpp
    int32_t now32 = (int32_t)time(nullptr);
```
With:
```cpp
    int32_t now32 = (int32_t)now();
```

- [ ] **Step 6: Commit**

```bash
git add src/display.cpp
git commit -m "feat: replace time()/localtime() with PaulStoffregen now()/hour()/minute()/day() in display"
```

---

## Task 6: Add timezone selector to web config form

**Files:**
- Modify: `src/web_server.cpp`

- [ ] **Step 1: Add `#include "timezone_db.h"` at top of `src/web_server.cpp`**

After the existing includes, add:
```cpp
#include "timezone_db.h"
```

- [ ] **Step 2: Replace the static `SETUP_HTML` with a dynamically-built page**

The current page is a `PROGMEM` string constant — it can't include a dynamic `<select>`. Replace the entire `SETUP_HTML` constant and `handle_root()` function with a function that builds the HTML at request time using the `tz_names()` list.

Remove the `static const char SETUP_HTML[] PROGMEM = R"rawhtml(...)rawhtml";` block and the `static void handle_root()` function, and replace with:

```cpp
static void handle_root() {
    // Build timezone <select> options from the lookup table
    String tz_opts = "";
    const char* const* names = tz_names();
    int count = tz_count();
    const char* saved_tz = s_cfg_ptr ? s_cfg_ptr->timezone : "Europe/London";
    for (int i = 0; i < count; i++) {
        tz_opts += "<option value=\"";
        tz_opts += names[i];
        tz_opts += "\"";
        if (strcmp(names[i], saved_tz) == 0) tz_opts += " selected";
        tz_opts += ">";
        tz_opts += names[i];
        tz_opts += "</option>";
    }

    String html = F("<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Calendar Setup</title>"
        "<style>"
        "body{background:#0b0c11;color:#e8eaf2;font-family:system-ui,sans-serif;"
        "display:flex;justify-content:center;padding:20px}"
        ".card{background:#111318;border-radius:10px;padding:24px;width:100%;max-width:420px}"
        "h1{color:#5b8ef0;font-size:18px;margin:0 0 20px}"
        "label{display:block;color:#8b90aa;font-size:12px;margin:12px 0 4px}"
        "input,select{width:100%;box-sizing:border-box;background:#1e202c;"
        "border:1px solid rgba(255,255,255,0.12);border-radius:6px;"
        "color:#e8eaf2;padding:8px 10px;font-size:14px}"
        "button{margin-top:20px;width:100%;background:#5b8ef0;color:#0b0c11;border:none;"
        "border-radius:6px;padding:10px;font-size:15px;font-weight:600;cursor:pointer}"
        ".note{color:#8b90aa;font-size:11px;margin-top:6px}"
        "</style>"
        "</head><body><div class='card'>"
        "<h1>nextUp &mdash; Calendar Setup</h1>"
        "<form method='POST' action='/save'>"
        "<label>WiFi SSID</label>"
        "<input name='ssid' required maxlength='63'>"
        "<label>WiFi Password</label>"
        "<input name='pass' type='password' maxlength='63'>"
        "<label>Calendar Server URL</label>"
        "<input name='url' value='https://calendar.mcmdhome.com/jsonCalendar?timeframe=1d' maxlength='255'>"
        "<label>Read Token <span class='note'>(optional)</span></label>"
        "<input name='token' maxlength='127'>"
        "<label>Timezone</label>"
        "<select name='tz'>");
    html += tz_opts;
    html += F("</select>"
        "<label>Refresh Interval (seconds, 60&ndash;3600)</label>"
        "<input name='refresh' type='number' value='300' min='60' max='3600'>"
        "<button type='submit'>Save &amp; Restart</button>"
        "</form></div></body></html>");

    s_server.send(200, "text/html", html);
}
```

- [ ] **Step 3: Parse and save `tz` field in `handle_save()`**

After the existing `token` parsing block (after the `strncpy(s_cfg_ptr->read_token, ...)` line), add:

```cpp
    String tz = s_server.arg("tz");
    if (tz.length() > 0) {
        strncpy(s_cfg_ptr->timezone, tz.c_str(), sizeof(s_cfg_ptr->timezone) - 1);
        s_cfg_ptr->timezone[sizeof(s_cfg_ptr->timezone) - 1] = '\0';
    }
```

- [ ] **Step 4: Commit**

```bash
git add src/web_server.cpp include/timezone_db.h src/timezone_db.cpp
git commit -m "feat: add timezone dropdown to web config form"
```

---

## Task 7: Build and verify

- [ ] **Step 1: Build**

```bash
cd e:/GoogleDrive/Arduino/esp32_oled_calendar
pio run
```

Expected: `SUCCESS` with no errors. Common issues to fix:
- `'weekday' was not declared` → missing `#include <TimeLib.h>` in display.cpp
- `'Timezone' was not declared` → missing `#include <Timezone.h>` in timezone_db.h (it is included via `<Timezone.h>` from PaulStoffregen/Time)
- Linker errors about `tz_lookup` → check `src/timezone_db.cpp` is in the `src/` directory

- [ ] **Step 2: Flash and smoke-test**

```bash
pio run --target upload && pio device monitor
```

Expected serial output after WiFi connects:
```
[ntp] UTC=<epoch>  local=<epoch+offset>  tz=Europe/London
```
Display should show correct local time in the header (BST = UTC+1 during summer, GMT = UTC+0 during winter).

- [ ] **Step 3: Test config form**

Open `http://<device-ip>/` in a browser. Confirm:
- The form renders with a Timezone dropdown pre-selected to the saved value
- Selecting a different timezone, saving, and rebooting results in the correct offset shown on the display

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "feat: NTPClient + PaulStoffregen/Time with configurable DST-aware timezone"
```
