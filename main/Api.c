#include "Api.h"

/*
    /                       [GET] -> 根网页
    ""
    ->
    "HTML ..."

    /config                 [GET] -> 配置网页
    ""
    ->
    "HTML ..."

    /api/get_config         [GET] -> 获取配置
    ""
    ->
    {
        "AP_Mode": ? (BOOL),
        "Wifi_SSID": "" (STRING),
        "Wifi_Password": "" (STRING),
        "AP_SSID": "" (STRING),
        "AP_Password": "" (STRING),
        "LED_Pin": ? (INT),
        "LED_Count": ? (INT),
        "DefaultBrightness": ? (INT)
        "Invert_Out": ? (BOOL)
    }

    /api/set_config         [POST] -> 设置配置
    {
        "AP_Mode": ? (BOOL),
        "Wifi_SSID": "" (STRING),
        "Wifi_Password": "" (STRING),
        "AP_SSID": "" (STRING),
        "AP_Password": "" (STRING),
        "LED_Pin": ? (INT),
        "LED_Count": ? (INT),
        "DefaultBrightness": ? (INT)
        "Invert_Out": ? (BOOL)
    }
    ->
    "OK"

    /api/get_single         [GET] -> 获取单个WS2812B状态
    {
        "index": ? (INT)
    }
    ->
    [R, G, B]

    /api/get_brightness     [GET] -> 获取亮度
    ""
    ->
    {
        "Brightness": ? (INT)
    }

    /api/get_all            [GET] -> 获取所有WS2812B状态
    ""
    ->
    {
        "WS2812B": [
            [R, G, B],
            ...
        ],
        "Brightness": ? (INT)
    }

    /api/set_single         [POST] -> 设置单个WS2812B状态
    {
        "index": ? (INT),
        "color": [R, G, B]
    }
    ->
    "OK"

    /api/set_brightness     [POST] -> 设置亮度
    {
        "Brightness": ? (INT)
    }
    ->
    "OK"

    /api/set_all            [POST] -> 设置所有WS2812B状态
    {
        "WS2812B": [
            [R, G, B],
            ...
        ],
        "Brightness": ? (INT)
    }
    ->
    "OK"

    /api/get_led_positions  [GET] -> 获取LED位置
    ""
    ->
    二进制数据 uint8+uint8 存储一个灯

    /api/set_led_positions  [POST] -> 设置LED位置
    二进制数据 uint8+uint8 存储一个灯
    ->
    "OK"

    /api/reboot             [POST] -> 重启
    {
        "AP_Mode": ? (BOOL) [默认为False]
    }
    ->
    "OK"

    /api/random             [POST] -> 随机WS2812B颜色
    ""
    ->
    "OK"
*/

void BeforeAPI()
{
    isConnected = true;
}

void AfterAPI()
{

}

esp_err_t API_Init()
{
    bufferMutex = xSemaphoreCreateMutex();
    if (bufferMutex == NULL)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t API_Page_Root(httpd_req_t *req)
{
    BeforeAPI();
    httpd_resp_set_type(req, "text/html");
    FILE *fp = fopen(LITTLE_FS_MOUNT_POINT "/" ROOT_HTML_FILE, "r");
    if (fp == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not open file");
        AfterAPI();
        return ESP_FAIL;
    }
    size_t n;
    LockBuffer();
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        httpd_resp_send_chunk(req, buffer, n);
    }
    UnlockBuffer();
    fclose(fp);
    httpd_resp_send_chunk(req, NULL, 0);
    AfterAPI();
    return ESP_OK;
}

esp_err_t API_Page_Config(httpd_req_t *req)
{
    BeforeAPI();
    httpd_resp_set_type(req, "text/html");
    FILE *fp = fopen(LITTLE_FS_MOUNT_POINT "/" CONFIG_HTML_FILE, "r");
    if (fp == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not open file");
        AfterAPI();
        return ESP_FAIL;
    }
    size_t n;
    LockBuffer();
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        httpd_resp_send_chunk(req, buffer, n);
    }
    UnlockBuffer();
    fclose(fp);
    httpd_resp_send_chunk(req, NULL, 0);
    AfterAPI();
    return ESP_OK;
}

esp_err_t API_Get_WS2812B_Position(httpd_req_t *req)
{
    BeforeAPI();
    httpd_resp_set_type(req, "application/octet-stream");
    FILE *fp = fopen(LITTLE_FS_MOUNT_POINT "/" LED_POSITION_FILE, "r");
    if (fp == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not open file");
        AfterAPI();
        return ESP_FAIL;
    }
    size_t n;
    LockBuffer();
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        if (httpd_resp_send_chunk(req, buffer, n) != ESP_OK)
        {
            break;
        }
    }
    UnlockBuffer();
    fclose(fp);
    httpd_resp_send_chunk(req, NULL, 0);
    AfterAPI();
    return ESP_OK;
}

esp_err_t API_Set_WS2812B_Position(httpd_req_t *req)
{
    BeforeAPI();
    FILE *fp = fopen(LITTLE_FS_MOUNT_POINT "/" LED_POSITION_FILE, "w");
    if (fp == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not open file");
        AfterAPI();
        return ESP_FAIL;
    }
    size_t total_len = req->content_len;
    size_t remaining = total_len;
    ssize_t recv_len;
    int TimeOutCount = 0;
    LockBuffer();
    while (remaining > 0) {
        size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
        recv_len = httpd_req_recv(req, buffer, to_read);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                if (++TimeOutCount >= API_Max_Retry) {
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive timeout");
                    goto FAIL;
                }
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            goto FAIL;
        }
        TimeOutCount = 0;
        size_t written = fwrite(buffer, 1, recv_len, fp);
        if (written != (size_t)recv_len) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write data to file");
            goto FAIL;
        }
        remaining -= recv_len;
    }
    UnlockBuffer();
    fclose(fp);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    AfterAPI();
    return ESP_OK;
FAIL:
    UnlockBuffer();
    fclose(fp);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "FAIL", 4);
    AfterAPI();
    return ESP_FAIL;
}

esp_err_t API_Get_Config(httpd_req_t *req)
{
    BeforeAPI();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "AP_Mode", config.AP_Mode);
    cJSON_AddStringToObject(root, "Wifi_SSID", config.Wifi_SSID);
    cJSON_AddStringToObject(root, "Wifi_Password", config.Wifi_Password);
    cJSON_AddStringToObject(root, "AP_SSID", config.AP_SSID);
    cJSON_AddStringToObject(root, "AP_Password", config.AP_Password);
    cJSON_AddNumberToObject(root, "LED_Pin", config.LED_Pin);
    cJSON_AddNumberToObject(root, "LED_Count", config.LED_Count);
    cJSON_AddNumberToObject(root, "DefaultBrightness", config.Brightness);
    cJSON_AddNumberToObject(root, "Invert_Out", config.InvertOut);
    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_Delete(root);
    free(json);
    AfterAPI();
    return ESP_OK;
}

esp_err_t API_Set_Config(httpd_req_t *req)
{
    BeforeAPI();
    LockBuffer();
    int ret = httpd_req_recv(req, buffer, sizeof(buffer));
    if (ret <= 0) goto FAIL;
    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) goto FAIL;

    cJSON *AP_Mode = cJSON_GetObjectItem(root, "AP_Mode");
    if (AP_Mode != NULL && cJSON_IsBool(AP_Mode)) config.AP_Mode = AP_Mode->valueint;
    cJSON *Wifi_SSID = cJSON_GetObjectItem(root, "Wifi_SSID");
    if (Wifi_SSID != NULL && cJSON_IsString(Wifi_SSID)) strcpy(config.Wifi_SSID, Wifi_SSID->valuestring);
    cJSON *Wifi_Password = cJSON_GetObjectItem(root, "Wifi_Password");
    if (Wifi_Password != NULL && cJSON_IsString(Wifi_Password)) strcpy(config.Wifi_Password, Wifi_Password->valuestring);
    cJSON *AP_SSID = cJSON_GetObjectItem(root, "AP_SSID");
    if (AP_SSID != NULL && cJSON_IsString(AP_SSID)) strcpy(config.AP_SSID, AP_SSID->valuestring);
    cJSON *AP_Password = cJSON_GetObjectItem(root, "AP_Password");
    if (AP_Password != NULL && cJSON_IsString(AP_Password)) strcpy(config.AP_Password, AP_Password->valuestring);
    cJSON *LED_Pin = cJSON_GetObjectItem(root, "LED_Pin");
    if (LED_Pin != NULL && cJSON_IsNumber(LED_Pin)) config.LED_Pin = (int8_t)LED_Pin->valueint;
    cJSON *LED_Count = cJSON_GetObjectItem(root, "LED_Count");
    if (LED_Count != NULL && cJSON_IsNumber(LED_Count)) config.LED_Count = LED_Count->valueint;
    cJSON *DefaultBrightness = cJSON_GetObjectItem(root, "DefaultBrightness");
    if (DefaultBrightness != NULL && cJSON_IsNumber(DefaultBrightness)) config.Brightness = DefaultBrightness->valueint;
    cJSON *Invert_Out = cJSON_GetObjectItem(root, "Invert_Out");
    if (Invert_Out != NULL && cJSON_IsNumber(Invert_Out)) config.InvertOut = Invert_Out->valueint;
    UnlockBuffer();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    AfterAPI();
    return ESP_OK;
FAIL:
    UnlockBuffer();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "FAIL", 4);
    AfterAPI();
    return ESP_FAIL;
}

esp_err_t API_Get_WS2812B_Single(httpd_req_t *req)
{
    BeforeAPI();
    LockBuffer();
    int req = httpd_req_recv(req, buffer, sizeof(buffer));
    if (req <= 0) goto FAIL;
    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) goto FAIL;
    
    cJSON *index = cJSON_GetObjectItem(root, "index");
    if (index == NULL || cJSON_IsNumber(index)) goto FAIL;
    int Index = index->valueint;
    if (Index < 0 || Index >= config.LED_Count) goto FAIL;
    UnlockBuffer();

    cJSON *root = cJSON_CreateArray();
    cJSON_AddItemToArray(root, cJSON_CreateNumber(current_colors[Index * 3 + 0]));
    cJSON_AddItemToArray(root, cJSON_CreateNumber(current_colors[Index * 3 + 1]));
    cJSON_AddItemToArray(root, cJSON_CreateNumber(current_colors[Index * 3 + 2]));
    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_Delete(root);
    free(json);
    AfterAPI();
    return ESP_OK;
FAIL:
    UnlockBuffer();
    AfterAPI();
    return ESP_FAIL;
}

esp_err_t API_Get_WS2812B_Brightness(httpd_req_t *req)
{
    BeforeAPI();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "Brightness", current_brightness);
    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_Delete(root);
    free(json);
    AfterAPI();
    return ESP_OK;
}

esp_err_t API_Get_WS2812B_All(httpd_req_t *req)
{
    BeforeAPI();
    cJSON *root = cJSON_CreateObject();
    cJSON *WS2812B = cJSON_CreateArray();
    for (int i = 0; i < config.LED_Count; i++) {
        cJSON *item = cJSON_CreateArray();
        cJSON_AddItemToArray(item, cJSON_CreateNumber(current_colors[i * 3 + 0]));
        cJSON_AddItemToArray(item, cJSON_CreateNumber(current_colors[i * 3 + 1]));
        cJSON_AddItemToArray(item, cJSON_CreateNumber(current_colors[i * 3 + 2]));
        cJSON_AddItemToArray(WS2812B, item);
    }
    cJSON_AddItemToObject(root, "WS2812B", WS2812B);
    cJSON_AddNumberToObject(root, "Brightness", current_brightness);
    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_Delete(root);
    free(json);
    AfterAPI();
    return ESP_OK;
}

esp_err_t API_Set_WS2812B_Single(httpd_req_t *req)
{
    BeforeAPI();
    LockBuffer();
    int ret = httpd_req_recv(req, buffer, sizeof(buffer));
    if (ret <= 0) goto FAIL;
    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) goto FAIL;
    cJSON *index = cJSON_GetObjectItem(root, "index");
    if (index == NULL || cJSON_IsNumber(index)) goto FAIL;
    int Index = index->valueint;
    if (Index < 0 || Index >= config.LED_Count) goto FAIL;
    cJSON *color = cJSON_GetObjectItem(root, "color");
    if (color == NULL || !cJSON_IsArray(color)) goto FAIL;
    cJSON *rj, *gj, *bj;
    rj = cJSON_GetArrayItem(color, 0);
    gj = cJSON_GetArrayItem(color, 1);
    bj = cJSON_GetArrayItem(color, 2);
    if (rj == NULL || gj == NULL || bj == NULL) goto FAIL;
    if (!cJSON_IsNumber(rj) || !cJSON_IsNumber(gj) || !cJSON_IsNumber(bj)) goto FAIL;
    setLED_RGB(Index, rj->valueint, gj->valueint, bj->valueint);
    refreshLED();
    UnlockBuffer();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    AfterAPI();
    return ESP_OK;
FAIL:
    UnlockBuffer();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "FAIL", 4);
    AfterAPI();
    return ESP_FAIL;
}

esp_err_t API_Set_WS2812B_Brightness(httpd_req_t *req)
{
    BeforeAPI();
    LockBuffer();
    int ret = httpd_req_recv(req, buffer, sizeof(buffer));
    if (ret <= 0) goto FAIL;
    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) goto FAIL;
    cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
    if (brightness == NULL || !cJSON_IsNumber(brightness)) goto FAIL;
    setBrightness(brightness->valueint);
    reapplyLED();
    UnlockBuffer();
    AfterAPI();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
FAIL:
    UnlockBuffer();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "FAIL", 4);
    AfterAPI();
    return ESP_FAIL;
}

esp_err_t API_Set_WS2812B_All(httpd_req_t *req)
{
    BeforeAPI();
    LockBuffer();
    int ret = httpd_req_recv(req, buffer, sizeof(buffer));
    if (ret <= 0) goto FAIL;
    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) goto FAIL;
    cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
    if (brightness == NULL || !cJSON_IsNumber(brightness)) goto FAIL;
    setBrightness(brightness->valueint);
    cJSON *WS2812B = cJSON_GetObjectItem(root, "WS2812B");
    if (WS2812B == NULL || !cJSON_IsArray(WS2812B)) goto FAIL;
    for (int i = 0; i < config.LED_Count; i++) { 
        cJSON *item = cJSON_GetArrayItem(WS2812B, i);
        cJSON *rj, *gj, *bj;
        rj = cJSON_GetArrayItem(item, 0);
        gj = cJSON_GetArrayItem(item, 1);
        bj = cJSON_GetArrayItem(item, 2);
        if (rj == NULL || gj == NULL || bj == NULL) goto FAIL;
        if (!cJSON_IsNumber(rj) || !cJSON_IsNumber(gj) || !cJSON_IsNumber(bj)) goto FAIL;
        setLED_RGB(i, rj->valueint, gj->valueint, bj->valueint);
    }
    refreshLED();
    UnlockBuffer();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    AfterAPI();
    return ESP_OK;
FAIL:
    UnlockBuffer();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "FAIL", 4);
    AfterAPI();
    return ESP_FAIL;
}

esp_err_t API_Get_WS2812B_Random(httpd_req_t *req)
{
    BeforeAPI();
    for (int i = 0; i < config.LED_Count; i++) { 
        setLED_Random(i);
    }
    refreshLED();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    AfterAPI();
    return ESP_OK;
}

esp_err_t API_Reboot(httpd_req_t *req)
{
    BeforeAPI();
    LockBuffer();
    bool R_AP_Mode = false;
    int ret = httpd_req_recv(req, buffer, sizeof(buffer));
    if (ret <= 0) goto LoadConfigCompleted;
    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) goto LoadConfigCompleted;
    cJSON *AP_Mode = cJSON_GetObjectItem(root, "AP_Mode");
    if (AP_Mode == NULL || !cJSON_IsBool(AP_Mode)) goto LoadConfigCompleted;
    R_AP_Mode = AP_Mode->valueint;
LoadConfigCompleted:
    config.AP_Mode = R_AP_Mode;
    saveConfig();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    AfterAPI();

    // 3秒后重启
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    esp_restart();
    return ESP_OK;
}