#include "WS2812B.h"
#include "Config.h"

led_strip_handle_t led_strip = NULL;
uint16_t current_led_count;
uint8_t* current_colors;
uint8_t current_brightness;

esp_err_t initLED()
{
    int8_t gpio_pin = config.LED_Pin;

    // 仅允许 0 ~ 11 的 GPIO
    if (gpio_pin < 0 || gpio_pin > 11) {
        ESP_LOGE("LED", "Invalid LED pin: %d (must be 0~11)", gpio_pin);
        return ESP_ERR_INVALID_ARG;
    }

    if (config.LED_Count < 1)
    {
        ESP_LOGE("LED", "Invalid LED count: %d (must be >= 1)", config.LED_Count);
        return ESP_ERR_INVALID_ARG;
    }

    current_led_count = config.LED_Count;
    current_colors = (uint8_t*)malloc(current_led_count * 3);
    memset(current_colors, 0, current_led_count * 3);
    current_brightness = config.Brightness;

    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_pin,
        .max_leds = current_led_count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .mem_block_symbols = WS2812B_MEMORY_BLOCK_WORDS,
        .flags = {
            .with_dma = 0,
        }
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    return ESP_OK;
}

esp_err_t HSV2RGB(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (h >= 360) {
        h = h % 360;
    }
    if (s > 255) {
        s = 255;
    }
    if (v > 255) {
        v = 255;
    }
    uint8_t f = (h % 60) * 255 / 60;
    uint8_t p = (255 - s) * (v / 255);
    uint8_t q = (255 - f * s / 255) * (v / 255);
    uint8_t t = (255 - (255 - f) * s / 255) * (v / 255);
    switch (h / 60)
    {
        case 0:
            *r = v;
            *g = t;
            *b = p;
            break;
        case 1:
            *r = q;
            *g = v;
            *b = p;
            break;
        case 2:
            *r = p;
            *g = v;
            *b = t;
            break;
        case 3:
            *r = p;
            *g = q;
            *b = v;
            break;
        case 4:
            *r = t;
            *g = p;
            *b = v;
            break;
        default:
            *r = v;
            *g = p;
            *b = q;
            break;
    }
    return ESP_OK;
}

esp_err_t setLED_RGB(uint16_t led_index, uint8_t r, uint8_t g, uint8_t b)
{
    if (led_index >= current_led_count) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t true_bright = (uint16_t)current_brightness + 1;
    uint8_t r_r, r_g, r_b;
    r_r = (r * true_bright) >> 8;
    r_g = (g * true_bright) >> 8;
    r_b = (b * true_bright) >> 8;
    esp_err_t ret = led_strip_set_pixel(led_strip, led_index, r_r, r_g, r_b);
    if (ret == ESP_OK)
    {
        current_colors[led_index * 3 + 0] = r;
        current_colors[led_index * 3 + 1] = g;
        current_colors[led_index * 3 + 2] = b;
    }
    return ret;
}
esp_err_t setLED_HSV(uint16_t led_index, uint16_t h, uint8_t s, uint8_t v)
{
    uint8_t r, g, b;
    esp_err_t ret = HSV2RGB(h, s, v, &r, &g, &b);
    if (ret == ESP_OK)
    {
        ret = setLED_RGB(led_index, r, g, b);
    }
    return ret;
}
esp_err_t setLED_Random(uint16_t led_index)
{
    uint16_t h = esp_random() % 360;
    setLED_HSV(led_index, h, 255, 255);
    if (current_brightness == 0)
    {
        current_colors[led_index * 3 + 0] = 0;
        current_colors[led_index * 3 + 1] = 0;
        current_colors[led_index * 3 + 2] = 0;
    }
    else
    {
        uint16_t true_bright = (uint16_t)current_brightness + 1;
        float scale = 255.0f / (float)current_brightness;
        for (int j = 0; j < 3; j++)
        {
            uint8_t orig = (current_colors[led_index * 3 + j] * true_bright) >> 8;
            uint8_t scaled = (uint8_t)((float)orig * scale);
            current_colors[led_index * 3 + j] = scaled;
        }
    }
    return ESP_OK;
}
esp_err_t refreshLED()
{
    return led_strip_refresh(led_strip);
}