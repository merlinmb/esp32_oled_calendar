#pragma once
#include <stdint.h>

struct AppConfig {
    char wifi_ssid[64];
    char wifi_password[64];
    char server_url[256];
    char read_token[128];
    char timezone[48];
    uint16_t refresh_secs;
};

void config_defaults(AppConfig &cfg);
void config_load(AppConfig &cfg);
void config_save(const AppConfig &cfg);
