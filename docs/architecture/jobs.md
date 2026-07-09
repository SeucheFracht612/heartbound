# Jobs Architecture

Jobs are an engine-owned execution boundary for asynchronous and parallelizable work.

Implemented foundation:

- `JobBackend`
  - `immediate` backend for deterministic tests, tools, and samples
  - `thread_pool` backend for worker-thread execution

- `IJobSystem`
  - submits named jobs
  - exposes backend name and counters
  - drains completed job results explicitly
  - reports job success and failure through structured result records

- Job descriptors
  - stable job name
  - priority
  - work function

- Job results
  - job id
  - name
  - priority
  - state
  - completion order
  - error code/message for failed jobs

The immediate backend executes jobs synchronously on submit and queues completed results
for deterministic tests and simple tools.

The thread-pool backend starts a fixed number of worker threads, runs queued jobs by
priority, publishes structured success/failure records, and joins workers on shutdown.
Completed results are drained explicitly so callers can decide where authoritative
world/save state is allowed to change.

Gameplay code should not own raw threads or platform-specific synchronization. Future
parallel systems should submit work through engine-owned job APIs, keep save/world
mutation on authoritative paths, and use explicit result handoff points.
