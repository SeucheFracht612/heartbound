# Replay and Command Recording

Replay support records authoritative command envelopes so command handling can be
debugged and tested deterministically.

Implemented foundation:

- `CommandReplayLog`
  - replay version
  - scenario id
  - world seed
  - ordered recorded commands

- `RecordedCommand`
  - command envelope
  - authoritative server timestamp
  - optional deterministic expectations for the resulting committed state, events, reserved ids,
    dirty flags, rejection error details, and operation trace

- `CommandReplayCodec`
  - small text format with magic header
  - delimiter-safe percent escaping
  - optional `expect=` records after command records, encoded with the shared deterministic command
    payload codec
  - validation after decode

- `CommandReplayRunner`
  - forces authoritative-server execution
  - dispatches recorded commands through `ServerCommandDispatcher` full dispatch reports
  - records per-step events, reserved ids, and mutation commit state
  - records accepted and rejected steps, including rejected-command error codes/messages
  - records the local operation trace from dispatch, including transaction stages, mutation
    descriptions, derived updates, and dirty flags
  - verifies optional per-command expectations and fails the replay on mismatched deterministic
    outcomes

- debug inspection
  - renders replay logs, replay steps, and replay reports as structured inspection data
  - summarizes command count, expectation count, and first/last command details for replay logs
  - summarizes succeeded, rejected, committed, and expectation-checked step counts, emitted event
    count, reserved id count, and first/last command types
  - exposes the retained operation trace for each replay step

- `heartstead_replay_inspector`
  - reads command replay text files
  - decodes and validates replay structure
  - prints replay log inspection fields for command count and deterministic expectation coverage

This is not a full deterministic simulation replay yet. It is the execution harness
that future replay files, command logs, and desync investigations should build on.
