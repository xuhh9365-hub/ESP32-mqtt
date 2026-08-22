#ifndef __DATA_SINK_H__
#define __DATA_SINK_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "data_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DATA_SINK       4
#define SINK_NAME_MAX_LEN   32

/**
 * @brief 数据出口 Sink 回调函数原型 (必须快速返回，禁止在回调内阻塞等待网络)
 */
typedef esp_err_t (*data_sink_callback_t)(
    const gateway_data_t *data,
    void *ctx
);

/**
 * @brief 数据出口 Sink 描述符
 */
typedef struct {
    char                    name[SINK_NAME_MAX_LEN];    /* Sink 名称，如 "mqtt", "debug", "flash", "http" */
    bool                    enabled;                    /* 是否使能 */
    data_sink_callback_t    callback;                   /* 分发回调函数 */
    void                   *ctx;                        /* 用户上下文私有指针 */
} data_sink_t;

/**
 * @brief 注册数据出口 Sink 插件
 * 
 * @param[in] name     Sink 唯一名称 (如 "mqtt")
 * @param[in] callback 快速分发回调函数
 * @param[in] ctx      用户私有上下文
 * @return esp_err_t   ESP_OK 成功, ESP_ERR_NO_MEM 槽位已满, ESP_ERR_INVALID_ARG 参数无效
 */
esp_err_t data_manager_register_sink(
    const char *name,
    data_sink_callback_t callback,
    void *ctx
);

/**
 * @brief 动态启用或禁用指定名称的 Sink 出口
 * 
 * @param[in] name   Sink 名称
 * @param[in] enable true: 启用, false: 禁用
 * @return esp_err_t ESP_OK 成功, ESP_ERR_NOT_FOUND 未找到
 */
esp_err_t data_manager_set_sink_enabled(
    const char *name,
    bool enable
);

#ifdef __cplusplus
}
#endif

#endif /* __DATA_SINK_H__ */
