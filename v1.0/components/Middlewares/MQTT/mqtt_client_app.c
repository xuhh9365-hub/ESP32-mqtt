#include "mqtt_client_app.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "mqtt_client.h"

/* ========================== 私有变量 ========================== */

static const char *TAG = "MQTT_APP";

/* MQTT 客户端句柄 */
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

/* MQTT 连接状态 */
static volatile bool s_mqtt_connected = false;

/* MQTT 数据回调函数 */
static mqtt_data_callback_t s_data_callback = NULL;

/* FreeRTOS EventGroup 用于同步等待MQTT连接 */
static EventGroupHandle_t s_mqtt_event_group;

#define MQTT_CONNECTED_BIT      BIT0

/* ========================== 事件处理 ========================== */

/**
 * @brief       MQTT 事件处理函数
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "===== MQTT 已连接到 Broker =====");
            s_mqtt_connected = true;
            xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);

            /* 连接成功后订阅测试主题，用于接收回环测试消息 */
            int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_TEST, 1);
            ESP_LOGI(TAG, "订阅主题: %s, msg_id=%d", MQTT_TOPIC_TEST, msg_id);

            msg_id=esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_CONTROL, 1);
            ESP_LOGI(TAG, "已订阅控制主题: %s", MQTT_TOPIC_CONTROL);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT 已断开连接");
            s_mqtt_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT 订阅成功, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT 取消订阅, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT 消息发布成功, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "===== 收到 MQTT 消息 =====");
            ESP_LOGI(TAG, "主题: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "数据: %.*s", event->data_len, event->data);
            if (s_data_callback != NULL) {
                s_data_callback(event->topic, event->topic_len, event->data, event->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT 错误事件");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
            {
                ESP_LOGE(TAG, "  TCP 传输错误: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "  TLS 栈错误: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGE(TAG, "  Socket errno: %d", event->error_handle->esp_transport_sock_errno);
            }
            break;

        default:
            ESP_LOGD(TAG, "MQTT 其他事件, id=%d", event->event_id);
            break;
    }
}

/* ========================== 公开函数 ========================== */

/**
 * @brief       启动MQTT客户端并连接到Broker
 */
esp_err_t mqtt_app_start(void)
{
    ESP_LOGI(TAG, "正在启动 MQTT 客户端...");
    ESP_LOGI(TAG, "Broker URI: %s", MQTT_BROKER_URI);

    /* 创建 EventGroup */
    s_mqtt_event_group = xEventGroupCreate();

    /* MQTT 客户端配置 */
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = MQTT_BROKER_URI,
            },
        },
        .credentials = {
            .client_id = "ESP32S3_Client",
        },
    };
    
    /* 创建 MQTT 客户端 */
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "MQTT 客户端创建失败");
        return ESP_FAIL;
    }

    /* 注册 MQTT 事件处理函数 */
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    /* 启动 MQTT 客户端 */
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));

    /* 等待 MQTT 连接成功（最多等待 15 秒） */
    EventBits_t bits = xEventGroupWaitBits(
        s_mqtt_event_group,
        MQTT_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(15000)
    );

    if (bits & MQTT_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "MQTT 客户端启动成功，已连接到 Broker");
        return ESP_OK;
    }
    else
    {
        ESP_LOGW(TAG, "MQTT 连接超时（15秒），客户端将在后台继续重试");
        return ESP_OK;  /* 仍然返回OK，因为MQTT客户端会自动重连 */
    }
}

/**
 * @brief       通过MQTT发布消息
 */
int mqtt_app_publish(const char *topic, const char *data, int qos)
{
    if (s_mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "MQTT 客户端未初始化");
        return -1;
    }

    if (!s_mqtt_connected)
    {
        ESP_LOGW(TAG, "MQTT 未连接，无法发布消息");
        return -1;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, data, 0, qos, 0);
    ESP_LOGI(TAG, "发布消息 -> 主题: %s, msg_id=%d", topic, msg_id);

    return msg_id;
}

/**
 * @brief       检查MQTT是否已连接
 */
bool mqtt_app_is_connected(void)
{
    return s_mqtt_connected;
}


/**
 * @brief 注册 MQTT 接收数据回调函数
 */
void mqtt_app_set_data_callback(mqtt_data_callback_t cb)
{
    s_data_callback = cb;
}