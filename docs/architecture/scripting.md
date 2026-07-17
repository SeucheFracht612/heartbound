# Scripting Architecture

Scripting is an engine-owned sandbox boundary for mod runtime behavior.

Implemented foundation:

- `ScriptBackend`
  - `disabled` backend for deterministic builds, tests, and tools
  - `luau` restricted foundation backend for early sandbox call flow

- `IScriptRuntime`
  - loads and unloads script module metadata
  - validates module ids, source mod ids, source size, API version, function names, and
    call budgets
  - enforces runtime source-size, module-count, argument-count, and string-value byte limits
  - validates return values before they cross back from the sandbox into native code
  - exposes loaded module inspection data
  - provides a single call boundary for sandbox execution
  - checks call-required permissions against module-declared grants before execution
  - validates emitted events against registered host API descriptors before returning them to
    the game/runtime layer

- Host API descriptors
  - stable lowercase dotted API/event id
  - owning script stage
  - minimum script module API version
  - required permissions
  - declared argument schema with stable names, value kinds, and trailing optional arguments
  - validation for malformed ids, duplicate registrations, stage mismatch, version mismatch, and
    missing grants
  - default Heartstead API registry is supplied by `game/runtime`, keeping game meaning out of the
    scripting backend

- Script host event queue
  - converts validated emitted events into ordered native `ScriptHostEvent` records
  - preserves module id, source mod, source path, stage, function name, arguments, module API
    version, and consumed instruction estimate
  - uses the emitted host API payload arguments, not the script function call arguments
  - rejects mismatched module/stage calls, unregistered host APIs, missing grants, malformed
    records, and non-contiguous batches without consuming sequence numbers
  - exposes inspectable event and batch records for tools and future debug overlays

- `ScriptModuleLoader`
  - materializes lifecycle-classified `.lua` and `.luau` files into `ScriptModuleDesc`
    records during mod validation
  - derives stable module ids from the owning mod id and mod-relative source path
  - maps lifecycle task kind to runtime server, runtime client, or migration stage
  - reads source through the validation pipeline before any backend execution
  - parses small source comments for module grants and API version:
    - `-- heartstead.permissions = "read_prototypes, emit_commands"`
    - `-- heartstead.api_version = "1"`
  - reports diagnostics for unreadable files, unsupported script extensions, invalid module ids,
    unknown permissions, duplicate permissions, oversized source, and bad API versions

- Script modules
  - stable `namespace:local_id` module id
  - source mod id
  - source path
  - runtime stage
  - declared permissions
  - API version

- Script stages
  - runtime server
  - runtime client
  - migration

- Script permissions
  - read prototypes
  - read assets
  - emit commands
  - read save data
  - write mod state
  - client UI

- Script calls
  - identify the module, function, stage, arguments, and instruction budget
  - may declare required permissions for the engine/game API being invoked
  - validate argument count and string argument byte size against runtime limits
  - fail with `scripting.permission_denied` if the loaded module lacks a required grant
  - return a simple value plus zero or more registered emitted host API events
  - validate emitted event argument counts and value kinds against the registered host API
    descriptor before native code receives them
  - may enqueue those emitted names as ordered host events after runtime validation

- Runtime limits
  - `max_source_bytes` caps individual module source
  - `max_modules` caps loaded modules per runtime
  - `max_call_arguments` caps call boundary fan-in
  - `max_string_value_bytes` caps string arguments crossing into the sandbox and string return
    values crossing back into native code
  - apply to both the disabled backend and the restricted Luau foundation backend

The current disabled backend intentionally does not execute source code. It validates
and stores module metadata, checks required call permissions, then reports
`scripting.runtime_disabled` for executable calls. This lets tools, tests, and mod
loading code use the future scripting contract without running untrusted code or
depending on a Lua implementation.

Mod and aggregate content validation now carry the materialized script module list.
Tools can inspect loaded module id, source mod, source path, stage, API version, source byte
count, and declared permissions without constructing a runtime backend. This keeps mod loading,
validation, and future production VM binding on the same data contract.

The current Luau backend is a restricted foundation runtime, not the production Luau VM
binding. It accepts a deliberately small Luau-like export table:

```lua
return {
  ping = function(value) return value end,
  notify = function(chunk, voxel, prototype)
    return emit("world.set_voxel", chunk, voxel, prototype)
  end
}
```

Loader directives are module metadata, not executable source. The loader parses recognized
`-- heartstead.*` directive lines and replaces their source bytes with spaces before the
restricted parser or a future VM receives the module. This preserves line offsets without making
the foundation evaluator understand comments or granting directives runtime meaning.

It validates source shape, exported function names, parameter counts, call stage,
required permissions, and instruction budgets, then returns simple literal or argument
values. Return values are checked against the same runtime string byte limit as call arguments
before native code receives them. It also supports `emit("event.name", ...)` as a constrained
return expression:
the event name must be a literal lowercase dotted identifier and must match a registered
host API descriptor for the runtime. Emitted payload values can be literals or function
parameters. The descriptor controls the allowed stage, minimum module API version, required
permissions, argument count, and argument value kinds. Missing grants fail with
`scripting.permission_denied`; unregistered emitted events fail with
`scripting.host_api_not_registered`; malformed payloads fail before native host events are queued.

Emitted events are data in `ScriptCallResult` and `ScriptHostEvent`, not direct engine
mutations. The authoritative game/runtime layer still decides whether those events become
commands, transactions, replication, or diagnostics. This gives engine tests, samples, and
mod loading code a real scripting execution boundary without direct filesystem access, raw
engine pointers, or renderer, physics, save, or networking handles.

The game runtime can route selected runtime-server host events into command envelopes through
game-owned command routes. This bridge only prepares deterministic command payloads. The existing
server command dispatcher remains responsible for validation, transactions, save dirtiness, and
replication.

Game-owned command routes must match the registered host API payload schema by argument name and
required/optional ordering. This keeps script-facing API shape in the engine scripting contract
while command type selection and command payload keys remain owned by the game runtime.

The same bridge can submit those envelopes to the authoritative dispatcher and return structured
per-event command reports. A rejected command remains a report, not a direct script-side mutation.

The production Luau integration must keep the same contract while replacing this
restricted parser/evaluator with a real VM binding, hard sandbox rules, bounded
execution, and explicit capability grants per module or mod stage. Gameplay scripts
should call stable engine/game APIs, not private shortcuts.
