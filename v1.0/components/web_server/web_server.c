#include "web_server.h"
#include "web_static_pages.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "config_manager.h"
#include "config_validator.h"
#include "data_manager.h"
#include "data_formatter.h"
#include "mqtt_sink.h"
#include <string.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t s_server = NULL;

#define JSON_RESP_BUF_SIZE      2048

/* ========================================================================= */
/*                          1. REST API 路由处理函数                          */
/* ========================================================================= */


/**
 * @brief GET /api/v1/system/status - 查询系统状态与健康指标
 */
static esp_err_t api_system_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddStringToObject(root, "gateway_id", "esp32_gateway_001");
    cJSON_AddStringToObject(root, "firmware_version", "v1.0");
    cJSON_AddNumberToObject(root, "config_version", (double)config_manager_get_version());
    cJSON_AddNumberToObject(root, "uptime_sec", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "free_heap_bytes", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap_bytes", (double)esp_get_minimum_free_heap_size());
    cJSON_AddStringToObject(root, "wifi_status", "connected");
    cJSON_AddStringToObject(root, "mqtt_status", mqtt_sink_is_connected() ? "connected" : "disconnected");

    char resp_buf[512];
    cJSON_bool printed = cJSON_PrintPreallocated(root, resp_buf, sizeof(resp_buf), false);
    cJSON_Delete(root);

    if (!printed) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, resp_buf, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief GET /api/v1/data/snapshot - 查询网关实时 LVC 快照数据
 */
static esp_err_t api_data_snapshot_handler(httpd_req_t *req)
{
    static gateway_data_t s_gw_snapshot;
    esp_err_t err = data_manager_get_gateway_snapshot(&s_gw_snapshot);
    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *json_buf = (char *)malloc(JSON_RESP_BUF_SIZE);
    if (json_buf == NULL) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    err = data_format_json(&s_gw_snapshot, json_buf, JSON_RESP_BUF_SIZE);
    if (err != ESP_OK) {
        free(json_buf);
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t ret = httpd_resp_send(req, json_buf, HTTPD_RESP_USE_STRLEN);
    free(json_buf);
    return ret;
}

/**
 * @brief GET /api/v1/config - 查询当前运行配置
 */
static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    char *json_buf = (char *)malloc(JSON_RESP_BUF_SIZE);
    if (json_buf == NULL) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = config_export_to_json(json_buf, JSON_RESP_BUF_SIZE);
    if (err != ESP_OK) {
        free(json_buf);
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t ret = httpd_resp_send(req, json_buf, HTTPD_RESP_USE_STRLEN);
    free(json_buf);
    return ret;
}

/**
 * @brief POST /api/v1/config/check - 配置 Dry-run 合法性深度校验 (不落盘，不修改运行态)
 */
static esp_err_t api_config_check_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= JSON_RESP_BUF_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    char *req_buf = (char *)malloc(JSON_RESP_BUF_SIZE);
    if (req_buf == NULL) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    int cur_len = 0;
    int received = 0;
    int timeout_count = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, req_buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                timeout_count++;
                if (timeout_count < 100) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
            }
            free(req_buf);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    req_buf[total_len] = '\0';

    config_validation_result_t val_res;
    esp_err_t err = config_manager_validate_json(req_buf, &val_res);
    free(req_buf);

    cJSON *resp_json = cJSON_CreateObject();
    if (resp_json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (err == ESP_OK) {
        cJSON_AddNumberToObject(resp_json, "code", 0);
        cJSON_AddStringToObject(resp_json, "message", "validation success");
    } else {
        cJSON_AddNumberToObject(resp_json, "code", 4001);
        char msg_buf[256];
        snprintf(msg_buf, sizeof(msg_buf), "validation failed: %s", 
                 val_res.err_detail[0] ? val_res.err_detail : "invalid configuration");
        cJSON_AddStringToObject(resp_json, "message", msg_buf);
        if (val_res.err_target[0] != '\0') {
            cJSON_AddStringToObject(resp_json, "error_target", val_res.err_target);
        }
    }

    char *resp_str = cJSON_PrintUnformatted(resp_json);
    cJSON_Delete(resp_json);
    if (resp_str == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t ret = httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    return ret;
}

/**
 * @brief POST /api/v1/config/apply - 配置在线热应用与 NVS 持久化
 */
static esp_err_t api_config_apply_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "===> 进入 api_config_apply_handler, content_len = %d", req->content_len);
    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= JSON_RESP_BUF_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    char *req_buf = (char *)malloc(JSON_RESP_BUF_SIZE);
    if (req_buf == NULL) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    int cur_len = 0;
    int received = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, req_buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            free(req_buf);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    req_buf[total_len] = '\0';
    ESP_LOGI(TAG, "收到 POST /api/v1/config/apply, 长度: %d 字节, 内容: %s", total_len, req_buf);

    // 阶段 1 & 2: 严格配置合法性检查
    config_validation_result_t val_res;
    esp_err_t val_err = config_manager_validate_json(req_buf, &val_res);
    if (val_err != ESP_OK) {
        free(req_buf);
        cJSON *resp_json = cJSON_CreateObject();
        if (resp_json == NULL) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cJSON_AddNumberToObject(resp_json, "code", 4001);
        char msg_buf[256];
        snprintf(msg_buf, sizeof(msg_buf), "validation failed: %s", 
                 val_res.err_detail[0] ? val_res.err_detail : "invalid configuration");
        cJSON_AddStringToObject(resp_json, "message", msg_buf);
        if (val_res.err_target[0] != '\0') {
            cJSON_AddStringToObject(resp_json, "error_target", val_res.err_target);
        }

        char *resp_str = cJSON_PrintUnformatted(resp_json);
        cJSON_Delete(resp_json);
        if (resp_str == NULL) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        httpd_resp_set_type(req, "application/json; charset=utf-8");
        esp_err_t ret = httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
        free(resp_str);
        return ret;
    }

    // 阶段 3 & 4 & 5: 生成新运行配置 -> 设备热重载 -> NVS 持久化
    uint32_t applied_version = 0;
    esp_err_t apply_err = config_manager_apply_json(req_buf, &applied_version);
    free(req_buf);

    cJSON *resp_json = cJSON_CreateObject();
    if (resp_json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (apply_err == ESP_OK) {
        cJSON_AddNumberToObject(resp_json, "code", 0);
        cJSON_AddStringToObject(resp_json, "message", "apply success");
        cJSON_AddNumberToObject(resp_json, "version", (double)applied_version);
        cJSON_AddNumberToObject(resp_json, "device_count", (double)config_get_device_num());
    } else {
        cJSON_AddNumberToObject(resp_json, "code", 5001);
        cJSON_AddStringToObject(resp_json, "message", "storage failed");
    }

    char *resp_str = cJSON_PrintUnformatted(resp_json);
    cJSON_Delete(resp_json);
    if (resp_str == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t ret = httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    return ret;
}

/**
 * @brief POST /api/v1/device/remove - 移除指定设备
 */
static esp_err_t api_device_remove_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    char req_buf[512];
    int received = httpd_req_recv(req, req_buf, sizeof(req_buf) - 1);
    if (received <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    req_buf[received] = '\0';

    cJSON *root = cJSON_Parse(req_buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *name_item = cJSON_GetObjectItem(root, "name");
    if (!cJSON_IsString(name_item) || name_item->valuestring == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'name' field");
        return ESP_FAIL;
    }

    const char *target_name = name_item->valuestring;
    ESP_LOGI(TAG, "收到删除设备请求: %s", target_name);

    int dev_num = config_get_device_num();
    if (dev_num <= 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No devices configured");
        return ESP_FAIL;
    }

    cJSON *new_root = cJSON_CreateObject();
    cJSON_AddNumberToObject(new_root, "version", (double)(config_manager_get_version() + 1));
    cJSON *dev_arr = cJSON_CreateArray();

    bool found = false;
    for (int i = 0; i < dev_num; i++) {
        device_config_t d;
        if (config_get_device(i, &d) == ESP_OK) {
            if (strncmp(d.name, target_name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
                found = true;
                continue; // 过滤删除
            }
            cJSON *d_item = cJSON_CreateObject();
            cJSON_AddStringToObject(d_item, "name", d.name);
            cJSON_AddNumberToObject(d_item, "slave_id", d.slave_id);
            cJSON_AddNumberToObject(d_item, "period", d.period);
            
            cJSON *reg_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(reg_obj, "address", d.register_addr);
            cJSON_AddStringToObject(reg_obj, "type", d.reg_type);
            cJSON_AddItemToObject(d_item, "register", reg_obj);

            cJSON *data_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(data_obj, "type", d.data_type);
            cJSON_AddNumberToObject(data_obj, "scale", (double)d.scale);
            cJSON_AddItemToObject(d_item, "data", data_obj);

            if (d.metric_count > 0) {
                cJSON *m_arr = cJSON_CreateArray();
                for (uint8_t m = 0; m < d.metric_count; m++) {
                    cJSON *m_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(m_obj, "name", d.metrics[m].metric_name);
                    cJSON_AddNumberToObject(m_obj, "write_register", d.metrics[m].write_register);
                    cJSON_AddNumberToObject(m_obj, "scale", (double)d.metrics[m].scale);
                    cJSON_AddNumberToObject(m_obj, "min", (double)d.metrics[m].min_value);
                    cJSON_AddNumberToObject(m_obj, "max", (double)d.metrics[m].max_value);
                    cJSON_AddItemToArray(m_arr, m_obj);
                }
                cJSON_AddItemToObject(d_item, "metrics", m_arr);
            }

            cJSON_AddItemToArray(dev_arr, d_item);
        }
    }
    cJSON_Delete(root);

    if (!found) {
        cJSON_Delete(new_root);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Device not found");
        return ESP_FAIL;
    }

    cJSON_AddItemToObject(new_root, "devices", dev_arr);
    char *new_json_str = cJSON_PrintUnformatted(new_root);
    cJSON_Delete(new_root);

    if (new_json_str == NULL) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    uint32_t new_version = 0;
    esp_err_t apply_err = config_manager_apply_json(new_json_str, &new_version);
    free(new_json_str);

    cJSON *resp_json = cJSON_CreateObject();
    if (apply_err == ESP_OK) {
        cJSON_AddNumberToObject(resp_json, "code", 0);
        cJSON_AddStringToObject(resp_json, "message", "device removed");
        cJSON_AddNumberToObject(resp_json, "version", (double)new_version);
        cJSON_AddNumberToObject(resp_json, "device_count", (double)config_get_device_num());
    } else {
        cJSON_AddNumberToObject(resp_json, "code", 5001);
        cJSON_AddStringToObject(resp_json, "message", "remove failed");
    }

    char *resp_str = cJSON_PrintUnformatted(resp_json);
    cJSON_Delete(resp_json);
    if (resp_str == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t ret = httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    return ret;
}

/* ========================================================================= */
/*                          3. 路由表与服务器生命周期                         */
/* ========================================================================= */

static const httpd_uri_t s_uri_root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = static_index_html_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_uri_style_css = {
    .uri       = "/style.css",
    .method    = HTTP_GET,
    .handler   = static_style_css_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_uri_api_js = {
    .uri       = "/api.js",
    .method    = HTTP_GET,
    .handler   = static_api_js_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_uri_config_editor_js = {
    .uri       = "/config_editor.js",
    .method    = HTTP_GET,
    .handler   = static_config_editor_js_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_uri_app_js = {
    .uri       = "/app.js",
    .method    = HTTP_GET,
    .handler   = static_app_js_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_uri_system_status = {
    .uri       = "/api/v1/system/status",
    .method    = HTTP_GET,
    .handler   = api_system_status_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_uri_data_snapshot = {
    .uri       = "/api/v1/data/snapshot",
    .method    = HTTP_GET,
    .handler   = api_data_snapshot_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_uri_config_get = {
    .uri       = "/api/v1/config",
    .method    = HTTP_GET,
    .handler   = api_config_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_uri_config_check = {
    .uri       = "/api/v1/config/check",
    .method    = HTTP_POST,
    .handler   = api_config_check_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_uri_config_apply = {
    .uri       = "/api/v1/config/apply",
    .method    = HTTP_POST,
    .handler   = api_config_apply_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_uri_device_remove = {
    .uri       = "/api/v1/device/remove",
    .method    = HTTP_POST,
    .handler   = api_device_remove_handler,
    .user_ctx  = NULL
};

esp_err_t web_server_init(void)
{
    if (s_server != NULL) {
        ESP_LOGW(TAG, "Web Server 已经在运行中");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "正在启动 HTTP Web Server 基础框架 (监听端口: 80)...");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;           /* 8KB 栈空间确保 cJSON 与 HTTP 解析安全 */
    config.task_priority = 3;          /* 优先级 3 (低于采集 5 与数据中枢 4) */
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册静态资源 Handlers
    httpd_register_uri_handler(s_server, &s_uri_root);
    httpd_register_uri_handler(s_server, &s_uri_style_css);
    httpd_register_uri_handler(s_server, &s_uri_api_js);
    httpd_register_uri_handler(s_server, &s_uri_config_editor_js);
    httpd_register_uri_handler(s_server, &s_uri_app_js);

    // 注册 REST API Handlers
    httpd_register_uri_handler(s_server, &s_uri_system_status);
    httpd_register_uri_handler(s_server, &s_uri_data_snapshot);
    httpd_register_uri_handler(s_server, &s_uri_config_get);
    httpd_register_uri_handler(s_server, &s_uri_config_check);
    httpd_register_uri_handler(s_server, &s_uri_config_apply);
    httpd_register_uri_handler(s_server, &s_uri_device_remove);

    ESP_LOGI(TAG, "HTTP Web Server 启动成功! 静态页面 SPA 及 REST API 已就绪 (端口: 80)");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "正在停止 Web Server...");
    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    return err;
}

bool web_server_is_running(void)
{
    return (s_server != NULL);
}
