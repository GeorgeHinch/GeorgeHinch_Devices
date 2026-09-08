# motor_controller v1.1.27 deployment test

Date: 2026-09-03

## Target

- Board: ESP32-C3 motor-controller carrier
- Serial port: COM4
- Station MAC: `E8:3D:C1:82:1F:8C`
- Router: HakoMachi, 2.4 GHz channel 11, HT20

## Results

| Check | Result |
|---|---|
| Clean compile with Arduino ESP32 core 3.3.10 | Pass |
| Flash and image verification | Pass |
| 8.5 dBm station power applied | Pass |
| Join `HakoMachi-Setup` for zero-config enrollment | Pass |
| DHCP on setup network | Pass, `10.42.0.171` |
| Receive production Wi-Fi and unique MQTT credentials | Pass |
| Join production SSID `HakoMachi-02D6` | Pass |
| MQTT authentication and state publishing | Pass |
| Router device state | Online, configuration current, firmware 1.1.27 |
| Production RSSI | Approximately -47 to -50 dBm |
| Router-to-device ping | Pass, 5/5 replies |
| Reboot without re-enrollment or credential rotation | Pass |
| Archived binary matches clean build | Pass |

## Firmware corrections

1. Station transmit power is capped at 8.5 dBm and reapplied before
   provisioning, initial connection, and reconnects.
2. Enrollment runs immediately after Wi-Fi connects, before saved MQTT broker
   settings are required.
3. Managed MQTT username and topic prefixes are compared using their actual
   lengths, so saved credentials remain valid across reboots.

## Router observation

The station created the password and ACL entries correctly, but its Mosquitto
`reload` action did not activate the new password database. Restarting only
Mosquitto loaded the credentials, after which the controller authenticated.
This is a router-side follow-up; it does not affect the verified motor firmware
reboot path once credentials have been activated.

## Release image

- Size: 1,334,144 bytes
- SHA-256: `cf3e2dd8ac5a71058378aeca1bb177176cc99248a890625da105b68ccf6ac788`
