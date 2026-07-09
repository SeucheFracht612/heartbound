# 0003 Modding Lifecycle

Decision:
The official base game is loaded as `mods/base` and uses the same mod discovery path as
community mods.

Why:
Vanilla-only shortcuts would become compatibility debt. Treating the base game as a mod
forces content, prototypes, assets, migrations, and validation to use public engine
paths early.

Alternatives considered:
Hardcoded vanilla registries; adding mods after gameplay stabilizes.

Consequences:
The engine needs mod validation, dependency ordering, data-update patching, and
diagnostics before most gameplay systems exist.

When to revisit:
When scripting and mod migration stages are implemented.
