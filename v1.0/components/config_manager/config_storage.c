#include "config_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include <string.h>

static const char *TAG = "CONFIG_STORAGE";

esp_err_t config_storage_init(void)
{
    // 1. 初始化底层 NVS Flash 分区
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要擦除重置...");
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "擦除 NVS 分区失败: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS Flash 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 尝试打开命名空间验证访问权限
    nvs_handle_t handle;
    ret = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开 NVS 命名空间 [%s] 失败: %s", CONFIG_NVS_NAMESPACE, esp_err_to_name(ret));
        return ret;
    }
    nvs_close(handle);

    ESP_LOGI(TAG, "NVS 配置存储层初始化成功 (命名空间: %s)", CONFIG_NVS_NAMESPACE);
    return ESP_OK;
}

esp_err_t config_storage_save(
    const char *data,
    size_t len,
    uint32_t *out_version)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开 NVS 失败: %s", esp_err_to_name(err));
        return err;
    }

    // 1. 读取当前历史元数据获取当前版本号
    config_storage_meta_t old_meta;
    size_t meta_size = sizeof(config_storage_meta_t);
    uint32_t current_ver = 0;

    if (nvs_get_blob(handle, CONFIG_NVS_KEY_META, &old_meta, &meta_size) == ESP_OK) {
        if (old_meta.magic == CONFIG_STORAGE_MAGIC) {
            current_ver = old_meta.version;
        }
    }

    uint32_t next_ver = current_ver + 1;

    // 2. 计算数据载荷的 CRC32 校验码
    uint32_t crc = esp_rom_crc32_le(0, (const uint8_t *)data, (uint32_t)len);

    // 3. 掉电安全写入顺序：首先写入数据实体 (data)
    err = nvs_set_blob(handle, CONFIG_NVS_KEY_DATA, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写入 NVS 数据载荷失败: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    // 4. 组装新元数据并写入 (meta)
    config_storage_meta_t new_meta = {
        .magic = CONFIG_STORAGE_MAGIC,
        .version = next_ver,
        .data_len = (uint32_t)len,
        .crc32 = crc,
        .update_timestamp = esp_timer_get_time() / 1000000 // 秒
    };

    err = nvs_set_blob(handle, CONFIG_NVS_KEY_META, &new_meta, sizeof(config_storage_meta_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写入 NVS 元数据失败: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    // 5. 提交事务
    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 事务提交失败: %s", esp_err_to_name(err));
        return err;
    }

    if (out_version != NULL) {
        *out_version = next_ver;
    }

    ESP_LOGI(TAG, "✅ 配置持久化成功 -> 版本: %lu, 大小: %u 字节, CRC32: 0x%08lX",
             (unsigned long)next_ver, (unsigned int)len, (unsigned long)crc);

    return ESP_OK;
}

esp_err_t config_storage_load(
    char *buffer,
    size_t buffer_size,
    uint32_t *out_version)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "NVS 中未发现持久化配置 (未初始化或命名空间不存在)");
        return ESP_ERR_NOT_FOUND;
    }

    // 1. 读取头部元数据
    config_storage_meta_t meta;
    size_t meta_size = sizeof(config_storage_meta_t);
    err = nvs_get_blob(handle, CONFIG_NVS_KEY_META, &meta, &meta_size);
    if (err != ESP_OK || meta_size != sizeof(config_storage_meta_t)) {
        ESP_LOGD(TAG, "未读取到有效的 NVS 配置元数据");
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }

    // 2. 校验魔数
    if (meta.magic != CONFIG_STORAGE_MAGIC) {
        ESP_LOGE(TAG, "NVS 配置魔数不匹配 (期望 0x%08X, 实际 0x%08lX)", 
                 CONFIG_STORAGE_MAGIC, (unsigned long)meta.magic);
        nvs_close(handle);
        return ESP_ERR_INVALID_STATE;
    }

    // 3. 校验接收缓冲区容量
    if (meta.data_len >= buffer_size) {
        ESP_LOGE(TAG, "接收缓冲区容量不足 (需要 %lu 字节, 缓冲区 %u 字节)",
                 (unsigned long)meta.data_len + 1, (unsigned int)buffer_size);
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    // 4. 读取配置数据实体
    size_t read_len = meta.data_len;
    err = nvs_get_blob(handle, CONFIG_NVS_KEY_DATA, buffer, &read_len);
    nvs_close(handle);

    if (err != ESP_OK || read_len != meta.data_len) {
        ESP_LOGE(TAG, "读取 NVS 配置载荷失败: %s", esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    buffer[meta.data_len] = '\0'; // 保证字符串以 null 结尾

    // 5. 校验 CRC32 完整性
    uint32_t calc_crc = esp_rom_crc32_le(0, (const uint8_t *)buffer, meta.data_len);
    if (calc_crc != meta.crc32) {
        ESP_LOGE(TAG, "❌ NVS 配置 CRC32 校验失败! (存储: 0x%08lX, 计算: 0x%08lX)",
                 (unsigned long)meta.crc32, (unsigned long)calc_crc);
        return ESP_ERR_INVALID_CRC;
    }

    if (out_version != NULL) {
        *out_version = meta.version;
    }

    ESP_LOGI(TAG, "✅ 成功从 NVS 加载持久化配置 -> 版本: %lu, 大小: %lu 字节, CRC32: 0x%08lX",
             (unsigned long)meta.version, (unsigned long)meta.data_len, (unsigned long)meta.crc32);

    return ESP_OK;
}

esp_err_t config_storage_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_erase_key(handle, CONFIG_NVS_KEY_META);
    nvs_erase_key(handle, CONFIG_NVS_KEY_DATA);
    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "已清除 NVS 中持久化的配置数据 (已恢复出厂状态)");
    return err;
}
