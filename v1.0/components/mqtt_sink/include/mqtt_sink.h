#ifndef MQTT_SINK_H
#define MQTT_SINK_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*                          MQTT 控制与遥测主题定义                          */
/* ========================================================================= */

#define MQTT_CONTROL_REQUEST_TOPIC      "gateway/control/request"
#define MQTT_CONTROL_RESPONSE_TOPIC     "gateway/control/response"
#define MQTT_TELEMETRY_TOPIC_FORMAT     "gateway/%s/telemetry"
#define MQTT_COMMAND_TOPIC_FORMAT       "gateway/%s/command"
#define MQTT_CONFIG_TOPIC_FORMAT        "gateway/%s/config"

/**
 * @brief MQTT Sink 运行配置结构体
 */
typedef struct {
    const char *broker_uri;     /* MQTT Broker URI, 如 "mqtt://192.168.0.7:1883" */
    const char *topic;          /* 遥测数据默认上报主题, 如 "gateway/esp32_gw_001/telemetry" */
    const char *client_id;      /* 客户端唯一标识, 如 "esp32_gateway_001" */
} mqtt_sink_config_t;

/**
 * @brief 初始化 MQTT Sink 适配器 (创建异步队列与 MQTT 客户端实例并向 data_manager 注册)
 * 
 * @param[in] config 配置参数指针
 * @return esp_err_t ESP_OK 表示初始化成功
 */
esp_err_t mqtt_sink_init(const mqtt_sink_config_t *config);

/**
 * @brief 启动 MQTT Sink (建立网络连接、启动发布 Worker 任务)
 * 
 * @return esp_err_t ESP_OK 表示启动成功
 */
esp_err_t mqtt_sink_start(void);

/**
 * @brief 停止 MQTT Sink 适配器
 * 
 * @return esp_err_t ESP_OK 表示停止成功
 */
esp_err_t mqtt_sink_stop(void);

/**
 * @brief 查询当前 MQTT Broker 连接状态
 * 
 * @return true 已连接, false 未连接
 */
bool mqtt_sink_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_SINK_H */
