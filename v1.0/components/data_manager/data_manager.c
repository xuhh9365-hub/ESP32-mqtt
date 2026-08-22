#include "data_internal.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DATA_MANAGER";
static data_manager_context_t s_data_ctx = {0};
static gateway_data_t s_dispatch_snapshot = {0};

data_manager_context_t *data_manager_get_context(void)
{
    return &s_data_ctx;
}

esp_err_t data_manager_init(void)
{
    ESP_LOGI(TAG, "正在初始化数据出口层中枢 (data_manager)...");

    // 1. 初始化 LVC 实时快照引擎
    esp_err_t err = data_lvc_init();
    if (err != ESP_OK) {
        return err;
    }

    // 2. 初始化 Sink 插件框架
    err = data_sink_init();
    if (err != ESP_OK) {
        return err;
    }

    // 3. 初始化数据缓冲队列
    err = data_queue_init();
    if (err != ESP_OK) {
        return err;
    }

    s_data_ctx.is_running = false;
    s_data_ctx.task_handle = NULL;

    ESP_LOGI(TAG, "数据出口层中枢初始化成功 (支持 LVC 实时快照与多 Sink 分发)");
    return ESP_OK;
}

esp_err_t data_manager_start(void)
{
    if (s_data_ctx.is_running) {
        ESP_LOGW(TAG, "data_manager 任务已经在运行中");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "正在启动数据分发中枢任务 (data_mgr_task)...");
    s_data_ctx.is_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        data_mgr_task,
        "data_mgr_task",
        8192,           // 栈空间 8192 字节
        NULL,
        4,              // 优先级 4 (高于 Sink 3，低于 Modbus/Control 5)
        &s_data_ctx.task_handle,
        tskNO_AFFINITY
    );

    if (ret != pdPASS) {
        s_data_ctx.is_running = false;
        ESP_LOGE(TAG, "创建 data_mgr_task 任务失败");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "数据出口层中枢已成功启动!");
    return ESP_OK;
}

esp_err_t data_manager_stop(void)
{
    if (!s_data_ctx.is_running) {
        return ESP_OK;
    }

    s_data_ctx.is_running = false;
    if (s_data_ctx.task_handle != NULL) {
        vTaskDelete(s_data_ctx.task_handle);
        s_data_ctx.task_handle = NULL;
    }

    ESP_LOGI(TAG, "data_manager 任务已停止");
    return ESP_OK;
}

esp_err_t data_manager_push(const device_data_t *data)
{
    return data_queue_push(data);
}

esp_err_t data_manager_push_sensor_data(const sensor_data_t *sdata)
{
    if (sdata == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    device_data_t dev_data;
    memset(&dev_data, 0, sizeof(device_data_t));
    strncpy(dev_data.device_name, sdata->device_id, DATA_DEVICE_NAME_MAX_LEN - 1);
    dev_data.status = sdata->status;
    dev_data.update_timestamp_ms = sdata->sample_timestamp_ms;
    dev_data.metric_count = 1;

    strncpy(dev_data.metrics[0].name, sdata->metric_name, DATA_METRIC_NAME_MAX_LEN - 1);
    dev_data.metrics[0].val_type = sdata->val_type;
    dev_data.metrics[0].value.f32 = sdata->value.f32;
    strncpy(dev_data.metrics[0].unit, sdata->unit, DATA_UNIT_MAX_LEN - 1);
    dev_data.metrics[0].status = sdata->status;
    dev_data.metrics[0].timestamp_ms = sdata->sample_timestamp_ms;

    return data_manager_push(&dev_data);
}

void data_mgr_task(void *arg)
{
    ESP_LOGI(TAG, "===== 数据出口分发任务 (data_mgr_task) 已就绪 (优先级: 4) =====");

    device_data_t packet;

    while (s_data_ctx.is_running) {
        // 阻塞等待数据到达，0 CPU 空转
        esp_err_t err = data_queue_receive(&packet, portMAX_DELAY);
        if (err != ESP_OK) {
            continue;
        }

        // 1. 刷新 LVC 实时数据快照表
        data_lvc_update(&packet);
        s_data_ctx.processed_packet_count++;

        // 2. 提取最新整机快照 (使用全局静态缓冲区，0 栈溢出风险)
        if (data_manager_get_gateway_snapshot(&s_dispatch_snapshot) == ESP_OK) {
            // 3. 广播给所有已启用的 Sink 插件 (MQTT, Debug, Flash, HTTP)
            data_sink_dispatch(&s_dispatch_snapshot);
        }
    }

    vTaskDelete(NULL);
}
