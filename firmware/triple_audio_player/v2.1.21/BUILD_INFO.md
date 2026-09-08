# triple_audio_player v2.1.21

- Status: Clean build verified
- Sketch: `triple_audio_player_2_1_21.ino`
- Binary: `triple_audio_player_2_1_21.bin`
- Board: `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- Binary size: 1,299,520 bytes
- Binary SHA-256: `db498c84c39f4d286428c243459369d118fa6244c253ea1dd56a3a7243fa847f`

This release removes the fixed 10-second full-state heartbeat. Retained state is
published when MQTT connects and after requested commands; real playback events
remain event-driven.

Run `tools\Publish-Firmware.ps1 -DeviceType triple_audio_player -Version 2.1.21 -Push` to compile, hash, commit, tag, and push this release.
