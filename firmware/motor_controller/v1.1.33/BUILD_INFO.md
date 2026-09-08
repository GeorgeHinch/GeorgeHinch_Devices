# motor_controller v1.1.33

- Status: Clean build completed; source and generated portal checks passed
- Sketch: `motor_controller_1_1_33.ino`
- Binary: `motor_controller_1_1_33.bin`
- Binary size: 1,362,912 bytes
- Binary SHA-256: `97d6bb92c9934b74969a713b3816ec8615741b38b44e93aec33d424f498d200b`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,362,753 / 1,966,080 bytes (69%)
- Dynamic memory: 44,628 / 327,680 bytes (13%)
- Change: makes positioned arcs highlight the exact Start-to-End sweep, adds clockwise/counter-clockwise route selection, removes the obsolete active-side setting, and makes motor travel follow the selected route in both directions.

Run `tools\Publish-Firmware.ps1 -DeviceType motor_controller -Version 1.1.33 -Push` to compile, hash, commit, tag, and push this release.
