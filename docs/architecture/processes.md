# Process Architecture

Long-running transformations use a shared timestamp-based process model.

Implemented foundation:

- `ProcessDefinition`
  - stable process prototype id
  - default required work in milliseconds
  - data-defined room requirement
  - data-defined power requirement and required capacity
  - base quality rate in per-mille
  - data tags for game/runtime specialization
  - materialized from process prototypes through `process_definition_from_prototype`

- `ProcessInstance`
  - stable process id
  - owner save id
  - prototype id
  - input and output slots with valid prototype ids and non-zero counts
  - start time and last update time
  - accumulated effective work
  - interruption state
  - validates known process state, work/time consistency, complete-state work, and stale
    interruption reasons

- `ProcessIdAllocator`
  - reserves process ids independently from permanent `SaveId` values
  - owned by `WorldState` so server-authoritative commands do not reuse process ids

- `ProcessRuntime`
  - creates validated process instances
  - advances running processes from timestamps
  - applies room/power/quality modifiers as deterministic per-mille rates
  - supports interruption and resume without granting hidden offline progress
  - can create instances directly from validated `ProcessDefinition` records
  - participates in simulation LOD through process-owner subjects derived from spatial owner
    records, carrying both owner `SaveId` and process `ProcessId` without storing processes as
    entities or build pieces

- `ProcessEnvironmentResolver`
  - finds the room associated with a process owner from derived room source ids
  - converts room descriptors and available power capacity into shared process modifiers
  - reports readable factors and warnings such as missing room, missing power, and insufficient
    power
  - keeps `ProcessRuntime` generic while data-defined process requirements and game/runtime rules
    feed it settlement context from rooms and networks

- `process.start`
  - server-authoritative command that validates a process prototype
  - rejects owners that do not reference an existing saved world object
  - materializes a `ProcessDefinition`
  - reserves a process id
  - creates and inserts a timestamped `ProcessInstance`

- `process.advance_all`
  - server-authoritative command for applying timestamp progress to all running processes
  - uses the command execution server time, not client time or frame count
  - rejects client-supplied process rate modifiers
  - materializes each process prototype definition to get room, power, and quality requirements
  - resolves per-process room and power modifiers from `WorldState` derived rooms and
    owner-scoped power-network ports
  - applies room, power, and quality modifiers through the shared per-mille rate model
  - commits only when at least one process changes state

This model is intentionally generic. Drying, firing, smoking, smelting, crop growth,
animal recovery, ward charging, and machine work should specialize it through game
runtime rules and mod prototypes rather than each inventing a private timer.
