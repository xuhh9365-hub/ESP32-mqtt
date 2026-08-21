#ifndef __DEVICE_INTERNAL_H__
#define __DEVICE_INTERNAL_H__

#include "device_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 设备管理器全局运行上下文
 */
typedef struct {
    device_instance_t   instances[DEVICE_MANAGER_MAX_DEVICES]; /* 运行态设备实例表 */
    uint8_t             device_count;                          /* 有效实例总数 */
    bool                is_initialized;                        /* 初始化标志 */
    bool                is_running;                            /* 调度运行标志 */
    TaskHandle_t        scheduler_task_handle;                 /* 调度器任务句柄 */
    SemaphoreHandle_t   lock;                                  /* 实例表访问互斥锁 */
} device_manager_context_t;

/**
 * @brief 获取内部上下文单例指针
 */
device_manager_context_t *device_manager_get_context(void);

/**
 * @brief 集中式 Modbus 采集调度器任务入口函数
 */
void device_scheduler_task(void *pvParameters);

/**
 * @brief 数据处理层接口占位钩子 (供未来对接 data_process 模块)
 * 
 * @param config 设备静态配置
 * @param raw_value 采集到的原始寄存器数值
 */
void data_process_handle_raw_data(const device_config_t *config, uint16_t raw_value);

#ifdef __cplusplus
}
#endif

#endif /* __DEVICE_INTERNAL_H__ */
