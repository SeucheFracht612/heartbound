# 0002 World Model Boundaries

Decision:
Heartstead uses separate storage models for terrain voxels, build pieces, assemblies,
workpieces, entities, items, cargo, networks, rooms, and processes.

Why:
A single universal block abstraction would make terrain, tactile crafting, settlement
simulation, logistics, machines, and saves fight over one storage model.

Alternatives considered:
Minecraft-style block entities for all interactive objects; ECS entities for every
world cell; one voxel format for terrain and crafting.

Consequences:
Conversion boundaries must be explicit, but each system can be optimized and saved for
its own scale.

When to revisit:
Do not revisit for convenience. Revisit only if a representation boundary proves
incorrect under a working sandbox.
