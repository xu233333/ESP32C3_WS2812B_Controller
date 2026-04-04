#include <stdio.h>

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

#define WS2812B_ROW_DEFAULT 5  // WS2812B的行数
#define WS2812B_COLUMN_DEFAULT 5  // WS2812B的列数
#define WS2812B_DEFAULT_BRIGHTNESS 16  // 默认亮度
#define WS2812B_MEMORY_BLOCK_WORDS 0  // ESP32C3 好像没有RMT DMA
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

#define WIFI_SSID_LENGTH 32
#define WIFI_PASSWORD_LENGTH 64

#define Device_AP_SSID "WS2812B_Controller"
#define Device_AP_PASSWORD "12345678"
#define Device_AP_CHANNEL 1

#define RESET_GPIO      GPIO_NUM_9
#define LONG_PRESS_MS   3000
#define DEBOUNCE_MS     50

struct Config
{
    uint8_t IsInit;
    char WIFI_SSID[WIFI_SSID_LENGTH];  // Wifi SSID
    char WIFI_PASSWORD[WIFI_PASSWORD_LENGTH];  // Wifi 密码
    char WS2812B_ROW;
    char WS2812B_COLUMN;
    int8_t WS2812B_LED_PIN;  // WS2812B的引脚
    uint8_t DEFAULT_BRIGHTNESS;  // 默认亮度
    char InvertData;
};

static struct Config config = {
    .IsInit = 0,
    .WIFI_SSID = "",
    .WIFI_PASSWORD = "",
    .WS2812B_ROW = WS2812B_ROW_DEFAULT,
    .WS2812B_COLUMN = WS2812B_COLUMN_DEFAULT,
    .WS2812B_LED_PIN = -1,
    .DEFAULT_BRIGHTNESS = WS2812B_DEFAULT_BRIGHTNESS,
    .InvertData = 0
};
static led_strip_handle_t led_strip;
static uint8_t* current_colors;
static uint8_t current_brightness = 0;
static uint16_t current_led_count = 0;
static uint8_t current_led_row = 0;
static uint8_t current_led_column = 0;
static const char *TAG = "WS2812B_Server";
static httpd_handle_t server = NULL;
static bool IsConnectWifi = false;
static bool IsConnectionHappened = false;
static bool EnableResetButton = true;
static char buf[4096];

const char* ROOT_PAGE = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1\"><title>WS2812B矩阵控制器</title><style>body{font-family:sans-serif;text-align:center;margin:20px}.grid{display:grid;gap:10px;justify-content:center;margin:20px auto}.cell{width:60px;height:60px;border-radius:8px;border:2px solid#ccc;cursor:pointer;position:relative}.cell:hover{transform:scale(1.05);border-color:#888}.cell input{position:absolute;top:0;left:0;width:100%;height:100%;opacity:0;cursor:pointer}button{padding:10px 20px;font-size:16px;margin:10px;cursor:pointer}.status{margin-top:20px;color:green;font-weight:bold}.batch-control{display:flex;justify-content:center;gap:20px;margin:20px auto}</style></head><body><h1>WS2812B矩阵控制器</h1><div id=\"grid\"class=\"grid\"></div><div class=\"brightness-control\"><label>亮度:<input type=\"range\"id=\"brightnessSlider\"min=\"0\"max=\"255\"value=\"16\"><input type=\"number\"id=\"brightnessNumber\"min=\"0\"max=\"255\"step=\"1\"value=\"16\"></label></div><div class=\"batch-control\"><div id=\"batchColorCell\"class=\"cell\"style=\"background-color: #000000; display: inline-block; margin: 0; cursor: pointer;\"><input type=\"color\"id=\"batchColor\"value=\"#000000\"></div><button id=\"batchApplyBtn\">全部应用此颜色</button><button id=\"randomBtn\">随机颜色</button></div><button id=\"updateBtn\">更新颜色</button><button id=\"setBtn\">应用颜色</button><div class=\"reboot-control\"><button id=\"rebootNormalBtn\">重启设备（正常模式）</button><button id=\"rebootAPBtn\">重启进入配网模式</button></div><script>let rows=5,cols=5;let totalLeds=rows*cols;let colors=[];let currentBrightness=16;let gridInitialized=false;function rgbToHex(rgb){return'#'+((1<<24)+(rgb[0]<<16)+(rgb[1]<<8)+rgb[2]).toString(16).slice(1)}function hexToRgb(hex){let r=parseInt(hex.slice(1,3),16);let g=parseInt(hex.slice(3,5),16);let b=parseInt(hex.slice(5,7),16);return[r,g,b]}function createGrid(){const gridDiv=document.getElementById('grid');gridDiv.style.gridTemplateColumns=`repeat(${cols},60px)`;gridDiv.innerHTML='';if(colors.length!==totalLeds){colors=new Array(totalLeds).fill('#000000')}for(let i=0;i<totalLeds;i++){const cell=document.createElement('div');cell.className='cell';cell.style.backgroundColor=colors[i];const input=document.createElement('input');input.type='color';input.value=colors[i];input.addEventListener('change',(function(idx,inp){return function(e){const newColor=e.target.value;colors[idx]=newColor;cell.style.backgroundColor=newColor}})(i,input));cell.appendChild(input);gridDiv.appendChild(cell)}}function updateGridColors(){const cells=document.querySelectorAll('.cell');for(let i=0;i<totalLeds&&i<cells.length;i++){cells[i].style.backgroundColor=colors[i];const input=cells[i].querySelector('input');if(input)input.value=colors[i]}}async function fetchColors(){try{const response=await fetch('/api/get');if(!response.ok)throw new Error('获取失败');const data=await response.json();if(!gridInitialized&&data.WS2812B_ROW&&data.WS2812B_COL){rows=data.WS2812B_ROW;cols=data.WS2812B_COL;totalLeds=rows*cols;gridInitialized=true;createGrid()}if(data.WS2812B&&Array.isArray(data.WS2812B)){const rgbArray=data.WS2812B;if(rgbArray.length===totalLeds){for(let i=0;i<totalLeds;i++){const rgb=rgbArray[i];if(rgb&&rgb.length===3){colors[i]=rgbToHex(rgb)}}updateGridColors()}else{console.warn(`颜色数组长度${rgbArray.length}与预期${totalLeds}不符`)}}if(typeof data.WS2812B_Brightness==='number'){currentBrightness=data.WS2812B_Brightness;document.getElementById('brightnessSlider').value=currentBrightness;document.getElementById('brightnessNumber').value=currentBrightness}}catch(err){console.error('获取颜色失败:',err)}}async function sendColors(){const rgbArray=[];for(let i=0;i<totalLeds;i++){rgbArray.push(hexToRgb(colors[i]))}currentBrightness=parseInt(document.getElementById('brightnessSlider').value,10);const payload={WS2812B:rgbArray,WS2812B_Brightness:currentBrightness};try{const response=await fetch('/api/set',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!response.ok)throw new Error('设置失败');const text=await response.text();console.log(text)}catch(err){console.error('设置颜色失败:',err)}}function setAllColors(color){for(let i=0;i<totalLeds;i++){colors[i]=color}updateGridColors()}async function fillRandom(){try{const response=await fetch('/api/random',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({})});if(!response.ok)throw new Error('随机填充失败');await fetchColors()}catch(err){console.error('随机填充失败:',err)}}async function reboot(apMode){try{const response=await fetch('/api/reboot',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({AP_MODE:apMode})});if(!response.ok)throw new Error('连接失败');alert('设备正在重启...')}catch(err){console.error('重启请求失败:',err);alert('重启失败: '+err.message)}}function initBatchColorPicker(){const colorPickerCell=document.getElementById('batchColorCell');const hiddenInput=document.getElementById('batchColor');colorPickerCell.addEventListener('click',()=>{hiddenInput.click()});hiddenInput.addEventListener('change',(e)=>{const newColor=e.target.value;colorPickerCell.style.backgroundColor=newColor})}const slider=document.getElementById('brightnessSlider');const numberInput=document.getElementById('brightnessNumber');slider.addEventListener('input',function(){numberInput.value=this.value;currentBrightness=parseInt(this.value,10)});numberInput.addEventListener('input',function(){let val=parseInt(this.value,10);if(isNaN(val))val=0;val=Math.min(255,Math.max(0,val));slider.value=val;this.value=val;currentBrightness=val});document.getElementById('batchApplyBtn').onclick=()=>{const batchColor=document.getElementById('batchColor').value;setAllColors(batchColor)};document.getElementById('randomBtn').onclick=fillRandom;document.getElementById('updateBtn').onclick=fetchColors;document.getElementById('setBtn').onclick=sendColors;document.getElementById('rebootNormalBtn').onclick=()=>reboot(0);document.getElementById('rebootAPBtn').onclick=()=>reboot(1);createGrid();initBatchColorPicker();fetchColors();setTimeout(()=>{fetchColors()},500);</script></body></html>";
const char* CONFIG_PAGE = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1\"><title>设备配置</title><style>body{font-family:sans-serif;max-width:400px;margin:30px auto;padding:20px;background:#f0f0f0}.container{background:white;padding:20px;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.1)}.form-group{margin-bottom:15px}label{display:block;margin-bottom:5px;font-weight:bold}input{width:100%;padding:8px;border:1px solid#ccc;border-radius:4px;box-sizing:border-box}.button-group{display:flex;gap:10px;margin-top:10px}button{flex:1;padding:10px;background:#28a745;color:white;border:none;border-radius:4px;font-size:16px;cursor:pointer}button:hover{background:#218838}.read-btn{background:#17a2b8}.read-btn:hover{background:#138496}.status{margin-top:15px;text-align:center;font-size:14px}.success{color:green}.error{color:red}hr{margin:20px 0}.reboot{background:#ffc107;color:#333}.reboot:hover{background:#e0a800}</style></head><body><div class=\"container\"><h2>设备配置</h2><div class=\"form-group\"><label>WiFi SSID</label><input type=\"text\"id=\"ssid\"placeholder=\"请输入WiFi名称\"></div><div class=\"form-group\"><label>WiFi密码</label><input type=\"text\"id=\"password\"placeholder=\"请输入WiFi密码\"></div><div class=\"form-group\"><label>LED行数</label><input type=\"number\"id=\"led_row\"placeholder=\"1~255\"min=\"1\"max=\"255\"></div><div class=\"form-group\"><label>LED列数</label><input type=\"number\"id=\"led_col\"placeholder=\"1~255\"min=\"1\"max=\"255\"></div><div class=\"form-group\"><label>LED引脚(GPIO)</label><input type=\"number\"id=\"led_pin\"placeholder=\"0~11\"min=\"0\"max=\"11\"></div><div class=\"form-group\"><label>默认亮度(0~255)</label><input type=\"number\"id=\"brightness\"placeholder=\"0~255\"min=\"0\"max=\"255\"></div><div class=\"form-group\"><label>信号反相(0~1)</label><input type=\"number\"id=\"invert_data\"placeholder=\"0~1\"min=\"0\"max=\"1\"></div><div class=\"button-group\"><button id=\"readBtn\"class=\"read-btn\">读取配置</button><button id=\"saveBtn\">保存配置</button></div><hr><div class=\"button-group\"><button id=\"rebootNormalBtn\"class=\"reboot\">重启设备(正常模式)</button><button id=\"rebootAPBtn\"class=\"reboot\"style=\"margin-top:5px;\">重启进入配网模式</button></div><div id=\"status\"class=\"status\"></div></div><script>async function loadConfig(){try{const response=await fetch('/api/get_config');if(!response.ok)throw new Error('获取配置失败');const data=await response.json();document.getElementById('ssid').value=data.ssid||'';document.getElementById('password').value=data.password||'';document.getElementById('led_row').value=data.led_row!==undefined?data.led_row:'';document.getElementById('led_col').value=data.led_col!==undefined?data.led_col:'';document.getElementById('led_pin').value=data.led_pin!==undefined?data.led_pin:'';document.getElementById('brightness').value=data.brightness!==undefined?data.brightness:'';document.getElementById('invert_data').value=data.invert_data!==undefined?data.invert_data:'';showStatus('配置已读取',true)}catch(err){showStatus('获取配置失败: '+err.message,false)}}async function saveConfig(){const payload={ssid:document.getElementById('ssid').value,password:document.getElementById('password').value,led_row:parseInt(document.getElementById('led_row').value,10),led_col:parseInt(document.getElementById('led_col').value,10),led_pin:parseInt(document.getElementById('led_pin').value,10),brightness:parseInt(document.getElementById('brightness').value,10),invert_data:parseInt(document.getElementById('invert_data').value,10)};if(isNaN(payload.led_row))payload.led_row=1;if(isNaN(payload.led_col))payload.led_col=1;if(isNaN(payload.led_pin))payload.led_pin=-1;if(isNaN(payload.brightness))payload.brightness=16;try{const response=await fetch('/api/set_config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!response.ok)throw new Error('保存失败');const text=await response.text();showStatus('配置已保存',true)}catch(err){showStatus('保存失败: '+err.message,false)}}async function reboot(apMode){try{const response=await fetch('/api/reboot',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({AP_MODE:apMode})});if(!response.ok)throw new Error('重启失败');showStatus('设备正在重启...',true);setTimeout(()=>{},2000)}catch(err){showStatus('重启失败: '+err.message,false)}}function showStatus(msg,isSuccess){const statusDiv=document.getElementById('status');statusDiv.textContent=msg;statusDiv.className='status '+(isSuccess?'success':'error');setTimeout(()=>{statusDiv.textContent='';statusDiv.className='status'},3000)}document.getElementById('saveBtn').onclick=saveConfig;document.getElementById('readBtn').onclick=loadConfig;document.getElementById('rebootNormalBtn').onclick=()=>reboot(0);document.getElementById('rebootAPBtn').onclick=()=>reboot(1);loadConfig();</script></body></html>";

esp_err_t save_config()
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("app_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    nvs_set_u8(nvs_handle, "IsInit", config.IsInit);
    nvs_set_str(nvs_handle, "WIFI_SSID", config.WIFI_SSID);
    nvs_set_str(nvs_handle, "WIFI_PASSWORD", config.WIFI_PASSWORD);
    nvs_set_u8(nvs_handle, "WS2812B_ROW", config.WS2812B_ROW);
    nvs_set_u8(nvs_handle, "WS2812B_COL", config.WS2812B_COLUMN);
    nvs_set_i8(nvs_handle, "WS2812B_LED_PIN", config.WS2812B_LED_PIN);
    nvs_set_u8(nvs_handle, "BRIGHTNESS", config.DEFAULT_BRIGHTNESS);
    nvs_set_u8(nvs_handle, "InvertData", config.InvertData);
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}

esp_err_t load_config()
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("app_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t u8_temp = 0;
    int8_t i8_temp = 0;
    if (nvs_get_u8(nvs_handle, "IsInit", &u8_temp) == ESP_OK) {
        config.IsInit = u8_temp;
    }
    size_t ssid_len = sizeof(config.WIFI_SSID);
    nvs_get_str(nvs_handle, "WIFI_SSID", config.WIFI_SSID, &ssid_len);
    size_t password_len = sizeof(config.WIFI_PASSWORD);
    nvs_get_str(nvs_handle, "WIFI_PASSWORD", config.WIFI_PASSWORD, &password_len);
    if (nvs_get_u8(nvs_handle, "WS2812B_ROW", &u8_temp) == ESP_OK)
    {
        config.WS2812B_ROW = u8_temp;
    }
    if (nvs_get_u8(nvs_handle, "WS2812B_COL", &u8_temp) == ESP_OK)
    {
        config.WS2812B_COLUMN = u8_temp;
    }
    if (nvs_get_i8(nvs_handle, "WS2812B_LED_PIN", &i8_temp) == ESP_OK)
    {
        config.WS2812B_LED_PIN = i8_temp;
    }
    if (nvs_get_u8(nvs_handle, "BRIGHTNESS", &u8_temp) == ESP_OK)
    {
        config.DEFAULT_BRIGHTNESS = u8_temp;
    }
    if (nvs_get_u8(nvs_handle, "InvertData", &u8_temp) == ESP_OK)
    {
        config.InvertData = u8_temp;
    }
    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t reset_config()
{
    config.IsInit = 0;
    return save_config();
}

// 设置WS2812B的颜色 会应用配置中的亮度 需要手动刷新
// r->(0~255) g -> (0~255) b -> (0~255)
static esp_err_t SetWS2812B_RGB(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= current_led_count) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t true_bright = (uint16_t)current_brightness + 1;
    uint8_t r_r, r_g, r_b;
    r_r = (r * true_bright) >> 8;
    r_g = (g * true_bright) >> 8;
    r_b = (b * true_bright) >> 8;
    esp_err_t ret = led_strip_set_pixel(led_strip, index, r_r, r_g, r_b);

    // if (!(current_colors[index][0] == r && current_colors[index][1] == g && current_colors[index][2] == b))
    // {
    //     ESP_LOGI(TAG, "SetWS2812B_RGB: %ld %d-%d, %d-%d, %d-%d", index, r, r_r, g, r_g, b, r_b);
    // }

    if (ret == ESP_OK)
    {
        current_colors[index * 3 + 0] = r;
        current_colors[index * 3 + 1] = g;
        current_colors[index * 3 + 2] = b;
    }

    return ret;
}

// h -> (0~359) s -> (0~255) v -> (0~255)
static esp_err_t SetWS2812B_HSV(uint32_t index, uint16_t h, uint8_t s, uint8_t v)
{
    if (index >= current_led_count) {
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

// 随机填充所有WS2812B的颜色 ReScaleColor == true 会根据亮度设置
static esp_err_t FillWS2812B_Random(bool ReScaleColor)
{
    for (int i = 0; i < current_led_count; i++)
    {
        uint16_t h = esp_random() % 360;
        SetWS2812B_HSV(i, h, 255, 255);
        if (ReScaleColor)
        {
            if (current_brightness == 0)
            {
                current_colors[i * 3 + 0] = 0;
                current_colors[i * 3 + 1] = 0;
                current_colors[i * 3 + 2] = 0;
            }
            else
            {
                uint16_t true_bright = (uint16_t)current_brightness + 1;  // 范围 1~256
                float scale = 255.0f / (float)current_brightness;
                for (int j = 0; j < 3; j++)
                {
                    uint8_t orig = (current_colors[i * 3 + j] * true_bright) >> 8;
                    uint8_t scaled = (uint8_t)((float)orig * scale);
                    current_colors[i * 3 + j] = scaled;
                }
            }
        }
    }
    return ESP_OK;
}

static void FillWS2812B_RGB(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < current_led_count; i++) {
        SetWS2812B_RGB(i, r, g, b);
    }
}

static void RefreshWS2812B()
{
    led_strip_refresh(led_strip);
}

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

static void set_right_digit(char c, uint8_t r, uint8_t g, uint8_t b) {
    if (c < '0' || c > '9') return;
    int idx = c - '0';
    for (int row = 0; row < 5; row++) {
        uint8_t bits = digit_patterns[idx][row];
        for (int col = 0; col < 3; col++) {
            if (bits & (1 << (2 - col))) {
                int led = row * current_led_column + (2 + col);
                SetWS2812B_RGB(led, r, g, b);
            }
        }
    }
}

static void set_left_led(int row, uint8_t r, uint8_t g, uint8_t b) {
    if (row < 0 || row >= 5) return;
    SetWS2812B_RGB(row * current_led_column + 0, r, g, b);
}

static void display_ip_sequence(const char *ip) {
    if (current_led_row < 5 || current_led_column < 5) return;
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
            FillWS2812B_RGB(0, 0, 0);
            set_left_led(p, colors[d % 3][0], colors[d % 3][1], colors[d % 3][2]);
            set_right_digit(part[d], 255, 255, 255);
            RefreshWS2812B();
            for (int t = 0; t < 20; t++) {
                if (IsConnectionHappened)
                {
                    FillWS2812B_RGB(0, 0, 0);
                    RefreshWS2812B();
                    return;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        FillWS2812B_RGB(0, 0, 0);
        set_left_led(4, 255, 0, 0);
        RefreshWS2812B();

        for (int t = 0; t < 20; t++) {
            if (IsConnectionHappened)
            {
                FillWS2812B_RGB(0, 0, 0);
                RefreshWS2812B();
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        set_left_led(4, 0, 0, 0);
        RefreshWS2812B();
    }
    FillWS2812B_RGB(0, 0, 0);
    RefreshWS2812B();
    free(ip_copy);
}

/*
  /api/get_config(GET)
  {
    "ssid": "",
    "password": "",
    "led_row": ?,
    "led_col": ?,
    "led_pin": ?,
    "brightness": ? (0~255)
    "invert_data: ? (0/1)
  }

  /api/set_config(POST)
  {
    "ssid": "",
    "password": "",
    "led_row": ?,
    "led_col": ?,
    "led_pin": ?,
    "brightness": ? (0~255)
    "invert_data: ? (0/1)
  }
*/


esp_err_t APSTA_GET_CONFIG(httpd_req_t *req)
{
    IsConnectionHappened = true;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", config.WIFI_SSID);
    cJSON_AddStringToObject(root, "password", config.WIFI_PASSWORD);
    cJSON_AddNumberToObject(root, "led_row", config.WS2812B_ROW);
    cJSON_AddNumberToObject(root, "led_col", config.WS2812B_COLUMN);
    cJSON_AddNumberToObject(root, "led_pin", config.WS2812B_LED_PIN);
    cJSON_AddNumberToObject(root, "brightness", config.DEFAULT_BRIGHTNESS);
    cJSON_AddNumberToObject(root, "invert_data", config.InvertData);
    char *response = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    cJSON_Delete(root);
    free(response);
    return ESP_OK;
}

esp_err_t APSTA_SET_CONFIG(httpd_req_t *req)
{
    IsConnectionHappened = true;
    memset(buf, 0, sizeof(buf));
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) return ESP_FAIL;

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "password");
    cJSON *led_row = cJSON_GetObjectItem(root, "led_row");
    cJSON *led_col = cJSON_GetObjectItem(root, "led_col");
    cJSON *led_pin = cJSON_GetObjectItem(root, "led_pin");
    cJSON *bright = cJSON_GetObjectItem(root, "brightness");
    cJSON *invert_data = cJSON_GetObjectItem(root, "invert_data");

    if (ssid && cJSON_IsString(ssid)) {
        strlcpy(config.WIFI_SSID, ssid->valuestring, sizeof(config.WIFI_SSID));
    }
    if (pass && cJSON_IsString(pass)) {
        strlcpy(config.WIFI_PASSWORD, pass->valuestring, sizeof(config.WIFI_PASSWORD));
    }
    if (led_row && cJSON_IsNumber(led_row))
    {
        config.WS2812B_ROW = (char)led_row->valueint;
    }
    if (led_col && cJSON_IsNumber(led_col))
    {
        config.WS2812B_COLUMN = (char)led_col->valueint;
    }
    if (led_pin && cJSON_IsNumber(led_pin)) {
        config.WS2812B_LED_PIN = (int8_t)led_pin->valueint;
    }
    if (bright && cJSON_IsNumber(bright)) {
        config.DEFAULT_BRIGHTNESS = (char)bright->valueint;
    }
    if (invert_data && cJSON_IsNumber(invert_data))
    {
        config.InvertData = (char)invert_data->valueint;
    }
    save_config();  // 必须确保 save_config 会保存 IsInit 和其他所有字段

    cJSON_Delete(root);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/*
 /api/reboot(POST):
 {
    "AP_MODE": 0 / 1 (默认为0)
 }
*/
esp_err_t APSTA_REBOOT(httpd_req_t *req)
{
    IsConnectionHappened = true;
    memset(buf, 0, sizeof(buf));
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    // 解析 JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // 获取 AP_MODE 字段，默认为 0
    int ap_mode = 0;
    cJSON *mode = cJSON_GetObjectItem(root, "AP_MODE");
    if (mode && cJSON_IsNumber(mode)) {
        ap_mode = mode->valueint;
    }
    cJSON_Delete(root);

    // 根据参数决定重启后的行为
    if (ap_mode == 1) {
        // 设置为进入 AP 模式：清除 IsInit 标志
        config.IsInit = 0;
        save_config();   // 持久化，确保重启后 IsInit == 0
        ESP_LOGI(TAG, "Will restart in AP mode (IsInit cleared)");
    } else {
        // 正常重启，保持原有配置
        ESP_LOGI(TAG, "Will restart normally");
    }

    // 发送响应
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();

    return ESP_OK;  // 不会执行到这里
}

/*
 /api/get(GET):
 {
    "WS2812B_ROW": ?
    "WS2812B_COL": ?
    "WS2812B": [
        [r, g, b],
        ***
    ]
    "WS2812B_Brightness": ?
 }

 /api/set(POST):
 {
    "WS2812B": [
        [r, g, b],
        ***
    ]
    "WS2812B_Brightness": ?
 }

 /api/random(POST):
 {
 }
*/

esp_err_t STA_GET_WS2812B(httpd_req_t *req)
{
    IsConnectionHappened = true;
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON create failed");
        return ESP_FAIL;
    }

    cJSON_AddNumberToObject(root, "WS2812B_ROW", current_led_row);
    cJSON_AddNumberToObject(root, "WS2812B_COL", current_led_column);
    cJSON_AddNumberToObject(root, "WS2812B_Brightness", current_brightness);

    cJSON *colors_array = cJSON_CreateArray();
    for (int i = 0; i < current_led_count; i++) {
        cJSON *rgb = cJSON_CreateArray();
        cJSON_AddItemToArray(rgb, cJSON_CreateNumber(current_colors[i * 3 + 0]));
        cJSON_AddItemToArray(rgb, cJSON_CreateNumber(current_colors[i * 3 + 1]));
        cJSON_AddItemToArray(rgb, cJSON_CreateNumber(current_colors[i * 3 + 2]));
        cJSON_AddItemToArray(colors_array, rgb);
    }
    cJSON_AddItemToObject(root, "WS2812B", colors_array);

    char *response = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    cJSON_Delete(root);
    free(response);
    return ESP_OK;
}

esp_err_t STA_SET_WS2812B(httpd_req_t *req)
{
    IsConnectionHappened = true;
    memset(buf, 0, sizeof(buf));
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *brightness = cJSON_GetObjectItem(root, "WS2812B_Brightness");
    if (brightness && cJSON_IsNumber(brightness)) {
        current_brightness = (uint8_t)brightness->valueint;
    }

    cJSON *colors = cJSON_GetObjectItem(root, "WS2812B");
    if (colors && cJSON_IsArray(colors)) {
        int array_size = cJSON_GetArraySize(colors);
        if (array_size != current_led_count) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Color array size mismatch");
            return ESP_FAIL;
        }

        for (int i = 0; i < array_size; i++) {
            cJSON *rgb = cJSON_GetArrayItem(colors, i);
            if (!rgb || !cJSON_IsArray(rgb) || cJSON_GetArraySize(rgb) != 3) {
                cJSON_Delete(root);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid color format");
                return ESP_FAIL;
            }

            cJSON *r_item = cJSON_GetArrayItem(rgb, 0);
            cJSON *g_item = cJSON_GetArrayItem(rgb, 1);
            cJSON *b_item = cJSON_GetArrayItem(rgb, 2);
            if (!r_item || !g_item || !b_item || !cJSON_IsNumber(r_item) || !cJSON_IsNumber(g_item) || !cJSON_IsNumber(b_item)) {
                cJSON_Delete(root);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid color value");
                return ESP_FAIL;
            }

            uint8_t r = (uint8_t)r_item->valueint;
            uint8_t g = (uint8_t)g_item->valueint;
            uint8_t b = (uint8_t)b_item->valueint;

            SetWS2812B_RGB(i, r, g, b);
        }
    }

    cJSON_Delete(root);

    RefreshWS2812B();

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t STA_Random_WS2812B(httpd_req_t *req)
{
    IsConnectionHappened = true;
    memset(buf, 0, sizeof(buf));
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    FillWS2812B_Random(true);
    RefreshWS2812B();
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t APSTA_CONFIG(httpd_req_t *req)
{
    IsConnectionHappened = true;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, CONFIG_PAGE, strlen(CONFIG_PAGE));
    return ESP_OK;
}

static esp_err_t STA_ROOT(httpd_req_t *req)
{
    IsConnectionHappened = true;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ROOT_PAGE, strlen(ROOT_PAGE));
    return ESP_OK;
}

static void AP_WEH(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

// 当IsInit == 0时启动 用于网络修改配置
esp_err_t AP_MODE()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &AP_WEH, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = Device_AP_SSID,
            .ssid_len = strlen(Device_AP_SSID),
            .channel = Device_AP_CHANNEL,
            .password = Device_AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(Device_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Debug AP Open. SSID:%s Password:%s Channel:%d",
             Device_AP_SSID, Device_AP_PASSWORD, Device_AP_CHANNEL);

    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.lru_purge_enable = true;  // 允许自动清理

    if (httpd_start(&server, &httpd_config) == ESP_OK) {
        httpd_uri_t uri_get_config = {
            .uri       = "/api/get_config",
            .method    = HTTP_GET,
            .handler   = APSTA_GET_CONFIG,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_get_config);
        httpd_uri_t uri_set_config = {
            .uri       = "/api/set_config",
            .method    = HTTP_POST,
            .handler   = APSTA_SET_CONFIG,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_set_config);
        httpd_uri_t uri_reboot = {
            .uri       = "/api/reboot",
            .method    = HTTP_POST,
            .handler   = APSTA_REBOOT,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_reboot);
        httpd_uri_t uri_config = {
            .uri       = "/config",
            .method    = HTTP_GET,
            .handler   = APSTA_CONFIG,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_config);
        httpd_uri_t uri_root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = APSTA_CONFIG,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_root);
    }

    config.IsInit = 1;
    save_config();
    return ESP_OK;
}

static void STA_WEH(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
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

esp_err_t STA_MODE()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &STA_WEH,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &STA_WEH,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *)wifi_config.sta.ssid, config.WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, config.WIFI_PASSWORD, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    while (!IsConnectWifi)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.lru_purge_enable = true;  // 允许自动清理

    if (httpd_start(&server, &httpd_config) == ESP_OK) {
        httpd_uri_t uri_get = {
            .uri       = "/api/get",
            .method    = HTTP_GET,
            .handler   = STA_GET_WS2812B,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_get);
        httpd_uri_t uri_set = {
            .uri       = "/api/set",
            .method    = HTTP_POST,
            .handler   = STA_SET_WS2812B,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_set);
        httpd_uri_t uri_random = {
            .uri       = "/api/random",
            .method    = HTTP_POST,
            .handler   = STA_Random_WS2812B,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_random);
        httpd_uri_t uri_get_config = {
            .uri       = "/api/get_config",
            .method    = HTTP_GET,
            .handler   = APSTA_GET_CONFIG,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_get_config);
        httpd_uri_t uri_set_config = {
            .uri       = "/api/set_config",
            .method    = HTTP_POST,
            .handler   = APSTA_SET_CONFIG,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_set_config);
        httpd_uri_t uri_reboot = {
            .uri       = "/api/reboot",
            .method    = HTTP_POST,
            .handler   = APSTA_REBOOT,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_reboot);
        httpd_uri_t uri_config = {
            .uri       = "/config",
            .method    = HTTP_GET,
            .handler   = APSTA_CONFIG,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_config);
        httpd_uri_t uri_root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = STA_ROOT,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_root);
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif, &ip_info);
    char ip_str[16];
    esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));

    while (!IsConnectionHappened) {
        display_ip_sequence(ip_str);
    }
    return ESP_OK;
}

led_strip_handle_t init_led(void)
{
    int8_t gpio_pin = config.WS2812B_LED_PIN;

    // 仅允许 0 ~ 11 的 GPIO
    if (gpio_pin < 0 || gpio_pin > 11) {
        ESP_LOGE("LED", "Invalid LED pin: %d (must be 0~11)", gpio_pin);
        return NULL;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_pin,
        .max_leds = current_led_count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = config.InvertData,
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
    led_strip_handle_t led_strip;
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    return led_strip;
}

void check_reset_button(void) {
    static uint32_t press_start_ms = 0;
    static bool pressed = false;

    int level = gpio_get_level(RESET_GPIO);

    if (level == 0) {
        if (!pressed) {
            press_start_ms = esp_timer_get_time() / 1000;
            pressed = true;
        } else {
            uint32_t now = esp_timer_get_time() / 1000;
            if (now - press_start_ms >= LONG_PRESS_MS) {
                ESP_LOGI(TAG, "Long press detected, clearing IsInit flag...");
                reset_config();
                while (gpio_get_level(RESET_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                pressed = false;
                press_start_ms = 0;
            }
        }
    } else {
        pressed = false;
        press_start_ms = 0;
    }
}

void button_task(void *pvParameter) {
    while (1) {
        check_reset_button();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void init_reset_button(void) {
    if (config.WS2812B_LED_PIN == RESET_GPIO)
    {
        ESP_LOGW(TAG, "LED pin conflicts with reset button GPIO%d, button disabled", RESET_GPIO);
        EnableResetButton = false;
        return;
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RESET_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    load_config();
    init_reset_button();

    current_led_row = config.WS2812B_ROW;
    current_led_column = config.WS2812B_COLUMN;
    current_led_count = current_led_row * current_led_column;
    current_colors = (uint8_t*)malloc(3 * current_led_count);
    memset(current_colors, 0, 3 * current_led_count);

    if (EnableResetButton)
    {
        xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
    }

    led_strip = init_led();
    current_brightness = config.DEFAULT_BRIGHTNESS;
    if (!config.IsInit || led_strip == NULL)
    {
        AP_MODE();
    }
    else
    {
        STA_MODE();
    }
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}