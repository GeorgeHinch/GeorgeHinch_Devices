# Output timing and reporting

The `output-waveform` FreeRTOS task is the sole runtime writer of the 74HC595.
It receives local-action and JMRI masks under `outputMux`, computes phases from
elapsed time, and writes only changed masks. It sleeps at least one OS tick
between checks. No network, serial query, storage, or sensor calls run in this
task. This isolates waveform timing from blocking main-loop operations; it is
not a hard-real-time guarantee during flash/cache stalls or interrupts.

- Test: one output at a time, 350 ms on/off, ends after 30 seconds even if the
  main loop is waiting. Start/stop/timeout metadata is reported by the main loop,
  not per pulse. Stop restores the current action/JMRI command, not forced off.
- Alternating/strobe: 180 ms phases. JMRI/discovery and Station `Active` fields
  describe whether the effect is active, never its current bright/dark phase.
- Shared ownership: JMRI ON holds an output on even during a dark action phase;
  test overrides both sources temporarily. Normal source state is preserved.
- Action transitions refresh Station state and JMRI discovery once per change.
  No new phase/status publications are made on light command topics, since those
  topics are also consumed as JMRI commands by this firmware.
- HTTP `/api/config` adds commanded state, controlling source, effect, and last
  output level written. The manifest exposes these through the shared renderer.
  These diagnostics are not included in retained MQTT values. Output level is
  a software command, not voltage/current feedback or proof of an attached load.

Run `node tools/test-audio-output-mqtt.mjs` from the repository root. Tests cover
effect/assignment/JMRI/test combinations, stop, timeout, unsigned timer rollover,
publication boundaries, and rendering. Physical cadence still needs validation
after deployment. This version has not been deployed by this change.
