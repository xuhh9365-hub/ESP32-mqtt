/**
 * ============================================================================
 * ESP32 工业网关设备配置编辑器 (config_editor.js)
 * 负责设备配置表渲染、增删改表单弹窗、Check -> Apply 完整闭环
 * ============================================================================
 */

let g_currentConfig = { version: 1, devices: [] };
let g_editingDeviceIndex = -1; // -1 表示新增，>=0 表示编辑现有设备

// 1. 加载并渲染设备配置表格
async function loadAndRenderConfigTable() {
  const tbody = document.getElementById('config-table-body');
  if (!tbody) return;
  
  try {
    const res = await GatewayAPI.getConfig();
    g_currentConfig = res;
    
    // 更新版本标签
    const verBadge = document.getElementById('config-version-badge');
    if (verBadge) verBadge.textContent = 'v' + (res.version || 1);
    
    tbody.innerHTML = '';
    const devices = res.devices || [];
    
    if (devices.length === 0) {
      tbody.innerHTML = '<tr><td colspan=\"6\" style=\"text-align:center;color:var(--text-muted);padding:24px;\">暂无配置设备，请点击上方按钮添加</td></tr>';
      return;
    }

    devices.forEach((dev, idx) => {
      const tr = document.createElement('tr');
      const regType = (dev.register && dev.register.type) ? dev.register.type : (dev.reg_type || 'holding');
      const regAddr = (dev.register && dev.register.address !== undefined) ? dev.register.address : (dev.address || dev.register_addr || 0);
      const dataType = (dev.data && dev.data.type) ? dev.data.type : (dev.data_type || 'uint16');
      const scale = (dev.data && dev.data.scale !== undefined) ? dev.data.scale : (dev.scale || 1.0);
      const period = dev.period || 1000;

      tr.innerHTML = 
        '<td><strong style=\"color:var(--text-main);\">' + dev.name + '</strong></td>' +
        '<td><span class=\"badge\">' + dev.slave_id + '</span></td>' +
        '<td><code style=\"color:var(--accent-cyan);\">0x' + regAddr.toString(16).toUpperCase().padStart(4, '0') + ' (' + regAddr + ')</code> <span style=\"color:var(--text-muted);font-size:12px;\">[' + regType + ']</span></td>' +
        '<td>' + dataType + ' (x' + scale + ')</td>' +
        '<td>' + period + ' ms</td>' +
        '<td>' +
          '<button class=\"btn btn-secondary btn-sm\" onclick=\"openEditDeviceModal(' + idx + ')\" style=\"margin-right:6px;\">编辑</button>' +
          '<button class=\"btn btn-danger btn-sm\" onclick=\"openDeleteDeviceModal(\'' + dev.name + '\')\">删除</button>' +
        '</td>';
      tbody.appendChild(tr);
    });
  } catch (err) {
    console.error('加载配置失败:', err);
    showToast('❌ 加载设备配置失败: ' + err.message, 'err');
  }
}

// 2. 打开添加设备弹窗
function openAddDeviceModal() {
  g_editingDeviceIndex = -1;
  document.getElementById('modal-device-title').textContent = '➕ 添加 Modbus 设备';
  document.getElementById('dev-form-name').value = '';
  document.getElementById('dev-form-name').disabled = false;
  document.getElementById('dev-form-slave').value = '2';
  document.getElementById('dev-form-addr').value = '16';
  document.getElementById('dev-form-regtype').value = 'holding';
  document.getElementById('dev-form-datatype').value = 'uint16';
  document.getElementById('dev-form-scale').value = '0.1';
  document.getElementById('dev-form-period').value = '1000';
  document.getElementById('check-result-box').style.display = 'none';

  document.getElementById('device-modal').classList.add('active');
}

// 3. 打开编辑设备弹窗
function openEditDeviceModal(idx) {
  g_editingDeviceIndex = idx;
  const dev = g_currentConfig.devices[idx];
  if (!dev) return;

  const regType = (dev.register && dev.register.type) ? dev.register.type : (dev.reg_type || 'holding');
  const regAddr = (dev.register && dev.register.address !== undefined) ? dev.register.address : (dev.address || dev.register_addr || 0);
  const dataType = (dev.data && dev.data.type) ? dev.data.type : (dev.data_type || 'uint16');
  const scale = (dev.data && dev.data.scale !== undefined) ? dev.data.scale : (dev.scale || 1.0);
  const period = dev.period || 1000;

  document.getElementById('modal-device-title').textContent = '✏️ 编辑 Modbus 设备: ' + dev.name;
  document.getElementById('dev-form-name').value = dev.name;
  document.getElementById('dev-form-name').disabled = true; // 设备名称不可更改
  document.getElementById('dev-form-slave').value = dev.slave_id;
  document.getElementById('dev-form-addr').value = regAddr;
  document.getElementById('dev-form-regtype').value = regType;
  document.getElementById('dev-form-datatype').value = dataType;
  document.getElementById('dev-form-scale').value = scale;
  document.getElementById('dev-form-period').value = period;
  document.getElementById('check-result-box').style.display = 'none';

  document.getElementById('device-modal').classList.add('active');
}

// 4. 关闭弹窗
function closeDeviceModal() {
  document.getElementById('device-modal').classList.remove('active');
}

// 5. 提取表单数据并构造全局待提交 JSON
function buildPayloadFromForm() {
  const name = document.getElementById('dev-form-name').value.trim();
  const slave_id = parseInt(document.getElementById('dev-form-slave').value, 10);
  const address = parseInt(document.getElementById('dev-form-addr').value, 10);
  const reg_type = document.getElementById('dev-form-regtype').value;
  const data_type = document.getElementById('dev-form-datatype').value;
  const scale = parseFloat(document.getElementById('dev-form-scale').value);
  const period = parseInt(document.getElementById('dev-form-period').value, 10);

  if (!name) throw new Error('设备名称不能为空');
  if (isNaN(slave_id) || slave_id < 1 || slave_id > 247) throw new Error('从站 ID 必须在 1 ~ 247 之间');
  if (isNaN(address) || address < 0 || address > 65535) throw new Error('寄存器地址必须在 0 ~ 65535 之间');
  if (isNaN(period) || period < 50 || period > 60000) throw new Error('采样周期必须在 50 ~ 60000 ms 之间');

  const targetDev = {
    name: name,
    slave_id: slave_id,
    register: { address: address, type: reg_type },
    data: { type: data_type, scale: isNaN(scale) ? 1.0 : scale },
    period: period
  };

  // 克隆现有配置列表
  let newDevices = JSON.parse(JSON.stringify(g_currentConfig.devices || []));

  if (g_editingDeviceIndex >= 0) {
    // 编辑现有设备
    newDevices[g_editingDeviceIndex] = targetDev;
  } else {
    // 新增设备：检查是否重名
    const dup = newDevices.find(d => d.name === name);
    if (dup) throw new Error('已存在同名设备 [' + name + ']');
    newDevices.push(targetDev);
  }

  return {
    version: (g_currentConfig.version || 1) + 1,
    devices: newDevices
  };
}

// 6. 执行 Dry-run 预校验 (测试配置)
async function onTestConfigClick() {
  const resBox = document.getElementById('check-result-box');
  resBox.style.display = 'block';
  resBox.innerHTML = '<span style=\"color:var(--text-muted);\">⏳ 正在向 ESP32 提交 Dry-run 预校验...</span>';

  try {
    const payload = buildPayloadFromForm();
    const res = await GatewayAPI.checkConfig(payload);
    if (res.code === 0) {
      resBox.innerHTML = '<span style=\"color:#34d399;font-weight:600;\">✅ Dry-run 校验通过！参数全部合法，可安全应用。</span>';
    } else {
      resBox.innerHTML = '<span style=\"color:#fb7185;font-weight:600;\">❌ 校验被拒绝: ' + (res.message || '未知错误') + ' (目标: ' + (res.error_target || '全局') + ')</span>';
    }
  } catch (err) {
    resBox.innerHTML = '<span style=\"color:#fb7185;font-weight:600;\">❌ 校验错误: ' + err.message + '</span>';
  }
}

// 7. 保存并应用配置 (严格遵循 Check -> Apply 闭环)
async function onSaveConfigClick() {
  const resBox = document.getElementById('check-result-box');
  resBox.style.display = 'block';
  resBox.innerHTML = '<span style=\"color:var(--text-muted);\">⏳ 正在校验并热应用至 ESP32...</span>';

  try {
    const payload = buildPayloadFromForm();

    // 阶段 1: 必须先通过 Check 校验
    const checkRes = await GatewayAPI.checkConfig(payload);
    if (checkRes.code !== 0) {
      resBox.innerHTML = '<span style=\"color:#fb7185;font-weight:600;\">❌ 校验失败，已终止应用: ' + checkRes.message + '</span>';
      showToast('❌ 配置校验失败: ' + checkRes.message, 'err');
      return;
    }

    // 阶段 2: 校验通过，执行 Apply
    const applyRes = await GatewayAPI.applyConfig(payload);
    if (applyRes.code === 0) {
      showToast('✅ 配置热应用成功! 生效版本: v' + applyRes.version + ', 活跃设备: ' + applyRes.device_count, 'ok');
      closeDeviceModal();
      await loadAndRenderConfigTable();
    } else {
      resBox.innerHTML = '<span style=\"color:#fb7185;font-weight:600;\">❌ 应用失败: ' + applyRes.message + '</span>';
      showToast('❌ 应用失败: ' + applyRes.message, 'err');
    }
  } catch (err) {
    resBox.innerHTML = '<span style=\"color:#fb7185;font-weight:600;\">❌ 错误: ' + err.message + '</span>';
    showToast('❌ 错误: ' + err.message, 'err');
  }
}

// 8. 打开删除确认弹窗
let g_deviceNameToDelete = '';
function openDeleteDeviceModal(name) {
  g_deviceNameToDelete = name;
  document.getElementById('delete-target-name').textContent = name;
  document.getElementById('delete-modal').classList.add('active');
}

function closeDeleteModal() {
  document.getElementById('delete-modal').classList.remove('active');
}

// 9. 确认删除设备
async function onConfirmDeleteDevice() {
  if (!g_deviceNameToDelete) return;
  try {
    const res = await GatewayAPI.removeDevice(g_deviceNameToDelete);
    if (res.code === 0) {
      showToast('✅ 设备 [' + g_deviceNameToDelete + '] 已成功移除并从 NVS 更新!', 'ok');
      closeDeleteModal();
      await loadAndRenderConfigTable();
    } else {
      showToast('❌ 删除失败: ' + res.message, 'err');
    }
  } catch (err) {
    showToast('❌ 删除出错: ' + err.message, 'err');
  }
}

// 10. 全局 Toast 提示
function showToast(msg, type) {
  const toast = document.getElementById('alert-toast');
  if (!toast) return;
  toast.textContent = msg;
  toast.className = 'alert-toast active ' + (type === 'ok' ? 'badge-ok' : (type === 'warn' ? 'badge-warn' : 'badge-err'));
  setTimeout(() => {
    toast.classList.remove('active');
  }, 4000);
}

window.loadAndRenderConfigTable = loadAndRenderConfigTable;
window.openAddDeviceModal = openAddDeviceModal;
window.openEditDeviceModal = openEditDeviceModal;
window.closeDeviceModal = closeDeviceModal;
window.onTestConfigClick = onTestConfigClick;
window.onSaveConfigClick = onSaveConfigClick;
window.openDeleteDeviceModal = openDeleteDeviceModal;
window.closeDeleteModal = closeDeleteModal;
window.onConfirmDeleteDevice = onConfirmDeleteDevice;
window.showToast = showToast;
