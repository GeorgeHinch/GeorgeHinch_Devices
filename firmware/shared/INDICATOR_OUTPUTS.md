# Indicator output contract

`LocalIndicatorOutputs.h` is shared by the pending motor-controller 1.1.37 and
triple-audio-player 2.1.23 releases. Each release embeds an identical copy so it
can build independently; `tools/test-indicator-parity.mjs` checks copy parity.

- Only LED indicator pins are owned by the timing task. Motor coils, stepping,
  and DFPlayer playback are not changed or accessed from that task.
- Normal logical state remains separate from the physical test waveform.
  MQTT state/discovery uses normal activity, not a test's bright/dark phase.
- One local test per board: 350 ms on/off, automatic stop at 30 seconds.
  Stopping restores the latest normal command, including changes during a test.
- Main-loop audio/network waits do not block this task. It sleeps between
  checks; this is not a hard-real-time guarantee against interrupt/cache stalls.
- HTTP diagnostics report commanded state, controlling source, effect, last
  written output level, and timing availability. These transient values are
  omitted from MQTT state snapshots. GPIO levels are commanded, not measured.
- Test start, stop, and timeout can still generate an ordinary lifecycle report.
  A timeout is serviced by the output task even if the main loop is waiting.

The Station live-state path also includes the triple player and merges motor
configuration telemetry, so both local and router pages use the same manifest.
Builds and simulated checks do not replace physical verification after flashing.
