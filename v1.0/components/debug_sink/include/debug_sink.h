#ifndef DEBUG_SINK_H
#define DEBUG_SINK_H

#include "data_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化并注册 Debug Sink 调试出口插件
 * 
 * @return esp_err_t ESP_OK 表示注册成功
 */
esp_err_t debug_sink_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_SINK_H */
