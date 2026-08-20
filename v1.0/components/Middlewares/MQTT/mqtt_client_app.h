#ifndef __MQTT_CLIENT_APP_H_
#define __MQTT_CLIENT_APP_H_

#include "esp_err.h"
#include <stdbool.h>

/* MQTT Broker 配置 */
#define MQTT_BROKER_URI         "mqtt://192.168.0.7:1883"

/* MQTT 默认主题 */
#define MQTT_TOPIC_TEST         "esp32s3/test"
#define MQTT_TOPIC_SENSOR       "home/room1/dht11"

/* MQTT 控制主题 */
#define MQTT_TOPIC_CONTROL       "home/room1/control"



/**
 * @brief       启动MQTT客户端并连接到Broker
 * @note        调用前需确保WiFi已连接并获取IP地址
 * @param       无
 * @retval      ESP_OK: 启动成功
 *              ESP_FAIL: 启动失败
 */
esp_err_t mqtt_app_start(void);

/**
 * @brief       通过MQTT发布消息
 * @param       topic: 发布主题
 * @param       data: 发布数据（字符串）
 * @param       qos: QoS等级（0, 1, 2）
 * @retval      >= 0: 消息ID（发布成功）
 *              -1: 发布失败
 */
int mqtt_app_publish(const char *topic, const char *data, int qos);

/**
 * @brief       检查MQTT是否已连接
 * @param       无
 * @retval      true: 已连接
 *              false: 未连接
 */
bool mqtt_app_is_connected(void);






/* MQTT 接收数据回调函数类型 */
typedef void (*mqtt_data_callback_t)(const char *topic, int topic_len, const char *data, int data_len);
/**
 * @brief 注册 MQTT 接收数据回调函数
 */
void mqtt_app_set_data_callback(mqtt_data_callback_t cb);




#endif /* __MQTT_CLIENT_APP_H_ */
