# audio_sensor v0.1.43

- Status: Clean compile passed
- Sketch: `audio_sensor_0_1_43.ino`
- Binary: `audio_sensor_0_1_43.bin`
- Binary size: 1,369,440 bytes
- Binary SHA-256: `6e8cf5ace27dd20f87c34cfccfd77a2f50ccda14aefd95a279c50985c4813c4e`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,369,283 / 1,966,080 bytes (69%)
- Dynamic memory: 43,588 / 327,680 bytes (13%)
- Change: advertises audio volume as a 0–30 range and presents it as a live-value slider in both the device portal and manifest-driven Station interface.

Run `tools\Publish-Firmware.ps1 -DeviceType audio_sensor -Version 0.1.43 -Push` to compile, hash, commit, tag, and push this release.
