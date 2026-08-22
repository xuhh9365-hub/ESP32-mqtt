#include "control_internal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "CONTROL_MANAGER";
static control_manager_context_t g_control_ctx = {0};

control_manager_context_t *control_manager_get_context(void)
{
    return &g_control_ctx;
}

esp_err_t control_manager_init(void)
{
    if (g_control_ctx.cmd_queue != NULL) {
        ESP_LOGW(TAG, "control_manager 已初始化，跳过重复初始化");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "正在初始化 control_manager (下行控制中枢)...");
    memset(&g_control_ctx, 0, sizeof(control_manager_context_t));

    // 1. 创建控制指令接收队列 (容量 CONTROL_QUEUE_SIZE = 8)
    g_control_ctx.cmd_queue = xQueueCreate(CONTROL_QUEUE_SIZE, sizeof(control_raw_packet_t));
    if (g_control_ctx.cmd_queue == NULL) {
        ESP_LOGE(TAG, "创建 cmd_queue 指令队列失败 (内存不足)");
        return ESP_ERR_NO_MEM;
    }

    // 2. 初始化幂等性事务缓存池
    g_control_ctx.txn_cache_head = 0;
    for (int i = 0; i < CONTROL_TXN_CACHE_SIZE; i++) {
        g_control_ctx.txn_cache[i].is_valid = false;
    }

    // 3. 初始化统计变量与状态
    g_control_ctx.is_running = false;
    g_control_ctx.task_handle = NULL;
    g_control_ctx.reply_sender = NULL;
    g_control_ctx.reply_user_ctx = NULL;
    g_control_ctx.rx_cmd_count = 0;
    g_control_ctx.success_cmd_count = 0;
    g_control_ctx.fail_cmd_count = 0;
    g_control_ctx.duplicate_cmd_count = 0;

    ESP_LOGI(TAG, "control_manager initialized");
    return ESP_OK;
}

esp_err_t control_manager_start(void)
{
    if (g_control_ctx.is_running) {
        return ESP_OK;
    }

    if (g_control_ctx.cmd_queue == NULL) {
        ESP_LOGE(TAG, "control_manager 尚未初始化，无法启动");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "正在启动 control_task 工作者任务...");
    g_control_ctx.is_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        control_task_entry,
        "control_task",
        CONTROL_TASK_STACK_SIZE,
        NULL,
        CONTROL_TASK_PRIORITY,
        &g_control_ctx.task_handle,
        tskNO_AFFINITY
    );

    if (ret != pdPASS) {
        g_control_ctx.is_running = false;
        ESP_LOGE(TAG, "创建 control_task 失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "control_task started");
    return ESP_OK;
}

esp_err_t control_manager_stop(void)
{
    if (!g_control_ctx.is_running) {
        return ESP_OK;
    }

    g_control_ctx.is_running = false;

    if (g_control_ctx.task_handle != NULL) {
        vTaskDelete(g_control_ctx.task_handle);
        g_control_ctx.task_handle = NULL;
    }

    ESP_LOGI(TAG, "control_task stopped");
    return ESP_OK;
}

bool control_manager_is_running(void)
{
    return g_control_ctx.is_running;
}

esp_err_t control_manager_push_raw_json(const char *json_str, size_t json_len)
{
    // 1. 参数合法性校验
    if (json_str == NULL || json_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (json_len >= CONTROL_RAW_BUF_SIZE) {
        ESP_LOGE(TAG, "下行控制报文过长: %u 字节 (最大允许 %d 字节)", 
                 (unsigned int)json_len, CONTROL_RAW_BUF_SIZE - 1);
        return ESP_ERR_INVALID_SIZE;
    }

    if (g_control_ctx.cmd_queue == NULL || !g_control_ctx.is_running) {
        return ESP_ERR_INVALID_STATE;
    }

    // 2. 静态结构体拷贝
    control_raw_packet_t packet;
    memcpy(packet.payload, json_str, json_len);
    packet.payload[json_len] = '\0';
    packet.length = json_len;

    // 3. 0 阻塞快速推入队列 (严禁丢弃旧指令)
    if (xQueueSend(g_control_ctx.cmd_queue, &packet, 0) != pdPASS) {
        ESP_LOGW(TAG, "指令队列已满 (容量 %d)，拒绝新控制指令", CONTROL_QUEUE_SIZE);
        return ESP_ERR_NO_MEM;
    }

    g_control_ctx.rx_cmd_count++;
    return ESP_OK;
}

esp_err_t control_manager_register_reply_sender(control_reply_sender_t sender, void *user_ctx)
{
    if (sender == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_control_ctx.reply_sender = sender;
    g_control_ctx.reply_user_ctx = user_ctx;
    ESP_LOGI(TAG, "已成功注册控制回执发送通道 (Reply Sender)");
    return ESP_OK;
}

bool control_txn_cache_lookup(const char *msg_id, control_response_t *out_cached_resp)
{
    if (msg_id == NULL || msg_id[0] == '\0') {
        return false;
    }

    for (int i = 0; i < CONTROL_TXN_CACHE_SIZE; i++) {
        if (g_control_ctx.txn_cache[i].is_valid && 
            strncmp(g_control_ctx.txn_cache[i].msg_id, msg_id, CONTROL_MSG_ID_MAX_LEN) == 0) {
            if (out_cached_resp != NULL) {
                *out_cached_resp = g_control_ctx.txn_cache[i].response;
            }
            g_control_ctx.duplicate_cmd_count++;
            return true;
        }
    }
    return false;
}

void control_txn_cache_insert(const control_response_t *resp)
{
    if (resp == NULL || resp->msg_id[0] == '\0') {
        return;
    }

    uint8_t slot = g_control_ctx.txn_cache_head;
    g_control_ctx.txn_cache[slot].is_valid = true;
    strncpy(g_control_ctx.txn_cache[slot].msg_id, resp->msg_id, CONTROL_MSG_ID_MAX_LEN - 1);
    g_control_ctx.txn_cache[slot].msg_id[CONTROL_MSG_ID_MAX_LEN - 1] = '\0';
    g_control_ctx.txn_cache[slot].response = *resp;
    g_control_ctx.txn_cache[slot].timestamp_ms = esp_timer_get_time() / 1000;

    g_control_ctx.txn_cache_head = (slot + 1) % CONTROL_TXN_CACHE_SIZE;
}
