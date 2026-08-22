#ifndef __DATA_INTERNAL_H__
#define __DATA_INTERNAL_H__

#include "data_manager.h"
#include "data_sink.h"
#include "data_formatter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define DATA_QUEUE_LENGTH       32

/**
 * @brief data_manager 内部全局上下文
 */
typedef struct {
    bool                    is_running;                     /* 任务运行标志 */
    TaskHandle_t            task_handle;                    /* 任务句柄 */
    SemaphoreHandle_t       lvc_mutex;                      /* LVC 实时快照互斥锁 */
    SemaphoreHandle_t       sink_lock;                      /* Sink 注册表互斥锁 */
    QueueHandle_t           data_queue;                     /* 异步数据缓冲队列 */
    gateway_data_t          latest_gateway_data;            /* LVC 全局最新快照 */
    uint32_t                dropped_packet_count;           /* 丢包统计计数 */
    uint32_t                processed_packet_count;         /* 累计处理数据包计数 */
} data_manager_context_t;

/**
 * @brief 获取内部上下文指针
 */
data_manager_context_t *data_manager_get_context(void);

/**
 * @brief 初始化内部数据队列
 */
esp_err_t data_queue_init(void);

/**
 * @brief 极速推入队列 (支持 Drop Oldest 防御)
 */
esp_err_t data_queue_push(const device_data_t *data);

/**
 * @brief 从队列接收数据
 */
esp_err_t data_queue_receive(device_data_t *data, TickType_t wait_ticks);

/**
 * @brief 初始化 LVC 实时快照引擎
 */
esp_err_t data_lvc_init(void);

/**
 * @brief 更新 LVC 实时数据快照
 */
esp_err_t data_lvc_update(const device_data_t *data);

/**
 * @brief 初始化 Sink 插件管理器
 */
esp_err_t data_sink_init(void);

/**
 * @brief 向所有已使能的 Sink 插件广播网关全局快照
 */
void data_sink_dispatch(const gateway_data_t *snapshot);

/**
 * @brief 核心消费分发任务入口
 */
void data_mgr_task(void *arg);

#endif /* __DATA_INTERNAL_H__ */
