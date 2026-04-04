#include <stdio.h>

#include "WS2812B_Controller.h"

#include "WS2812B.c"
#include "Config.c"
#include "Api.c"

esp_err_t FileSystem_Init()
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = LITTLE_FS_MOUNT_POINT,
        .partition_label = LITTLE_FS_PARTITION_NAME,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        return ret;
    }
    return ESP_OK;
}

void app_main(void)
{
}