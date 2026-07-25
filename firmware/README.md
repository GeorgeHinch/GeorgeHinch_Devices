# Firmware release layout

Each firmware release lives in its own versioned folder:

```text
firmware/
  <device-type>/
    v<version>/
      BUILD_INFO.md
      audio_sensor_<version with underscores>.ino
      audio_sensor_<version with underscores>.bin
      <local headers>
```

For example, v0.1.5 contains `audio_sensor_0_1_5.ino` and `audio_sensor_0_1_5.bin`. The publisher creates a temporary Arduino-compatible staging folder during compilation; that implementation detail is not part of the release layout.

A new firmware release gets a new version folder; do not reuse an older build folder.

Before uploading, compile from that folder with a clean build, verify the binary hash, and then upload the binary produced by that same build. After reset, confirm the firmware version and device identity over serial.

## Publishing

Create the next patch release from the latest version:

```powershell
.\tools\New-FirmwareRelease.ps1 -DeviceType audio_sensor -NextPatch
```

Compile, update `BUILD_INFO.md`, commit, tag, and push it to GitHub:

```powershell
.\tools\Publish-Firmware.ps1 -DeviceType audio_sensor -Version 0.1.6 -Push
```

The local publisher requires this workspace to be a Git repository with an `origin` remote. Pushing the `audio_sensor/v*` tag starts the GitHub Actions workflow, which validates the hash and creates a GitHub release containing the binary and a ZIP archive.

One-time repository setup, using your actual GitHub repository URL:

```powershell
git init
git remote add origin https://github.com/<owner>/<repository>.git
gh auth login
```

## DistanceSensor.h

`DistanceSensor.h` is a small state container for one VL53L0X time-of-flight distance sensor. It stores the sensor driver object, its XSHUT pin and label, the latest measured distance, occupancy state, debounce counters, and whether it triggered during the current cycle. It does not define the board wiring or crossing behavior; the main sketch uses this reusable state structure for the entrance and exit sensors.
