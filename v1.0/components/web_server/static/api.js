/**
 * ============================================================================
 * ESP32 工业网关统一 REST API 客户端封装 (api.js)
 * 现场工程师页面与 ESP32 核心后端的数据桥梁
 * ============================================================================
 */
const GatewayAPI = {
  // 1. 获取网关当前运行设备配置
  async getConfig() {
    const resp = await fetch('/api/v1/config');
    if (!resp.ok) throw new Error('HTTP ' + resp.status);
    return await resp.json();
  },

  // 2. Dry-run 配置预校验 (仅测试，不写 NVS，不修改运行态)
  async checkConfig(data) {
    const resp = await fetch('/api/v1/config/check', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    });
    return await resp.json();
  },

  // 3. 配置热应用与 NVS 持久化
  async applyConfig(data) {
    const resp = await fetch('/api/v1/config/apply', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    });
    return await resp.json();
  },

  // 4. 删除指定设备
  async removeDevice(deviceName) {
    const resp = await fetch('/api/v1/device/remove', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: deviceName })
    });
    return await resp.json();
  },

  // 5. 获取网关实时遥测与数字孪生快照
  async getSnapshot() {
    const resp = await fetch('/api/v1/data/snapshot');
    if (!resp.ok) throw new Error('HTTP ' + resp.status);
    return await resp.json();
  },

  // 6. 获取网关系统状态 (内存/WiFi/MQTT/Uptime)
  async getStatus() {
    const resp = await fetch('/api/v1/system/status');
    if (!resp.ok) throw new Error('HTTP ' + resp.status);
    return await resp.json();
  }
};
window.GatewayAPI = GatewayAPI;
