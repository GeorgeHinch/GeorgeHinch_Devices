# Audio Sensor firmware v0.1.15

- Feature: `ALARM ALL` diagnostic mode.
- `ALARM ALL` toggles all eight 74HC595 outputs together while looping the configured sound track.
- `ALARM ALL OFF` stops the test and clears all eight outputs.
- Normal `ALARM` behavior remains limited to QA/QB (the D1/D2 outputs).
- DFPlayer UART acknowledgements enabled using the confirmed bidirectional GPIO20/GPIO21 wiring.
- FQBN: `esp32:esp32:esp32c3:CDCOnBoot=cdc`
- Application binary size: 1,310,096 bytes
- Application binary SHA-256: `C3AE6E2FDF19ECC2409DA10C23ED6AA8AC6CB1261C8D5F279429A37CDD43E99B`
