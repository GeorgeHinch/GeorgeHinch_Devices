import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../firmware/audio_sensor/v0.1.44/audio_sensor_0_1_44.ino', import.meta.url), 'utf8');
// Execute the actual small C++ state functions with only scalar syntax adapted
// to JavaScript. Full Arduino compilation separately checks C++ integration.
function body(name) {
  const definition = new RegExp(`^[\\w:*& ]+ ${name}\\([^;{}]*\\)\\s*\\{`, 'm').exec(source);
  assert.ok(definition, name);
  const start = definition.index;
  const open = source.indexOf('{', start);
  let depth = 1, end = open + 1;
  while (depth && end < source.length) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
    end++;
  }
  return source.slice(open + 1, end - 1);
}
const ctx = vm.createContext({
  EXTERNAL_OUTPUT_COUNT: 8, OUTPUT_RESERVED: 0, OUTPUT_JMRI: 1, OUTPUT_SHARED: 2,
  JMRI_ACTION: 0, JMRI_SENSOR_1: 1, JMRI_SENSOR_2: 2, JMRI_OUTPUT_1: 3, JMRI_OBJECT_COUNT: 11,
  OUTPUT_TEST_BLINK_MS: 350, OUTPUT_TEST_TIMEOUT_MS: 30000,
  cfg: {externalOutputAssignment: Array(8).fill(0)},
  actionActive: false, jmriOutputState: Array(8).fill(false),
  activeOutputTest: -1, outputTestPhase: false, outputTestStartedMs: 0,
  outputLocalMask: 0, outputRemoteMask: 0, outputPhysicalMask: 0,
  outputPattern: 0, outputActionActive: false, outputEffectStartedMs: 0,
  outputTestEndedPending: false, outputTaskHandle: 1, outputMux: 0,
  LIGHT_PATTERN_ON: 2, LIGHT_PATTERN_STROBE: 1, FLASH_INTERVAL_MS: 180,
  millis: () => 0, portENTER_CRITICAL: () => {}, portEXIT_CRITICAL: () => {},
});
for (const [name, args] of Object.entries({operationalOutputState:'index', calculateOutputWaveform:'now',
  refreshExternalOutputs:'', jmriObjectState:'index', setOutputTest:'index, enabled', serviceOutputTest:'now'})) {
  const adapted = body(name).replace(/static_cast<[^>]+>\(([^()]*)\)/g, '($1)')
    .replace(/const (?:bool|OutputAssignment|uint8_t) /g, 'const ')
    .replace(/uint8_t /g, 'let ').replace(/1U/g, '1')
    .replace(/&outputMux/g, 'outputMux').replace(/nullptr/g, 'null')
    .replace(/\(now - (outputEffectStartedMs|outputTestStartedMs)\)/g, '((now - $1) >>> 0)')
    .replace(/\(\(\(now - (outputEffectStartedMs|outputTestStartedMs)\) >>> 0\) \/ (FLASH_INTERVAL_MS|OUTPUT_TEST_BLINK_MS)\)/g,
      'Math.floor(((now - $1) >>> 0) / $2)');
  vm.runInContext(`function ${name}(${args}) {${adapted}}`, ctx);
}
let cases = 0;
for (let index = 0; index < 8; index++) for (let assignment = 0; assignment < 3; assignment++) {
  for (const action of [false,true]) for (const pattern of [0,1,2]) for (const remote of [false,true]) {
    ctx.cfg.externalOutputAssignment[index] = assignment;
    ctx.actionActive = action;
    ctx.cfg.lightPattern = pattern;
    ctx.jmriOutputState[index] = remote;
    const localOn = index < 2 && action;
    const expected = assignment === 0 ? localOn : assignment === 1 ? remote : localOn || remote;
    ctx.setOutputTest(index, true);
    for (const [time, phase] of [[0,true],[350,false],[700,true]]) {
      const mask = ctx.calculateOutputWaveform(time);
      assert.equal(Boolean(mask & (1 << index)), phase, 'physical test output');
      assert.equal(ctx.jmriObjectState(index + 3), expected ? 'ON' : 'OFF', 'JMRI ignores blink phase');
      cases++;
    }
    ctx.setOutputTest(index, false);
    assert.equal(ctx.operationalOutputState(index), expected, 'stop preserves operational state');
  }
}
ctx.setOutputTest(2, true);
ctx.calculateOutputWaveform(350);
assert.equal(ctx.serviceOutputTest(350), false, 'blink does not request Station publish');
assert.equal(ctx.outputTestPhase, false);
assert.equal(ctx.serviceOutputTest(700), false);
ctx.calculateOutputWaveform(30000);
assert.equal(ctx.serviceOutputTest(30000), true, 'timeout reports test ended once');
assert.equal(ctx.activeOutputTest, -1);
assert.equal(ctx.serviceOutputTest(30001), false);
for (const name of ['serviceOutputTest','refreshExternalOutputs','calculateOutputWaveform','outputWaveformTask']) {
  assert.doesNotMatch(body(name), /mqttClient|publishJmri|publishStation|publishMqtt/);
}
assert.match(body('publishJmriDiscovery'), /jmriObjectState\(i\)/);
assert.match(body('publishJmriObjectState'), /jmriObjectState\(index\)/);
assert.doesNotMatch(body('publishStationState'), /effectiveOutputState|outputTestPhase/);
assert.match(body('publishStationState'), /appendStationConfiguration\(doc\)/);
assert.match(source, /appendStationConfiguration\(doc, true\)/);
assert.match(body('appendStationConfiguration'), /if \(includeLiveOutputs\)/);
assert.doesNotMatch(body('outputWaveformTask'), /mp3|mqttClient|webServer|prefs|Wire/);
assert.match(body('publishMqttState'), /if \(actionActive != lastPublishedActionState\)/);
assert.match(body('publishMqttState'), /publishJmriDiscovery\(\)/);
assert.doesNotMatch(body('publishMqttState'), /outputTestPhase|outputPhysicalMask/);
// The existing shared renderer can display the new firmware-defined fields.
const browser = {window:{}};
vm.runInNewContext(readFileSync(new URL('../firmware/shared/device-manifest-renderer.js', import.meta.url), 'utf8'), browser);
const markup = browser.window.HakoMachiManifest.render({values:{oCommanded:'On',oSource:'Output test',oEffect:'Test blink',oLevel:'Low'},
  sections:[{id:'outputs',layout:'one',fields:[{key:'oStatus',type:'status-group',readOnly:true,label:'Output 1 live status',
    items:['Commanded','Source','Effect','Level'].map(suffix=>({key:`o${suffix}`,label:suffix}))}]}]});
for (const label of ['Output 1 live status','Output test','Test blink','Low']) assert.ok(markup.includes(label));
// A long wait in the main loop does not prevent servicing the waveform task.
ctx.outputLocalMask = 3; ctx.outputRemoteMask = 0; ctx.outputPattern = 0;
ctx.outputEffectStartedMs = 0;
assert.equal(ctx.calculateOutputWaveform(0), 2);
assert.equal(ctx.calculateOutputWaveform(180), 1);
assert.equal(ctx.calculateOutputWaveform(360), 2);
assert.equal(ctx.calculateOutputWaveform(540), 1);
ctx.outputPattern = 1;
assert.equal(ctx.calculateOutputWaveform(0), 0);
assert.equal(ctx.calculateOutputWaveform(180), 3);
ctx.outputPattern = 2;
assert.equal(ctx.calculateOutputWaveform(180), 3);
ctx.outputPattern = 1; ctx.outputRemoteMask = 1;
assert.equal(ctx.calculateOutputWaveform(0), 1, 'shared JMRI ON overrides dark effect phase');
ctx.setOutputTest(7, true);
ctx.outputTestStartedMs = 0xffffff00;
assert.equal(Boolean(ctx.calculateOutputWaveform(0x5e) & 128), false, 'timer rollover at 350ms');
ctx.calculateOutputWaveform((0xffffff00 + 30000) >>> 0);
assert.equal(ctx.activeOutputTest, -1, 'timeout across rollover');
console.log(`PASS: ${cases} effect/assignment/action/JMRI/test-phase combinations; waveform cadence, timeout, rollover, and HTTP-only diagnostics`);
