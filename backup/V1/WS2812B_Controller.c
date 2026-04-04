#include <stdio.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "led_strip.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "led_strip_interface.h"
#include "nvs_flash.h"

#define WS2812B_COUNT 25  // WS2812B的数量 如果需要修改数量 需要同时修改html 否则无法在网页控制
#define WS2812B_MEMORY_BLOCK_WORDS 0  // ESP32C3 好像没有RMT DMA
#define LED_STRIP_GPIO_PIN 4  // WS2812B 控制引脚 经测试能在GPIO9工作
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)
#define DEFAULT_LED_BRIGHTNESS 16  // 初始亮度 0~255 如果供电不足不推荐满亮度

#define WIFI_SSID      "CMCC-2.4G Xu"  // 修改为自己的WIFI名称
#define WIFI_PASS      "xu123456789"   // 修改为自己的WIFI密码

static u_int8_t LED_BRIGHTNESS = DEFAULT_LED_BRIGHTNESS;
static const char *TAG = "WS2812B_Server";
static bool IsConnectWifi = false;
static bool IsConnectionHappened = false;

static uint8_t current_colors[WS2812B_COUNT][3] = {0};  // 存储WS2812B的颜色 我暂时找不到对应的API去查询颜色
static led_strip_handle_t led_strip;
static httpd_handle_t server = NULL;
static esp_err_t handle_api_set(httpd_req_t *req);
static esp_err_t handle_api_get(httpd_req_t *req);
static esp_err_t handle_api_fill_random(httpd_req_t *req);
static esp_err_t handle_api_set_brightness(httpd_req_t *req);
static esp_err_t handle_root(httpd_req_t *req);

static const uint8_t digit_patterns[10][5] = {
    {0x07, 0x05, 0x05, 0x05, 0x07}, // 0
    {0x02, 0x02, 0x02, 0x02, 0x02}, // 1
    {0x07, 0x01, 0x07, 0x04, 0x07}, // 2
    {0x07, 0x01, 0x07, 0x01, 0x07}, // 3
    {0x05, 0x05, 0x07, 0x01, 0x01}, // 4
    {0x07, 0x04, 0x07, 0x01, 0x07}, // 5
    {0x07, 0x04, 0x07, 0x05, 0x07}, // 6
    {0x07, 0x01, 0x02, 0x02, 0x02}, // 7
    {0x07, 0x05, 0x07, 0x05, 0x07}, // 8
    {0x07, 0x05, 0x07, 0x01, 0x07}  // 9
};

// 设置WS2812B的颜色 会应用配置中的亮度 需要手动刷新
// r->(0~255) g -> (0~255) b -> (0~255)
static esp_err_t SetWS2812B_RGB(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= WS2812B_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t true_bright = (uint16_t)LED_BRIGHTNESS + 1;
    uint8_t r_r, r_g, r_b;
    r_r = (r * true_bright) >> 8;
    r_g = (g * true_bright) >> 8;
    r_b = (b * true_bright) >> 8;
    esp_err_t ret = led_strip_set_pixel(led_strip, index, r_r, r_g, r_b);

    if (!(current_colors[index][0] == r && current_colors[index][1] == g && current_colors[index][2] == b))
    {
        ESP_LOGI(TAG, "SetWS2812B_RGB: %ld %d-%d, %d-%d, %d-%d", index, r, r_r, g, r_g, b, r_b);
    }

    if (ret == ESP_OK)
    {
        current_colors[index][0] = r;
        current_colors[index][1] = g;
        current_colors[index][2] = b;
    }

    return ret;
}

// h -> (0~359) s -> (0~255) v -> (0~255)
static esp_err_t SetWS2812B_HSV(uint32_t index, uint16_t h, uint8_t s, uint8_t v)
{
    if (index >= WS2812B_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t r, g, b;
    uint8_t region, remainder;

    if (s == 0) {
        r = g = b = v;
    } else {
        uint16_t hue = h % 360;
        region = hue / 60;
        remainder = (hue % 60) * 255 / 60;

        uint16_t p_val = (v * (255 - s)) / 255;
        uint16_t q_val = (v * (255 - (s * remainder) / 255)) / 255;
        uint16_t t_val = (v * (255 - (s * (255 - remainder)) / 255)) / 255;

        switch (region) {
        case 0:
            r = v;
            g = t_val;
            b = p_val;
            break;
        case 1:
            r = q_val;
            g = v;
            b = p_val;
            break;
        case 2:
            r = p_val;
            g = v;
            b = t_val;
            break;
        case 3:
            r = p_val;
            g = q_val;
            b = v;
            break;
        case 4:
            r = t_val;
            g = p_val;
            b = v;
            break;
        default:
            r = v;
            g = p_val;
            b = q_val;
            break;
        }
    }

    return SetWS2812B_RGB(index, r, g, b);
}

// 随机填充所有WS2812B的颜色
static esp_err_t FillWS2812B_Random()
{
    for (int i = 0; i < WS2812B_COUNT; i++)
    {
        uint16_t h = esp_random() % 360;
        SetWS2812B_HSV(i, h, 255, 255);
    }
    return ESP_OK;
}

static void clear_leds(void)
{
    for (int i = 0; i < WS2812B_COUNT; i++) {
        SetWS2812B_RGB(i, 0, 0, 0);
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi 断开，尝试重连...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "获得 IP：" IPSTR, IP2STR(&event->ip_info.ip));
        IsConnectWifi = true;
    }
}

static void init_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理（可选，用于打印连接状态）
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi 启动，正在连接...");
}


led_strip_handle_t init_led(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,
        .max_leds = WS2812B_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .mem_block_symbols = WS2812B_MEMORY_BLOCK_WORDS,
        .flags = {
            .with_dma = 0,
        }
    };
    led_strip_handle_t led_strip;
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    return led_strip;
}

static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;  // 允许自动清理

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = handle_root,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_root);

        httpd_uri_t uri_get = {
            .uri       = "/api/get",
            .method    = HTTP_GET,
            .handler   = handle_api_get,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_get);

        httpd_uri_t uri_set = {
            .uri       = "/api/set",
            .method    = HTTP_POST,
            .handler   = handle_api_set,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_set);

        httpd_uri_t uri_fill_random = {
            .uri       = "/api/fill_random",
            .method    = HTTP_POST,
            .handler   = handle_api_fill_random,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_fill_random);

        httpd_uri_t uri_set_brightness = {
            .uri       = "/api/set_brightness",
            .method    = HTTP_POST,
            .handler   = handle_api_set_brightness,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_set_brightness);

        ESP_LOGI(TAG, "HTTP 服务器已启动");
    } else {
        ESP_LOGE(TAG, "HTTP 服务器启动失败");
    }
}

static esp_err_t handle_api_set(httpd_req_t *req)
{
    IsConnectionHappened = true;
    int total_len = req->content_len;
    if (total_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    char *buf = (char *)malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    int ret = httpd_req_recv(req, buf, total_len);
    if (ret != total_len) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected JSON array");
        return ESP_FAIL;
    }

    int array_size = cJSON_GetArraySize(root);
    if (array_size != WS2812B_COUNT) {
        cJSON_Delete(root);
        char err_msg[64];
        snprintf(err_msg, sizeof(err_msg), "Array size must be %d", WS2812B_COUNT);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err_msg);
        return ESP_FAIL;
    }

    for (int i = 0; i < array_size; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsString(item)) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Array element must be string");
            return ESP_FAIL;
        }
        const char *color_str = item->valuestring;
        if (strlen(color_str) != 7 || color_str[0] != '#') {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid color format, expected #RRGGBB");
            return ESP_FAIL;
        }
        for (int j = 1; j < 7; j++) {
            char c = color_str[j];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
                cJSON_Delete(root);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid hex character in color");
                return ESP_FAIL;
            }
        }

        uint32_t rgb = strtol(color_str + 1, NULL, 16);
        uint8_t r = (rgb >> 16) & 0xFF;
        uint8_t g = (rgb >> 8) & 0xFF;
        uint8_t b = rgb & 0xFF;

        esp_err_t ret = SetWS2812B_RGB(i, r, g, b);
        if (ret != ESP_OK) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set LED");
            return ESP_FAIL;
        }
    }
    led_strip_refresh(led_strip);

    cJSON_Delete(root);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Colors applied successfully");
    return ESP_OK;
}

static esp_err_t handle_api_get(httpd_req_t *req)
{
    IsConnectionHappened = true;
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    for (int i = 0; i < WS2812B_COUNT; i++) {
        uint8_t r = current_colors[i][0];
        uint8_t g = current_colors[i][1];
        uint8_t b = current_colors[i][2];
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, b);
        cJSON *item = cJSON_CreateString(hex);
        if (!item) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
            return ESP_FAIL;
        }
        cJSON_AddItemToArray(root, item);
    }

    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!response) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    free(response);
    return ESP_OK;
}

static esp_err_t handle_api_fill_random(httpd_req_t *req)
{
    IsConnectionHappened = true;
    FillWS2812B_Random();
    led_strip_refresh(led_strip);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Random colors applied successfully");
    return ESP_OK;
}

static esp_err_t handle_api_set_brightness(httpd_req_t *req)
{
    IsConnectionHappened = true;
    int total_len = req->content_len;
    if (total_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    char *buf = (char *)malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    int ret = httpd_req_recv(req, buf, total_len);
    if (ret != total_len) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    cJSON *brightness_obj = cJSON_GetObjectItem(root, "brightness");
    if (!brightness_obj || !cJSON_IsNumber(brightness_obj)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'brightness' field (must be number)");
        return ESP_FAIL;
    }

    int brightness = brightness_obj->valueint;
    if (brightness < 0 || brightness > 255) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Brightness must be between 0 and 255");
        return ESP_FAIL;
    }

    LED_BRIGHTNESS = brightness;

    // Re-apply all current colors with new brightness
    for (int i = 0; i < WS2812B_COUNT; i++) {
        uint8_t r = current_colors[i][0];
        uint8_t g = current_colors[i][1];
        uint8_t b = current_colors[i][2];
        SetWS2812B_RGB(i, r, g, b);
    }
    led_strip_refresh(led_strip);

    cJSON_Delete(root);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Brightness updated successfully");
    return ESP_OK;
}

static esp_err_t handle_root(httpd_req_t *req)
{
    IsConnectionHappened = true;
    const char *html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1\"><title>WS2812B 5x5控制器</title><style>body{font-family:sans-serif;text-align:center;margin:20px}.grid{display:grid;grid-template-columns:repeat(5,60px);gap:10px;justify-content:center;margin:20px auto}.cell{width:60px;height:60px;border-radius:8px;border:2px solid#ccc;cursor:pointer;position:relative}.cell:hover{transform:scale(1.05);border-color:#888}.cell input{position:absolute;top:0;left:0;width:100%;height:100%;opacity:0;cursor:pointer}button{padding:10px 20px;font-size:16px;margin:10px;cursor:pointer}.status{margin-top:20px;color:green;font-weight:bold}.batch-control{display:flex;justify-content:center;gap:20px;margin:20px auto}</style></head><body><h1>WS2812B 5x5灯珠控制</h1><div id=\"grid\"class=\"grid\"></div><div class=\"batch-control\"><div id=\"batchColorCell\"class=\"cell\"style=\"background-color: #000000; display: inline-block; margin: 0; cursor: pointer;\"><input type=\"color\"id=\"batchColor\"value=\"#000000\"></div><button id=\"batchApplyBtn\">全部应用此颜色</button><button id=\"randomBtn\">随机颜色</button></div><div class=\"brightness-control\"><label>亮度:<input type=\"range\"id=\"brightnessSlider\"min=\"0\"max=\"255\"value=\"16\"><input type=\"number\"id=\"brightnessNumber\"min=\"0\"max=\"255\"step=\"1\"value=\"16\"></label><button id=\"setBrightnessBtn\">设置亮度</button></div><button id=\"updateBtn\">更新颜色</button><button id=\"setBtn\">应用颜色</button><div id=\"status\"class=\"status\"></div><script>const NUM_LEDS=25;let colors=new Array(NUM_LEDS).fill('#000000');function createGrid(){const gridDiv=document.getElementById('grid');gridDiv.innerHTML='';for(let i=0;i<NUM_LEDS;i++){const cell=document.createElement('div');cell.className='cell';cell.style.backgroundColor=colors[i];const input=document.createElement('input');input.type='color';input.value=colors[i];input.addEventListener('change',(function(idx,inp){return function(e){const newColor=e.target.value;colors[idx]=newColor;cell.style.backgroundColor=newColor}})(i,input));cell.appendChild(input);gridDiv.appendChild(cell)}}async function fetchColors(){try{const response=await fetch('/api/get');if(!response.ok)throw new Error('获取失败');colors=await response.json();const cells=document.querySelectorAll('.cell');for(let i=0;i<NUM_LEDS;i++){cells[i].style.backgroundColor=colors[i];const input=cells[i].querySelector('input');if(input)input.value=colors[i]}document.getElementById('status').innerText='已从服务器更新颜色';setTimeout(()=>document.getElementById('status').innerText='',2000)}catch(err){document.getElementById('status').innerText='更新失败: '+err.message}}async function sendColors(){try{const response=await fetch('/api/set',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(colors)});const text=await response.text();if(!response.ok)throw new Error(text);document.getElementById('status').innerText=text;setTimeout(()=>document.getElementById('status').innerText='',2000)}catch(err){document.getElementById('status').innerText='设置失败: '+err.message}}function setAllColors(color){const cells=document.querySelectorAll('.cell');for(let i=0;i<NUM_LEDS;i++){colors[i]=color;cells[i].style.backgroundColor=color;const input=cells[i].querySelector('input');if(input)input.value=color}document.getElementById('status').innerText='已批量设置为 '+color;setTimeout(()=>document.getElementById('status').innerText='',2000)}async function fillRandom(){try{const response=await fetch('/api/fill_random',{method:'POST',headers:{'Content-Type':'application/json'}});const text=await response.text();if(!response.ok)throw new Error(text);document.getElementById('status').innerText=text;await fetchColors()}catch(err){document.getElementById('status').innerText='随机填充失败: '+err.message}}async function setBrightness(){const brightness=parseInt(document.getElementById('brightnessSlider').value,10);try{const response=await fetch('/api/set_brightness',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({brightness:brightness})});const text=await response.text();if(!response.ok)throw new Error(text);document.getElementById('status').innerText=text}catch(err){document.getElementById('status').innerText='设置亮度失败: '+err.message}}function initBatchColorPicker(){const colorPickerCell=document.getElementById('batchColorCell');const hiddenInput=document.getElementById('batchColor');colorPickerCell.addEventListener('click',()=>{hiddenInput.click()});hiddenInput.addEventListener('change',(e)=>{const newColor=e.target.value;colorPickerCell.style.backgroundColor=newColor})}const slider=document.getElementById('brightnessSlider');const numberInput=document.getElementById('brightnessNumber');slider.addEventListener('input',function(){numberInput.value=this.value});numberInput.addEventListener('input',function(){let val=parseInt(this.value,10);if(isNaN(val))val=0;val=Math.min(255,Math.max(0,val));slider.value=val;this.value=val});document.getElementById('updateBtn').onclick=fetchColors;document.getElementById('setBtn').onclick=sendColors;document.getElementById('batchApplyBtn').onclick=()=>{const batchColor=document.getElementById('batchColor').value;setAllColors(batchColor)};document.getElementById('randomBtn').onclick=fillRandom;document.getElementById('setBrightnessBtn').onclick=setBrightness;createGrid();initBatchColorPicker();setTimeout(()=>{fetchColors()},500);</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

static void set_right_digit(char c, uint8_t r, uint8_t g, uint8_t b) {
    if (c < '0' || c > '9') return;
    int idx = c - '0';
    for (int row = 0; row < 5; row++) {
        uint8_t bits = digit_patterns[idx][row];
        for (int col = 0; col < 3; col++) {
            if (bits & (1 << (2 - col))) {
                int led = row * 5 + (2 + col);
                SetWS2812B_RGB(led, r, g, b);
            }
        }
    }
}

static void set_left_led(int row, uint8_t r, uint8_t g, uint8_t b) {
    if (row < 0 || row >= 5) return;
    SetWS2812B_RGB(row * 5 + 0, r, g, b);
}

static void display_ip_sequence(const char *ip) {
    char *ip_copy = strdup(ip);
    if (!ip_copy) return;
    char *parts[4];
    int part_cnt = 0;
    char *token = strtok(ip_copy, ".");
    while (token && part_cnt < 4) {
        parts[part_cnt++] = token;
        token = strtok(NULL, ".");
    }

    const uint8_t colors[3][3] = {{255,0,0}, {0,255,0}, {0,0,255}}; // 红、绿、蓝

    for (int p = 0; p < part_cnt; p++) {
        char *part = parts[p];
        int len = strlen(part);
        for (int d = 0; d < len; d++) {
            clear_leds();
            set_left_led(p, colors[d % 3][0], colors[d % 3][1], colors[d % 3][2]);
            set_right_digit(part[d], 127, 127, 127);
            led_strip_refresh(led_strip);
            for (int t = 0; t < 20; t++) {
                if (IsConnectionHappened)
                {
                    clear_leds();
                    led_strip_refresh(led_strip);
                    return;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        clear_leds();
        set_left_led(4, 255, 0, 0);
        led_strip_refresh(led_strip);

        for (int t = 0; t < 20; t++) {
            if (IsConnectionHappened)
            {
                clear_leds();
                led_strip_refresh(led_strip);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        set_left_led(4, 0, 0, 0);
        led_strip_refresh(led_strip);
    }
    clear_leds();
    led_strip_refresh(led_strip);
    free(ip_copy);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    led_strip = init_led();
    init_wifi();
    while (!IsConnectWifi)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif, &ip_info);
    char ip_str[16];
    esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));

    start_webserver();

    while (!IsConnectionHappened) {
        display_ip_sequence(ip_str);
    }

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
