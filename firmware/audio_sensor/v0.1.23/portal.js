/* DEVICE_EXCLUDE_START */
const PREVIEW_DEFAULTS = {
  deviceId: 'AUDIOSEN_123456789ABC', ip: '192.168.4.1', firmware: '0.1.14',
  configSsid: 'AUDIOSEN_123456789ABC', wifiSsid: 'Layout Wi-Fi',
  mqttEnabled: true, mqttHost: 'mqtt.layout.local', mqttPort: 1883,
  mqttUser: 'layout-controller', mqttPrefix: '/trains/',
  sensorCount: 2, sensors: [
    { present: true, distance: 198, threshold: 175, baseline: 200, calibrated: true, calibrating: false },
    { present: true, distance: 202, threshold: 175, baseline: 200, calibrated: true, calibrating: false }
  ],
  hysteresis: 50, samplePeriod: 40, clearDelayMs: 800,
  sensorTriggerEnabled: false, sensorTriggerMode: 1, sensorTriggerRunSeconds: 20, sensorClearHoldMs: 1000,
  lightPattern: 0,
  playerPresent: true, playerMuted: false, playerMode: 0, track: 1, folder: 1, volume: 15,
  manualButtonEnabled: true, manualButtonRunSeconds: 20,
  otaState: 'Up to date (v0.1.14)'
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
/* DEVICE_EXCLUDE_END */

/* DEVICE_ONLY_START
async function loadConfig() {
  const response = await fetch('/api/config', { cache: 'no-store' });
  if (!response.ok) throw new Error('Config unavailable');
  return response.json();
}
DEVICE_ONLY_END */
/* DEVICE_EXCLUDE_START */
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
/* DEVICE_EXCLUDE_END */

function setValue(id, value) { document.querySelector(`#${id}`).value = value; }
function setChecked(id, value) { document.querySelector(`#${id}`).checked = Boolean(value); }

function updateMqttUi() {
  const enabled = document.querySelector('#mqttEnabled').checked;
  document.querySelector('#mqtt-settings').hidden = !enabled;
  ['mqttHost', 'mqttPort', 'mqttUser', 'mqttPass', 'mqttPrefix'].forEach((id) => {
    document.querySelector(`#${id}`).disabled = !enabled;
  });
}

function updateManualUi() {
  const enabled = document.querySelector('#manualButtonEnabled').checked;
  document.querySelector('#external-button-settings').hidden = !enabled;
  document.querySelector('#manualButtonRunSeconds').disabled = !enabled;
}

function updateSensorTriggerUi(config) {
  const enabled = document.querySelector('#sensorTriggerEnabled').checked;
  const settings = document.querySelector('#sensor-trigger-settings');
  const mode = document.querySelector('#sensorTriggerMode');
  const timeField = document.querySelector('#sensor-time-field');
  const clearField = document.querySelector('#sensor-clear-field');
  const help = document.querySelector('#sensor-trigger-help');
  const anyPresent = config.sensors.some((sensor) => sensor.present);
  const bothPresent = config.sensors.every((sensor) => sensor.present);
  document.querySelector('#timed-option').disabled = !anyPresent;
  document.querySelector('#enter-exit-option').disabled = !bothPresent;
  if (mode.value === '2' && !bothPresent) mode.value = '1';
  settings.hidden = !enabled;
  mode.disabled = !enabled;
  timeField.hidden = !enabled || mode.value !== '1';
  clearField.hidden = !enabled || mode.value !== '2';
  document.querySelector('#sensorTriggerRunSeconds').disabled = !enabled || mode.value !== '1';
  document.querySelector('#sensorClearHoldMs').disabled = !enabled || mode.value !== '2';
  help.hidden = !enabled;
  help.textContent = mode.value === '1'
    ? 'Any detected sensor starts the washing sequence for the configured run time.'
    : 'The entrance sensor starts the sequence. After the exit sensor is reached, it must remain clear for the configured hold time before the sequence stops.';
}

function updatePlayerUi() {
  const playerPresent = currentConfig.playerPresent;
  const mode = document.querySelector('#playerMode').value;
  document.querySelector('#track-field').hidden = mode === '2';
  document.querySelector('#folder-field').hidden = mode !== '2';
  document.querySelector('#playerMuted').disabled = !playerPresent;
  document.querySelector('#playerMode').disabled = !playerPresent;
  document.querySelector('#volume').disabled = !playerPresent;
  document.querySelector('#track').disabled = !playerPresent || mode === '2';
  document.querySelector('#folder').disabled = !playerPresent || mode !== '2';
}

function setSensorStatus(index, sensor) {
  const number = index + 1;
  const status = document.querySelector(`#sensor${number}-status`);
  const reading = document.querySelector(`#sensor${number}-reading`);
  const calibration = document.querySelector(`#sensor${number}-calibration`);
  const assumption = document.querySelector(`#sensor${number}-assumption`);
  status.textContent = sensor.present ? 'Detected' : 'Not detected';
  status.className = `status ${sensor.present ? 'detected' : 'missing'}`;
  document.querySelector(`#sensor${number}-calibrate`).disabled = !sensor.present;
  if (!sensor.present) {
    reading.textContent = 'Current distance: unavailable';
    calibration.textContent = 'Connect the module, then restart the board.';
    assumption.textContent = '';
  } else if (sensor.calibrating) {
    reading.textContent = sensor.distance ? `Current distance: ${sensor.distance} mm` : 'Current distance: waiting for reading';
    calibration.textContent = 'Reading a stable clear track…';
    assumption.textContent = 'Assumed clear distance: collecting readings';
  } else if (sensor.calibrated) {
    reading.textContent = sensor.distance ? `Current distance: ${sensor.distance} mm` : 'Current distance: waiting for reading';
    calibration.textContent = 'Calibration saved.';
    assumption.textContent = sensor.baseline
      ? `Assumed clear distance: ${sensor.baseline} mm · occupied below ${sensor.threshold} mm`
      : `Occupied below ${sensor.threshold} mm`;
  } else {
    reading.textContent = sensor.distance ? `Current distance: ${sensor.distance} mm` : 'Current distance: waiting for reading';
    calibration.textContent = 'Waiting for a stable clear-track reading.';
    assumption.textContent = '';
  }
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
  setValue('lightPattern', config.lightPattern);
  const playerStatus = document.querySelector('#player-status');
  playerStatus.textContent = config.playerPresent ? 'Detected' : 'Not detected';
  playerStatus.className = `status ${config.playerPresent ? 'detected' : 'missing'}`;
  setChecked('playerMuted', config.playerMuted);
  setValue('playerMode', config.playerMode);
  setValue('track', config.track);
  setValue('folder', config.folder);
  setValue('volume', config.volume);
  updatePlayerUi();
  setChecked('manualButtonEnabled', config.manualButtonEnabled);
  setValue('manualButtonRunSeconds', config.manualButtonRunSeconds);
  setChecked('sensorTriggerEnabled', config.sensorTriggerEnabled);
  setValue('sensorTriggerMode', config.sensorTriggerMode);
  setValue('sensorTriggerRunSeconds', config.sensorTriggerRunSeconds);
  setValue('sensorClearHoldMs', config.sensorClearHoldMs);
  document.querySelector('#ota-state').textContent = config.otaState;
  updateMqttUi();
  updateManualUi();
  updateSensorTriggerUi(config);
}

let currentConfig;
loadConfig().then((config) => { currentConfig = config; populate(config); });
document.querySelector('#mqttEnabled').addEventListener('change', updateMqttUi);
document.querySelector('#manualButtonEnabled').addEventListener('change', updateManualUi);
document.querySelector('#sensorTriggerEnabled').addEventListener('change', () => updateSensorTriggerUi(currentConfig));
document.querySelector('#sensorTriggerMode').addEventListener('change', () => updateSensorTriggerUi(currentConfig));
document.querySelector('#playerMode').addEventListener('change', updatePlayerUi);

/* DEVICE_EXCLUDE_START */
document.querySelector('#settings-form').addEventListener('submit', (event) => {
  if (!isPreviewMode()) return;
  event.preventDefault();
  const notice = document.querySelector('#notice');
  notice.textContent = event.submitter?.dataset.calibrate !== undefined
    ? 'Preview only — calibration would collect clear-track readings on a connected board.'
    : 'Preview only — these values were not saved to a device.';
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
/* DEVICE_EXCLUDE_END */

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
  /* DEVICE_EXCLUDE_START */
  if (isPreviewMode()) {
    resetDialog.close();
    const notice = document.querySelector('#notice');
    notice.textContent = 'Preview only — no stored settings were reset.';
    notice.classList.remove('hidden');
    return;
  }
  /* DEVICE_EXCLUDE_END */
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
