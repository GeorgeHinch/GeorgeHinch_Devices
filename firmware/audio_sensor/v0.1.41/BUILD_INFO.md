# audio_sensor v0.1.41

- Status: Clean compile passed
- Sketch: `audio_sensor_0_1_41.ino`
- Binary: `audio_sensor_0_1_41.bin`
- Binary size: 1,369,072 bytes
- Binary SHA-256: `2c13d4bc2700d01e45bed82c842c69f0da8a11705f7982b6a20bad46386299fa`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,368,913 / 1,966,080 bytes (69%)
- Dynamic memory: 43,588 / 327,680 bytes (13%)
- Change: keeps the device HTTP API available after enrollment for router-requested live sensor and player telemetry, while MQTT publishes only discrete state changes such as occupancy and calibration completion.

Run `tools\Publish-Firmware.ps1 -DeviceType audio_sensor -Version 0.1.41 -Push` to compile, hash, commit, tag, and push this release.
