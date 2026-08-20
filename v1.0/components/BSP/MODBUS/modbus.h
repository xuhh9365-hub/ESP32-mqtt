#ifndef __MODBUS_H__
#define __MODBUS_H__

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_modbus_master.h"






// Modbus 特征 ID
enum {
    CID_DEV_DATA = 0,   // 一次性批量读取所有从站寄存器 (0x0010 ~ 0x0014)
    CID_SET_TEMP_LIMIT,
    CID_SET_HUMI_LIMIT,
    CID_SET_STATUS,
    CID_COUNT
};

// 原始 Modbus 保持寄存器结构体（严格对应 5 个 16 位寄存器）
typedef struct {
    uint16_t temperature;   // 0x0010: 原始温度值 (如 253 代表 25.3℃)
    uint16_t humidity;      // 0x0011: 原始湿度值 (如 652 代表 65.2%)
    uint16_t status;        // 0x0012: 设备状态
    uint16_t temp_limit;    // 0x0013: 原始温度上限 (如 400 代表 40.0℃)
    uint16_t humi_limit;    // 0x0014: 原始湿度下限 (如 800 代表 80.0%)
} __attribute__((packed)) holding_reg_params_t;

// 网关应用层数据结构体（转换为实际物理量，供 MQTT/业务层使用）
typedef struct {
    float    temperature;   // 实际温度值 (℃)
    float    humidity;      // 实际湿度值 (%)
    uint16_t status;        // 运行状态
    float    temp_limit;    // 实际温度上限 (℃)
    float    humi_limit;    // 实际湿度下限 (%)
} gateway_data_t;

/**
 * @brief 初始化 Modbus Master 协议栈
 */
esp_err_t modbus_master_init(void);

/**
 * @brief 重置/重建 Modbus Master 协议栈（清除超时和脏数据状态）
 */
esp_err_t modbus_master_reset(void);

/**
 * @brief 采集所有从站寄存器数据
 * 
 * @param out_data 输出数据指针
 * @return esp_err_t ESP_OK 表示采集成功
 */
esp_err_t modbus_master_read_all(gateway_data_t *out_data);


esp_err_t modbus_master_write_temp_limit(float limit);

esp_err_t modbus_master_write_humi_limit(float limit);

esp_err_t modbus_master_write_status(uint16_t status);



#endif /* __MODBUS_H__ */
