#include "Config.h"

struct Config config = {
    .AP_Mode = 1,
    .Wifi_SSID = "",
    .Wifi_Password = "",
    .AP_SSID = "",
    .AP_Password = "",
    .LED_Pin = -1,
    .LED_Count = -1,
    .Brightness = 16,
    .InvertOut = 0
};

esp_err_t saveConfig()
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("app_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    nvs_set_u8(nvs_handle, "AP_Mode", config.AP_Mode);
    nvs_set_str(nvs_handle, "Wifi_SSID", config.Wifi_SSID);
    nvs_set_str(nvs_handle, "Wifi_Password", config.Wifi_Password);
    nvs_set_str(nvs_handle, "AP_SSID", config.AP_SSID);
    nvs_set_str(nvs_handle, "AP_Password", config.AP_Password);
    nvs_set_i8(nvs_handle, "LED_Pin", config.LED_Pin);
    nvs_set_u16(nvs_handle, "LED_Count", config.LED_Count);
    nvs_set_u8(nvs_handle, "Brightness", config.Brightness);
    nvs_set_u8(nvs_handle, "InvertOut", config.InvertOut);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return ESP_OK;
}
esp_err_t loadConfig()
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("app_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t u8Temp;
    int8_t i8Temp;
    if (nvs_get_u8(nvs_handle, "AP_Mode", &u8Temp) == ESP_OK)
    {
        config.AP_Mode = u8Temp;
    }
    size_t ssid_len = sizeof(config.Wifi_SSID);
    nvs_get_str(nvs_handle, "WIFI_SSID", config.Wifi_SSID, &ssid_len);
    size_t password_len = sizeof(config.Wifi_Password);
    nvs_get_str(nvs_handle, "WIFI_PASSWORD", config.Wifi_Password, &password_len);
    size_t ap_ssid_len = sizeof(config.AP_SSID);
    nvs_get_str(nvs_handle, "AP_SSID", config.AP_SSID, &ap_ssid_len);
    size_t ap_password_len = sizeof(config.AP_Password);
    nvs_get_str(nvs_handle, "AP_PASSWORD", config.AP_Password, &ap_password_len);
    if (nvs_get_i8(nvs_handle, "LED_Pin", &i8Temp) == ESP_OK)
    {
        config.LED_Pin = i8Temp;
    }
    if (nvs_get_u8(nvs_handle, "LED_Count", &u8Temp) == ESP_OK)
    {
        config.LED_Count = u8Temp;
    }
    if (nvs_get_u8(nvs_handle, "Brightness", &u8Temp) == ESP_OK)
    {
        config.Brightness = u8Temp;
    }
    if (nvs_get_u8(nvs_handle, "InvertOut", &u8Temp) == ESP_OK)
    {
        config.InvertOut = u8Temp;
    }
    nvs_close(nvs_handle);
    return ESP_OK;
}

/*

 {
    "AP_Mode": ? (BOOL),
    "Wifi_SSID": "" (STRING),
    "Wifi_Password": "" (STRING),
    "AP_SSID": "" (STRING),
    "AP_Password": "" (STRING),
    "LED_Pin": ? (INT),
    "LED_Block_Count": ? (INT),
    "DefaultBrightness": ? (INT)
    "InvertOut" ? (INT) (0/1)
 }

*/

esp_err_t configToJson(cJSON* root)
{
    cJSON_AddBoolToObject(root, "AP_Mode", config.AP_Mode);
    cJSON_AddStringToObject(root, "Wifi_SSID", config.Wifi_SSID);
    cJSON_AddStringToObject(root, "Wifi_Password", config.Wifi_Password);
    cJSON_AddStringToObject(root, "AP_SSID", config.AP_SSID);
    cJSON_AddStringToObject(root, "AP_Password", config.AP_Password);
    cJSON_AddNumberToObject(root, "LED_Pin", config.LED_Pin);
    cJSON_AddNumberToObject(root, "LED_Count", config.LED_Count);
    cJSON_AddNumberToObject(root, "Brightness", config.Brightness);
    cJSON_AddNumberToObject(root, "InvertOut", config.InvertOut);
    return ESP_OK;
}

esp_err_t jsonToConfig(cJSON* root)
{
    config.AP_Mode = cJSON_GetObjectItem(root, "AP_Mode")->valueint;
    strcpy(config.Wifi_SSID, cJSON_GetObjectItem(root, "Wifi_SSID")->valuestring);
    strcpy(config.Wifi_Password, cJSON_GetObjectItem(root, "Wifi_Password")->valuestring);
    strcpy(config.AP_SSID, cJSON_GetObjectItem(root, "AP_SSID")->valuestring);
    strcpy(config.AP_Password, cJSON_GetObjectItem(root, "AP_Password")->valuestring);
    config.LED_Pin = (int8_t)cJSON_GetObjectItem(root, "LED_Pin")->valueint;
    config.LED_Count = cJSON_GetObjectItem(root, "LED_Count")->valueint;
    config.Brightness = cJSON_GetObjectItem(root, "Brightness")->valueint;
    config.InvertOut = cJSON_GetObjectItem(root, "InvertOut")->valueint;
    return ESP_OK;
}