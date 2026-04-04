#ifndef WS2812B_CONTROLLER_CONFIG_H
#define WS2812B_CONTROLLER_CONFIG_H

#include "WS2812B_Controller.h"

struct Config
{
    uint8_t AP_Mode;
    char Wifi_SSID[Wifi_SSID_Length];
    char Wifi_Password[Wifi_Password_Length];
    char AP_SSID[Wifi_SSID_Length];
    char AP_Password[Wifi_Password_Length];
    int8_t LED_Pin;
    uint16_t LED_Count;
    uint8_t Brightness;
};

extern struct Config config;
extern esp_err_t saveConfig();
extern esp_err_t loadConfig();
extern esp_err_t configToJson(cJSON* root);
extern esp_err_t jsonToConfig(cJSON* root);

#endif //WS2812B_CONTROLLER_CONFIG_H