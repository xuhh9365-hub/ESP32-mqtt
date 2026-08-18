#include "wifi_sta.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

/* ========================== 配置宏 ========================== */

/* WiFi 接入点信息 —— 请修改为您实际的WiFi名称和密码 */
#define WIFI_SSID               "88888"
#define WIFI_PASS               "888888aa"

/* 最大重连次数 */
#define WIFI_MAXIMUM_RETRY      10

/* ========================== 私有变量 ========================== */

static const char *TAG = "WIFI_STA";

/* FreeRTOS EventGroup 用于同步等待WiFi连接结果 */
static EventGroupHandle_t s_wifi_event_group;

/* EventGroup 位定义 */
#define WIFI_CONNECTED_BIT      BIT0        /* WiFi 连接成功并获取IP */
#define WIFI_FAIL_BIT           BIT1        /* WiFi 连接失败 */

/* 当前重试计数 */
static int s_retry_num = 0;

/* ========================== 事件处理 ========================== */

/**
 * @brief       WiFi 和 IP 事件处理函数
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        /* WiFi 启动完成，发起连接 */
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi STA 启动，正在连接...");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        /* 断开连接，尝试重连 */
        if (s_retry_num < WIFI_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "WiFi 断开，正在重连... (%d/%d)", s_retry_num, WIFI_MAXIMUM_RETRY);
        }
        else
        {
            /* 超过最大重试次数 */
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi 连接失败，已达最大重试次数");
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        /* 获取到IP地址 */
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi 连接成功! IP地址: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ========================== 公开函数 ========================== */

/**
 * @brief       初始化WiFi Station模式并连接到指定AP
 */
esp_err_t wifi_sta_init(void)
{
    esp_err_t ret;

    /* 1. 初始化 NVS Flash（WiFi 驱动依赖 NVS） */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS 分区异常，擦除后重新初始化...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Flash 初始化完成");

    /* 2. 创建 EventGroup */
    s_wifi_event_group = xEventGroupCreate();

    /* 3. 初始化 TCP/IP 协议栈 和 默认事件循环 */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 4. 创建默认 WiFi STA 网络接口 */
    esp_netif_create_default_wifi_sta();

    /* 5. 使用默认参数初始化 WiFi 驱动 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 6. 注册事件处理函数 */
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifi_event_handler, NULL, &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &wifi_event_handler, NULL, &instance_got_ip));

    /* 7. 配置 WiFi 连接参数 */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,   /* 最低认证模式 */
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    /* 8. 启动 WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi 驱动初始化完成，等待连接...");

    /* 9. 阻塞等待连接结果 */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,       /* 不清除位 */
        pdFALSE,       /* 任意一位满足即返回 */
        portMAX_DELAY   /* 无限等待 */
    );

    /* 10. 判断连接结果 */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "===== WiFi 连接成功 =====");
        ESP_LOGI(TAG, "SSID: %s", WIFI_SSID);
        return ESP_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGE(TAG, "===== WiFi 连接失败 =====");
        return ESP_FAIL;
    }
    else
    {
        ESP_LOGE(TAG, "WiFi 连接出现未知错误");
        return ESP_FAIL;
    }
}
