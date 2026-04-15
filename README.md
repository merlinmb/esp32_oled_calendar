# esp32_oled_calendar

Next-up calendar display for the **LilyGo T-Display-S3** (ESP32-S3, 1.9" ST7789, 170×320).

Fetches the next upcoming event from a self-hosted JSON calendar API and displays it on-screen in the `nextUp` dark theme. Configured entirely through a captive-portal web UI — no reflashing needed to change WiFi or server settings.

## Hardware

- LilyGo T-Display-S3 (ESP32-S3, ST7789 170×320 LCD)
- USB-C cable

## Dependencies

Built with [PlatformIO](https://platformio.org/). Libraries resolved automatically on first build:

- `bodmer/TFT_eSPI` — ST7789 display driver
- `bblanchon/ArduinoJson` — JSON parsing

## First Boot

1. Flash the firmware:
   ```bash
   pio run --target upload
   ```
2. The display shows **Setup Mode**
3. Connect to WiFi network **CalendarSetup** (open, no password)
4. Open **http://192.168.4.1** in a browser
5. Enter your WiFi credentials, calendar server URL, optional read token, and refresh interval
6. Click **Save & Restart** — the device reboots and connects automatically

## Configuration

All settings are stored in NVS and configured via the web portal:

| Field | Default |
|---|---|
| Server URL | `https://calendar.mcmdhome.com/jsonCalendar?timeframe=1d` |
| Read Token | (empty — omitted from request) |
| Refresh interval | 300 seconds |

To reconfigure: connect to the device via serial and trigger a NVS erase, or flash fresh firmware to reset credentials.

## Display Layout

```
┌────────────────────────────────────────────────────┐
│ nextUp                                  HH:MM      │  ← header
├────────────────────────────────────────────────────┤
│  WED 16 APR                                        │  ← date (blue)
│                                                    │
│  Morgan football training                          │  ← event title
│                                                    │
│  18:00 -> 19:30                                    │  ← time
│  @ Pitch 3                                         │  ← location
│                                                    │
│  ████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   │  ← progress bar
└────────────────────────────────────────────────────┘
```

- Dark theme matching the `nextUp` Electron calendar app (`#0b0c11` background, `#5b8ef0` accent)
- Header updates every second
- Progress bar updates every 60 seconds
- Anti-burn-in: frame shifts ±2px every 60 seconds in a 4-step cycle
- Offline indicator shown if fetch fails (last known event retained)

## API Format

The app expects a JSON endpoint returning:

```json
{
  "events": [
    {
      "title": "Morgan football training",
      "start": "2026-04-16T18:00:00",
      "end":   "2026-04-16T19:30:00",
      "isAllDay": false,
      "location": "Pitch 3"
    }
  ]
}
```

Only `events[0]` (the next upcoming event) is displayed.

## Project Structure

```
esp32_oled_calendar/
├── platformio.ini
├── include/
│   ├── config.h          # AppConfig struct, NVS declarations
│   ├── web_server.h      # AP mode, captive portal declarations
│   ├── display.h         # Display rendering declarations, CalEvent struct
│   └── calendar_api.h    # HTTPS fetch declaration
└── src/
    ├── main.cpp          # Boot sequence and millis() main loop
    ├── config.cpp        # NVS Preferences read/write
    ├── web_server.cpp    # AP, DNSServer, WebServer, setup form
    ├── display.cpp       # TFT_eSPI sprite rendering, pixel shift
    └── calendar_api.cpp  # WiFiClientSecure + ArduinoJson
```
