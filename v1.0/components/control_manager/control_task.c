#include "control_internal.h"
#include "device_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "CONTROL_TASK";

/**
 * @brief 将 ESP-IDF / device_manager 错误码映射为标准下行控制状态码
 */
static int esp_err_to_control_code(esp_err_t err, const char *device_name)
{
    switch (err) {
    case ESP_OK:
        return CONTROL_OK;
    case ESP_ERR_NOT_FOUND: {
        device_info_t dummy_info;
        if (device_name != NULL && device_manager_get_device_info(device_name, &dummy_info) == ESP_OK) {
            return CONTROL_ERR_METRIC_NOT_FOUND; // 405
        }
        return CONTROL_ERR_DEVICE_NOT_FOUND; // 404
    }
    case ESP_ERR_INVALID_STATE:
        return CONTROL_ERR_DEVICE_OFFLINE; // 407
    case ESP_ERR_INVALID_ARG:
        return CONTROL_ERR_PARAM_OUT_OF_RANGE; // 402
    case ESP_ERR_TIMEOUT:
        return CONTROL_ERR_TIMEOUT; // 504
    default:
        return CONTROL_ERR_WRITE_FAILED; // 500
    }
}

/**
 * @brief 发送控制回执 JSON
 */
static void control_send_response(control_manager_context_t *ctx, const control_response_t *resp)
{
    char reply_buf[CONTROL_REPLY_MAX_LEN];
    esp_err_t ret = control_response_format(resp, reply_buf, sizeof(reply_buf));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "格式化回执报文失败: %s", esp_err_to_name(ret));
        return;
    }

    if (ctx->reply_sender != NULL) {
        esp_err_t send_err = ctx->reply_sender(MQTT_CONTROL_RESPONSE_TOPIC, reply_buf, ctx->reply_user_ctx);
        if (send_err != ESP_OK) {
            ESP_LOGE(TAG, "发送控制回执失败 (Topic: %s, err: %s)", 
                     MQTT_CONTROL_RESPONSE_TOPIC, esp_err_to_name(send_err));
        } else {
            ESP_LOGI(TAG, "已成功发布控制回执 -> 主题: %s, 载荷: %s", 
                     MQTT_CONTROL_RESPONSE_TOPIC, reply_buf);
        }
    } else {
        ESP_LOGW(TAG, "未注册 reply_sender 回执发送通道，回执仅本地记录: %s", reply_buf);
    }
}

/**
 * @brief 处理单帧原始下行控制数据包
 */
static void control_process_packet(control_manager_context_t *ctx, const control_raw_packet_t *packet)
{
    int64_t recv_ts = esp_timer_get_time() / 1000;
    control_message_t msg;
    int parse_err_code = CONTROL_OK;

    // 1. 解析原始 JSON 报文
    esp_err_t ret = control_parser_parse(packet->payload, packet->length, &msg, &parse_err_code);
    if (ret != ESP_OK) {
        // 解析失败，快速构造 NACK 回执
        control_response_t nack_resp = {
            .code = parse_err_code,
            .timestamp_ms = esp_timer_get_time() / 1000,
            .cost_ms = (uint32_t)(esp_timer_get_time() / 1000 - recv_ts)
        };
        strncpy(nack_resp.msg_id, (msg.msg_id[0] != '\0') ? msg.msg_id : "unknown", sizeof(nack_resp.msg_id) - 1);
        nack_resp.message[0] = '\0';

        ctx->fail_cmd_count++;
        control_send_response(ctx, &nack_resp);
        return;
    }

    // 2. 幂等性查重 (防止 MQTT QoS 重发导致物理设备重复写入)
    control_response_t cached_resp;
    if (control_txn_cache_lookup(msg.msg_id, &cached_resp)) {
        ESP_LOGW(TAG, "⚠️ 命中幂等事务缓存 (重复 msg_id: %s)，直接返回历史回执 (code=%d)", 
                 msg.msg_id, cached_resp.code);
        control_send_response(ctx, &cached_resp);
        return;
    }

    // 3. 调度底层执行物理控制 (严格通过 device_manager 语义接口)
    ESP_LOGI(TAG, "▶ 开始执行控制 -> 设备: %s, 测点: %s, 物理量: %.2f", 
             msg.device, msg.metric, msg.value.f32);

    esp_err_t write_err = device_manager_write_metric(
        msg.device,
        msg.metric,
        msg.value,
        msg.val_type,
        msg.timeout_ms
    );

    int64_t finish_ts = esp_timer_get_time() / 1000;
    int control_code = esp_err_to_control_code(write_err, msg.device);

    // 4. 组装标准执行回执
    control_response_t resp = {
        .code = control_code,
        .timestamp_ms = finish_ts,
        .cost_ms = (uint32_t)(finish_ts - msg.receive_timestamp_ms)
    };
    strncpy(resp.msg_id, msg.msg_id, sizeof(resp.msg_id) - 1);
    resp.msg_id[sizeof(resp.msg_id) - 1] = '\0';
    resp.message[0] = '\0';

    // 5. 更新统计指标
    if (control_code == CONTROL_OK) {
        ctx->success_cmd_count++;
        ESP_LOGI(TAG, "✅ 控制执行成功 [msg_id: %s] -> 耗时 %lu ms", resp.msg_id, (unsigned long)resp.cost_ms);
    } else {
        ctx->fail_cmd_count++;
        ESP_LOGW(TAG, "❌ 控制执行失败 [msg_id: %s] -> 错误码 %d, 耗时 %lu ms", 
                 resp.msg_id, resp.code, (unsigned long)resp.cost_ms);
    }

    // 6. 插入幂等事务环形缓存池
    control_txn_cache_insert(&resp);

    // 7. 发送控制回执
    control_send_response(ctx, &resp);
}

void control_task_entry(void *pvParameters)
{
    control_manager_context_t *ctx = control_manager_get_context();
    ESP_LOGI(TAG, "===== control_task 异步工作者任务已就绪 (优先级: %d) =====", CONTROL_TASK_PRIORITY);

    control_raw_packet_t packet;

    while (ctx->is_running) {
        // 阻塞等待下行控制报文到达
        if (xQueueReceive(ctx->cmd_queue, &packet, portMAX_DELAY) == pdPASS) {
            ESP_LOGD(TAG, "收到原始控制指令报文 (%d 字节): %s", (int)packet.length, packet.payload);
            control_process_packet(ctx, &packet);
        }
    }

    ESP_LOGI(TAG, "control_task 任务已退出");
    vTaskDelete(NULL);
}
