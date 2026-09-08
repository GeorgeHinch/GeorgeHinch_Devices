# audio_sensor v0.1.40

- Status: Clean compile passed
- Sketch: `audio_sensor_0_1_40.ino`
- Binary: `audio_sensor_0_1_40.bin`
- Binary size: 1,368,656 bytes
- Binary SHA-256: `72b0c6f64c2b7a25c91733c096cafa659569911db04b76950583006d371a4a92`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,368,505 / 1,966,080 bytes (69%)
- Dynamic memory: 43,580 / 327,680 bytes (13%)
- Change: verifies the DFPlayer over RX/TX, reports live player status, adopts action terminology, and simplifies the device configuration header.

Run `tools\Publish-Firmware.ps1 -DeviceType audio_sensor -Version 0.1.40 -Push` to compile, hash, commit, tag, and push this release.
