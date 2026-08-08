const PREVIEW_DEFAULTS = {
  deviceId: 'AUDIOPLAY_123456789ABC', ip: '192.168.4.1', firmware: '2.1.5', connected: true,
  globalBegEnabled: true, begAction: 0,
  players: [
    { present: true, mode: 'LOOP_TRACK', volume: 15, track: 1, folder: 1, muted: false },
    { present: true, mode: 'SINGLE', volume: 18, track: 4, folder: 1, muted: false },
    { present: true, mode: 'LOOP_FOLDER', volume: 12, track: 1, folder: 2, muted: true }
  ],
  wifiSsid: 'Layout Wi-Fi', mqttEnabled: true, mqttBroker: 'mqtt.layout.local', mqttPort: 1883,
  mqttUser: 'layout-controller', jmriChannel: '/trains/track/', otaState: 'Idle: 2.1.5'
};

function previewConfig() {
  const config = JSON.parse(JSON.stringify(PREVIEW_DEFAULTS));
  const scenario = new URLSearchParams(location.search).get('scenario') || 'all';
  if (scenario === 'two') config.players[2].present = false;
  if (scenario === 'one') { config.players[1].present = false; config.players[2].present = false; }
  if (scenario === 'none') config.players.forEach((player) => { player.present = false; });
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
    const notice = document.querySelector('#notice');
    notice.textContent = 'Unable to load device settings. Showing safe preview values.';
    notice.classList.remove('hidden');
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

function setPlayer(index, player) {
  const number = index + 1;
  const status = document.querySelector(`#player${number}-status`);
  status.textContent = player.present ? 'Detected' : 'Not detected';
  status.className = `status ${player.present ? 'detected' : 'missing'}`;
  setValue(`mode${number}`, player.mode);
  setValue(`volume${number}`, player.volume);
  setValue(`track${number}`, player.track);
  setValue(`folder${number}`, player.folder);
  setChecked(`muted${number}`, player.muted);
  document.querySelector(`#player${number}-panel`).disabled = !player.present;
}

function populate(config) {
  document.querySelector('#device-id').textContent = config.deviceId;
  document.querySelector('#device-ip').textContent = config.ip;
  document.querySelector('#firmware-version').textContent = config.firmware;
  document.querySelector('#config-ssid').textContent = config.configSsid || config.deviceId;
  config.players.forEach(setPlayer);
  const count = config.players.filter((player) => player.present).length;
  const summary = document.querySelector('#player-summary');
  summary.textContent = `${count} of 3 detected`;
  summary.className = `status ${count ? 'detected' : 'missing'}`;
  setValue('begaction', config.begAction);
  setValue('wifi_ssid', config.wifiSsid);
  setChecked('mqtt_enabled', config.mqttEnabled);
  setValue('mqtt_broker', config.mqttBroker);
  setValue('mqtt_port', config.mqttPort);
  setValue('mqtt_user', config.mqttUser);
  setValue('jmri_channel', config.jmriChannel);
  document.querySelector('#ota-state').textContent = config.otaState;
  updateMqttUi();
}

loadConfig().then(populate);
document.querySelector('#mqtt_enabled').addEventListener('change', updateMqttUi);

document.querySelector('#settings-form').addEventListener('submit', (event) => {
  const isPreview = location.protocol === 'file:' || new URLSearchParams(location.search).get('preview') === '1';
  if (!isPreview) return;
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
