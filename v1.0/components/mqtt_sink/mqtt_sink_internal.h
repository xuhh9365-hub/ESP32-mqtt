#ifndef __MQTT_SINK_INTERNAL_H__
#define __MQTT_SINK_INTERNAL_H__

#include "mqtt_sink.h"
#include "data_manager.h"
#include "data_types.h"
#include "data_sink.h"
#include "data_formatter.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define MQTT_QUEUE_SIZE         16
#define MQTT_PAYLOAD_MAX_LEN    1536
#define MQTT_TOPIC_MAX_LEN      128

/**
 * @brief MQTT 内部消息结构体 (发布队列承载)
 */
typedef struct {
    char payload[MQTT_PAYLOAD_MAX_LEN];
    char topic[MQTT_TOPIC_MAX_LEN];
} mqtt_message_t;

/**
 * @brief MQTT Sink 内部全局上下文
 */
typedef struct {
    esp_mqtt_client_handle_t    client;                 /* MQTT 客户端句柄 */
    QueueHandle_t               queue;                  /* 异步发布消息队列 (容量 16) */
    TaskHandle_t                task;                   /* 后台发布工作者任务句柄 */
    char                        broker_uri_buf[128];    /* Broker URI 字符串缓存 */
    char                        topic_buf[64];          /* 默认上报主题字符串缓存 */
    char                        client_id_buf[64];      /* Client ID 字符串缓存 */
    volatile bool               is_connected;           /* Broker 连接标志 */
    bool                        is_running;             /* 适配器运行标志 */
    uint32_t                    published_count;        /* 成功发布消息计数 */
    uint32_t                    dropped_count;          /* 队列溢出丢弃计数 */
} mqtt_sink_context_t;

mqtt_sink_context_t *mqtt_sink_get_context(void);

#endif /* __MQTT_SINK_INTERNAL_H__ */
