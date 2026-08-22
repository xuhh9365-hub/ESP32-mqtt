#ifndef __WEB_SERVER_H__
#define __WEB_SERVER_H__

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化并启动 HTTP Web Server 基础框架 (监听端口 80，注册静态页面与 REST API)
 * 
 * @return esp_err_t ESP_OK 表示启动成功
 */
esp_err_t web_server_init(void);

/**
 * @brief 停止 Web Server
 */
esp_err_t web_server_stop(void);

/**
 * @brief 检查 Web Server 是否正在运行
 */
bool web_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* __WEB_SERVER_H__ */
