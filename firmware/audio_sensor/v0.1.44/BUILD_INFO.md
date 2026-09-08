# audio_sensor v0.1.44

- Status: Clean compile passed; rebuilt 2026-09-07 with current shared portal assets
- Sketch: `audio_sensor_0_1_44.ino`
- Binary: `audio_sensor_0_1_44.bin`
- Binary size: 1,377,456 bytes
- Binary SHA-256: `641a179cab7c270e65a91f97377b16cce7fbafaff0f9425e8f3a66d83a5b9ab5`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,377,303 / 1,966,080 bytes (70%)
- Dynamic memory: 43,588 / 327,680 bytes (13%)
- Change: replaces the legacy portal form with the shared manifest renderer, live polling, local commands, and restart-free configuration saves.
- Output tests: physical blink phases are local only. JMRI state and discovery use operational output state, never the test override. Test start/stop/timeout reporting is unchanged.
- Output timing: dedicated waveform task isolates blink/strobe/test timing from blocking main-loop audio and network calls. HTTP-only diagnostics expose commanded state, source, effect, and last commanded output level. Effect state remains ON throughout bright/dark phases.
- Verification: Arduino compilation passed; `node tools/test-audio-output-mqtt.mjs` passed 864 state combinations plus cadence, stop, timeout, timer-rollover, publication-boundary, and shared-renderer checks.
- Deployment: this build has not been flashed to the device.

Run `tools\Publish-Firmware.ps1 -DeviceType audio_sensor -Version 0.1.44 -Push` to compile, hash, commit, tag, and push this release.
