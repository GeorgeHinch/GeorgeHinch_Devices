# MQTT Motor Controller firmware

Target: ESP32-C3 SuperMini on the MQTT Motor Controller PCB.

The PCB has two independently controlled 28BYJ-48 channels. Each channel drives
two physical motors in parallel through one ULN2003, so the four connected motors
are controlled as two pairs, not four independent motors.

## Required Arduino libraries

- PubSubClient
- ArduinoJson
- Adafruit VL53L0X

The ESP32 board package supplies WiFi, Wire, Preferences, and the ESP32 MAC API.

## JMRI objects

The device ID is generated from the full ESP32 MAC address in the same style as
the Audio Sensor (`MOTORCON_XXXXXXXXXXXX`). Each local ID below is prefixed by
that device ID in MQTT.

| ID | Type | Purpose |
| --- | --- | --- |
| `M1_RUN`, `M2_RUN` | Turnout | THROWN runs continuously; CLOSED stops |
| `M1_DIR`, `M2_DIR` | Turnout | THROWN reverse; CLOSED forward |
| `M1_MOVING`, `M2_MOVING` | Sensor | Reports physical movement |
| `M1_SPEED`, `M2_SPEED` | Memory | Half-steps per second, 50-1000 |
| `M1_MOVE`, `M2_MOVE` | Memory | Write a signed relative half-step count; positive is clockwise, negative is counterclockwise, and 0 stops |
| `M1_POS`, `M2_POS` | Memory | Session-relative position; not persisted |
| `S1_OCCUPIED`, `S2_OCCUPIED` | Sensor | Distance below threshold |
| `S1_MM`, `S2_MM` | Memory | Latest distance in millimetres, or -1 |
| `S1_THRESHOLD`, `S2_THRESHOLD` | Memory | Occupancy threshold in millimetres |
| `BEG` | Light | Reports the ring LED state |

## Safety behavior

- `/OE` is left disabled until a zero byte has been latched into the 74HC595.
- Coils are de-energized when stopped by default.
- Motors stop when an established MQTT connection is lost.
- Reconnection is postponed while a locally started motor is moving so MQTT
  connection attempts cannot interrupt step timing.
- On connection, stopped state is published before subscribing. This clears any
  retained RUN command and prevents an unexpected startup.
- Sensor XSHUT pins use low or high-impedance only, which is safe with the
  breakout boards' pull-ups.

## Configuration decisions

The choices most likely to need adjustment are collected in `config.h`:

- Motor speed and the safe speed range.
- Direction inversion for either paired motor channel.
- Whether coils stay energized after a move.
- Whether MQTT loss stops the motors.
- Button action.
- Distance threshold, hysteresis, and sampling period.

`M1_SPEED`, `M2_SPEED`, `M1_DIR`, `M2_DIR`, and both sensor thresholds are saved
in nonvolatile memory. Motor position is deliberately session-relative because
the board has no home or limit switches.

## Wi-Fi configuration portal

The configuration page is available at the controller's normal Wi-Fi IP address.
If Wi-Fi cannot connect for 30 seconds, or the BEG button is held while powering
on, the controller also creates a captive setup network with the same name as
the device ID, such as `MOTORCON_XXXXXXXXXXXX`. This temporary setup network is
open, matching the other device firmware. The Wi-Fi password entered in the portal is only used
when the controller joins the layout's normal network.

The page saves Wi-Fi, MQTT/JMRI, motor, sensor, safety, button, and indicator
settings. “Link motor groups” is enabled by default. While linked, Motor Group 2
mirrors Group 1's direction and speed and its two controls are disabled. MQTT
speed and direction updates to Group 1 are mirrored as well; Group 2 setting
updates are rejected until the groups are unlinked. Movement commands remain
independent so either connector pair can still be started or stopped separately.

The BEG button action is selected in the portal and defaults to **Disabled**.
It can toggle both motor groups together, Motor Group 1 only, or Motor Group 2
only. A selected group stops if it is already moving and starts continuously if
it is stopped. **Run indefinitely** is enabled by default. If it is disabled,
the selected group stops automatically after the configured BEG run time; a
second button press still stops it immediately.

The BEG LED is a fixed ready indicator. It is lit whenever both motor groups are
stopped and turns off whenever either motor group is running. JMRI's global
`BEG` Light object is the highest-priority override: `OFF` forces the LED off,
while `ON` permits the normal ready behavior. The last global state is saved so
nighttime mode survives rebooting or temporarily losing MQTT.

VL53L0X motor automation also defaults to **Disabled**. **Any Sensor
(Time-based)** starts the selected motor group or groups when either sensor
becomes occupied; another activation restarts the configured timer.
**Enter-Exit Sensor** treats whichever sensor activates first as the entrance,
starts the selected motors, and stops them when the other sensor activates. The
configured time is also a fail-safe so a missed exit detection cannot leave the
motors running indefinitely.

The portal shows each VL53L0X as **Detected** or **Not detected**. A missing
sensor's threshold field is disabled automatically. Timed mode requires at least
one detected sensor; enter-exit mode requires both. Detection and address
assignment occur during startup, so restart the controller after plugging in or
removing a sensor.
