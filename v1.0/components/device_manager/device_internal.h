#ifndef __DEVICE_INTERNAL_H__
#define __DEVICE_INTERNAL_H__

#include "device_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define DEVICE_CONSECUTIVE_FAIL_LIMIT   3       /* 判定离线的连续通信失败门限 */

/**
 * @brief device_manager 全局内部上下文 (双互斥锁分离架构)
 */
typedef struct {
    bool                is_running;                             /* 调度器运行状态 */
    TaskHandle_t        scheduler_task_handle;                  /* 调度器任务句柄 */
    SemaphoreHandle_t   instance_lock;                          /* 专职保护 instances[] 结构体内存 (< 1μs) */
    SemaphoreHandle_t   bus_lock;                               /* 专职保护 RS485 物理总线排他访问 (20~200ms) */
    device_instance_t   instances[DEVICE_MANAGER_MAX_DEVICES];  /* 设备运行态实例池 */
    uint8_t             device_count;                           /* 当前有效设备数 */
    uint32_t            bus_wait_count;                         /* 累计总线请求次数 */
    uint32_t            bus_timeout_count;                      /* 累计总线超时次数 */
    volatile bool       is_bus_busy;                            /* 当前物理总线收发标志 */
} device_manager_context_t;

/**
 * @brief 获取全局上下文指针
 */
device_manager_context_t *device_manager_get_context(void);

/**
 * @brief 集中式 Modbus 轮询调度器任务主入口
 */
void device_scheduler_task(void *pvParameters);

#endif /* __DEVICE_INTERNAL_H__ */
