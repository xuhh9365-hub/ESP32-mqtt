#include "mqtt_sink_internal.h"
#include "control_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MQTT_SINK";
static mqtt_sink_context_t s_ctx = {0};

mqtt_sink_context_t *mqtt_sink_get_context(void)
{
    return &s_ctx;
}

/**
 * @brief 控制回执发送适配器函数 (注入给 control_manager 调用)
 */
static esp_err_t mqtt_control_reply_sender(const char *topic, const char *json_payload, void *user_ctx)
{
    if (topic == NULL || json_payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.is_connected || s_ctx.client == NULL) {
        ESP_LOGW(TAG, "MQTT 尚未连接，无法发送控制回执");
        return ESP_ERR_INVALID_STATE;
    }

    // QoS 1 保证回执可靠发布
    int msg_id = esp_mqtt_client_publish(
        s_ctx.client,
        topic,
        json_payload,
        0,  // strlen 自动计算
        1,  // QoS 1
        0   // retain = 0
    );

    if (msg_id >= 0) {
        ESP_LOGI(TAG, "MQTT 发布控制回执成功 -> 主题: %s, ID: %d", topic, msg_id);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "MQTT 发布控制回执失败 (ID: %d)", msg_id);
        return ESP_FAIL;
    }
}

/**
 * @brief MQTT 底层事件处理回调函数
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "===== MQTT 成功连接至 Broker: %s =====", s_ctx.broker_uri_buf);
        s_ctx.is_connected = true;

        // 1. 订阅云端下行控制主题 (QoS 1)
        int sub_id = esp_mqtt_client_subscribe(s_ctx.client, MQTT_CONTROL_REQUEST_TOPIC, 1);
        ESP_LOGI(TAG, "已发起订阅下行控制主题: %s (QoS 1, sub_id=%d)", MQTT_CONTROL_REQUEST_TOPIC, sub_id);

        // 2. 向 control_manager 注册控制回执发送器
        control_manager_register_reply_sender(mqtt_control_reply_sender, NULL);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "⚠️ MQTT 连接断开，正在由后台协议栈自动尝试重连...");
        s_ctx.is_connected = false;
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "收到 MQTT 下行消息 -> 主题: %.*s, 长度: %d 字节",
                 event->topic_len, event->topic, event->data_len);

        // 校验主题是否为控制请求主题
        if (event->topic_len == strlen(MQTT_CONTROL_REQUEST_TOPIC) &&
            strncmp(event->topic, MQTT_CONTROL_REQUEST_TOPIC, event->topic_len) == 0) {
            
            // 极速非阻塞投递给 control_manager 处理队列 (耗时 < 2μs)
            esp_err_t push_err = control_manager_push_raw_json(event->data, (size_t)event->data_len);
            if (push_err != ESP_OK) {
                ESP_LOGE(TAG, "向 control_manager 投递控制报文失败: %s", esp_err_to_name(push_err));
            }
        }
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT 报文发布确认 (msg_id=%d)", event->msg_id);
        break;

    case MQTT_EVENT_ERROR:
        if (event->error_handle != NULL) {
            ESP_LOGE(TAG, "MQTT 发生错误事件 (type=%d, sock_errno=%d)", 
                     event->error_handle->error_type, event->error_handle->esp_transport_sock_errno);
        } else {
            ESP_LOGE(TAG, "MQTT 发生错误事件");
        }
        break;

    default:
        break;
    }
}

/**
 * @brief Sink 回调函数：由 data_manager 广播调用 (极速非阻塞入队)
 */
static esp_err_t mqtt_sink_publish(const gateway_data_t *data, void *ctx)
{
    if (data == NULL || s_ctx.queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mqtt_message_t msg;
    memset(&msg, 0, sizeof(mqtt_message_t));

    // 1. 使用 JSON Formatter 格式化为标准物模型 JSON
    esp_err_t fmt_err = data_format_json(data, msg.payload, sizeof(msg.payload));
    if (fmt_err != ESP_OK) {
        ESP_LOGW(TAG, "物模型 JSON 格式化失败: %s", esp_err_to_name(fmt_err));
        return fmt_err;
    }

    // 2. 构造发布主题: gateway/{gateway_id}/telemetry
    snprintf(msg.topic, sizeof(msg.topic), MQTT_TELEMETRY_TOPIC_FORMAT, data->gateway_id);

    // 3. 0 阻塞快速发送至 MQTT 内部专用发布队列 (防溢出策略: 队列满时丢弃最旧数据)
    if (xQueueSend(s_ctx.queue, &msg, 0) != pdPASS) {
        mqtt_message_t dummy;
        xQueueReceive(s_ctx.queue, &dummy, 0);
        s_ctx.dropped_count++;
        xQueueSend(s_ctx.queue, &msg, 0);
    }

    return ESP_OK;
}

/**
 * @brief 后台独立的 MQTT 发布工作者任务 (mqtt_publish_task)
 */
static void mqtt_publish_task(void *pvParameters)
{
    ESP_LOGI(TAG, "===== MQTT 发布工作者任务 (mqtt_publish_task) 已启动 =====");
    mqtt_message_t msg;

    while (1) {
        // 阻塞等待待发布数据到达
        if (xQueueReceive(s_ctx.queue, &msg, portMAX_DELAY) == pdPASS) {
            if (s_ctx.is_connected && s_ctx.client != NULL) {
                // 发起 MQTT 报文发布 (QoS 1)
                int msg_id = esp_mqtt_client_publish(
                    s_ctx.client,
                    msg.topic,
                    msg.payload,
                    0,  // 计算 strlen
                    1,  // QoS 1
                    0   // retain = 0
                );

                if (msg_id >= 0) {
                    s_ctx.published_count++;
                    ESP_LOGI(TAG, "MQTT 遥测上报成功 -> 主题: %s, ID: %d, Payload: %s",
                             msg.topic, msg_id, msg.payload);
                } else {
                    ESP_LOGW(TAG, "MQTT 发送失败 (msg_id=%d)", msg_id);
                }
            }
        }
    }
}

esp_err_t mqtt_sink_init(const mqtt_sink_config_t *config)
{
    if (config == NULL || config->broker_uri == NULL || config->topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "正在初始化 MQTT Sink 适配器...");

    memset(&s_ctx, 0, sizeof(mqtt_sink_context_t));
    strncpy(s_ctx.broker_uri_buf, config->broker_uri, sizeof(s_ctx.broker_uri_buf) - 1);
    strncpy(s_ctx.topic_buf, config->topic, sizeof(s_ctx.topic_buf) - 1);
    if (config->client_id != NULL) {
        strncpy(s_ctx.client_id_buf, config->client_id, sizeof(s_ctx.client_id_buf) - 1);
    } else {
        strncpy(s_ctx.client_id_buf, "esp32_gateway_001", sizeof(s_ctx.client_id_buf) - 1);
    }

    // 1. 创建独立发布队列
    s_ctx.queue = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(mqtt_message_t));
    if (s_ctx.queue == NULL) {
        ESP_LOGE(TAG, "创建 MQTT 发布队列失败");
        return ESP_ERR_NO_MEM;
    }

    // 2. 配置并初始化 ESP-MQTT 客户端
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_ctx.broker_uri_buf,
        .credentials.client_id = s_ctx.client_id_buf,
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 2000,
    };

    s_ctx.client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_ctx.client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init 初始化失败");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_ctx.client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "注册 MQTT 事件回调失败: %s", esp_err_to_name(err));
        return err;
    }

    // 3. 向 data_manager 注册 Sink 插件
    err = data_manager_register_sink("mqtt", mqtt_sink_publish, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "向 data_manager 注册 mqtt sink 失败: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "MQTT Sink 适配器初始化成功 (Broker: %s, Topic: %s)",
             s_ctx.broker_uri_buf, s_ctx.topic_buf);

    return ESP_OK;
}

esp_err_t mqtt_sink_start(void)
{
    if (s_ctx.is_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "正在启动 MQTT Sink 客户端与后台 Worker...");
    s_ctx.is_running = true;

    // 1. 创建后台独立的 MQTT 发布工作者任务
    BaseType_t ret = xTaskCreatePinnedToCore(
        mqtt_publish_task,
        "mqtt_publish_task",
        4096,
        NULL,
        3,  // 优先级 3 (低于 data_manager 4 与采集 5)
        &s_ctx.task,
        tskNO_AFFINITY
    );

    if (ret != pdPASS) {
        s_ctx.is_running = false;
        ESP_LOGE(TAG, "创建 mqtt_publish_task 任务失败");
        return ESP_ERR_NO_MEM;
    }

    // 2. 启动 ESP-MQTT 客户端网络连接
    esp_err_t err = esp_mqtt_client_start(s_ctx.client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start 启动失败: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t mqtt_sink_stop(void)
{
    if (!s_ctx.is_running) {
        return ESP_OK;
    }

    s_ctx.is_running = false;
    if (s_ctx.task != NULL) {
        vTaskDelete(s_ctx.task);
        s_ctx.task = NULL;
    }

    if (s_ctx.client != NULL) {
        esp_mqtt_client_stop(s_ctx.client);
    }

    ESP_LOGI(TAG, "MQTT Sink 适配器已停止");
    return ESP_OK;
}

bool mqtt_sink_is_connected(void)
{
    return s_ctx.is_connected;
}
