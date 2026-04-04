#ifndef WS2812B_CONTROLLER_WS2812B_H
#define WS2812B_CONTROLLER_WS2812B_H

#include "WS2812B_Controller.h"

led_strip_handle_t led_strip;
uint16_t current_led_count;
uint8_t* current_colors;
uint8_t current_brightness;

esp_err_t initLED();
esp_err_t setLED_RGB(uint16_t led_index, uint8_t r, uint8_t g, uint8_t b);
esp_err_t setLED_HSV(uint16_t led_index, uint16_t h, uint8_t s, uint8_t v);
esp_err_t setLED_Random(uint16_t led_index);
esp_err_t refreshLED();

#endif // WS2812B_CONTROLLER_WS2812B_H
