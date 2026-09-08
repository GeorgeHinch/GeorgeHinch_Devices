# audio_sensor v0.1.36

- Status: Clean build verified
- Sketch: `audio_sensor_0_1_36.ino`
- Binary: `audio_sensor_0_1_36.bin`
- Board: `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- Binary size: 1,350,224 bytes
- Binary SHA-256: `16e239b8a22966eaca86a828f463130156930794626b2dba4eb8c2fec4de5a95`

This release keeps the event-driven station state behavior from 0.1.35 and
prevents optional JMRI output topics from echoing the sensor's own publications
in a loop.

Run `tools\Publish-Firmware.ps1 -DeviceType audio_sensor -Version 0.1.36 -Push` to compile, hash, commit, tag, and push this release.
