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
  - does not currently expose generic cancellation

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
priority, publishes structured success/failure records, and joins workers on shutdown. Priority
chooses the next queued job; it does not preempt work already running. Job callback exceptions are
converted into failed result records.
Completed results are drained explicitly so callers can decide where authoritative
world/save state is allowed to change.

`max_completed_results` bounds the completed-result mailbox, not the submitted-work queue. There
is no generic pending-job queue limit: workers wait when the result mailbox is full and callers must
drain it to let completion publication continue. `pending_count` includes queued/running work until
its result is published (or discarded during shutdown). A submit can still be accepted while
workers are about to fill the result mailbox, so this setting is not submission backpressure.

Shutdown lets workers finish already queued callbacks. If the completed mailbox is full once
shutdown begins, results that cannot be published are discarded so destruction can join rather
than deadlock. Callers that need every result must stop submitting and drain completions before
destroying the system.

Chunk meshing layers a typed result mailbox over this generic execution API. Job closures capture
only immutable neighborhood/render-table snapshots and a cancellation token. The mailbox owns
`ChunkMeshResult` payloads until the renderer owner thread drains them; generic job results remain
useful for lifecycle and failure accounting. Cancellation never grants a worker access to live
world state and is checked before the expensive mesh build.

The cancellation token in that paragraph belongs to `ChunkMeshScheduler`; generic `JobContext`
currently always reports `cancellation_requested=false`, and generic `JobState::cancelled` is a
reserved state rather than reachable behavior.

Gameplay code should not own raw threads or platform-specific synchronization. Future
parallel systems should submit work through engine-owned job APIs, keep save/world
mutation on authoritative paths, and use explicit result handoff points.
