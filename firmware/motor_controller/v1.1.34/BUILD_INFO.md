# motor_controller v1.1.34

- Status: Clean build completed; source and generated portal checks passed
- Sketch: `motor_controller_1_1_34.ino`
- Binary: `motor_controller_1_1_34.bin`
- Binary size: 1,363,760 bytes
- Binary SHA-256: `6622220e878abbb2b3804f92d82633efa042f0d68233adcd02c0a8ab2d78146f`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,363,605 / 1,966,080 bytes (69%)
- Dynamic memory: 44,628 / 327,680 bytes (13%)
- Change: adds a logical Home action between the endpoint adjustment buttons and shows the live motor angle as a radial tick with degree and step readouts.

Run `tools\Publish-Firmware.ps1 -DeviceType motor_controller -Version 1.1.34 -Push` to compile, hash, commit, tag, and push this release.
