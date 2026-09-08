# triple_audio_player v2.1.23

- Status: Clean compile passed; rebuilt 2026-09-07 with current shared portal assets
- Sketch: `triple_audio_player_2_1_23.ino`
- Binary: `triple_audio_player_2_1_23.bin`
- Board: `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- ESP32 Arduino core: 3.3.10
- Binary size: 1,323,568 bytes
- Binary SHA-256: `c1151fc0cfaa831a7735137c00239ef9f8511329c4fd7790e7cae1d9d3799835`
- Program storage: 1,323,413 / 1,966,080 bytes (67%)
- Dynamic memory: 42,564 / 327,680 bytes (12%)
- Indicators: shared independent timing helper, local-only ready LED test, logical MQTT state, and HTTP-only diagnostics. Playback behavior is unchanged.
- Verification: firmware compilation, 12,288 shared waveform cases, publication-path and renderer checks passed. Station live-telemetry tests and Linux build passed separately.
- Deployment: not deployed by this change; physical testing remains pending.

This release renders the same `hakomachi.device-ui/v1` manifest on the device
and Station, adds live polling and local playback commands, and applies saved
settings without restarting the controller. Volume fields use the shared range
control.

Run `tools\Publish-Firmware.ps1 -DeviceType triple_audio_player -Version 2.1.23 -Push` to compile, hash, commit, tag, and push this release.
