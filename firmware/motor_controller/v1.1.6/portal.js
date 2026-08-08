const PREVIEW_DEFAULTS = {
  deviceId: 'MOTORCON_123456789ABC', ip: '192.168.4.1', firmware: '1.1.6', connected: true,
  linked: true, hold: false, stopLoss: true,
  motors: [{ direction: 'cw', speed: 500 }, { direction: 'cw', speed: 500 }],
  begAction: 0, begIndefinite: true, begTime: 30, globalBegEnabled: true,
  sensors: [{ present: true, threshold: 250 }, { present: true, threshold: 250 }],
  hysteresis: 20, samplePeriod: 100, sensorMode: 0, sensorTarget: 0, sensorTime: 30, sensorClearHoldMs: 500,
  wifiSsid: 'Layout Wi-Fi', mqttEnabled: true, mqttBroker: 'mqtt.layout.local', mqttPort: 1883,
  mqttUser: 'layout-controller', jmriChannel: 'trains/', otaState: 'Up to date (v1.1.5)'
};

function previewConfig() {
  const config = structuredClone(PREVIEW_DEFAULTS);
  const scenario = new URLSearchParams(location.search).get('scenario') || 'all';
  if (scenario === 'sensor1') config.sensors[1].present = false;
  if (scenario === 'none') {
    config.sensors[0].present = false;
    config.sensors[1].present = false;
  }
  if (scenario === 'unlinked') {
    config.linked = false;
    config.motors[1] = { direction: 'ccw', speed: 350 };
    config.begAction = 1;
  }
  if (scenario === 'night') config.globalBegEnabled = false;
  return config;
}

async function loadConfig() {
  const params = new URLSearchParams(location.search);
  const isPreview = location.protocol === 'file:' || params.get('preview') === '1';
  if (isPreview) return previewConfig();
  try {
    const response = await fetch('/api/config', { cache: 'no-store' });
    if (!response.ok) throw new Error('Config endpoint unavailable');
    return await response.json();
  } catch (error) {
    document.querySelector('#notice').textContent = 'Unable to load device settings. Showing safe preview values.';
    document.querySelector('#notice').classList.remove('hidden');
    return previewConfig();
  }
}

function setValue(id, value) { document.querySelector(`#${id}`).value = value; }
function setChecked(id, value) { document.querySelector(`#${id}`).checked = Boolean(value); }

function updateMqttUi() {
  const enabled = document.querySelector('#mqtt_enabled').checked;
  ['mqtt_broker', 'mqtt_port', 'mqtt_user', 'mqtt_password', 'jmri_channel'].forEach((id) => {
    document.querySelector(`#${id}`).disabled = !enabled;
  });
}

function setSensorStatus(index, sensor) {
  const status = document.querySelector(`#sensor${index + 1}-status`);
  const threshold = document.querySelector(`#thr${index + 1}`);
  status.textContent = sensor.present ? 'Detected' : 'Not detected';
  status.className = `status ${sensor.present ? 'detected' : 'missing'}`;
  threshold.value = sensor.threshold;
  threshold.disabled = !sensor.present;
}

function updateLinkedUi() {
  const linked = document.querySelector('#linked').checked;
  const dir1 = document.querySelector('#dir1');
  const speed1 = document.querySelector('#speed1');
  const dir2 = document.querySelector('#dir2');
  const speed2 = document.querySelector('#speed2');
  if (linked) { dir2.value = dir1.value; speed2.value = speed1.value; }
  dir2.disabled = linked;
  speed2.disabled = linked;
}

function updateButtonUi() {
  const disabled = document.querySelector('#begaction').value === '0';
  const indefinite = document.querySelector('#begindef');
  indefinite.disabled = disabled;
  document.querySelector('#begtime').disabled = disabled || indefinite.checked;
}

function updateSensorUi(config) {
  const anyPresent = config.sensors.some((sensor) => sensor.present);
  const bothPresent = config.sensors.every((sensor) => sensor.present);
  document.querySelector('#timed-option').disabled = !anyPresent;
  document.querySelector('#enter-exit-option').disabled = !bothPresent;
  document.querySelector('#hyst').disabled = !anyPresent;
  document.querySelector('#sample').disabled = !anyPresent;
  const mode = document.querySelector('#sensormode').value;
  const targetField = document.querySelector('#sensor-target-field');
  const timeField = document.querySelector('#sensor-time-field');
  const clearField = document.querySelector('#sensor-clear-field');
  const target = document.querySelector('#sensortarget');
  const runTime = document.querySelector('#sensortime');
  const clearHold = document.querySelector('#sensorclear');
  const help = document.querySelector('#sensor-action-help');
  const enabled = mode !== '0';
  targetField.hidden = !enabled;
  timeField.hidden = mode !== '1';
  clearField.hidden = mode !== '2';
  target.disabled = !enabled;
  runTime.disabled = mode !== '1';
  clearHold.disabled = mode !== '2';
  help.hidden = !enabled;
  help.textContent = mode === '1'
    ? 'Any detected sensor starts or extends the selected motors for the configured run time.'
    : 'The first sensor starts the selected motors. After the other sensor is reached, it must remain clear for the configured hold time before the motors stop.';
  const summary = document.querySelector('#sensor-summary');
  const count = config.sensors.filter((sensor) => sensor.present).length;
  summary.textContent = `${count} of 2 detected`;
  summary.className = `status ${count ? 'detected' : 'missing'}`;
}

function populate(config) {
  document.querySelector('#device-id').textContent = config.deviceId;
  document.querySelector('#device-ip').textContent = config.ip;
  document.querySelector('#firmware-version').textContent = config.firmware;
  document.querySelector('#config-ssid').textContent = config.configSsid || config.deviceId;
  setChecked('linked', config.linked); setChecked('hold', config.hold); setChecked('stoploss', config.stopLoss);
  setValue('dir1', config.motors[0].direction); setValue('speed1', config.motors[0].speed);
  setValue('dir2', config.motors[1].direction); setValue('speed2', config.motors[1].speed);
  setValue('begaction', config.begAction); setChecked('begindef', config.begIndefinite); setValue('begtime', config.begTime);
  config.sensors.forEach((sensor, index) => setSensorStatus(index, sensor));
  setValue('hyst', config.hysteresis); setValue('sample', config.samplePeriod);
  setValue('sensormode', config.sensorMode); setValue('sensortarget', config.sensorTarget); setValue('sensortime', config.sensorTime); setValue('sensorclear', config.sensorClearHoldMs);
  setValue('wifi_ssid', config.wifiSsid); setChecked('mqtt_enabled', config.mqttEnabled);
  setValue('mqtt_broker', config.mqttBroker); setValue('mqtt_port', config.mqttPort);
  setValue('mqtt_user', config.mqttUser); setValue('jmri_channel', config.jmriChannel);
  document.querySelector('#ota-state').textContent = config.otaState;
  updateMqttUi(); updateLinkedUi(); updateButtonUi(); updateSensorUi(config);
}

let currentConfig;
loadConfig().then((config) => { currentConfig = config; populate(config); });

document.querySelector('#linked').addEventListener('change', updateLinkedUi);
document.querySelector('#mqtt_enabled').addEventListener('change', updateMqttUi);
document.querySelector('#dir1').addEventListener('change', updateLinkedUi);
document.querySelector('#speed1').addEventListener('input', updateLinkedUi);
document.querySelector('#begaction').addEventListener('change', updateButtonUi);
document.querySelector('#begindef').addEventListener('change', updateButtonUi);
document.querySelector('#sensormode').addEventListener('change', () => updateSensorUi(currentConfig));

document.querySelector('#settings-form').addEventListener('submit', (event) => {
  const isPreview = location.protocol === 'file:' || new URLSearchParams(location.search).get('preview') === '1';
  if (!isPreview) return;
  event.preventDefault();
  const notice = document.querySelector('#notice');
  notice.textContent = 'Preview only — no settings were saved.';
  notice.classList.remove('hidden');
  notice.scrollIntoView({ behavior: 'smooth', block: 'center' });
});
document.querySelector('#ota-form').addEventListener('submit', (event) => {
  if (!isPreviewMode()) return;
  event.preventDefault();
  const notice = document.querySelector('#notice');
  notice.textContent = 'Preview only — no firmware update was requested.';
  notice.classList.remove('hidden');
});
const resetDefinitions = {
  wifi: {
    title: 'Reset stored Wi-Fi?',
    message: 'This resets the saved Wi-Fi network name and password to the firmware defaults. The board will restart.',
    confirm: 'Reset Wi-Fi'
  },
  mqtt: {
    title: 'Reset stored MQTT?',
    message: 'This resets the saved MQTT broker, port, username, and password to the firmware defaults. JMRI and device-specific settings remain unchanged. The board will restart.',
    confirm: 'Reset MQTT'
  },
  factory: {
    title: 'Factory restore this board?',
    message: 'This erases all saved network, JMRI, device settings, and synchronized names, restores the firmware defaults, and restarts the board.',
    confirm: 'Factory restore',
    dangerous: true
  }
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

function isPreviewMode() {
  return location.protocol === 'file:' || new URLSearchParams(location.search).get('preview') === '1';
}

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

document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape') closeDeviceMenu();
});

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

resetDialog.addEventListener('click', (event) => {
  if (event.target === resetDialog) resetDialog.close();
});

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
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
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
