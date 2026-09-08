# audio_sensor v0.1.34

- Status: Clean build completed
- Sketch: `audio_sensor_0_1_34.ino`
- Binary: `audio_sensor_0_1_34.bin`
- FQBN: `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- Binary size: 1350304 bytes
- Binary SHA-256: `c7c558a4514748a256a25f6ed6f2a6af27c2d54f3befef4087d8caad7ec6af1a`

This release carries the verified motor-controller network corrections into the
audio sensor: an 8.5 dBm station transmit-power cap, zero-config enrollment
before MQTT gating, exact managed-credential prefix matching, and explicit
Wi-Fi/MQTT recovery after delayed connections or disconnects.
