import { readFileSync, writeFileSync } from 'node:fs';
import { basename, join, resolve } from 'node:path';
import { gzipSync } from 'node:zlib';

const directory = resolve(process.argv[2] || '.');
const sharedDirectory = resolve(directory, '..', '..', 'shared');
const symbol = process.argv[3];
const outputName = process.argv[4];
if (!symbol || !outputName) {
  throw new Error('Usage: node tools/Generate-PortalAsset.mjs <directory> <SYMBOL> <output.h>');
}

let html = readFileSync(join(directory, 'portal.html'), 'utf8');
const css = readFileSync(join(directory, 'portal.css'), 'utf8');
const js = readFileSync(join(directory, 'portal.js'), 'utf8');
const rendererCss = readFileSync(join(sharedDirectory, 'device-manifest-renderer.css'), 'utf8');
const portalShellCss = readFileSync(join(sharedDirectory, 'device-portal-shell.css'), 'utf8');
const rendererJs = readFileSync(join(sharedDirectory, 'device-manifest-renderer.js'), 'utf8');
const portalJs = readFileSync(join(sharedDirectory, 'device-manifest-portal.js'), 'utf8');
html = html.replace(/<link[^>]+data-manifest-renderer-css[^>]*>/, `<style>${rendererCss}</style>`);
html = html.replace(/<link[^>]+data-portal-css[^>]*>/, `<style>${portalShellCss}\n${css}</style>`);
html = html.replace(/<script[^>]+data-manifest-renderer-js[^>]*><\/script>/, `<script>${rendererJs}</script>`);
html = html.replace(/<script[^>]+data-manifest-portal-js[^>]*><\/script>/, `<script>${portalJs}</script>`);
html = html.replace(/<script[^>]+data-portal-js[^>]*><\/script>/, `<script>${js}</script>`);
const compressed = gzipSync(Buffer.from(html), { level: 9 });
const rows = [];
for (let offset = 0; offset < compressed.length; offset += 16) {
  rows.push(`  ${[...compressed.subarray(offset, offset + 16)].map(value => `0x${value.toString(16).padStart(2, '0')}`).join(', ')},`);
}
const source = `#pragma once
#include <Arduino.h>

// Gzip-compressed portal generated from the shared manifest renderer and this device shell.
const uint8_t ${symbol}[] PROGMEM = {
${rows.join('\n')}
};
const size_t ${symbol}_LENGTH = sizeof(${symbol});
`;
writeFileSync(join(directory, basename(outputName)), source);
console.log(`${outputName}: ${compressed.length} compressed bytes`);
