# Firmware library

The complete versioned source and package library is mirrored at
`Z:\Firmware Packages` (the `3D Models` share on `NAS927984`).
The older individual sketch folders on that share are preserved as legacy
working copies; use this versioned library for current firmware.

Current packages:

- Audio Sensor: `firmware/audio_sensor/v0.1.44`
- Motor Controller: `firmware/motor_controller/v1.1.37`
- Triple Audio Player: `firmware/triple_audio_player/v2.1.23`

The GitHub package repository is `GeorgeHinch/GeorgeHinch_Devices`.
Package source is synchronized on `main` and the established
`agent/audio-sensor-portal-v0.1.6` publishing branch. Each device/version tag
publishes its binary, source ZIP, and OTA manifest through GitHub Actions.
Publish device tags one at a time, waiting for each release to finish, so
each manifest carries forward the previously published device entries.

The latest packages were rebuilt from their source and generated portal assets
for this sync. They have not been flashed or physically verified by this sync.

To refresh another firmware-library copy, run:

```powershell
.\tools\Sync-FirmwareLibrary.ps1 -Destination 'Z:\Firmware Packages'
```

The sync checks SHA-256 hashes, preserves unrelated destination files, and
backs up changed destination files under `.sync-backups`. It does not copy
Git internals, build caches, or the private `include/config.h` override.
