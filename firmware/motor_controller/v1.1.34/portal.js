const PREVIEW_DEFAULTS = {
  deviceId: 'MOTORCON_123456789ABC', ip: '192.168.4.1', firmware: '1.1.34', connected: true,
  linked: true, hold: false, stopLoss: true,
  motors: [
    { direction: 'arc', speed: 500, arcStart: 0, arcEnd: 512, arcPath: 'clockwise', position: 0, positionKnown: true, moving: false },
    { direction: 'cw', speed: 500, arcStart: 0, arcEnd: 512, arcPath: 'clockwise', position: 0, positionKnown: false, moving: false }
  ],
  begAction: 0, begTime: 20, globalBegEnabled: true,
  sensors: [
    { present: true, distance: 285, threshold: 250, baseline: 285, calibrated: true, calibrating: false },
    { present: true, distance: 290, threshold: 250, baseline: 290, calibrated: true, calibrating: false }
  ],
  hysteresis: 20, samplePeriod: 100, sensorMode: 0, sensorTarget: 0, sensorTime: 20, sensorClearHoldMs: 1000,
  wifiSsid: 'Layout Wi-Fi', mqttEnabled: true, mqttBroker: 'mqtt.layout.local', mqttPort: 1883,
  mqttUser: 'layout-controller', jmriChannel: '/trains/', motorLedAssignments: [0, 0], otaState: 'Up to date (v1.1.34)'
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

function arcAngle(steps) {
  return ((Number(steps) % 4096) + 4096) % 4096 / 4096 * 360;
}

function arcDegreeLabel(steps) {
  return arcAngle(steps).toFixed(1).replace(/\.0$/, '');
}

function arcSweepStyle(motor) {
  const start = arcAngle(motor.arcStart);
  const end = arcAngle(motor.arcEnd);
  const clockwise = motor.arcPath !== 'counterclockwise';
  const sweep = clockwise ? (end - start + 360) % 360 : (start - end + 360) % 360;
  const sweepPercent = sweep / 3.6;
  return `--arc-origin:${clockwise ? start : end}deg;--arc-sweep:${sweepPercent};--arc-gap:${100 - sweepPercent}`;
}

function arcDialMarkup(motor, interactive = false, selected = '') {
  const marker = (kind, value) => kind === 'current'
    ? `<i class="arc-marker current" style="--angle:${arcAngle(value)}deg" role="img" aria-label="Current angle ${arcDegreeLabel(value)} degrees"></i>`
    : interactive
      ? `<button class="arc-marker ${kind} ${selected === kind ? 'selected' : ''}" type="button" data-arc-select="${kind}" data-arc-endpoint="${kind}" style="--angle:${arcAngle(value)}deg" aria-label="Select and drag ${kind} point" aria-pressed="${selected === kind}"><span>${kind === 'start' ? 'S' : 'E'}</span></button>`
      : `<i class="arc-marker ${kind}" style="--angle:${arcAngle(value)}deg" aria-hidden="true"></i>`;
  return `<div class="arc-ring"><svg class="arc-sweep" viewBox="0 0 100 100" aria-hidden="true"><circle cx="50" cy="50" r="45.7" pathLength="100" style="${arcSweepStyle(motor)}"></circle></svg>${marker('start', motor.arcStart)}${marker('end', motor.arcEnd)}${motor.positionKnown ? marker('current', motor.position) : ''}<span>${interactive ? '4096 steps' : ''}</span></div>`;
}

function refreshArcUi(index) {
  const number = index + 1;
  const motor = currentConfig.motors[index];
  const direction = document.querySelector(`#dir${number}`).value;
  const summary = document.querySelector(`#arc${number}-summary`);
  summary.hidden = direction !== 'arc';
  summary.querySelector(`[data-arc-dial="${number}"]`).innerHTML = arcDialMarkup(motor);
  summary.querySelector(`[data-arc-status="${number}"]`).textContent = motor.moving ? 'Moving' : motor.positionKnown ? `At ${arcDegreeLabel(motor.position)}° · step ${motor.position}` : 'Position needs setup';
  summary.querySelector(`[data-arc-range="${number}"]`).textContent = `Start ${motor.arcStart} · End ${motor.arcEnd} · ${motor.arcPath === 'counterclockwise' ? 'Counter-clockwise' : 'Clockwise'} route`;
  setValue(`arcStart${number}`, motor.arcStart);
  setValue(`arcEnd${number}`, motor.arcEnd);
  setValue(`arcPath${number}`, motor.arcPath || 'clockwise');
}

function showJmriOutputNames(config) {
  const names = (config.jmriOutputNames || []).map((item) => item.name || '').filter(Boolean);
  const element = document.querySelector('#jmri-output-names');
  element.hidden = names.length === 0;
  element.classList.toggle('hidden', names.length === 0);
  element.textContent = names.length ? `JMRI names: ${names.join(' · ')}` : '';
}

function updateMqttUi() {
  const enabled = document.querySelector('#mqtt_enabled').checked;
  document.querySelector('#mqtt-settings').hidden = !enabled;
  ['mqtt_broker', 'mqtt_port', 'mqtt_user', 'mqtt_password', 'jmri_channel'].forEach((id) => {
    document.querySelector(`#${id}`).disabled = !enabled;
  });
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

function updateLinkedUi() {
  const linked = document.querySelector('#linked').checked;
  const dir1 = document.querySelector('#dir1');
  const speed1 = document.querySelector('#speed1');
  const dir2 = document.querySelector('#dir2');
  const speed2 = document.querySelector('#speed2');
  if (linked) { dir2.value = dir1.value; speed2.value = speed1.value; }
  dir2.disabled = linked;
  speed2.disabled = linked;
  if (currentConfig) {
    if (linked) currentConfig.motors[1] = { ...currentConfig.motors[1], ...currentConfig.motors[0] };
    refreshArcUi(0);
    refreshArcUi(1);
  }
}

function updateButtonUi() {
  const enabled = document.querySelector('#begenabled').checked;
  const action = document.querySelector('#begaction');
  document.querySelector('#external-button-settings').hidden = !enabled;
  action.disabled = !enabled;
  document.querySelector('#begtime').disabled = !enabled;
  if (enabled && action.value === '0') action.value = '1';
}

function updateSensorUi(config) {
  const anyPresent = config.sensors.some((sensor) => sensor.present);
  const bothPresent = config.sensors.every((sensor) => sensor.present);
  const enabled = document.querySelector('#sensorTriggerEnabled').checked;
  document.querySelector('#timed-option').disabled = !anyPresent;
  document.querySelector('#enter-exit-option').disabled = !bothPresent;
  const modeControl = document.querySelector('#sensormode');
  if (enabled && modeControl.value !== '1' && modeControl.value !== '2') modeControl.value = '1';
  const mode = modeControl.value;
  const settings = document.querySelector('#sensor-trigger-settings');
  const targetField = document.querySelector('#sensor-target-field');
  const timeField = document.querySelector('#sensor-time-field');
  const clearField = document.querySelector('#sensor-clear-field');
  const target = document.querySelector('#sensortarget');
  const runTime = document.querySelector('#sensortime');
  const clearHold = document.querySelector('#sensorclear');
  const help = document.querySelector('#sensor-action-help');
  settings.hidden = !enabled;
  targetField.hidden = !enabled;
  timeField.hidden = !enabled || mode !== '1';
  clearField.hidden = !enabled || mode !== '2';
  document.querySelector('#sensormode').disabled = !enabled;
  target.disabled = !enabled;
  runTime.disabled = !enabled || mode !== '1';
  clearHold.disabled = !enabled || mode !== '2';
  help.hidden = !enabled;
  help.textContent = mode === '1'
    ? 'Any detected sensor starts or extends the selected motors for the configured run time.'
    : 'The first sensor starts the selected motors. After the other sensor is reached, it must remain clear for the configured hold time before the motors stop.';
}

function populate(config) {
  document.querySelector('#device-id').textContent = config.deviceId;
  document.querySelector('#device-ip').textContent = config.ip;
  document.querySelector('#firmware-version').textContent = config.firmware;
  document.querySelector('#config-ssid').textContent = config.configSsid || config.deviceId;
  setChecked('linked', config.linked); setChecked('hold', config.hold); setChecked('stoploss', config.stopLoss);
  setValue('dir1', config.motors[0].direction); setValue('speed1', config.motors[0].speed);
  setValue('dir2', config.motors[1].direction); setValue('speed2', config.motors[1].speed);
  setChecked('begenabled', config.begAction !== 0); setValue('begaction', config.begAction); setValue('begtime', config.begTime);
  config.sensors.forEach((sensor, index) => setSensorStatus(index, sensor));
  const sensorSummary = document.querySelector('#sensor-summary');
  const detectedSensorCount = config.sensors.filter((sensor) => sensor.present).length;
  sensorSummary.textContent = `${detectedSensorCount}/2`;
  sensorSummary.className = `status ${detectedSensorCount === 2 ? 'detected' : detectedSensorCount === 1 ? 'partial' : 'missing'}`;
  setChecked('sensorTriggerEnabled', config.sensorMode !== 0);
  setValue('sensormode', config.sensorMode === 2 ? 2 : 1); setValue('sensortarget', config.sensorTarget); setValue('sensortime', config.sensorTime); setValue('sensorclear', config.sensorClearHoldMs);
  setValue('wifi_ssid', config.wifiSsid); setChecked('mqtt_enabled', config.mqttEnabled);
  setValue('mqtt_broker', config.mqttBroker); setValue('mqtt_port', config.mqttPort);
  setValue('mqtt_user', config.mqttUser); setValue('jmri_channel', config.jmriChannel);
  showJmriOutputNames(config);
  (config.motorLedAssignments || [0, 0]).forEach((assignment, index) => setValue(`motorLed${index + 1}Assignment`, assignment));
  document.querySelector('#ota-state').textContent = config.otaState;
  config.motors.forEach((motor) => {
    motor.arcStart ??= 0; motor.arcEnd ??= 512; motor.arcPath ??= 'clockwise';
    motor.position ??= 0; motor.positionKnown ??= false; motor.moving ??= false;
  });
  updateMqttUi(); updateLinkedUi(); updateButtonUi(); updateSensorUi(config);
}

let currentConfig;
loadConfig().then((config) => { currentConfig = config; populate(config); });

document.querySelector('#linked').addEventListener('change', updateLinkedUi);
document.querySelector('#mqtt_enabled').addEventListener('change', updateMqttUi);
document.querySelector('#dir1').addEventListener('change', updateLinkedUi);
document.querySelector('#dir2').addEventListener('change', updateLinkedUi);
document.querySelector('#speed1').addEventListener('input', updateLinkedUi);
document.querySelector('#begenabled').addEventListener('change', updateButtonUi);
document.querySelector('#sensorTriggerEnabled').addEventListener('change', () => updateSensorUi(currentConfig));
document.querySelector('#sensormode').addEventListener('change', () => updateSensorUi(currentConfig));

const arcDialog = document.querySelector('#arc-dialog');
let activeArcMotor = 0;
const selectedArcEndpoints = ['start', 'start'];
const arcDraftEndpoints = [new Set(), new Set()];
let arcDrag;

function normalizeArcPosition(value) { return ((Math.round(Number(value) || 0) % 4096) + 4096) % 4096; }

function selectArcEndpoint(endpoint) {
  if (!['start', 'end'].includes(endpoint)) return;
  selectedArcEndpoints[activeArcMotor] = endpoint;
  arcDialog.querySelectorAll('[data-arc-select]').forEach(control => {
    const selected = control.dataset.arcSelect === endpoint;
    control.classList.toggle('selected', selected);
    control.setAttribute('aria-pressed', String(selected));
  });
  document.querySelector('#arc-selected-label').textContent = `${endpoint === 'start' ? 'Start' : 'End'} point selected.`;
  const motor = currentConfig.motors[activeArcMotor];
  document.querySelector('#arc-move-selected').disabled = !motor.positionKnown || motor.moving || arcDraftEndpoints[activeArcMotor].has(endpoint);
  document.querySelector('#arc-home').disabled = !motor.positionKnown || motor.moving;
}

function setArcEndpoint(endpoint, value) {
  const motor = currentConfig.motors[activeArcMotor];
  const property = endpoint === 'start' ? 'arcStart' : 'arcEnd';
  motor[property] = normalizeArcPosition(value);
  arcDraftEndpoints[activeArcMotor].add(endpoint);
  setValue(`${property}${activeArcMotor + 1}`, motor[property]);
  document.querySelector(endpoint === 'start' ? '#arc-dialog-start' : '#arc-dialog-end').textContent = motor[property];
  arcDialog.querySelector(`[data-arc-endpoint="${endpoint}"]`)?.style.setProperty('--angle', `${arcAngle(motor[property])}deg`);
  refreshArcUi(activeArcMotor);
  selectArcEndpoint(endpoint);
}

function arcPositionFromPointer(ring, event) {
  const bounds = ring.getBoundingClientRect();
  const x = event.clientX - (bounds.left + bounds.width / 2);
  const y = event.clientY - (bounds.top + bounds.height / 2);
  const degrees = (Math.atan2(x, -y) * 180 / Math.PI + 360) % 360;
  return normalizeArcPosition(degrees / 360 * 4096);
}

function refreshArcDialog() {
  const motor = currentConfig.motors[activeArcMotor];
  document.querySelector('#arc-dialog-title').textContent = `Motor group ${activeArcMotor + 1} setup`;
  const selected = selectedArcEndpoints[activeArcMotor];
  document.querySelector('#arc-dialog-dial').innerHTML = arcDialMarkup(motor, true, selected);
  document.querySelector('#arc-dialog-start').textContent = motor.arcStart;
  document.querySelector('#arc-dialog-end').textContent = motor.arcEnd;
  document.querySelector('#arc-dialog-position').textContent = motor.positionKnown ? `${arcDegreeLabel(motor.position)}° · step ${motor.position}` : 'Unknown';
  document.querySelectorAll('[data-arc-path]').forEach(button => button.setAttribute('aria-pressed', String(button.dataset.arcPath === (motor.arcPath || 'clockwise'))));
  selectArcEndpoint(selected);
}

document.querySelectorAll('[data-open-arc]').forEach(button => button.addEventListener('click', () => {
  activeArcMotor = Number(button.dataset.openArc) - 1;
  refreshArcDialog();
  arcDialog.showModal();
}));
document.querySelector('#close-arc').addEventListener('click', () => arcDialog.close());
arcDialog.addEventListener('click', event => { if (event.target === arcDialog) arcDialog.close(); });

async function motorPreviewAction(action, value) {
  const motor = currentConfig.motors[activeArcMotor];
  if (isPreviewMode()) {
    if (action === 'jog') { motor.position += Number(value); motor.positionKnown = true; }
    if (action === 'markEndpoint') motor[value === 'end' ? 'arcEnd' : 'arcStart'] = motor.positionKnown ? motor.position : 0;
    if (action === 'moveEndpoint' && motor.positionKnown) motor.position = motor[value === 'end' ? 'arcEnd' : 'arcStart'];
    if (action === 'home' && motor.positionKnown) motor.position = 0;
  } else {
    const response = await fetch('/motor', {
      method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ action, motor: String(activeArcMotor + 1), value: String(value) })
    });
    if (!response.ok) throw new Error(await response.text() || 'Motor command failed.');
    if (action === 'jog' || action === 'moveEndpoint' || action === 'home') await new Promise(resolve => setTimeout(resolve, Math.abs(Number(value)) > 0 ? 450 : 850));
    currentConfig = await loadConfig();
  }
  refreshArcUi(activeArcMotor);
  refreshArcDialog();
}

arcDialog.querySelectorAll('[data-arc-step]').forEach(button => button.addEventListener('click', () => {
  const endpoint = selectedArcEndpoints[activeArcMotor];
  const motor = currentConfig.motors[activeArcMotor];
  setArcEndpoint(endpoint, motor[endpoint === 'start' ? 'arcStart' : 'arcEnd'] + Number(button.dataset.arcStep));
}));
document.querySelector('#arc-use-current').addEventListener('click', () => {
  const endpoint = selectedArcEndpoints[activeArcMotor];
  arcDraftEndpoints[activeArcMotor].delete(endpoint);
  motorPreviewAction('markEndpoint', endpoint).catch(error => alert(error.message));
});
document.querySelector('#arc-move-selected').addEventListener('click', () => motorPreviewAction('moveEndpoint', selectedArcEndpoints[activeArcMotor]).catch(error => alert(error.message)));
document.querySelector('#arc-home').addEventListener('click', () => motorPreviewAction('home', '').catch(error => alert(error.message)));
arcDialog.addEventListener('click', event => {
  const select = event.target.closest('[data-arc-select]');
  if (select) selectArcEndpoint(select.dataset.arcSelect);
});
arcDialog.addEventListener('pointerdown', event => {
  const marker = event.target.closest('.arc-large [data-arc-endpoint]');
  if (!marker) return;
  selectArcEndpoint(marker.dataset.arcEndpoint);
  arcDrag = { marker, endpoint: marker.dataset.arcEndpoint, ring: marker.closest('.arc-ring'), pointerId: event.pointerId };
  try { marker.setPointerCapture?.(event.pointerId); } catch (_) {}
  event.preventDefault();
});
arcDialog.addEventListener('pointermove', event => {
  if (!arcDrag || event.pointerId !== arcDrag.pointerId) return;
  setArcEndpoint(arcDrag.endpoint, arcPositionFromPointer(arcDrag.ring, event));
  event.preventDefault();
});
function finishArcDrag(event) {
  if (!arcDrag || event.pointerId !== arcDrag.pointerId) return;
  try { arcDrag.marker.releasePointerCapture?.(event.pointerId); } catch (_) {}
  arcDrag = null;
}
arcDialog.addEventListener('pointerup', finishArcDrag);
arcDialog.addEventListener('pointercancel', finishArcDrag);
document.querySelectorAll('[data-arc-path]').forEach(button => button.addEventListener('click', event => {
  const motor = currentConfig.motors[activeArcMotor];
  motor.arcPath = event.currentTarget.dataset.arcPath;
  setValue(`arcPath${activeArcMotor + 1}`, motor.arcPath);
  refreshArcUi(activeArcMotor);
  refreshArcDialog();
}));

document.querySelector('#settings-form').addEventListener('submit', (event) => {
  const isPreview = location.protocol === 'file:' || new URLSearchParams(location.search).get('preview') === '1';
  if (!isPreview) return;
  event.preventDefault();
  const notice = document.querySelector('#notice');
  notice.textContent = event.submitter?.dataset.calibrate !== undefined
    ? 'Preview only — calibration would collect clear-track readings on a connected board.'
    : 'Preview only — no settings were saved.';
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
