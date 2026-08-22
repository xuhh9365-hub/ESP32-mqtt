#ifndef __DEVICE_MANAGER_TEST_H__
#define __DEVICE_MANAGER_TEST_H__

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 Device Manager API 稳定性与生命周期安全完整验证测试集 (Test 1 ~ Test 6)
 */
void device_manager_run_test_suite(void);

/**
 * @brief 启动 Step 7.3.3-2A config_manager_apply_update 测试集 (Test 1 ~ Test 3)
 */
void config_manager_run_test_suite(void);

/**
 * @brief 启动 Step 7.3.3-2B device_manager 动态热重载与调度平滑切换测试集 (Test 1 ~ Test 4)
 */
void device_manager_hot_reload_run_test_suite(void);

/**
 * @brief 启动 Step 7.3.4-1 data_manager 基础数据中枢测试集 (Test 1 ~ Test 3)
 */
void data_manager_run_test_suite(void);

/**
 * @brief 启动 Step 7.3.4-2 MQTT Sink 与物模型上传链路测试集 (Test 1 ~ Test 4)
 */
void mqtt_sink_run_test_suite(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEVICE_MANAGER_TEST_H__ */
