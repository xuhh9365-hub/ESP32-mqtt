#ifndef __WIFI_STA_H_
#define __WIFI_STA_H_

#include "esp_err.h"

/**
 * @brief       初始化WiFi Station模式并连接到指定AP
 * @note        该函数会阻塞直到WiFi连接成功获取IP地址或达到最大重试次数
 * @param       无
 * @retval      ESP_OK: 连接成功
 *              ESP_FAIL: 连接失败（超过最大重试次数）
 */
esp_err_t wifi_sta_init(void);

#endif /* __WIFI_STA_H_ */
