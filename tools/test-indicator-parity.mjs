import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const root = new URL('../', import.meta.url);
const shared = readFileSync(new URL('firmware/shared/LocalIndicatorOutputs.h', root), 'utf8');
function body(source, name) {
  const match = new RegExp(`^[\\w:*& ]+ ${name}\\([^;{}]*\\)\\s*\\{`, 'm').exec(source);
  assert.ok(match, name);
  const start = source.indexOf('{', match.index);
  let end = start + 1, depth = 1;
  while (depth) { if(source[end]==='{') depth++; if(source[end]==='}') depth--; end++; }
  return source.slice(start + 1, end - 1);
}
const context = vm.createContext({normalMask:0,testIndex:-1,testStarted:0,ended:false});
for (const [name, args] of [['test','index, enabled, now'],['step','now']]) {
  const cpp = body(shared, name).replace(/static_cast<[^>]+>\(([^()]*)\)/g,'($1)')
    .replace(/const uint(?:8|32)_t /g,'const ').replace(/uint8_t /g,'let ')
    .replace(/1U/g,'1').replace(/now - testStarted/g,'((now - testStarted) >>> 0)')
    .replace(/\(elapsed \/ 350\)/g,'Math.floor(elapsed / 350)');
  vm.runInContext(`function ${name}(${args}) {${cpp}}`, context);
}
let checks = 0;
for (let normal = 0; normal < 256; normal++) for (let index = 0; index < 8; index++) {
  context.normalMask = normal;
  context.test(index,true,1000);
  for (const elapsed of [0,349,350,699,700,29999]) {
    const actual = context.step(1000 + elapsed);
    const bit = 1 << index;
    const expected = Math.floor(elapsed / 350) % 2 === 0 ? normal | bit : normal & ~bit;
    assert.equal(actual,expected);
    assert.equal(context.normalMask,normal,'test must not change reported logical state');
    checks++;
  }
  assert.equal(context.step(31000),normal,'timeout restores current normal output');
  assert.equal(context.testIndex,-1);
  assert.equal(context.ended,true);
  context.test(index,true,32000);
  context.normalMask = normal ^ 255;
  context.test(index,false,32500);
  assert.equal(context.step(32500),normal ^ 255,'stop restores latest command, not a stale saved one');
}
context.test(0,true,0xffffff00);
assert.equal(context.step(0x5e)&1,0,'unsigned rollover at 350 ms');
context.step((0xffffff00 + 30000) >>> 0);
assert.equal(context.testIndex,-1);
context.test(0,true,0); context.test(1,true,10); context.test(0,false,20);
assert.equal(context.testIndex,1,'stopping a different output does not cancel selected test');
assert.doesNotMatch(body(shared,'service'),/mqtt|publish|Serial|delay\(|Preferences|HTTP|Wire/);
const targets = [['motor_controller','1.1.37','motor_controller_1_1_37'],['triple_audio_player','2.1.23','triple_audio_player_2_1_23']];
for (const [type,version,stem] of targets) {
  const dir = `firmware/${type}/v${version}/`;
  assert.equal(readFileSync(new URL(dir+'LocalIndicatorOutputs.h',root),'utf8'),shared,'embedded helper matches shared source');
  const source = readFileSync(new URL(dir+stem+'.ino',root),'utf8');
  const portal = readFileSync(new URL(dir+'portal.ino',root),'utf8');
  assert.match(portal,/appendStationConfiguration\(doc, true\)/);
  assert.match(body(source,'publishStationState'),/appendStationConfiguration\(doc\)/);
  assert.doesNotMatch(body(source,'publishStationState'),/physicalMask|testIndex/);
  assert.doesNotMatch(source,/digitalWrite\(PIN_(?:MOTOR1_LED|MOTOR2_LED|BEG_LED)/);
  assert.match(source,/if \(includeLiveOutputs\) indicatorOutputs.appendLive/);
  assert.match(source,/consumeEnded\(\)/);
  if(type==='motor_controller') {
    assert.doesNotMatch(body(source,'operationalMotorLedState'),/testIndex|TestPhase|snapshot/);
    assert.match(body(source,'setMotorLed'),/operationalMotorLedState/);
  }
}
// All status items use the existing shared renderer; read-only assignment still
// permits the local test button and does not create an editable network setting.
const browser = {window:{}};
vm.runInNewContext(readFileSync(new URL('firmware/shared/device-manifest-renderer.js',root),'utf8'),browser);
const markup=browser.window.HakoMachiManifest.render({values:{begLedAssignment:0,begLedTesting:true},sections:[{fields:[{
  key:'begLedAssignment',type:'output',readOnly:true,testControl:'begLed',testStateKey:'begLedTesting',options:[{value:0,label:'Managed ready indicator'}]
}]}]});
assert.match(markup,/Stop test/);
assert.match(markup,/data-config-value="false"/);
assert.doesNotMatch(markup,/<button[^>]*data-config-action[^>]* disabled/);
console.log(`PASS: ${checks} waveform cases, latest-state restore, timeout, rollover, isolated MQTT/HTTP paths, helper parity, renderer`);
