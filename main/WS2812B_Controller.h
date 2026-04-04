#ifndef WS2812B_CONTROLLER_H
#define WS2812B_CONTROLLER_H

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "led_strip.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "led_strip_interface.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "math.h"
#include <esp_littlefs.h>

#define Wifi_SSID_Length 32
#define Wifi_Password_Length 64
#define API_Max_Retry 5

#define ROOT_HTML_FILE "root.html"
#define CONFIG_HTML_FILE "config.html"
#define LED_POSITION_FILE "led_positions.bin"

static const char *TAG = "WS2812B_Server";
#define WS2812B_MEMORY_BLOCK_WORDS 0
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

#define LITTLE_FS_MOUNT_POINT "/lfs"
#define LITTLE_FS_PARTITION_NAME "lfs"

#endif //WS2812B_CONTROLLER_H