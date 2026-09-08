# motor_controller v1.1.32

- Status: Clean compile passed and flashed to `MOTORCON_E83DC1821F8C`
- Sketch: `motor_controller_1_1_32.ino`
- Binary: `motor_controller_1_1_32.bin`
- Binary size: 1,361,328 bytes
- Binary SHA-256: `FA276488B89B8C582BC584EBAF8AE46C34569EC5672496CFA21FFE9409EDCFF5`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,361,171 / 1,966,080 bytes (69%)
- Dynamic memory: 44,628 / 327,680 bytes (13%)
- Change: adds a positioned-arc motor mode with persistent start/end points, selectable active endpoint, endpoint moves, circular position previews, and ±1/±10/±50 half-step setup controls in both the Station manifest and the board portal.

Run `tools\Publish-Firmware.ps1 -DeviceType motor_controller -Version 1.1.32 -Push` to commit, tag, and publish this release.
