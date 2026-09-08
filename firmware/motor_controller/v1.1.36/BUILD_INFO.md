# motor_controller v1.1.36

- Status: Clean build completed; source and generated portal checks passed
- Sketch: `motor_controller_1_1_36.ino`
- Binary: `motor_controller_1_1_36.bin`
- Binary size: 1,363,840 bytes
- Binary SHA-256: `dfbe1b316af1da5398f12b74b7c5129e63613e736891039c92e6146b7bd53d7b`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,363,597 / 1,966,080 bytes (69%)
- Dynamic memory: 44,628 / 327,680 bytes (13%)
- Change: publishes a complete Station state after physical-button, sensor-automation, local portal, and JMRI motor transitions so every start and stop is immediately reflected by the router.

Run `tools\Publish-Firmware.ps1 -DeviceType motor_controller -Version 1.1.36 -Push` to compile, hash, commit, tag, and push this release.
