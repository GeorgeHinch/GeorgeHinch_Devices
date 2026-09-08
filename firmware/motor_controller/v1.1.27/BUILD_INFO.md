# motor_controller v1.1.27

- Status: Clean build completed
- Sketch: `motor_controller_1_1_27.ino`
- Binary: `motor_controller_1_1_27.bin`
- FQBN: `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- Binary size: 1334144 bytes
- Binary SHA-256: `cf3e2dd8ac5a71058378aeca1bb177176cc99248a890625da105b68ccf6ac788`

This release caps station-mode Wi-Fi transmit power at 8.5 dBm and reapplies
the cap before zero-config provisioning, initial connection, and reconnects.
It also services zero-config enrollment before requiring saved MQTT settings.
Managed MQTT credentials are recognized with exact prefix lengths and are reused
across reboots instead of being rotated again.
