# ESP32 GitHub OTA

GitHub Releases-based OTA firmware distribution for ESP32 layout controllers.

## What this repository provides

- ESP32-C3 Arduino/PlatformIO OTA client
- Stable latest-release manifest URL
- Device-type and hardware-target compatibility checks
- HTTPS certificate validation
- SHA-256 verification before activation
- GitHub Actions build and release workflow
- Automatic daily checks, ready to be replaced or supplemented by MQTT approval

## Update flow

1. A version tag such as `v0.1.1` is pushed.
2. GitHub Actions builds each firmware environment.
3. The workflow creates SHA-256 hashes and `manifest.json`.
4. A GitHub Release is published with the firmware and manifest.
5. Devices read:

   `https://github.com/GeorgeHinch/esp32-github-ota/releases/latest/download/manifest.json`

6. A compatible device downloads its `.bin`, validates its SHA-256 hash, writes the inactive OTA partition, and reboots.

## Initial setup

1. Copy `include/config.example.h` to `include/config.h`.
2. Add Wi-Fi credentials.
3. Insert the current trusted root CA certificate for GitHub.
4. Build and flash over USB once:

```bash
pio run -e crossing-controller -t upload
```

The first USB flash must use a partition table that includes OTA application slots. The standard ESP32 Arduino partition layout normally does; verify the selected board and partition configuration before deployment.

## Publishing an update

Update `FIRMWARE_VERSION` in `platformio.ini`, commit, and tag the same version:

```bash
git tag v0.1.1
git push origin main --tags
```

The release workflow will publish:

- `crossing-controller.bin`
- `manifest.json`

## Adding controller types

Add another PlatformIO environment:

```ini
[env:tof-controller]
board = esp32-c3-devkitm-1
build_flags =
  ${env.build_flags}
  -D DEVICE_TYPE=\"tof-controller\"
  -D HARDWARE_TARGET=\"esp32-c3\"
  -D HARDWARE_REVISION=1
  -D FIRMWARE_VERSION=\"0.1.0\"
```

Then add the environment to the workflow build and copy steps. The manifest generator automatically creates an entry for every `.bin` in `release-assets/`.

## Recommended layout behavior

For operating model trains, do not install automatically merely because an update exists. A safer production pattern is:

- Check GitHub and report update availability.
- Publish availability and current version through MQTT.
- Install only after receiving an explicit MQTT command while the controller is in a safe state.
- Report the new version after reboot.

## Security

- Do not use `WiFiClientSecure::setInsecure()` on deployed devices.
- Keep HTTPS certificate validation enabled.
- Verify SHA-256 against the release manifest.
- For stronger protection, add signed manifests or ESP32 Secure Boot and signed application images.
- A public repository means anyone can download the firmware.

## Current scope

The included firmware is a working OTA foundation. Device-specific crossing, ToF, RFID, MQTT, and safe-state logic should be integrated into separate PlatformIO environments or source modules.
