# motor_controller v1.1.28

- Status: Clean build verified
- Sketch: `motor_controller_1_1_28.ino`
- Binary: `motor_controller_1_1_28.bin`
- Board: `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- Binary size: 1,334,096 bytes
- Binary SHA-256: `e951fcdddd010a15e3c055b6cc6cd3f8705b7479c3aca5eca6ebdeb47bd6b3fa`

This release removes the fixed 10-second full-state heartbeat. Retained state is
published when MQTT connects and after requested commands; real hardware events
remain event-driven.

Run `tools\Publish-Firmware.ps1 -DeviceType motor_controller -Version 1.1.28 -Push` to compile, hash, commit, tag, and push this release.
