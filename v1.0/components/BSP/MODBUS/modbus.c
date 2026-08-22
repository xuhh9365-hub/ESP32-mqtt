#include "modbus.h"
#include "rs485.h"        // 引用引脚定义宏 (RS485_UART_NUM, TX_PIN, RX_PIN, RTS_PIN)
#include <stddef.h>
#include <string.h>

static const char *TAG = "MODBUS_MASTER";
static void *master_handler = NULL;   // esp-modbus Master 控制器句柄

// ========== 描述符表 (用于满足 esp-modbus v2.1.3 的 mbc_master_start 启动约束) ==========
static const mb_parameter_descriptor_t device_parameters[] = {
    {
        .cid = CID_DEV_DATA,
        .param_key = "DevData",
        .param_units = "raw",
        .mb_slave_addr = 0x02,
        .mb_param_type = MB_PARAM_HOLDING,
        .mb_reg_start = 0x0010,
        .mb_size = 5,
        .param_offset = 0,
        .param_type = PARAM_TYPE_U16,
        .param_size = sizeof(holding_reg_params_t),
        .param_opts = { .opt1 = 0, .opt2 = 0, .opt3 = 0 },
        .access = PAR_PERMS_READ
    },
    {
        .cid = CID_SET_TEMP_LIMIT,
        .param_key = "SetTempLimit",
        .param_units = "0.1C",
        .mb_slave_addr = 0x02,
        .mb_param_type = MB_PARAM_HOLDING,
        .mb_reg_start = 0x0013,
        .mb_size = 1,
        .param_offset = 0,
        .param_type = PARAM_TYPE_U16,
        .param_size = sizeof(uint16_t),
        .param_opts = { .opt1 = 0, .opt2 = 0, .opt3 = 0 },
        .access = PAR_PERMS_WRITE
    },
    {
        .cid = CID_SET_HUMI_LIMIT,
        .param_key = "SetHumiLimit",
        .param_units = "0.1%",
        .mb_slave_addr = 0x02,
        .mb_param_type = MB_PARAM_HOLDING,
        .mb_reg_start = 0x0014,
        .mb_size = 1,
        .param_offset = 0,
        .param_type = PARAM_TYPE_U16,
        .param_size = sizeof(uint16_t),
        .param_opts = { .opt1 = 0, .opt2 = 0, .opt3 = 0 },
        .access = PAR_PERMS_WRITE
    },
    {
        .cid = CID_SET_STATUS,
        .param_key = "SetStatus",
        .param_units = "bin",
        .mb_slave_addr = 0x02,
        .mb_param_type = MB_PARAM_HOLDING,
        .mb_reg_start = 0x0012,
        .mb_size = 1,
        .param_offset = 0,
        .param_type = PARAM_TYPE_U16,
        .param_size = sizeof(uint16_t),
        .param_opts = { .opt1 = 0, .opt2 = 0, .opt3 = 0 },
        .access = PAR_PERMS_WRITE
    },
};

static const uint16_t num_device_parameters = sizeof(device_parameters) / sizeof(device_parameters[0]);

/* ========================================================================= */
/*                   1. 协议栈生命周期与初始化实现                           */
/* ========================================================================= */

esp_err_t modbus_master_init_baud(uint32_t baudrate)
{
    esp_err_t err;

    if (master_handler != NULL) {
        return ESP_OK;
    }

    if (baudrate == 0) {
        baudrate = 115200;
    }

    // 第1步：创建官方串口 Master 控制器 (内部安装 UART 驱动并建立事件队列)
    mb_communication_info_t comm = {
        .ser_opts = {
            .mode = MB_RTU,
            .port = RS485_UART_NUM,       // UART_NUM_1
            .baudrate = baudrate,
            .data_bits = UART_DATA_8_BITS,
            .stop_bits = UART_STOP_BITS_1,
            .parity = UART_PARITY_DISABLE,
            .response_tout_ms = 500,
        }
    };
    err = mbc_master_create_serial(&comm, &master_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "创建串口Master失败: %s", esp_err_to_name(err));
        return err;
    }

    // 第2步：绑定物理引脚
    err = uart_set_pin(RS485_UART_NUM, RS485_UART_TX_PIN, RS485_UART_RX_PIN,
                       RS485_UART_RTS_PIN, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置UART引脚失败: %s", esp_err_to_name(err));
        return err;
    }

    // 第3步：开启 RS485 硬件半双工模式 (由 ESP32 硬件自动控制 RTS 翻转)
    err = uart_set_mode(RS485_UART_NUM, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置RS485模式失败: %s", esp_err_to_name(err));
        return err;
    }

    // 第4步：注册描述符表 (满足 esp-modbus v2.1.3 启动要求: mbm_param_descriptor_size >= 1)
    err = mbc_master_set_descriptor(master_handler, device_parameters, num_device_parameters);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "注册描述符表失败: %s", esp_err_to_name(err));
        return err;
    }

    // 第5步：启动协议栈
    err = mbc_master_start(master_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启动协议栈失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Modbus Master 通用协议栈初始化成功 (波特率: %lu)!", (unsigned long)baudrate);
    return ESP_OK;
}

esp_err_t modbus_master_init(void)
{
    return modbus_master_init_baud(115200);
}

esp_err_t modbus_master_reset(void)
{
    ESP_LOGW(TAG, "重置 Modbus Master 协议栈状态...");
    if (master_handler != NULL) {
        mbc_master_delete(master_handler);
        master_handler = NULL;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return modbus_master_init();
}

/* ========================================================================= */
/*                   2. 通用动态 Modbus 请求 API 实现                         */
/* ========================================================================= */

esp_err_t modbus_master_read_holding_registers(uint8_t slave_id,
                                               uint16_t reg_addr,
                                               uint16_t reg_num,
                                               uint16_t *dest_buf)
{
    if (dest_buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (slave_id < 1 || slave_id > 247) {
        return ESP_ERR_INVALID_ARG;
    }
    if (reg_num == 0 || reg_num > 125) {
        return ESP_ERR_INVALID_ARG;
    }

    if (master_handler == NULL) {
        esp_err_t init_err = modbus_master_init();
        if (init_err != ESP_OK) {
            return init_err;
        }
    }

    mb_param_request_t request = {
        .slave_addr = slave_id,
        .command    = 0x03,  // MB_FUNC_READ_HOLDING_REGISTER
        .reg_start  = reg_addr,
        .reg_size   = reg_num
    };

    return mbc_master_send_request(master_handler, &request, (void *)dest_buf);
}

esp_err_t modbus_master_read_input_registers(uint8_t slave_id,
                                             uint16_t reg_addr,
                                             uint16_t reg_num,
                                             uint16_t *dest_buf)
{
    if (dest_buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (slave_id < 1 || slave_id > 247) {
        return ESP_ERR_INVALID_ARG;
    }
    if (reg_num == 0 || reg_num > 125) {
        return ESP_ERR_INVALID_ARG;
    }

    if (master_handler == NULL) {
        esp_err_t init_err = modbus_master_init();
        if (init_err != ESP_OK) {
            return init_err;
        }
    }

    mb_param_request_t request = {
        .slave_addr = slave_id,
        .command    = 0x04,  // MB_FUNC_READ_INPUT_REGISTER
        .reg_start  = reg_addr,
        .reg_size   = reg_num
    };

    return mbc_master_send_request(master_handler, &request, (void *)dest_buf);
}

esp_err_t modbus_master_write_single_register(uint8_t slave_id,
                                              uint16_t reg_addr,
                                              uint16_t value)
{
    if (slave_id < 1 || slave_id > 247) {
        return ESP_ERR_INVALID_ARG;
    }

    if (master_handler == NULL) {
        esp_err_t init_err = modbus_master_init();
        if (init_err != ESP_OK) {
            return init_err;
        }
    }

    mb_param_request_t request = {
        .slave_addr = slave_id,
        .command    = 0x06,  // MB_FUNC_WRITE_REGISTER
        .reg_start  = reg_addr,
        .reg_size   = 1
    };

    return mbc_master_send_request(master_handler, &request, (void *)&value);
}

esp_err_t modbus_master_write_multiple_registers(uint8_t slave_id,
                                                 uint16_t reg_addr,
                                                 uint16_t reg_num,
                                                 const uint16_t *src_buf)
{
    if (src_buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (slave_id < 1 || slave_id > 247) {
        return ESP_ERR_INVALID_ARG;
    }
    if (reg_num == 0 || reg_num > 123) {
        return ESP_ERR_INVALID_ARG;
    }

    if (master_handler == NULL) {
        esp_err_t init_err = modbus_master_init();
        if (init_err != ESP_OK) {
            return init_err;
        }
    }

    mb_param_request_t request = {
        .slave_addr = slave_id,
        .command    = 0x10,  // MB_FUNC_WRITE_MULTIPLE_REGISTERS
        .reg_start  = reg_addr,
        .reg_size   = reg_num
    };

    return mbc_master_send_request(master_handler, &request, (void *)src_buf);
}

/* ========================================================================= */
/*                   3. 兼容旧版本业务 API (路由到新通用接口)                 */
/* ========================================================================= */

esp_err_t modbus_master_read_all(modbus_legacy_data_t *out_data)
{
    if (out_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw_regs[5] = {0};

    // 调用通用接口读取从站 0x02 起始 0x0010 的 5 个保持寄存器
    esp_err_t err = modbus_master_read_holding_registers(0x02, 0x0010, 5, raw_regs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "从站数据读取失败: %s", esp_err_to_name(err));
        return err;
    }

    // 解析转换物理量
    out_data->temperature = (float)raw_regs[0] / 10.0f;
    out_data->humidity    = (float)raw_regs[1] / 10.0f;
    out_data->status      = raw_regs[2];
    out_data->temp_limit  = (float)raw_regs[3] / 10.0f;
    out_data->humi_limit  = (float)raw_regs[4] / 10.0f;

    return ESP_OK;
}

esp_err_t modbus_master_write_temp_limit(float limit)
{
    uint16_t raw_val = (uint16_t)(limit * 10.0f + 0.5f);
    esp_err_t err = modbus_master_write_single_register(0x02, 0x0013, raw_val);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "写入温度上限成功: %.1f ℃ (寄存器值: %d)", limit, raw_val);
    } else {
        ESP_LOGE(TAG, "写入温度上限失败: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t modbus_master_write_humi_limit(float limit)
{
    uint16_t raw_val = (uint16_t)(limit * 10.0f + 0.5f);
    esp_err_t err = modbus_master_write_single_register(0x02, 0x0014, raw_val);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "写入湿度下限成功: %.1f %% (寄存器值: %d)", limit, raw_val);
    } else {
        ESP_LOGE(TAG, "写入湿度下限失败: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t modbus_master_write_status(uint16_t status)
{
    esp_err_t err = modbus_master_write_single_register(0x02, 0x0012, status);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "写入设备状态成功: %d", status);
    } else {
        ESP_LOGE(TAG, "写入设备状态失败: %s", esp_err_to_name(err));
    }
    return err;
}