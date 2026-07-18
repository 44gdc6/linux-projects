const AUTH_KEY = 'sensor-web-auth';
const AUTH_META_KEY = 'sensor-web-auth-meta';
const API_TOKEN_KEY = 'sensor-web-api-token';
const LOGIN_FAIL_KEY = 'sensor-web-login-fail';
const DEFAULT_USER = 'admin';
const DEFAULT_PASS = 'admin123';
const EMPTY_TEXT = '--';
const MAX_LOGIN_ATTEMPTS = 5;
const LOGIN_LOCK_MS = 60 * 1000;

let lastHistoryRows = [];
let lastHistorySensor = '';
let currentAlarms = [];

function isLoginPage() {
  return location.pathname.endsWith('login.html');
}

function pageUrl(fileName) {
  return `/${fileName}`;
}

function isAuthed() {
  return localStorage.getItem(AUTH_KEY) === 'ok';
}

function guardAuth() {
  if (!isLoginPage() && !isAuthed()) {
    location.href = pageUrl('login.html');
    return false;
  }
  if (isLoginPage() && isAuthed()) {
    location.href = pageUrl('index.html');
    return false;
  }
  return true;
}

async function getJson(url) {
  const resp = await fetch(url, {
    cache: 'no-store',
    headers: apiHeaders({ Accept: 'application/json' }),
  });
  if (!resp.ok) throw new Error(`${resp.status} ${resp.statusText}`);
  return resp.json();
}

function apiHeaders(base = {}) {
  const headers = { ...base };
  const token = localStorage.getItem(API_TOKEN_KEY);
  if (token) headers.Authorization = `Bearer ${token}`;
  return headers;
}

function valueText(value) {
  if (value === null || value === undefined || value === '') return EMPTY_TEXT;
  return String(value);
}

function numberText(value, digits = 2) {
  const number = Number(value);
  if (!Number.isFinite(number)) return EMPTY_TEXT;
  return number.toFixed(digits);
}

function setText(id, value) {
  const el = document.getElementById(id);
  if (el) el.textContent = valueText(value);
}

function setClass(el, className) {
  if (el) el.className = className;
}

function clearNode(node) {
  if (node) node.replaceChildren();
}

function makeEl(tag, options = {}, children = []) {
  const el = document.createElement(tag);
  if (options.className) el.className = options.className;
  if (options.text !== undefined) el.textContent = valueText(options.text);
  if (options.title !== undefined) el.title = valueText(options.title);
  if (options.type) el.type = options.type;
  if (options.href) el.href = options.href;
  if (options.disabled !== undefined) el.disabled = Boolean(options.disabled);
  if (options.ariaLabel) el.setAttribute('aria-label', options.ariaLabel);
  if (options.role) el.setAttribute('role', options.role);
  children.forEach(child => {
    if (child === null || child === undefined) return;
    el.append(child.nodeType ? child : document.createTextNode(valueText(child)));
  });
  return el;
}

function renderEmpty(container, message) {
  clearNode(container);
  container.append(makeEl('div', { className: 'row empty-row' }, [
    makeEl('span', { className: 'muted', text: message }),
  ]));
}

function timeText(ts) {
  const value = Number(ts);
  if (!Number.isFinite(value) || value <= 0) return EMPTY_TEXT;
  const ms = value > 1000000000000 ? value : value * 1000;
  return new Date(ms).toLocaleString();
}

function ageText(ts) {
  const value = Number(ts);
  if (!Number.isFinite(value) || value <= 0) return EMPTY_TEXT;
  const ms = value > 1000000000000 ? value : value * 1000;
  const seconds = Math.max(0, Math.round((Date.now() - ms) / 1000));
  if (seconds < 60) return `${seconds} 秒前`;
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) return `${minutes} 分钟前`;
  const hours = Math.round(minutes / 60);
  return `${hours} 小时前`;
}

function hasTelemetry(data) {
  return Boolean(data?.sensors && (data.sensors.cargo || data.sensors.cabin));
}

function alarmFlags(data) {
  if (!hasTelemetry(data)) return [];
  const cabin = data.sensors?.cabin || {};
  const cargo = data.sensors?.cargo || {};
  const vib = cargo.vibration || {};
  const gps = cabin.gps || {};
  const flags = [];
  if (cabin.alcohol_alarm) flags.push('酒精');
  if (vib.motion_alarm) flags.push('振动');
  if (gps.fence_alarm) flags.push('围栏');
  if (cargo.flame?.status) flags.push('火焰');
  return flags;
}

function riskText(data) {
  if (!hasTelemetry(data)) return '离线';
  if (alarmFlags(data).length) return '告警';
  if (data.online) return '正常';
  return '离线';
}

function riskClass(risk) {
  if (risk === '告警') return 'bad';
  if (risk === '正常') return 'ok';
  return 'neutral';
}

function updateOnlineBadge(online) {
  const badge = document.getElementById('onlineBadge');
  if (!badge) return;
  badge.textContent = online ? '在线' : '离线';
  badge.className = `status-pill ${online ? 'ok' : 'neutral'}`;
}

function updateRiskBadge(risk) {
  const riskEl = document.getElementById('risk');
  if (!riskEl) return;
  riskEl.textContent = risk;
  riskEl.className = `status-pill ${riskClass(risk)}`;
}

function currentPageKey() {
  const page = document.body?.dataset?.page;
  if (page) return page;
  const name = location.pathname.split('/').pop() || 'index.html';
  return {
    'index.html': 'dashboard',
    'history.html': 'history',
    'alarms.html': 'alarms',
    'config.html': 'config',
  }[name] || 'dashboard';
}

function markActiveNav() {
  const page = currentPageKey();
  document.querySelectorAll('[data-nav]').forEach(link => {
    const active = link.dataset.nav === page;
    link.classList.toggle('active', active);
    if (active) {
      link.setAttribute('aria-current', 'page');
    } else {
      link.removeAttribute('aria-current');
    }
  });
}

function readLoginState() {
  try {
    return JSON.parse(localStorage.getItem(LOGIN_FAIL_KEY) || '{}');
  } catch (_) {
    return {};
  }
}

function writeLoginState(state) {
  localStorage.setItem(LOGIN_FAIL_KEY, JSON.stringify(state));
}

function loginLockRemaining(state = readLoginState()) {
  const until = Number(state.lockedUntil || 0);
  return Math.max(0, until - Date.now());
}

function updateLoginStateUi() {
  const state = readLoginState();
  const remaining = loginLockRemaining(state);
  const form = document.getElementById('loginForm');
  const submit = document.getElementById('loginSubmit');
  const msg = document.getElementById('loginMessage');
  const status = document.getElementById('loginStatus');
  const locked = remaining > 0;

  if (submit) submit.disabled = locked;
  if (form) form.classList.toggle('is-locked', locked);
  if (status) {
    status.textContent = locked
      ? `登录已临时锁定，${Math.ceil(remaining / 1000)} 秒后重试`
      : '前端演示登录，仅保护页面入口';
  }
  if (locked && msg) msg.textContent = '失败次数过多，请稍后再试';
}

function recordLoginFailure() {
  const state = readLoginState();
  const attempts = Number(state.attempts || 0) + 1;
  const next = { attempts, lockedUntil: 0 };
  if (attempts >= MAX_LOGIN_ATTEMPTS) {
    next.attempts = 0;
    next.lockedUntil = Date.now() + LOGIN_LOCK_MS;
  }
  writeLoginState(next);
  return next;
}

function bindAuth() {
  const form = document.getElementById('loginForm');
  const logout = document.getElementById('logoutBtn');
  const toggle = document.getElementById('togglePassword');
  const password = document.getElementById('password');

  if (toggle && password) {
    toggle.addEventListener('click', () => {
      const visible = password.type === 'text';
      password.type = visible ? 'password' : 'text';
      toggle.textContent = visible ? '显示' : '隐藏';
      toggle.setAttribute('aria-pressed', String(!visible));
    });
  }

  if (form) {
    updateLoginStateUi();
    setInterval(updateLoginStateUi, 1000);
    form.addEventListener('submit', event => {
      event.preventDefault();
      const msg = document.getElementById('loginMessage');
      if (loginLockRemaining() > 0) {
        updateLoginStateUi();
        return;
      }

      const user = document.getElementById('username')?.value.trim() || '';
      const pass = password?.value || '';
      const apiToken = document.getElementById('apiToken')?.value.trim() || '';
      if (user === DEFAULT_USER && pass === DEFAULT_PASS) {
        localStorage.setItem(AUTH_KEY, 'ok');
        localStorage.setItem(AUTH_META_KEY, JSON.stringify({ user, loginAt: Date.now(), hasApiToken: Boolean(apiToken) }));
        if (apiToken) {
          localStorage.setItem(API_TOKEN_KEY, apiToken);
        } else {
          localStorage.removeItem(API_TOKEN_KEY);
        }
        localStorage.removeItem(LOGIN_FAIL_KEY);
        location.href = pageUrl('index.html');
      } else {
        const next = recordLoginFailure();
        if (msg) {
          const left = Math.max(0, MAX_LOGIN_ATTEMPTS - Number(next.attempts || 0));
          msg.textContent = loginLockRemaining(next) > 0
            ? '失败次数过多，请稍后再试'
            : `用户名或密码错误，还可尝试 ${left} 次`;
        }
        updateLoginStateUi();
      }
    });
  }

  if (logout) {
    logout.addEventListener('click', () => {
      localStorage.removeItem(AUTH_KEY);
      localStorage.removeItem(AUTH_META_KEY);
      localStorage.removeItem(API_TOKEN_KEY);
      location.href = pageUrl('login.html');
    });
  }
}

function formatGps(gps) {
  const lat = Number(gps?.lat);
  const lon = Number(gps?.lon);
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) return EMPTY_TEXT;
  return `${lat.toFixed(6)}, ${lon.toFixed(6)}`;
}

function formatBoolStatus(value, okText = '正常', badText = '告警') {
  return value ? badText : okText;
}

function renderCanNodes(nodes) {
  const nodeBox = document.getElementById('canNodes');
  if (!nodeBox) return;
  const list = Array.isArray(nodes) ? nodes : [];
  clearNode(nodeBox);
  if (!list.length) {
    renderEmpty(nodeBox, '暂无 CAN 节点数据');
    return;
  }
  list.forEach(node => {
    const online = Boolean(node.online);
    const name = node.name || `CAN-${node.id || EMPTY_TEXT}`;
    const detail = [
      node.flame_status !== undefined ? `火焰:${node.flame_status ? '异常' : '正常'}` : '',
      node.dht11_temperature !== undefined ? `DHT11:${node.dht11_temperature}℃/${valueText(node.dht11_humidity)}%` : '',
    ].filter(Boolean).join(' · ');
    nodeBox.append(makeEl('div', { className: 'row node-row' }, [
      makeEl('div', {}, [
        makeEl('strong', { text: name }),
        makeEl('span', { className: 'muted', text: detail || '等待节点遥测' }),
      ]),
      makeEl('span', {
        className: `status-pill ${online ? 'ok' : 'bad'}`,
        text: online ? '在线' : '离线',
      }),
    ]));
  });
}

function resetRealtime(message) {
  setText('subtitle', message || '等待设备遥测数据...');
  setText('device', EMPTY_TEXT);
  setText('timestamp', EMPTY_TEXT);
  setText('temperature', EMPTY_TEXT);
  setText('humidity', EMPTY_TEXT);
  setText('alcohol', EMPTY_TEXT);
  setText('motion', EMPTY_TEXT);
  setText('motionAlarm', EMPTY_TEXT);
  setText('gps', EMPTY_TEXT);
  setText('speed', EMPTY_TEXT);
  setText('satellites', EMPTY_TEXT);
  setText('fence', EMPTY_TEXT);
  setText('dataFreshness', '无实时数据');
  setText('ingestStatus', '等待 MQTT/OneNET 上报');
  setText('operatorHint', '请先确认转发脚本、OneNET 数据流转或测试 curl 是否已写入遥测');
  setText('activeAlarms', '0');
  setText('nodeCount', '0');
  updateOnlineBadge(false);
  updateRiskBadge('离线');
  renderCanNodes([]);
}

function renderRealtime(data) {
  if (!hasTelemetry(data)) {
    resetRealtime(data?.message === 'no telemetry received' ? '等待设备遥测数据...' : data?.message);
    return;
  }

  const cargo = data.sensors?.cargo || {};
  const cabin = data.sensors?.cabin || {};
  const vib = cargo.vibration || {};
  const gps = cabin.gps || {};
  const online = Boolean(data.online);
  const flags = alarmFlags(data);
  const risk = riskText(data);
  const nodes = Array.isArray(data.can_nodes) ? data.can_nodes : [];

  setText('subtitle', online ? '实时数据已连接' : '设备离线，显示最近一次数据');
  setText('device', data.device);
  setText('timestamp', timeText(data.timestamp));
  setText('temperature', numberText(cargo.temperature));
  setText('humidity', numberText(cabin.humidity));
  setText('alcohol', cabin.alcohol_raw);
  setText('motion', `${valueText(vib.accel_x)} / ${valueText(vib.accel_y)} / ${valueText(vib.accel_z)}`);
  setText('motionAlarm', formatBoolStatus(vib.motion_alarm));
  setText('gps', formatGps(gps));
  setText('speed', gps.speed != null ? `${numberText(gps.speed, 1)} km/h` : EMPTY_TEXT);
  setText('satellites', gps.satellites);
  setText('fence', gps.fence_alarm ? '越界' : '正常');
  setText('dataFreshness', ageText(data.timestamp));
  setText('ingestStatus', online ? '接收正常' : '缓存数据');
  setText('operatorHint', flags.length ? `需要处理：${flags.join('、')}告警` : '当前无活动告警');
  setText('activeAlarms', String(flags.length));
  setText('nodeCount', `${nodes.filter(node => node.online).length}/${nodes.length}`);
  updateOnlineBadge(online);
  updateRiskBadge(risk);
  renderCanNodes(nodes);
}

async function loadRealtime() {
  try {
    renderRealtime(await getJson('/api/realtime'));
  } catch (err) {
    resetRealtime(`实时接口暂不可用：${err.message}`);
  }
}

function alarmName(type) {
  const names = { alcohol: '酒精告警', motion: '振动告警', geofence: '围栏告警', flame: '火焰告警' };
  return names[type] || type || '告警';
}

function alarmLimit() {
  return currentPageKey() === 'dashboard' ? 5 : 100;
}

function alarmClass(type) {
  return type === 'flame' || type === 'alcohol' ? 'bad' : 'warn';
}

function selectedAlarmFilter() {
  return document.getElementById('alarmFilter')?.value || 'all';
}

function filteredAlarms() {
  const filter = selectedAlarmFilter();
  return filter === 'all' ? currentAlarms : currentAlarms.filter(alarm => alarm.type === filter);
}

function renderAlarmSummary(alarms) {
  const counts = alarms.reduce((acc, alarm) => {
    acc.total += 1;
    acc[alarm.type] = (acc[alarm.type] || 0) + 1;
    return acc;
  }, { total: 0 });
  setText('alarmTotal', counts.total);
  setText('alarmAlcohol', counts.alcohol || 0);
  setText('alarmMotion', counts.motion || 0);
  setText('alarmGeofence', counts.geofence || 0);
  setText('alarmFlame', counts.flame || 0);
}

function renderAlarms(alarms) {
  const box = document.getElementById('alarms');
  if (!box) return;
  currentAlarms = Array.isArray(alarms) ? alarms : [];
  renderAlarmSummary(currentAlarms);
  const list = filteredAlarms();
  clearNode(box);
  if (!list.length) {
    renderEmpty(box, selectedAlarmFilter() === 'all' ? '暂无告警' : '当前筛选条件下暂无告警');
    return;
  }
  list.forEach(alarm => {
    box.append(makeEl('div', { className: 'row alarm-row' }, [
      makeEl('div', {}, [
        makeEl('strong', { text: alarmName(alarm.type) }),
        makeEl('span', { className: 'muted', text: `${valueText(alarm.device)} · ${timeText(alarm.timestamp)}` }),
      ]),
      makeEl('span', {
        className: `status-pill ${alarmClass(alarm.type)}`,
        text: alarm.level || 'warning',
      }),
    ]));
  });
}

async function loadAlarms() {
  const box = document.getElementById('alarms');
  if (!box) return;
  try {
    const data = await getJson(`/api/alarms?limit=${alarmLimit()}`);
    renderAlarms(data.alarms);
    setText('alarmStatus', `已刷新：${new Date().toLocaleTimeString()}`);
  } catch (err) {
    currentAlarms = [];
    renderAlarmSummary([]);
    renderEmpty(box, `告警加载失败：${err.message}`);
    setText('alarmStatus', '接口不可用');
  }
}

function historyRows(data) {
  return Array.isArray(data?.data) ? data.data : [];
}

function updateHistorySummary(rows) {
  const values = rows.map(item => Number(item.value)).filter(Number.isFinite);
  setText('historyCount', rows.length);
  setText('historyLatest', rows.length ? timeText(rows[0].timestamp) : EMPTY_TEXT);
  setText('historyMin', values.length ? Math.min(...values).toFixed(2) : EMPTY_TEXT);
  setText('historyMax', values.length ? Math.max(...values).toFixed(2) : EMPTY_TEXT);
}

function renderHistoryChart(data) {
  const chart = document.getElementById('historyChart');
  if (!chart) return;
  const rows = historyRows(data).slice().reverse().filter(item => Number.isFinite(Number(item.value)));
  clearNode(chart);
  if (!rows.length) {
    chart.append(makeEl('span', { className: 'muted', text: '暂无历史数据' }));
    return;
  }
  const values = rows.map(item => Number(item.value));
  const max = Math.max(...values.map(v => Math.abs(v)), 1);
  rows.forEach(item => {
    const value = Number(item.value);
    const height = Math.max(8, Math.round(Math.abs(value) / max * 150));
    const bar = makeEl('div', {
      className: 'bar',
      title: `${timeText(item.timestamp)}: ${value.toFixed(2)}`,
    });
    bar.style.height = `${height}px`;
    chart.append(bar);
  });
}

function selectedHistoryLimit() {
  const raw = Number(document.getElementById('historyLimit')?.value || 100);
  return Number.isFinite(raw) ? Math.max(1, Math.min(1000, raw)) : 100;
}

function setHistoryStatus(message, tone = 'muted') {
  const status = document.getElementById('historyStatus');
  if (!status) return;
  status.textContent = message;
  status.className = tone;
}

async function loadHistory() {
  const output = document.getElementById('historyOutput');
  const select = document.getElementById('sensorSelect');
  const exportBtn = document.getElementById('exportHistory');
  if (!output || !select) return;
  output.textContent = '加载中...';
  setHistoryStatus('正在读取历史接口...');
  if (exportBtn) exportBtn.disabled = true;
  try {
    const sensor = select.value;
    const data = await getJson(`/api/history?sensor=${encodeURIComponent(sensor)}&limit=${selectedHistoryLimit()}`);
    lastHistorySensor = data.sensor || sensor;
    lastHistoryRows = historyRows(data);
    renderHistoryChart(data);
    updateHistorySummary(lastHistoryRows);
    output.textContent = JSON.stringify(data, null, 2);
    setHistoryStatus(`已加载 ${lastHistoryRows.length} 条记录`);
    if (exportBtn) exportBtn.disabled = lastHistoryRows.length === 0;
  } catch (err) {
    lastHistoryRows = [];
    renderHistoryChart({ data: [] });
    updateHistorySummary([]);
    output.textContent = JSON.stringify({ error: `历史数据加载失败：${err.message}` }, null, 2);
    setHistoryStatus('历史接口不可用', 'bad');
  }
}

function csvEscape(value) {
  let text = valueText(value);
  if (/^[=+\-@]/.test(text)) text = `'${text}`;
  return /[",\n\r]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

function downloadBlob(blob, filename) {
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = filename;
  document.body.append(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(link.href);
}

async function exportHistoryCsv() {
  if (!lastHistoryRows.length) {
    setHistoryStatus('当前没有可导出的历史数据', 'warn');
    return;
  }
  const stamp = new Date().toISOString().replace(/[:.]/g, '-');
  const sensor = lastHistorySensor || document.getElementById('sensorSelect')?.value || 'history';
  const limit = selectedHistoryLimit();
  const exportUrl = `/api/export?sensor=${encodeURIComponent(sensor)}&limit=${limit}`;

  try {
    setHistoryStatus('正在请求后端 CSV 导出...');
    const resp = await fetch(exportUrl, {
      cache: 'no-store',
      headers: apiHeaders({ Accept: 'text/csv' }),
    });
    if (resp.ok) {
      const blob = await resp.blob();
      downloadBlob(blob, `${sensor}-${stamp}.csv`);
      setHistoryStatus('已通过后端导出 CSV');
      return;
    }
  } catch (_) {
    // Fall back to exporting the already loaded rows below.
  }

  const rows = [
    ['sensor', 'timestamp', 'time', 'value'],
    ...lastHistoryRows.map(row => [
      sensor,
      valueText(row.timestamp),
      timeText(row.timestamp),
      valueText(row.value),
    ]),
  ];
  const csv = rows.map(row => row.map(csvEscape).join(',')).join('\n');
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8' });
  downloadBlob(blob, `${sensor}-${stamp}.csv`);
  setHistoryStatus('后端导出不可用，已导出当前查询结果');
}

function renderConfig(data) {
  setText('configCollectInterval', data.collect_interval != null ? `${data.collect_interval} 秒` : EMPTY_TEXT);
  setText('configMqttMode', data.mqtt?.mode || EMPTY_TEXT);
  setText('configAlcoholHigh', data.alarm_thresholds?.alcohol_high ?? EMPTY_TEXT);
  setText('configMotionDelta', data.alarm_thresholds?.motion_delta ?? EMPTY_TEXT);
  const collectInput = document.getElementById('collectIntervalInput');
  const alcoholInput = document.getElementById('alcoholHighInput');
  const motionInput = document.getElementById('motionDeltaInput');
  if (collectInput && data.collect_interval != null) collectInput.value = data.collect_interval;
  if (alcoholInput && data.alarm_thresholds?.alcohol_high != null) alcoholInput.value = data.alarm_thresholds.alcohol_high;
  if (motionInput && data.alarm_thresholds?.motion_delta != null) motionInput.value = data.alarm_thresholds.motion_delta;
}

function renderStatus(data) {
  const telemetry = data.telemetry || {};
  setText('statusBind', data.bind && data.port ? `${data.bind}:${data.port}` : EMPTY_TEXT);
  setText('statusAuth', data.auth?.enabled ? 'Token 已启用' : 'Token 未启用');
  setText('statusTelemetry', telemetry.has_sample
    ? `${telemetry.device || 'device'} · ${ageText(telemetry.timestamp)}`
    : '暂无遥测');
  setText('statusServerTime', timeText(data.server_time));
  setText('statusDbPath', data.paths?.db || EMPTY_TEXT);
  setText('statusConfigPath', data.paths?.config || EMPTY_TEXT);
  setText('statusHealth', '服务正常');
  const output = document.getElementById('statusOutput');
  if (output) output.textContent = JSON.stringify(data, null, 2);
}

async function loadStatus() {
  const output = document.getElementById('statusOutput');
  if (!output) return;
  output.textContent = '加载中...';
  try {
    const data = await getJson('/api/status');
    renderStatus(data);
  } catch (err) {
    output.textContent = JSON.stringify({ error: `状态加载失败：${err.message}` }, null, 2);
    setText('statusHealth', err.message.startsWith('401') ? 'Token 无效或缺失' : '状态接口不可用');
  }
}

async function loadConfig() {
  const output = document.getElementById('configOutput');
  if (!output) return;
  output.textContent = '加载中...';
  setText('apiHealth', '检查中');
  try {
    const data = await getJson('/api/config');
    renderConfig(data);
    output.textContent = JSON.stringify(data, null, 2);
    setText('apiHealth', '配置接口可用');
  } catch (err) {
    output.textContent = JSON.stringify({ error: `配置加载失败：${err.message}` }, null, 2);
    setText('apiHealth', '配置接口不可用');
  }
  syncApiTokenUi();
  loadStatus();
  checkOptionalExportApi();
}

async function checkOptionalExportApi() {
  const target = document.getElementById('exportApiStatus');
  if (!target) return;
  try {
    const resp = await fetch('/api/export?limit=1', {
      cache: 'no-store',
      headers: apiHeaders({ Accept: 'text/csv,application/json' }),
    });
    if (resp.status === 401) {
      target.textContent = '后端导出接口需要 Token';
      target.className = 'warn';
    } else {
      target.textContent = resp.ok ? '后端导出接口可用' : '未提供后端导出接口，历史页使用前端 CSV';
      target.className = resp.ok ? 'ok' : 'muted';
    }
  } catch (_) {
    target.textContent = '未提供后端导出接口，历史页使用前端 CSV';
    target.className = 'muted';
  }
}

function numberInputValue(id, min, max) {
  const input = document.getElementById(id);
  const value = Number(input?.value);
  if (!Number.isInteger(value) || value < min || value > max) {
    throw new Error(`${input?.previousElementSibling?.textContent || id} 超出范围`);
  }
  return value;
}

async function saveConfig() {
  const save = document.getElementById('saveConfig');
  try {
    const body = {
      collect_interval: numberInputValue('collectIntervalInput', 1, 3600),
      alarm_thresholds: {
        alcohol_high: numberInputValue('alcoholHighInput', 0, 65535),
        motion_delta: numberInputValue('motionDeltaInput', 0, 100000),
      },
    };
    if (save) save.disabled = true;
    setText('configSaveStatus', '正在保存配置...');
    const resp = await fetch('/api/config', {
      method: 'PUT',
      cache: 'no-store',
      headers: apiHeaders({
        Accept: 'application/json',
        'Content-Type': 'application/json',
      }),
      body: JSON.stringify(body),
    });
    if (!resp.ok) throw new Error(`${resp.status} ${resp.statusText}`);
    const data = await resp.json();
    renderConfig(data);
    const output = document.getElementById('configOutput');
    if (output) output.textContent = JSON.stringify(data, null, 2);
    setText('configSaveStatus', '已保存到 Ubuntu 本地配置文件，设备下发尚未启用');
    setText('apiHealth', '配置接口可写');
  } catch (err) {
    setText('configSaveStatus', `保存失败：${err.message}`);
  } finally {
    if (save) save.disabled = false;
  }
}

function bindConfigForm() {
  const save = document.getElementById('saveConfig');
  const saveToken = document.getElementById('saveApiToken');
  const clearToken = document.getElementById('clearApiToken');
  if (save) save.addEventListener('click', saveConfig);
  if (saveToken) saveToken.addEventListener('click', saveApiTokenSetting);
  if (clearToken) clearToken.addEventListener('click', clearApiTokenSetting);
  syncApiTokenUi();
}

function syncApiTokenUi() {
  const input = document.getElementById('apiTokenInput');
  const hasToken = Boolean(localStorage.getItem(API_TOKEN_KEY));
  if (input) {
    input.value = '';
    input.placeholder = hasToken ? '已保存，输入新 Token 可覆盖' : '未保存 Token';
  }
  setText('apiTokenStatus', hasToken ? '当前浏览器已保存 API Token' : '当前浏览器未保存 API Token');
}

function saveApiTokenSetting() {
  const input = document.getElementById('apiTokenInput');
  const value = input?.value.trim() || '';
  if (!value) {
    setText('apiTokenStatus', '请输入新的 API Token');
    return;
  }
  localStorage.setItem(API_TOKEN_KEY, value);
  setText('apiTokenStatus', 'API Token 已保存');
  syncApiTokenUi();
  loadConfig();
}

function clearApiTokenSetting() {
  localStorage.removeItem(API_TOKEN_KEY);
  setText('apiTokenStatus', 'API Token 已清空');
  syncApiTokenUi();
  loadConfig();
}

document.addEventListener('DOMContentLoaded', () => {
  if (!guardAuth()) return;
  bindAuth();
  if (isLoginPage()) return;

  markActiveNav();

  const hasRealtime = Boolean(document.getElementById('subtitle'));
  const hasAlarms = Boolean(document.getElementById('alarms'));
  const historyBtn = document.getElementById('loadHistory');
  const sensorSelect = document.getElementById('sensorSelect');
  const historyLimit = document.getElementById('historyLimit');
  const exportBtn = document.getElementById('exportHistory');
  const configOutput = document.getElementById('configOutput');
  const alarmBtn = document.getElementById('refreshAlarms');
  const alarmFilter = document.getElementById('alarmFilter');

  if (hasRealtime) loadRealtime();
  if (hasAlarms) loadAlarms();
  if (historyBtn) loadHistory();
  if (configOutput) loadConfig();
  bindConfigForm();

  if (historyBtn) historyBtn.addEventListener('click', loadHistory);
  if (sensorSelect) sensorSelect.addEventListener('change', loadHistory);
  if (historyLimit) historyLimit.addEventListener('change', loadHistory);
  if (exportBtn) exportBtn.addEventListener('click', exportHistoryCsv);
  if (alarmBtn) alarmBtn.addEventListener('click', loadAlarms);
  if (alarmFilter) alarmFilter.addEventListener('change', () => renderAlarms(currentAlarms));

  if (hasRealtime) {
    setInterval(() => {
      loadRealtime();
      if (hasAlarms) loadAlarms();
    }, 2000);
  }
});
