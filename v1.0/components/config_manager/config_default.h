#ifndef __CONFIG_DEFAULT_H__
#define __CONFIG_DEFAULT_H__

/**
 * @brief Phase 1 内置默认设备配置 JSON 字符串
 *        严格符合 docs/data_structure.md 规范
 *        - temperature_sensor: 从站 2, 采集寄存器 16 (0x0010), 控制测点 temp_limit (写寄存器 19, 0x0013)
 *        - humidity_sensor:    从站 2, 采集寄存器 17 (0x0011), 控制测点 humi_limit (写寄存器 20, 0x0014)
 */
static const char DEFAULT_DEVICE_CONFIG_JSON[] = 
"{\n"
"  \"devices\": [\n"
"    {\n"
"      \"name\": \"temperature_sensor\",\n"
"      \"slave_id\": 2,\n"
"      \"register\": {\n"
"        \"address\": 16,\n"
"        \"type\": \"holding\"\n"
"      },\n"
"      \"data\": {\n"
"        \"type\": \"uint16\",\n"
"        \"scale\": 0.1\n"
"      },\n"
"      \"period\": 1000,\n"
"      \"metrics\": [\n"
"        {\n"
"          \"name\": \"temp_limit\",\n"
"          \"write_register\": 19,\n"
"          \"scale\": 0.1,\n"
"          \"min\": 0.0,\n"
"          \"max\": 100.0\n"
"        }\n"
"      ]\n"
"    },\n"
"    {\n"
"      \"name\": \"humidity_sensor\",\n"
"      \"slave_id\": 2,\n"
"      \"register\": {\n"
"        \"address\": 17,\n"
"        \"type\": \"holding\"\n"
"      },\n"
"      \"data\": {\n"
"        \"type\": \"uint16\",\n"
"        \"scale\": 0.1\n"
"      },\n"
"      \"period\": 1000,\n"
"      \"metrics\": [\n"
"        {\n"
"          \"name\": \"humi_limit\",\n"
"          \"write_register\": 20,\n"
"          \"scale\": 0.1,\n"
"          \"min\": 0.0,\n"
"          \"max\": 100.0\n"
"        }\n"
"      ]\n"
"    }\n"
"  ]\n"
"}";

#endif /* __CONFIG_DEFAULT_H__ */
