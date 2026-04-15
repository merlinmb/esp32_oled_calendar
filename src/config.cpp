#include "config.h"
#include <Preferences.h>
#include <string.h>

static const char* NVS_NS = "calclock";

void config_defaults(AppConfig &cfg) {
    strncpy(cfg.server_url,
            "https://calendar.mcmdhome.com/jsonCalendar?timeframe=1d",
            sizeof(cfg.server_url) - 1);
    cfg.server_url[sizeof(cfg.server_url) - 1] = '\0';
    cfg.wifi_ssid[0]     = '\0';
    cfg.wifi_password[0] = '\0';
    cfg.read_token[0]    = '\0';
    cfg.refresh_secs     = 300;
}

void config_load(AppConfig &cfg) {
    config_defaults(cfg);
    Preferences prefs;
    // Open read-write so namespace is created on first boot (avoids NOT_FOUND error)
    if (!prefs.begin(NVS_NS, false)) return;
    prefs.getString("wifi_ssid",  cfg.wifi_ssid,     sizeof(cfg.wifi_ssid));
    prefs.getString("wifi_pass",  cfg.wifi_password, sizeof(cfg.wifi_password));
    prefs.getString("server_url", cfg.server_url,    sizeof(cfg.server_url));
    prefs.getString("read_token", cfg.read_token,    sizeof(cfg.read_token));
    cfg.refresh_secs = prefs.getUShort("refresh_secs", 300);
    if (cfg.refresh_secs < 60 || cfg.refresh_secs > 3600) cfg.refresh_secs = 300;
    prefs.end();
}

void config_save(const AppConfig &cfg) {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putString("wifi_ssid",    cfg.wifi_ssid);
    prefs.putString("wifi_pass",    cfg.wifi_password);
    prefs.putString("server_url",   cfg.server_url);
    prefs.putString("read_token",   cfg.read_token);
    prefs.putUShort("refresh_secs", cfg.refresh_secs);
    prefs.end();
}
