# audio_sensor v0.1.42

- Status: Clean compile passed
- Sketch: `audio_sensor_0_1_42.ino`
- Binary: `audio_sensor_0_1_42.bin`
- Binary size: 1,369,232 bytes
- Binary SHA-256: `15c2be799100ecbc20619aa5975d65f1d2fdb9f0cf9f1461041a2c434e2fba4a`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,369,081 / 1,966,080 bytes (69%)
- Dynamic memory: 43,588 / 327,680 bytes (13%)
- Change: groups the DFPlayer serial link, playback, reported track, reported volume, and latest response into one full-width reported-status card in manifest-driven interfaces.

Run `tools\Publish-Firmware.ps1 -DeviceType audio_sensor -Version 0.1.42 -Push` to compile, hash, commit, tag, and push this release.
