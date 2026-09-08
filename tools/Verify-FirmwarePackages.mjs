import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { gunzipSync } from 'node:zlib';
const root = new URL('../', import.meta.url);
const text = name => readFileSync(new URL(name, root), 'utf8');
const normalize = value => value.replace(/\r\n/g, '\n');
for (const [device, version] of [['audio_sensor','0.1.44'], ['motor_controller','1.1.37'], ['triple_audio_player','2.1.23']]) {
  const folder = `firmware/${device}/v${version}`;
  const stem = `${device}_${version.replaceAll('.', '_')}`;
  assert.deepEqual(readdirSync(new URL(folder, root)).filter(name => name.endsWith('.bin')), [`${stem}.bin`], `${device}: one unambiguous application image per package`);
  const binary = readFileSync(new URL(`${folder}/${stem}.bin`, root));
  const hash = createHash('sha256').update(binary).digest('hex');
  const info = text(`${folder}/BUILD_INFO.md`).toLowerCase();
  assert.ok(info.includes(hash), `${device}: recorded SHA-256 must match the binary`);
  assert.ok(info.replaceAll(',', '').includes(`${binary.length} bytes`), `${device}: recorded binary size must match`);
  assert.ok(text(`${folder}/${stem}.ino`).includes(`"${version}"`), `${device}: source version must match package`);
  const header = text(`${folder}/${device}_portal.h`);
  const compressed = Buffer.from([...header.matchAll(/0x([0-9a-f]{2})/gi)].map(match => parseInt(match[1], 16)));
  assert.ok(binary.includes(compressed), `${device}: compiled firmware must contain this exact generated portal`);
  const html = normalize(gunzipSync(compressed).toString());
  for (const asset of ['device-manifest-renderer.css','device-manifest-renderer.js','device-manifest-portal.js']) {
    assert.ok(html.includes(normalize(text(`firmware/shared/${asset}`))), `${device}: generated portal must include current shared ${asset}`);
  }
  console.log(`PASS: ${device} v${version}, ${binary.length} bytes, SHA-256 ${hash}, current shared portal assets`);
}
