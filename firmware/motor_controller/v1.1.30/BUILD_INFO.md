# motor_controller v1.1.30

- Status: Clean build verified and deployed to `MOTORCON_E83DC1821F8C`
- Sketch: `motor_controller_1_1_30.ino`
- Binary: `motor_controller_1_1_30.bin`
- Board: `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- ESP32 Arduino core: 3.3.10
- Binary size: 1,349,216 bytes
- Binary SHA-256: `56bef55376410b5521c7bfdada5fcf7887b651f2b0f8ec9d09a666e7974623c0`

This release publishes the complete motor-controller configuration as a
`hakomachi.device-ui/v1` manifest. The station can now build its controls from
the firmware-defined motor, safety, LED, sensor, button, and software settings,
apply saved settings through MQTT, and issue live motor and calibration commands.

Run `tools\Publish-Firmware.ps1 -DeviceType motor_controller -Version 1.1.30 -Push` to compile, hash, commit, tag, and push this release.
