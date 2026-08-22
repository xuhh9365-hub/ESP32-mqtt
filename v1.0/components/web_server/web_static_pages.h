#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t static_index_html_handler(httpd_req_t *req);
esp_err_t static_style_css_handler(httpd_req_t *req);
esp_err_t static_api_js_handler(httpd_req_t *req);
esp_err_t static_config_editor_js_handler(httpd_req_t *req);
esp_err_t static_app_js_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif
