#ifndef WS2812B_CONTROLLER_API_H
#define WS2812B_CONTROLLER_API_H

#include "WS2812B_Controller.h"
#include "WS2812B.h"
#include "Config.h"

static char buffer[4096];
static SemaphoreHandle_t bufferMutex;
static bool isConnected = false;

inline void LockBuffer()
{
    xSemaphoreTake(bufferMutex, portMAX_DELAY);
}

inline void UnlockBuffer()
{
    xSemaphoreGive(bufferMutex);
}

esp_err_t API_Init();
void BeforeAPI();
void AfterAPI();

esp_err_t API_Page_Root(httpd_req_t *req);
esp_err_t API_Page_Config(httpd_req_t *req);
esp_err_t API_Get_Config(httpd_req_t *req);
esp_err_t API_Set_Config(httpd_req_t *req);
esp_err_t API_Get_WS2812B_Single(httpd_req_t *req);
esp_err_t API_Get_WS2812B_Brightness(httpd_req_t *req);
esp_err_t API_Get_WS2812B_All(httpd_req_t *req);
esp_err_t API_Set_WS2812B_Single(httpd_req_t *req);
esp_err_t API_Set_WS2812B_Brightness(httpd_req_t *req);
esp_err_t API_Set_WS2812B_All(httpd_req_t *req);
esp_err_t API_Get_WS2812B_Random(httpd_req_t *req);
esp_err_t API_Get_WS2812B_Position(httpd_req_t *req);
esp_err_t API_Set_WS2812B_Position(httpd_req_t *req);
esp_err_t API_Reboot(httpd_req_t *req);

#endif //WS2812B_CONTROLLER_API_H