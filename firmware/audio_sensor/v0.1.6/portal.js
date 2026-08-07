const PREVIEW_DEFAULTS = {
  deviceId: 'AUDIOSEN_123456789ABC', ip: '192.168.4.1', firmware: '0.1.6',
  configSsid: 'AUDIOSEN_123456789ABC', wifiSsid: 'Layout Wi-Fi',
  mqttEnabled: true, mqttHost: 'mqtt.layout.local', mqttPort: 1883,
  mqttUser: 'layout-controller', mqttPrefix: '/trains/',
  sensorCount: 2, sensors: [{ present: true }, { present: true }], clearDelayMs: 800,
  lightPattern: 0, defaultLightOnly: false,
  playerPresent: true, track: 1, volume: 25,
  manualButtonEnabled: true, manualButtonAction: 1,
  hardwareTarget: 'esp32-c3', hardwareVersion: 'v0.2',
  otaSource: 'GeorgeHinch/GeorgeHinch_Devices', otaState: 'Idle: 0.1.6'
};

function isPreviewMode() {
  return location.protocol === 'file:' || new URLSearchParams(location.search).get('preview') === '1';
}

function previewConfig() {
  const config = JSON.parse(JSON.stringify(PREVIEW_DEFAULTS));
  const scenario = new URLSearchParams(location.search).get('scenario') || 'all';
  if (scenario === 'sensor1') config.sensors[1].present = false;
  if (scenario === 'none') config.sensors.forEach((sensor) => { sensor.present = false; });
  if (scenario === 'player-missing') config.playerPresent = false;
  if (scenario === 'mqtt-off') config.mqttEnabled = false;
  return config;
}

async function loadConfig() {
  if (isPreviewMode()) return previewConfig();
  try {
    const response = await fetch('/api/config', { cache: 'no-store' });
    if (!response.ok) throw new Error('Config endpoint unavailable');
    return await response.json();
  } catch (error) {
    const notice = document.querySelector('#notice');
    notice.textContent = 'Unable to load device settings. Showing safe preview values.';
    notice.classList.remove('hidden');
    return previewConfig();
  }
}

function setValue(id, value) { document.querySelector(`#${id}`).value = value; }
function setChecked(id, value) { document.querySelector(`#${id}`).checked = Boolean(value); }

function updateMqttUi() {
  const enabled = document.querySelector('#mqttEnabled').checked;
  ['mqttHost', 'mqttPort', 'mqttUser', 'mqttPass', 'mqttPrefix'].forEach((id) => {
    document.querySelector(`#${id}`).disabled = !enabled;
  });
}

function updateManualUi() {
  document.querySelector('#manualButtonAction').disabled = !document.querySelector('#manualButtonEnabled').checked;
}

function setSensorStatus(index, sensor) {
  const number = index + 1;
  const status = document.querySelector(`#sensor${number}-status`);
  status.textContent = sensor.present ? 'Detected' : 'Not detected';
  status.className = `status ${sensor.present ? 'detected' : 'missing'}`;
  document.querySelector(`#sensor${number}-panel`).disabled = !sensor.present;
}

function populate(config) {
  document.querySelector('#device-id').textContent = config.deviceId;
  document.querySelector('#device-ip').textContent = config.ip;
  document.querySelector('#firmware-version').textContent = config.firmware;
  document.querySelector('#config-ssid').textContent = config.configSsid || config.deviceId;
  setValue('wifiSsid', config.wifiSsid);
  setChecked('mqttEnabled', config.mqttEnabled);
  setValue('mqttHost', config.mqttHost);
  setValue('mqttPort', config.mqttPort);
  setValue('mqttUser', config.mqttUser);
  setValue('mqttPrefix', config.mqttPrefix);
  config.sensors.forEach(setSensorStatus);
  const count = config.sensors.filter((sensor) => sensor.present).length;
  const sensorSummary = document.querySelector('#sensor-summary');
  sensorSummary.textContent = `${count} of 2 detected`;
  sensorSummary.className = `status ${count ? 'detected' : 'missing'}`;
  setValue('sensorCount', config.sensorCount);
  setValue('clearDelayMs', config.clearDelayMs);
  setValue('lightPattern', config.lightPattern);
  setChecked('defaultLightOnly', config.defaultLightOnly);
  const playerStatus = document.querySelector('#player-status');
  playerStatus.textContent = config.playerPresent ? 'Detected' : 'Not detected';
  playerStatus.className = `status ${config.playerPresent ? 'detected' : 'missing'}`;
  setValue('track', config.track);
  setValue('volume', config.volume);
  document.querySelector('#track').disabled = !config.playerPresent;
  document.querySelector('#volume').disabled = !config.playerPresent;
  setChecked('manualButtonEnabled', config.manualButtonEnabled);
  setValue('manualButtonAction', config.manualButtonAction);
  document.querySelector('#hardware-target').textContent = `${config.hardwareTarget} / ${config.hardwareVersion}`;
  document.querySelector('#ota-source').textContent = config.otaSource;
  document.querySelector('#ota-state').textContent = config.otaState;
  updateMqttUi();
  updateManualUi();
}

loadConfig().then(populate);
document.querySelector('#mqttEnabled').addEventListener('change', updateMqttUi);
document.querySelector('#manualButtonEnabled').addEventListener('change', updateManualUi);

document.querySelector('#settings-form').addEventListener('submit', (event) => {
  if (!isPreviewMode()) return;
  event.preventDefault();
  const notice = document.querySelector('#notice');
  notice.textContent = 'Preview only — these values were not saved to a device.';
  notice.classList.remove('hidden');
  notice.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
});

document.querySelector('#ota-form').addEventListener('submit', (event) => {
  if (!isPreviewMode()) return;
  event.preventDefault();
  const notice = document.querySelector('#notice');
  notice.textContent = 'Preview only — no firmware update was requested.';
  notice.classList.remove('hidden');
});

const resetDefinitions = {
  wifi: { title: 'Reset stored Wi-Fi?', message: 'This clears the saved Wi-Fi network and restarts the board in setup mode.', confirm: 'Reset Wi-Fi' },
  mqtt: { title: 'Reset stored MQTT?', message: 'This disables MQTT and resets the broker, port, username, password, and topic prefix.', confirm: 'Reset MQTT' },
  factory: { title: 'Factory restore this board?', message: 'This erases all saved network and Audio Sensor settings, restores firmware defaults, and restarts the board.', confirm: 'Factory restore', dangerous: true }
};

const menuTrigger = document.querySelector('#menu-trigger');
const deviceMenu = document.querySelector('#device-menu');
const resetDialog = document.querySelector('#reset-dialog');
const resetTitle = document.querySelector('#reset-title');
const resetMessage = document.querySelector('#reset-message');
const resetError = document.querySelector('#reset-error');
const cancelReset = document.querySelector('#cancel-reset');
const confirmReset = document.querySelector('#confirm-reset');
let pendingResetAction = '';

function closeDeviceMenu() {
  deviceMenu.hidden = true;
  menuTrigger.setAttribute('aria-expanded', 'false');
}

menuTrigger.addEventListener('click', (event) => {
  event.stopPropagation();
  deviceMenu.hidden = !deviceMenu.hidden;
  menuTrigger.setAttribute('aria-expanded', String(!deviceMenu.hidden));
});

document.addEventListener('click', (event) => {
  if (!deviceMenu.hidden && !event.target.closest('.menu-wrap')) closeDeviceMenu();
});
document.addEventListener('keydown', (event) => { if (event.key === 'Escape') closeDeviceMenu(); });

document.querySelectorAll('[data-reset-action]').forEach((button) => {
  button.addEventListener('click', () => {
    pendingResetAction = button.dataset.resetAction;
    const definition = resetDefinitions[pendingResetAction];
    resetTitle.textContent = definition.title;
    resetMessage.textContent = definition.message;
    resetError.textContent = '';
    resetError.classList.add('hidden');
    confirmReset.textContent = definition.confirm;
    confirmReset.classList.toggle('danger', Boolean(definition.dangerous));
    confirmReset.disabled = false;
    cancelReset.disabled = false;
    cancelReset.hidden = false;
    confirmReset.hidden = false;
    closeDeviceMenu();
    if (typeof resetDialog.showModal === 'function') resetDialog.showModal();
    else resetDialog.setAttribute('open', '');
  });
});

cancelReset.addEventListener('click', () => resetDialog.close());
resetDialog.addEventListener('click', (event) => { if (event.target === resetDialog) resetDialog.close(); });

confirmReset.addEventListener('click', async () => {
  if (!pendingResetAction) return;
  if (isPreviewMode()) {
    resetDialog.close();
    const notice = document.querySelector('#notice');
    notice.textContent = 'Preview only — no stored settings were reset.';
    notice.classList.remove('hidden');
    return;
  }
  confirmReset.disabled = true;
  cancelReset.disabled = true;
  resetError.classList.add('hidden');
  try {
    const response = await fetch('/reset', {
      method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ action: pendingResetAction })
    });
    if (!response.ok) throw new Error('The board rejected the reset request.');
    resetMessage.textContent = 'Reset accepted. The board is restarting now.';
    cancelReset.hidden = true;
    confirmReset.hidden = true;
  } catch (error) {
    resetError.textContent = error.message || 'Unable to request the reset.';
    resetError.classList.remove('hidden');
    confirmReset.disabled = false;
    cancelReset.disabled = false;
  }
});
