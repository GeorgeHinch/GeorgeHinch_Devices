# audio_sensor v0.1.39

- Status: Clean compile passed
- Sketch: `audio_sensor_0_1_39.ino`
- Binary: `audio_sensor_0_1_39.bin`
- Binary size: 1,365,568 bytes
- Binary SHA-256: `09430E14A7DB4B90883BC5B6ABC7FB6828AACEDB63640F0ECE383990FAB53120`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,365,417 / 1,966,080 bytes (69%)
- Dynamic memory: 43,548 / 327,680 bytes (13%)
- Change: external outputs now advertise first-class output controls with friendly JMRI names and a transient 350 ms identification blink, limited to one output at a time and stopped automatically after 30 seconds.

Run `tools\Publish-Firmware.ps1 -DeviceType audio_sensor -Version 0.1.39 -Push` to compile, hash, commit, tag, and push this release.
