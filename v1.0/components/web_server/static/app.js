/**
 * ============================================================================
 * ESP32 工业网关主程序逻辑 (app.js)
 * 负责 SPA Tab 路由切换、实时遥测快照轮询、系统状态监控
 * ============================================================================
 */

let g_activeTab = 'dashboard';
let g_pollTimer = null;

// 1. Tab 切换逻辑
function switchTab(tabId) {
  g_activeTab = tabId;
  document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.classList.toggle('active', btn.dataset.tab === tabId);
  });
  document.querySelectorAll('.tab-panel').forEach(panel => {
    panel.classList.toggle('active', panel.id === 'tab-' + tabId);
  });

  if (tabId === 'config') {
    loadAndRenderConfigTable();
  } else if (tabId === 'status') {
    refreshSystemStatus();
  } else if (tabId === 'dashboard') {
    refreshTelemetrySnapshot();
  }
}

// 2. 格式化运行时间 (秒 -> hh:mm:ss)
function formatUptime(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return [h, m, s].map(v => v.toString().padStart(2, '0')).join(':');
}

// 3. 刷新系统状态 (Tab 3 与 Header 徽章)
async function refreshSystemStatus() {
  try {
    const data = await GatewayAPI.getStatus();
    
    // 更新 Header 徽章
    const wifiBadge = document.getElementById('header-wifi-badge');
    if (wifiBadge) {
      const isConnected = data.wifi_status === 'connected';
      wifiBadge.className = 'badge ' + (isConnected ? 'badge-ok' : 'badge-err');
      wifiBadge.textContent = 'WiFi: ' + (isConnected ? '已连接' : '离线');
    }

    const mqttBadge = document.getElementById('header-mqtt-badge');
    if (mqttBadge) {
      const isConnected = data.mqtt_status === 'connected';
      mqttBadge.className = 'badge ' + (isConnected ? 'badge-ok' : 'badge-warn');
      mqttBadge.textContent = 'MQTT: ' + (isConnected ? '在线' : '离线');
    }

    const uptimeBadge = document.getElementById('header-uptime-badge');
    if (uptimeBadge) {
      uptimeBadge.textContent = '运行: ' + formatUptime(data.uptime_sec || 0);
    }

    // 更新 Tab 3 系统状态卡片内容
    const setVal = (id, val) => {
      const el = document.getElementById(id);
      if (el) el.textContent = val;
    };

    setVal('stat-gw-id', data.gateway_id || 'esp32_gateway_001');
    setVal('stat-fw-ver', data.firmware_version || 'v1.0');
    setVal('stat-cfg-ver', 'v' + (data.config_version || 1));
    setVal('stat-uptime', formatUptime(data.uptime_sec || 0) + ' (' + data.uptime_sec + 's)');
    
    const freeHeapKb = Math.round((data.free_heap_bytes || 0) / 1024);
    const minFreeHeapKb = Math.round((data.min_free_heap_bytes || 0) / 1024);
    setVal('stat-heap', freeHeapKb + ' KB (最低: ' + minFreeHeapKb + ' KB)');
    setVal('stat-wifi', data.wifi_status === 'connected' ? '🟢 已连接 (STA)' : '🔴 断开');
    setVal('stat-mqtt', data.mqtt_status === 'connected' ? '🟢 已连接 (EMQX)' : '🟡 未就绪');
  } catch (err) {
    console.warn('获取系统状态失败:', err);
  }
}

// 4. 刷新实时遥测与数字孪生快照 (Tab 1)
async function refreshTelemetrySnapshot() {
  const container = document.getElementById('telemetry-cards-container');
  if (!container) return;

  try {
    const data = await GatewayAPI.getSnapshot();
    const devices = data.devices || [];

    if (devices.length === 0) {
      container.innerHTML = '<div class=\"card\" style=\"grid-column:1/-1;text-align:center;color:var(--text-muted);padding:32px;\">当前没有活跃的采集设备，请切换至 [设备配置] 添加 Modbus 节点</div>';
      return;
    }

    let cardsHtml = '';
    devices.forEach(dev => {
      const isOnline = (dev.status === 0);
      const statusBadge = isOnline 
        ? '<span class=\"badge badge-ok\">● 通信正常</span>'
        : (dev.status === 1 ? '<span class=\"badge badge-err\">● 离线</span>' : '<span class=\"badge badge-warn\">● 通信超时</span>');

      let metricsHtml = '';
      const metrics = dev.metrics || [];
      if (metrics.length === 0) {
        metricsHtml = '<div class=\"metric-row\"><span class=\"metric-name\">测点数据</span><span class=\"metric-val\">--</span></div>';
      } else {
        metrics.forEach(m => {
          const valStr = (typeof m.value === 'number') ? m.value.toFixed(2) : (m.value !== undefined ? m.value : '--');
          const unitStr = m.unit || '';
          metricsHtml += '<div class=\"metric-row\">' +
            '<span class=\"metric-name\">' + m.name + '</span>' +
            '<span class=\"metric-val\">' + valStr + ' ' + unitStr + '</span>' +
          '</div>';
        });
      }

      cardsHtml += '<div class=\"card\">' +
        '<div class=\"card-header\">' +
          '<h2>' + dev.name + ' <small style=\"font-size:12px;color:var(--text-muted);font-weight:normal;\">(从站: ' + dev.slave + ')</small></h2>' +
          statusBadge +
        '</div>' +
        '<div class=\"card-body\">' +
          metricsHtml +
        '</div>' +
      '</div>';
    });

    container.innerHTML = cardsHtml;
  } catch (err) {
    console.warn('获取遥测快照失败:', err);
  }
}

// 5. 页面初始化
document.addEventListener('DOMContentLoaded', () => {
  // 绑定 Tab 按钮点击事件
  document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => switchTab(btn.dataset.tab));
  });

  // 初始加载
  refreshSystemStatus();
  refreshTelemetrySnapshot();

  // 启动后台定时轮询 (每 1.5 秒自动更新遥测与状态)
  g_pollTimer = setInterval(() => {
    refreshSystemStatus();
    if (g_activeTab === 'dashboard') {
      refreshTelemetrySnapshot();
    }
  }, 1500);
});
