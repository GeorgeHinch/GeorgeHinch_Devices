#include <WebServer.h>
#include <DNSServer.h>
#include "motor_controller_portal.h"

WebServer configServer(80);
DNSServer configDns;

bool configApActive = false;
bool configServerStarted = false;
uint32_t configPortalBootMs = 0;
uint32_t restartAtMs = 0;
char configApSsid[33];

bool isConfigAccessPointActive() {
  return configApActive;
}

void updateConfigAccessPointSsid() {
  snprintf(configApSsid, sizeof(configApSsid), "%s", deviceId);
}

#if 0  // Replaced by the generated portal shared with the browser preview.
String htmlEscape(const char* text) {
  String escaped(text ? text : "");
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}

String checkedAttribute(bool checked) {
  return checked ? " checked" : "";
}

String selectedAttribute(bool selected) {
  return selected ? " selected" : "";
}

String renderConfigPage(const char* notice = nullptr) {
  updateConfigAccessPointSsid();
  String page;
  page.reserve(15000);
  page += F("<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Motor Controller</title><style>"
            ":root{--accent:#1769d2;--danger:#b42318;font-family:system-ui,sans-serif;color:#172033;background:#eef2f7}"
            "body{margin:0;padding:18px}.wrap{max-width:780px;margin:auto}"
            ".titleRow{display:flex;align-items:center;justify-content:space-between;gap:16px}"
            ".eyebrow{margin:0 0 5px;color:var(--accent);font-size:.75rem;font-weight:800;letter-spacing:.1em;text-transform:uppercase}"
            "h1{font-size:1.65rem;margin:.2rem 0}h2{font-size:1.1rem;margin:0 0 14px}"
            ".sub{color:#526071;margin:0 0 18px}.card{background:#fff;border-radius:12px;"
            "padding:18px;margin:14px 0;box-shadow:0 2px 10px #1c2b4214}"
            ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px}"
            "label{display:block;font-weight:600;font-size:.9rem}input,select{box-sizing:border-box;"
            "width:100%;margin-top:6px;padding:10px;border:1px solid #aeb9c8;border-radius:7px;"
            "background:#fff;font:inherit}input:focus,select:focus{outline:2px solid var(--accent);outline-offset:1px;border-color:var(--accent)}"
            "input[type=checkbox]{accent-color:var(--accent)}.check{display:flex;gap:10px;align-items:center}.check input{"
            "width:auto;margin:0}.toggle{display:flex;align-items:center;margin-top:10px;cursor:pointer}.toggle input{position:absolute;width:1px;height:1px;margin:0;opacity:0}"
            ".toggle span{position:relative;display:flex;min-height:24px;align-items:center;padding-left:50px}"
            ".toggle span:before{position:absolute;left:0;top:50%;width:40px;height:22px;border:1px solid #aeb9c8;border-radius:99px;background:#d7dde5;content:'';transform:translateY(-50%);transition:.16s ease}"
            ".toggle span:after{position:absolute;left:3px;top:50%;width:18px;height:18px;border-radius:50%;background:#fff;box-shadow:0 1px 3px #17203355;content:'';transform:translateY(-50%);transition:.16s ease}"
            ".toggle input:checked+span:before{border-color:var(--accent);background:var(--accent)}.toggle input:checked+span:after{transform:translate(16px,-50%)}"
            ".toggle input:focus-visible+span:before{outline:2px solid var(--accent);outline-offset:2px}.toggle input:disabled+span{opacity:.55;cursor:not-allowed}"
            ".menuWrap{position:relative;flex:none}.menuTrigger{width:40px;height:40px;padding:0;border:1px solid #c7d0dc;border-radius:9px;color:#38465a;background:#fff;font-size:1.65rem;line-height:1;cursor:pointer}"
            ".menuTrigger:hover,.menuTrigger:focus-visible{border-color:var(--accent);color:var(--accent);outline:2px solid var(--accent);outline-offset:1px}"
            ".overflowMenu{position:absolute;z-index:10;top:46px;right:0;width:230px;padding:7px;border:1px solid #d2dae5;border-radius:10px;background:#fff;box-shadow:0 10px 28px #17203326}"
            ".menuGroup{display:grid;gap:2px}.menuGroup button{padding:10px 11px;border:0;border-radius:7px;color:inherit;background:transparent;font:inherit;font-weight:650;text-align:left;cursor:pointer}"
            ".menuGroup button:hover,.menuGroup button:focus-visible{background:#edf4fd;outline:none}.menuGroup .dangerItem{color:var(--danger)}.menuGroup .dangerItem:hover,.menuGroup .dangerItem:focus-visible{background:#fff0ef}"
            ".menuSeparator{height:1px;margin:7px 4px;background:#dfe5ed}.resetDialog{width:min(440px,calc(100% - 28px));padding:0;border:0;border-radius:13px;color:inherit;background:transparent}"
            ".resetDialog::backdrop{background:#10182880}.dialogCard{padding:22px;border-radius:13px;background:#fff;box-shadow:0 18px 50px #10182840}.dialogCard h2{margin-bottom:8px}.dialogCard p{margin:0;color:#526071;line-height:1.5}"
            ".dialogActions{display:flex;justify-content:flex-end;gap:10px;margin-top:22px}.dialogActions button{padding:10px 15px;border-radius:8px;font:inherit;font-weight:750;cursor:pointer}.secondary{border:1px solid #b7c1ce;color:#344054;background:#fff}"
            ".confirm{border:1px solid var(--accent);color:#fff;background:var(--accent)}.confirm.danger{border-color:var(--danger);background:var(--danger)}.dialogActions button:disabled{opacity:.55;cursor:wait}.dialogError{margin-top:12px!important;color:var(--danger)!important}.hidden{display:none}"
            ".motor{border:1px solid #d8e0ea;border-radius:9px;padding:14px}"
            ".status{font-size:.72rem;padding:3px 7px;border-radius:99px;vertical-align:middle}"
            ".detected{background:#dff6e5;color:#236835}.missing{background:#f9e1e1;color:#8b2d2d}"
            "input:disabled,select:disabled{background:#e9edf2;color:#657183}small{display:block;"
            "color:#667487;margin-top:6px}.notice{background:#dff6e5;border:1px solid #82ca95;"
            "padding:12px;border-radius:8px}.save{background:var(--accent);color:#fff;border:0;"
            "border-radius:8px;padding:12px 20px;font-weight:700;font-size:1rem;cursor:pointer}"
            "@media(max-width:520px){body{padding:10px}.card{padding:14px}.titleRow{gap:10px}}"
            "</style></head><body><main class='wrap'><header><p class='eyebrow'>Device configuration</p><div class='titleRow'><h1>Motor Controller</h1><div class='menuWrap'>"
            "<button class='menuTrigger' id='menuTrigger' type='button' aria-label='More board options' aria-haspopup='menu' aria-controls='deviceMenu' aria-expanded='false'>&#8942;</button>"
            "<div class='overflowMenu' id='deviceMenu' role='menu' hidden><div class='menuGroup'>"
            "<button type='button' role='menuitem' data-reset-action='wifi'>Reset stored Wi-Fi</button>"
            "<button type='button' role='menuitem' data-reset-action='mqtt'>Reset stored MQTT</button></div>"
            "<div class='menuSeparator'></div><div class='menuGroup'>"
            "<button class='dangerItem' type='button' role='menuitem' data-reset-action='factory'>Factory restore board</button>"
            "</div></div></div></div>");
  page += "<p class='sub'>" + htmlEscape(deviceId) + " &middot; ";
  page += WiFi.status() == WL_CONNECTED ? htmlEscape(WiFi.localIP().toString().c_str()) : "Wi-Fi not connected";
  page += F(" &middot; firmware ");
  page += FIRMWARE_VERSION;
  page += F("</p></header>");

  if (notice && notice[0]) page += "<p class='notice'>" + htmlEscape(notice) + "</p>";

  page += F("<form method='post' action='/save' id='settingsForm'>"
            "<section class='card'><h2>Wi-Fi</h2>"
            "<small>If Wi-Fi cannot connect for 30 seconds, join the open <strong>");
  page += htmlEscape(configApSsid);
  page += F("</strong> network and browse to 192.168.4.1. While the device is running, hold the onboard BOOT button for 3 seconds to open setup.</small>"
            "<div class='grid' style='margin-top:15px'><label>Network name (SSID)"
            "<input name='wifi_ssid' maxlength='32' value='");
  page += htmlEscape(settings.wifiSsid);
  page += F("'></label><label>Password<input type='password' name='wifi_password' maxlength='64' placeholder='Leave blank to keep current'></label>"
            "</div></section>"
            "<section class='card'><h2>MQTT and JMRI</h2><div class='grid'><label>MQTT broker"
            "<input name='mqtt_broker' maxlength='127' value='");
  page += htmlEscape(settings.mqttBroker);
  page += F("'></label><label>MQTT port<input type='number' name='mqtt_port' min='1' max='65535' value='");
  page += String(settings.mqttPort);
  page += F("'></label><label>MQTT username<input name='mqtt_user' maxlength='64' value='");
  page += htmlEscape(settings.mqttUser);
  page += F("'></label><label>MQTT password<input type='password' name='mqtt_password' maxlength='64' placeholder='Leave blank to keep current'></label>"
            "<label>JMRI channel root<input name='jmri_channel' maxlength='64' value='");
  page += htmlEscape(settings.jmriChannel);
  page += F("'></label></div></section>"
            "<section class='card'><h2>Motor groups</h2>"
            "<label class='toggle'><input type='checkbox' name='linked' id='linked'");
  page += checkedAttribute(settings.linkMotorGroups);
  page += F("><span>Link motor groups</span></label>"
            "<small>When linked, Motor Group 2 uses Group 1's direction and speed.</small>"
            "<div class='grid' style='margin-top:16px'><div class='motor'><h2>Motor Group 1</h2>"
            "<label>Direction<select name='dir1' id='dir1'><option value='cw'");
  page += selectedAttribute(!motors[0].reverse);
  page += F(">Clockwise</option><option value='ccw'");
  page += selectedAttribute(motors[0].reverse);
  page += F(">Counterclockwise</option></select></label>"
            "<label style='margin-top:12px'>Speed (half-steps/second)"
            "<input type='number' name='speed1' id='speed1' min='50' max='1000' value='");
  page += String(motors[0].speed);
  page += F("'></label></div><div class='motor'><h2>Motor Group 2</h2>"
            "<label>Direction<select name='dir2' id='dir2'><option value='cw'");
  page += selectedAttribute(!motors[1].reverse);
  page += F(">Clockwise</option><option value='ccw'");
  page += selectedAttribute(motors[1].reverse);
  page += F(">Counterclockwise</option></select></label>"
            "<label style='margin-top:12px'>Speed (half-steps/second)"
            "<input type='number' name='speed2' id='speed2' min='50' max='1000' value='");
  page += String(motors[1].speed);
  page += F("'></label></div></div></section>"
            "<section class='card'><h2>Motor safety</h2><div class='grid'>"
            "<label class='toggle'><input type='checkbox' name='hold'");
  page += checkedAttribute(settings.holdWhenStopped);
  page += F("><span>Keep coils energized while stopped</span></label>"
            "<label class='toggle'><input type='checkbox' name='stoploss'");
  page += checkedAttribute(settings.stopOnConnectionLoss);
  page += F("><span>Stop motors if MQTT disconnects</span></label></div></section>"
            "<section class='card'><h2>BEG button and status light</h2><div class='grid'>"
            "<label>BEG button action<select name='begaction'>"
            "<option value='0'");
  page += selectedAttribute(settings.begButtonAction == BEG_BUTTON_DISABLED);
  page += F(">Disabled (Default)</option><option value='1'");
  page += selectedAttribute(settings.begButtonAction == BEG_BUTTON_BOTH_GROUPS);
  page += F(">Both Motor Groups</option><option value='2'");
  page += selectedAttribute(settings.begButtonAction == BEG_BUTTON_GROUP_1);
  page += F(">Motor Group 1</option><option value='3'");
  page += selectedAttribute(settings.begButtonAction == BEG_BUTTON_GROUP_2);
  page += F(">Motor Group 2</option></select></label>"
            "<label class='toggle'><input type='checkbox' name='begindef' id='begindef'");
  page += checkedAttribute(settings.begButtonRunIndefinitely);
  page += F("><span>Run indefinitely</span></label>"
            "<label>BEG run time (seconds)<input type='number' name='begtime' id='begtime' min='1' max='3600' value='");
  page += String(settings.begButtonRunSeconds);
  page += F("'></label></div>"
            "<small>The BEG LED is on while the motors are stopped and turns off whenever either motor group runs. JMRI's global BEG Light OFF setting overrides this and keeps the LED off.</small></section>"
            "<section class='card'><h2>Distance sensors</h2><div class='grid'>"
            "<div class='motor'><h2>Sensor 1 <span class='status ");
  page += ranges[0].present ? "detected'>Detected" : "missing'>Not detected";
  page += F("</span></h2><label>Occupied threshold (mm)<input type='number' name='thr1' min='20' max='2000' value='");
  page += String(ranges[0].thresholdMm);
  page += ranges[0].present ? "'>" : "' disabled>";
  page += F("</label><small>I2C address 0x30</small></div>"
            "<div class='motor'><h2>Sensor 2 <span class='status ");
  page += ranges[1].present ? "detected'>Detected" : "missing'>Not detected";
  page += F("</span></h2><label>Occupied threshold (mm)<input type='number' name='thr2' min='20' max='2000' value='");
  page += String(ranges[1].thresholdMm);
  page += ranges[1].present ? "'>" : "' disabled>";
  page += F("</label><small>I2C address 0x31</small></div>"
            "<label>Hysteresis (mm)<input type='number' name='hyst' min='0' max='500' value='");
  page += String(settings.occupiedHysteresisMm);
  page += (ranges[0].present || ranges[1].present) ? "'>" : "' disabled>";
  page += F("</label><label>Sample period (ms)<input type='number' name='sample' min='30' max='1000' value='");
  page += String(settings.sensorSamplePeriodMs);
  page += (ranges[0].present || ranges[1].present) ? "'>" : "' disabled>";
  page += F("</label></div><small>Thresholds determine when each sensor reports occupied. Sensor availability is checked during startup; restart after plugging in or removing a module.</small></section>"
            "<section class='card'><h2>VL53L0X sensor action</h2><div class='grid'>"
            "<label>Action<select name='sensormode' id='sensormode'><option value='0'");
  page += selectedAttribute(settings.sensorControlMode == SENSOR_CONTROL_DISABLED);
  page += F(">Disabled (Default)</option><option value='1'");
  page += selectedAttribute(settings.sensorControlMode == SENSOR_CONTROL_ANY_TIMED);
  if (!ranges[0].present && !ranges[1].present) page += " disabled";
  page += F(">Any Sensor (Time-based)</option><option value='2'");
  page += selectedAttribute(settings.sensorControlMode == SENSOR_CONTROL_ENTER_EXIT);
  if (!ranges[0].present || !ranges[1].present) page += " disabled";
  page += F(">Enter-Exit Sensor</option></select></label>"
            "<label>Motor target<select name='sensortarget' id='sensortarget'><option value='0'");
  page += selectedAttribute(settings.sensorMotorTarget == MOTOR_TARGET_BOTH);
  page += F(">Both Motor Groups</option><option value='1'");
  page += selectedAttribute(settings.sensorMotorTarget == MOTOR_TARGET_GROUP_1);
  page += F(">Motor Group 1</option><option value='2'");
  page += selectedAttribute(settings.sensorMotorTarget == MOTOR_TARGET_GROUP_2);
  page += F(">Motor Group 2</option></select></label>"
            "<label>Run time / safety timeout (seconds)<input type='number' name='sensortime' id='sensortime' min='1' max='3600' value='");
  page += String(settings.sensorRunSeconds);
  page += F("'></label></div><small>Any Sensor starts or extends a timed run. Enter-Exit starts on the first sensor reached and stops on the other; the timer is its fail-safe.</small></section>"
            "<p><button class='save' type='submit'>Save and restart</button></p></form>"
            "<dialog class='resetDialog' id='resetDialog' aria-labelledby='resetTitle'><div class='dialogCard'>"
            "<h2 id='resetTitle'>Confirm reset</h2><p id='resetMessage'></p><p class='dialogError hidden' id='resetError' role='alert'></p>"
            "<div class='dialogActions'><button class='secondary' id='cancelReset' type='button'>Cancel</button>"
            "<button class='confirm' id='confirmReset' type='button'>Confirm reset</button></div></div></dialog>"
            "<script>const link=document.getElementById('linked'),d1=document.getElementById('dir1'),"
            "d2=document.getElementById('dir2'),s1=document.getElementById('speed1'),s2=document.getElementById('speed2'),"
            "ba=document.querySelector('[name=begaction]'),bi=document.getElementById('begindef'),bt=document.getElementById('begtime'),"
            "sm=document.getElementById('sensormode'),st=document.getElementById('sensortarget'),tm=document.getElementById('sensortime');"
            "function sync(){const on=link.checked;if(on){d2.value=d1.value;s2.value=s1.value;}d2.disabled=on;s2.disabled=on;}"
            "function buttonUi(){const off=ba.value==='0';bi.disabled=off;bt.disabled=off||bi.checked;}"
            "function sensorUi(){const off=sm.value==='0';st.disabled=off;tm.disabled=off;}"
            "link.addEventListener('change',sync);d1.addEventListener('change',sync);s1.addEventListener('input',sync);"
            "ba.addEventListener('change',buttonUi);bi.addEventListener('change',buttonUi);"
            "sm.addEventListener('change',sensorUi);sync();buttonUi();sensorUi();"
            "const resetDefs={wifi:{title:'Reset stored Wi-Fi?',message:'This resets the saved Wi-Fi network name and password to the firmware defaults. The board will restart.',confirm:'Reset Wi-Fi'},"
            "mqtt:{title:'Reset stored MQTT?',message:'This resets the saved MQTT broker, port, username, and password to the firmware defaults. JMRI and device-specific settings remain unchanged. The board will restart.',confirm:'Reset MQTT'},"
            "factory:{title:'Factory restore this board?',message:'This erases all saved network, JMRI, device settings, and synchronized names, restores the firmware defaults, and restarts the board.',confirm:'Factory restore',danger:true}};"
            "const menuTrigger=document.getElementById('menuTrigger'),deviceMenu=document.getElementById('deviceMenu'),resetDialog=document.getElementById('resetDialog'),resetTitle=document.getElementById('resetTitle'),resetMessage=document.getElementById('resetMessage'),resetError=document.getElementById('resetError'),cancelReset=document.getElementById('cancelReset'),confirmReset=document.getElementById('confirmReset');let pendingReset='';"
            "function closeMenu(){deviceMenu.hidden=true;menuTrigger.setAttribute('aria-expanded','false')}menuTrigger.addEventListener('click',e=>{e.stopPropagation();deviceMenu.hidden=!deviceMenu.hidden;menuTrigger.setAttribute('aria-expanded',String(!deviceMenu.hidden))});"
            "document.addEventListener('click',e=>{if(!deviceMenu.hidden&&!e.target.closest('.menuWrap'))closeMenu()});document.addEventListener('keydown',e=>{if(e.key==='Escape')closeMenu()});"
            "document.querySelectorAll('[data-reset-action]').forEach(button=>button.addEventListener('click',()=>{pendingReset=button.dataset.resetAction;const def=resetDefs[pendingReset];resetTitle.textContent=def.title;resetMessage.textContent=def.message;resetError.textContent='';resetError.classList.add('hidden');confirmReset.textContent=def.confirm;confirmReset.classList.toggle('danger',!!def.danger);confirmReset.disabled=false;cancelReset.disabled=false;cancelReset.hidden=false;confirmReset.hidden=false;closeMenu();resetDialog.showModal()}));"
            "cancelReset.addEventListener('click',()=>resetDialog.close());resetDialog.addEventListener('click',e=>{if(e.target===resetDialog)resetDialog.close()});"
            "confirmReset.addEventListener('click',async()=>{if(!pendingReset)return;confirmReset.disabled=true;cancelReset.disabled=true;resetError.classList.add('hidden');try{const response=await fetch('/reset',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action='+encodeURIComponent(pendingReset)});if(!response.ok)throw new Error('The board rejected the reset request.');resetMessage.textContent='Reset accepted. The board is restarting now.';cancelReset.hidden=true;confirmReset.hidden=true}catch(error){resetError.textContent=error.message||'Unable to request the reset.';resetError.classList.remove('hidden');confirmReset.disabled=false;cancelReset.disabled=false}});"
            "</script></main></body></html>");
  return page;
}
#endif

long boundedFormNumber(const char* name, long fallback, long minimum, long maximum) {
  if (!configServer.hasArg(name)) return fallback;
  String value = configServer.arg(name);
  char* end = nullptr;
  long parsed = strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') return fallback;
  return constrain(parsed, minimum, maximum);
}

void copyFormText(const char* name, char* destination, size_t destinationSize, bool allowEmpty = true) {
  if (!configServer.hasArg(name)) return;
  String value = configServer.arg(name);
  value.trim();
  if (!allowEmpty && value.isEmpty()) return;
  snprintf(destination, destinationSize, "%s", value.c_str());
}

void handleConfigJson() {
  JsonDocument doc;
  updateConfigAccessPointSsid();
  doc["deviceId"] = deviceId;
  doc["firmware"] = FIRMWARE_VERSION;
  doc["configSsid"] = configApSsid;
  doc["connected"] = WiFi.status() == WL_CONNECTED;
  doc["ip"] = WiFi.status() == WL_CONNECTED
                  ? WiFi.localIP().toString()
                  : WiFi.softAPIP().toString();
  doc["linked"] = settings.linkMotorGroups;
  doc["hold"] = settings.holdWhenStopped;
  doc["stopLoss"] = settings.stopOnConnectionLoss;
  doc["begAction"] = settings.begButtonAction;
  doc["begIndefinite"] = settings.begButtonRunIndefinitely;
  doc["begTime"] = settings.begButtonRunSeconds;
  doc["globalBegEnabled"] = settings.globalBegLedsEnabled;

  JsonArray motorArray = doc["motors"].to<JsonArray>();
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    JsonObject motor = motorArray.add<JsonObject>();
    motor["direction"] = motors[i].reverse ? "ccw" : "cw";
    motor["speed"] = motors[i].speed;
  }

  JsonArray sensorArray = doc["sensors"].to<JsonArray>();
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
    JsonObject sensor = sensorArray.add<JsonObject>();
    sensor["present"] = ranges[i].present;
    sensor["threshold"] = ranges[i].thresholdMm;
  }

  doc["hysteresis"] = settings.occupiedHysteresisMm;
  doc["samplePeriod"] = settings.sensorSamplePeriodMs;
  doc["sensorMode"] = settings.sensorControlMode;
  doc["sensorTarget"] = settings.sensorMotorTarget;
  doc["sensorTime"] = settings.sensorRunSeconds;
  doc["sensorClearHoldMs"] = settings.sensorClearHoldMs;
  doc["wifiSsid"] = settings.wifiSsid;
  doc["mqttEnabled"] = settings.mqttEnabled;
  doc["mqttBroker"] = settings.mqttBroker;
  doc["mqttPort"] = settings.mqttPort;
  doc["mqttUser"] = settings.mqttUser;
  doc["jmriChannel"] = settings.jmriChannel;
  doc["otaState"] = DeviceOta::lastState;

  String json;
  serializeJson(doc, json);
  configServer.sendHeader("Cache-Control", "no-store");
  configServer.send(200, "application/json; charset=utf-8", json);
}

void handleConfigSave() {
  stopAllMotors(false);

  copyFormText("wifi_ssid", settings.wifiSsid, sizeof(settings.wifiSsid));
  if (!configServer.arg("wifi_password").isEmpty())
    copyFormText("wifi_password", settings.wifiPassword, sizeof(settings.wifiPassword));

  settings.mqttEnabled = configServer.hasArg("mqtt_enabled");
  copyFormText("mqtt_broker", settings.mqttBroker, sizeof(settings.mqttBroker), false);
  settings.mqttPort = boundedFormNumber("mqtt_port", settings.mqttPort, 1, 65535);
  copyFormText("mqtt_user", settings.mqttUser, sizeof(settings.mqttUser));
  if (!configServer.arg("mqtt_password").isEmpty())
    copyFormText("mqtt_password", settings.mqttPassword, sizeof(settings.mqttPassword));

  copyFormText("jmri_channel", settings.jmriChannel, sizeof(settings.jmriChannel), false);
  String channel(settings.jmriChannel);
  if (!channel.endsWith("/")) channel += '/';
  snprintf(settings.jmriChannel, sizeof(settings.jmriChannel), "%s", channel.c_str());

  settings.linkMotorGroups = configServer.hasArg("linked");
  motors[0].reverse = configServer.arg("dir1") == "ccw";
  motors[0].speed = boundedFormNumber("speed1", motors[0].speed,
                                      MIN_SPEED_STEPS_PER_SECOND, MAX_SPEED_STEPS_PER_SECOND);
  if (settings.linkMotorGroups) {
    motors[1].reverse = motors[0].reverse;
    motors[1].speed = motors[0].speed;
  } else {
    motors[1].reverse = configServer.arg("dir2") == "ccw";
    motors[1].speed = boundedFormNumber("speed2", motors[1].speed,
                                        MIN_SPEED_STEPS_PER_SECOND, MAX_SPEED_STEPS_PER_SECOND);
  }

  settings.holdWhenStopped = configServer.hasArg("hold");
  settings.stopOnConnectionLoss = configServer.hasArg("stoploss");
  if (configServer.hasArg("begenabled")) {
    settings.begButtonAction = static_cast<BegButtonAction>(boundedFormNumber(
        "begaction", BEG_BUTTON_BOTH_GROUPS, BEG_BUTTON_BOTH_GROUPS, BEG_BUTTON_GROUP_2));
    settings.begButtonRunSeconds = boundedFormNumber(
        "begtime", settings.begButtonRunSeconds, MIN_SENSOR_RUN_SECONDS, MAX_SENSOR_RUN_SECONDS);
  } else {
    settings.begButtonAction = BEG_BUTTON_DISABLED;
  }
  // The physical external button is always timed. JMRI can still run motor
  // groups indefinitely through their MQTT turnout commands.
  settings.begButtonRunIndefinitely = false;
  settings.sensorControlMode = static_cast<SensorControlMode>(boundedFormNumber(
      "sensormode", DEFAULT_SENSOR_CONTROL_MODE, SENSOR_CONTROL_DISABLED, SENSOR_CONTROL_ENTER_EXIT));
  if ((settings.sensorControlMode == SENSOR_CONTROL_ANY_TIMED &&
       !ranges[0].present && !ranges[1].present) ||
      (settings.sensorControlMode == SENSOR_CONTROL_ENTER_EXIT &&
       (!ranges[0].present || !ranges[1].present))) {
    settings.sensorControlMode = SENSOR_CONTROL_DISABLED;
  }
  settings.sensorMotorTarget = static_cast<MotorGroupTarget>(boundedFormNumber(
      "sensortarget", settings.sensorMotorTarget, MOTOR_TARGET_BOTH, MOTOR_TARGET_GROUP_2));
  settings.sensorRunSeconds = boundedFormNumber(
      "sensortime", settings.sensorRunSeconds, MIN_SENSOR_RUN_SECONDS, MAX_SENSOR_RUN_SECONDS);
  settings.sensorClearHoldMs = boundedFormNumber(
      "sensorclear", settings.sensorClearHoldMs, 0, MAX_SENSOR_CLEAR_HOLD_MS);
  ranges[0].thresholdMm = boundedFormNumber("thr1", ranges[0].thresholdMm, 20, 2000);
  ranges[1].thresholdMm = boundedFormNumber("thr2", ranges[1].thresholdMm, 20, 2000);
  settings.occupiedHysteresisMm = boundedFormNumber("hyst", settings.occupiedHysteresisMm, 0, 500);
  settings.sensorSamplePeriodMs = boundedFormNumber("sample", settings.sensorSamplePeriodMs, 30, 1000);

  saveAllSettings();
  configServer.sendHeader("Cache-Control", "no-store");
  configServer.send(200, "text/html; charset=utf-8",
      "<!doctype html><meta name='viewport' content='width=device-width'><style>body{font-family:system-ui;padding:2rem;max-width:40rem;margin:auto}a{color:#1769d2}</style><h1>Settings saved</h1><p>The Motor Controller is restarting now.</p><p>If this page does not close, reconnect to its normal Wi-Fi network.</p>");
  restartAtMs = millis() + 1500;
}

void handleConfigReset() {
  String action = configServer.arg("action");

  if (action == "wifi") {
    preferences.remove("wifiSsid");
    preferences.remove("wifiPass");
  } else if (action == "mqtt") {
    preferences.remove("mqttEn");
    preferences.remove("mqttHost");
    preferences.remove("mqttPort");
    preferences.remove("mqttUser");
    preferences.remove("mqttPass");
    preferences.remove("mqttPref");
  } else if (action == "factory") {
    preferences.clear();
  } else {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"Unknown reset action\"}");
    return;
  }

  stopAllMotors(false);
  configServer.sendHeader("Cache-Control", "no-store");
  configServer.send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
  restartAtMs = millis() + 800;
}

void handleConfigRoot() {
  configServer.sendHeader("Cache-Control", "no-store");
  configServer.sendHeader("Content-Encoding", "gzip");
  configServer.send_P(200, "text/html; charset=utf-8",
                      reinterpret_cast<PGM_P>(MOTOR_CONTROLLER_PORTAL_HTML),
                      MOTOR_CONTROLLER_PORTAL_HTML_LENGTH);
}

void handleConfigOta() {
  String action = configServer.hasArg("action") ? configServer.arg("action") : "STATUS";
  action.trim();
  action.toUpperCase();
  const bool safeForOta = !anyMotorMoving();
  if (action == "CHECK") DeviceOta::check(mqtt, settings.mqttEnabled, safeForOta, false);
  else if (action == "FORCE") DeviceOta::check(mqtt, settings.mqttEnabled, safeForOta, true);
  configServer.sendHeader("Location", "/", true);
  configServer.send(303, "text/plain", "");
}

void startConfigAccessPoint() {
  if (configApActive) return;
  stopAllMotors(false);
  updateConfigAccessPointSsid();

  WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP(configApSsid)) {
    configDns.start(53, "*", WiFi.softAPIP());
    configApActive = true;
    Serial.printf("Open config portal: %s at http://%s\n", configApSsid,
                  WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println(F("Unable to start configuration access point"));
  }
}

void initializeConfigPortal() {
  configServer.on("/", HTTP_GET, handleConfigRoot);
  configServer.on("/api/config", HTTP_GET, handleConfigJson);
  configServer.on("/save", HTTP_POST, handleConfigSave);
  configServer.on("/reset", HTTP_POST, handleConfigReset);
  configServer.on("/ota", HTTP_ANY, handleConfigOta);
  configServer.on("/generate_204", HTTP_ANY, handleConfigRoot);
  configServer.on("/hotspot-detect.html", HTTP_ANY, handleConfigRoot);
  configServer.on("/ncsi.txt", HTTP_ANY, handleConfigRoot);
  configServer.onNotFound(handleConfigRoot);
  configServer.begin();
  configServerStarted = true;
  configPortalBootMs = millis();

  lastButtonReading = digitalRead(PIN_BEG_BUTTON);
  stableButtonState = lastButtonReading;
  if (settings.wifiSsid[0] == '\0') startConfigAccessPoint();
}

void serviceConfigPortal() {
  if (restartAtMs && static_cast<int32_t>(millis() - restartAtMs) >= 0) {
    stopAllMotors(false);
    delay(20);
    ESP.restart();
  }

  if (!configApActive && !anyMotorMoving() && WiFi.status() != WL_CONNECTED &&
      millis() - configPortalBootMs >= CONFIG_PORTAL_FALLBACK_MS) {
    startConfigAccessPoint();
  }

  // Serving a page can take long enough to disturb step timing, so configuration
  // requests wait until both motor groups are stopped.
  if (!anyMotorMoving()) {
    if (configApActive) configDns.processNextRequest();
    if (configServerStarted) configServer.handleClient();
  }
}
