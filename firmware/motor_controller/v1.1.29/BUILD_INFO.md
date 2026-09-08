# motor_controller v1.1.29

- Status: Clean build verified and deployed to `MOTORCON_E83DC1821F8C`
- Sketch: `motor_controller_1_1_29.ino`
- Binary: `motor_controller_1_1_29.bin`
- Board: `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- Binary size: 1,334,112 bytes
- Binary SHA-256: `8877c29ba6f5499085e98e7c0f9f440e7f9131bf5c43a8d022fe71a9c6246f31`

This release keeps the event-driven station state behavior from 1.1.28 and
prevents subscribed JMRI direction, speed, and light topics from echoing the
controller's own publications in a loop.

Run `tools\Publish-Firmware.ps1 -DeviceType motor_controller -Version 1.1.29 -Push` to compile, hash, commit, tag, and push this release.
