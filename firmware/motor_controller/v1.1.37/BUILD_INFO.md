# motor_controller v1.1.37

- Status: Clean compile passed; rebuilt 2026-09-07 with current shared portal assets
- Sketch: `motor_controller_1_1_37.ino`
- Binary: `motor_controller_1_1_37.bin`
- Binary size: 1,369,184 bytes
- Binary SHA-256: `3a47b72cd74866c2233c195c1b08d65d7ff00680b4edbe42e84c90e1b1a53dd1`
- Target: ESP32-C3, Arduino ESP32 core 3.3.10, `CDCOnBoot=cdc`, `PartitionScheme=min_spiffs`
- Program storage: 1,369,027 / 1,966,080 bytes (69%)
- Dynamic memory: 44,652 / 327,680 bytes (13%)
- Change: uses the shared manifest renderer for live controls, linked-group visibility, sensor guards, output actions, arc setup, and restart-free saves.
- Indicators: dedicated shared timing helper; motor/ready LED tests remain local, MQTT reports logical activity, HTTP exposes output diagnostics. Motor-coil driving is unchanged.
- Verification: firmware compilation, 12,288 shared waveform cases, publication-path and renderer checks passed. Station live-telemetry tests and Linux build passed separately.
- Deployment: not deployed by this change; physical testing remains pending.

Run `tools\Publish-Firmware.ps1 -DeviceType motor_controller -Version 1.1.37 -Push` to compile, hash, commit, tag, and push this release.
