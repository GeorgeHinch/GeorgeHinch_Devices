# triple_audio_player v2.1.22

- Status: Clean build verified
- Sketch: `triple_audio_player_2_1_22.ino`
- Binary: `triple_audio_player_2_1_22.bin`
- Board: `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- ESP32 Arduino core: 3.3.10
- Binary size: 1,312,720 bytes
- Binary SHA-256: `a160ec3563c05463bf23a6c082d81ad0a7e8c0f4cd3ef1d947c3fde469cff7e6`

This release publishes the complete three-player configuration as a
`hakomachi.device-ui/v1` manifest. The station can now build its controls from
the firmware-defined playback, player, external-button, and software settings,
apply saved settings through MQTT, and issue live playback commands.

Run `tools\Publish-Firmware.ps1 -DeviceType triple_audio_player -Version 2.1.22 -Push` to compile, hash, commit, tag, and push this release.
