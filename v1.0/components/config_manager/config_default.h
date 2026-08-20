#ifndef __CONFIG_DEFAULT_H__
#define __CONFIG_DEFAULT_H__

/**
 * @brief Phase 1 内置默认设备配置 JSON 字符串
 *        严格符合 docs/data_structure.md 规范
 */
static const char DEFAULT_DEVICE_CONFIG_JSON[] = 
"{\n"
"  \"devices\": [\n"
"    {\n"
"      \"name\": \"temperature_sensor\",\n"
"      \"slave_id\": 1,\n"
"      \"register\": {\n"
"        \"address\": 16,\n"
"        \"type\": \"holding\"\n"
"      },\n"
"      \"data\": {\n"
"        \"type\": \"uint16\",\n"
"        \"scale\": 0.1\n"
"      },\n"
"      \"period\": 1000\n"
"    },\n"
"    {\n"
"      \"name\": \"humidity_sensor\",\n"
"      \"slave_id\": 1,\n"
"      \"register\": {\n"
"        \"address\": 17,\n"
"        \"type\": \"holding\"\n"
"      },\n"
"      \"data\": {\n"
"        \"type\": \"uint16\",\n"
"        \"scale\": 0.1\n"
"      },\n"
"      \"period\": 1000\n"
"    }\n"
"  ]\n"
"}";

#endif /* __CONFIG_DEFAULT_H__ */
