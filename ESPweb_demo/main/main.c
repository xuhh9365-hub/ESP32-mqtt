#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "cJSON.h"
#include "led.h"
#include "key.h"
#include "rs485.h"

static const char *TAG = "APP_CONFIG";

#define LITTLEFS_MOUNT_POINT   "/littlefs"
#define LITTLEFS_PARTITION_TAG "storage"
#define CONFIG_FILE_PATH       "/littlefs/config.json"

/* 2. 默认配置 JSON 字符串宏定义 */
#define DEFAULT_CONFIG_JSON "{\n" \
    "  \"config_version\": 1,\n" \
    "  \"gateway_name\": \"ESP32-GW-001\",\n" \
    "  \"serial_baud\": 115200,\n" \
    "  \"sensor_count\": 2,\n" \
    "  \"sensors\": [\n" \
    "    {\"key\": \"temp_1\", \"slave_addr\": 1, \"reg_addr\": 16},\n" \
    "    {\"key\": \"humi_1\", \"slave_addr\": 1, \"reg_addr\": 17}\n" \
    "  ]\n" \
    "}"

/**
 * @brief 1. 初始化 LittleFS（分区标签 "storage"，挂载点 "/littlefs"）
 */
esp_err_t init_littlefs(void)
{
    ESP_LOGI(TAG, "正在初始化 LittleFS (挂载点: %s, 分区标签: %s)...", LITTLEFS_MOUNT_POINT, LITTLEFS_PARTITION_TAG);

    esp_vfs_littlefs_conf_t conf = {
        .base_path = LITTLEFS_MOUNT_POINT,
        .partition_label = LITTLEFS_PARTITION_TAG,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "挂载或格式化 LittleFS 失败");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "未找到名为 '%s' 的分区", LITTLEFS_PARTITION_TAG);
        } else {
            ESP_LOGE(TAG, "初始化 LittleFS 失败 (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取 LittleFS 分区信息失败 (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LittleFS 挂载成功! 总空间: %u 字节, 已使用: %u 字节", (unsigned int)total, (unsigned int)used);
    }

    return ESP_OK;
}

/**
 * @brief 5. 保存配置函数 save_config(const char *json_str)
 *   - 先用 cJSON_Parse 验证 JSON 合法性
 *   - 合法 → 写入文件 → 返回 ESP_OK
 *   - 非法 → 不写入 → 返回 ESP_ERR_INVALID_ARG
 * 
 * @param json_str JSON 格式的字符串
 * @return esp_err_t 
 */
esp_err_t save_config(const char *json_str)
{
    if (json_str == NULL) {
        ESP_LOGE(TAG, "save_config 失败: json_str 为 NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 1. 先用 cJSON_Parse 验证 JSON 合法性
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "save_config: JSON 格式非法，错误位置附近: %s", error_ptr);
        } else {
            ESP_LOGE(TAG, "save_config: JSON 格式非法，解析失败");
        }
        return ESP_ERR_INVALID_ARG;
    }

    // 语法合法，释放解析生成的临时 cJSON 对象
    cJSON_Delete(root);

    // 2. 合法 → 写入文件
    FILE *f = fopen(CONFIG_FILE_PATH, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "save_config 失败: 无法打开文件 %s 进行写入", CONFIG_FILE_PATH);
        return ESP_FAIL;
    }

    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "save_config 失败: 写入数据不完整 (%u/%u 字节)", (unsigned int)written, (unsigned int)len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "save_config: 配置已成功保存到 %s (共 %u 字节)", CONFIG_FILE_PATH, (unsigned int)written);
    return ESP_OK;
}

/**
 * @brief 从文件读取整个 JSON 配置字符串（调用方需 free 返回的内存）
 */
static char *read_config_file(void)
{
    FILE *f = fopen(CONFIG_FILE_PATH, "r");
    if (f == NULL) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    char *buffer = (char *)malloc(size + 1);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "read_config_file: 内存分配失败 (大小: %ld)", size);
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, f);
    buffer[read_bytes] = '\0';
    fclose(f);

    return buffer;
}

/**
 * @brief 3. 首次启动检测：如果 /littlefs/config.json 不存在 → 写入默认配置
 */
esp_err_t check_and_init_config(void)
{
    struct stat st;
    if (stat(CONFIG_FILE_PATH, &st) == 0) {
        ESP_LOGI(TAG, "检测到配置文件已存在: %s (大小: %ld 字节)", CONFIG_FILE_PATH, (long)st.st_size);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "未检测到配置文件 %s，执行首次启动初始化，正在写入默认配置...", CONFIG_FILE_PATH);
    return save_config(DEFAULT_CONFIG_JSON);
}

/**
 * @brief 4. 每次启动：读取 config.json → cJSON 解析 → 串口打印信息：
 *   - config_version
 *   - gateway_name  
 *   - sensor_count
 *   - 遍历 sensors 数组，逐个打印 key + slave_addr + reg_addr
 */
esp_err_t load_and_print_config(void)
{
    char *json_str = read_config_file();
    if (json_str == NULL) {
        ESP_LOGE(TAG, "load_and_print_config: 读取配置文件 %s 失败", CONFIG_FILE_PATH);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (root == NULL) {
        ESP_LOGE(TAG, "load_and_print_config: cJSON 解析失败");
        return ESP_ERR_INVALID_ARG;
    }

    // 获取并解析各字段
    cJSON *item_version = cJSON_GetObjectItem(root, "config_version");
    cJSON *item_gw_name = cJSON_GetObjectItem(root, "gateway_name");
    cJSON *item_baud    = cJSON_GetObjectItem(root, "serial_baud");
    cJSON *item_count   = cJSON_GetObjectItem(root, "sensor_count");
    cJSON *item_sensors = cJSON_GetObjectItem(root, "sensors");

    printf("\n======================================================\n");
    printf("              📋 网关配置信息 (config.json)           \n");
    printf("======================================================\n");
    
    if (cJSON_IsNumber(item_version)) {
        printf(" 🔹 配置版本 (config_version) : %d\n", item_version->valueint);
    } else {
        printf(" 🔹 配置版本 (config_version) : 未设置/类型错误\n");
    }

    if (cJSON_IsString(item_gw_name) && item_gw_name->valuestring) {
        printf(" 🔹 网关名称 (gateway_name)   : %s\n", item_gw_name->valuestring);
    } else {
        printf(" 🔹 网关名称 (gateway_name)   : 未设置/类型错误\n");
    }

    if (cJSON_IsNumber(item_baud)) {
        printf(" 🔹 串口波特率 (serial_baud)  : %d\n", item_baud->valueint);
    }

    if (cJSON_IsNumber(item_count)) {
        printf(" 🔹 传感器数量 (sensor_count) : %d\n", item_count->valueint);
    } else {
        printf(" 🔹 传感器数量 (sensor_count) : 未设置/类型错误\n");
    }

    if (cJSON_IsArray(item_sensors)) {
        int sensor_len = cJSON_GetArraySize(item_sensors);
        printf(" 🔹 传感器列表 (sensors 数组, 共 %d 个):\n", sensor_len);
        for (int i = 0; i < sensor_len; i++) {
            cJSON *sensor = cJSON_GetArrayItem(item_sensors, i);
            if (cJSON_IsObject(sensor)) {
                cJSON *key = cJSON_GetObjectItem(sensor, "key");
                cJSON *slave_addr = cJSON_GetObjectItem(sensor, "slave_addr");
                cJSON *reg_addr = cJSON_GetObjectItem(sensor, "reg_addr");

                const char *key_val = (cJSON_IsString(key) && key->valuestring) ? key->valuestring : "未知";
                int slave_val = (cJSON_IsNumber(slave_addr)) ? slave_addr->valueint : -1;
                int reg_val = (cJSON_IsNumber(reg_addr)) ? reg_addr->valueint : -1;

                printf("    [%d] key: %-8s | slave_addr: 0x%02X (%2d) | reg_addr: 0x%04X (%d)\n",
                       i, key_val, slave_val, slave_val, reg_val, reg_val);
            }
        }
    } else {
        printf(" 🔹 传感器列表 : 未找到有效数组\n");
    }
    printf("======================================================\n\n");

    cJSON_Delete(root);
    return ESP_OK;
}

static void LED_task(void *pvParameters)
{
    while(1)
    {
        LED_TOGGLE();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief 6. 在 app_main 中演示完整流程：
 *   加载 → 打印 → 修改一个值 → 保存 → 重新加载 → 再次打印（验证写入生效）
 */
void app_main(void)
{
    // 初始化板载 LED
    led_init();
    xTaskCreate(LED_task, "LED_task", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "==================== LittleFS 配置管理系统演示 ====================");

    // 1. 初始化 LittleFS
    ESP_ERROR_CHECK(init_littlefs());

    // 2. 首次启动检测（如果不存在则写入默认配置）
    check_and_init_config();

    // 3. 加载并打印配置
    ESP_LOGI(TAG, ">>> [流程 1] 加载当前配置并打印:");
    load_and_print_config();

    // 演示 save_config 的非法 JSON 校验拦截能力
    ESP_LOGI(TAG, ">>> [安全测试] 测试 save_config 校验非法 JSON 字符串...");
    const char *invalid_json = "{\"config_version\": 1, \"gateway_name\": }"; // 语法错误
    esp_err_t test_ret = save_config(invalid_json);
    if (test_ret == ESP_ERR_INVALID_ARG) {
        ESP_LOGI(TAG, ">>> 校验测试通过: save_config 成功拦截非法 JSON 并返回 ESP_ERR_INVALID_ARG\n");
    }

    // 4. 修改配置中的值
    ESP_LOGI(TAG, ">>> [流程 2] 修改配置: 将 gateway_name 修改为 'ESP32-GW-888-PRO'");
    char *json_str = read_config_file();
    if (json_str != NULL) {
        cJSON *root = cJSON_Parse(json_str);
        free(json_str);

        if (root != NULL) {
            // 修改网关名称
            cJSON_ReplaceItemInObject(root, "gateway_name", cJSON_CreateString("ESP32-GW-888-PRO"));

            // 也可以更新版本号
            cJSON *ver_item = cJSON_GetObjectItem(root, "config_version");
            if (cJSON_IsNumber(ver_item)) {
                cJSON_SetIntValue(ver_item, ver_item->valueint + 1);
            }

            // 序列化为格式化的 JSON 字符串
            char *modified_json = cJSON_Print(root);
            cJSON_Delete(root);

            if (modified_json != NULL) {
                // 5. 保存修改后的合法配置
                ESP_LOGI(TAG, ">>> [流程 3] 保存修改后的合法配置...");
                save_config(modified_json);
                free(modified_json);
            }
        }
    }

    // 6. 重新加载并再次打印（验证写入生效）
    ESP_LOGI(TAG, ">>> [流程 4] 重新加载 config.json 并打印（验证修改是否生效）:");
    load_and_print_config();

    ESP_LOGI(TAG, "==================== 演示完成，系统正常运行 ====================");
}
