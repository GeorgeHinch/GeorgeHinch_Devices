# audio_sensor v0.1.35

- Status: Clean build verified
- Sketch: `audio_sensor_0_1_35.ino`
- Binary: `audio_sensor_0_1_35.bin`
- Board: `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- Binary size: 1,350,240 bytes
- Binary SHA-256: `1a061b3ecfc91309d4f0df4a98aef11e34cc10b944a171823206d78d5a048f04`

This release removes the fixed 10-second full-state heartbeat. Retained state is
published when MQTT connects and after requested commands; real sensor events
remain event-driven.

Run `tools\Publish-Firmware.ps1 -DeviceType audio_sensor -Version 0.1.35 -Push` to compile, hash, commit, tag, and push this release.
