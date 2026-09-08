# motor_controller v1.1.31

- Status: Clean compile passed
- Sketch: `motor_controller_1_1_31.ino`
- Binary: `motor_controller_1_1_31.bin`
- Binary size: 1,350,256 bytes
- Binary SHA-256: `DCA112125202C2611402F4C6682D664D3C0B2414862A3F71481E458FD5331DA9`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,350,101 / 1,966,080 bytes (68%)
- Dynamic memory: 44,604 / 327,680 bytes (13%)
- Change: the two motor-status LEDs now advertise stable JMRI resources, friendly-name controls, and a transient 350 ms identification blink limited to one LED at a time and stopped automatically after 30 seconds.

Run `tools\Publish-Firmware.ps1 -DeviceType motor_controller -Version 1.1.31 -Push` to compile, hash, commit, tag, and push this release.
