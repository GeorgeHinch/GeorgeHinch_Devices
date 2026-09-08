# motor_controller v1.1.35

- Status: Clean build completed; source and generated portal checks passed
- Sketch: `motor_controller_1_1_35.ino`
- Binary: `motor_controller_1_1_35.bin`
- Binary size: 1,363,744 bytes
- Binary SHA-256: `ae3313803923ee58c78071d910fe042f93140953264d7f4d29388f5349167ff4`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,363,597 / 1,966,080 bytes (69%)
- Dynamic memory: 44,628 / 327,680 bytes (13%)
- Change: disables unavailable sensor calibration controls visibly and stacks the two onboard LED outputs with concise, consistent labels while preserving stable JMRI resource IDs.

Run `tools\Publish-Firmware.ps1 -DeviceType motor_controller -Version 1.1.35 -Push` to compile, hash, commit, tag, and push this release.
