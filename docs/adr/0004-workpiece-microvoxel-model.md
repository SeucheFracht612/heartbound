# 0004 Workpiece Microvoxel Model

Decision:
Physical crafting uses local workpiece grids, separate from world terrain chunks.

Why:
Tactile crafting needs compact editable local shapes, operation history, template
matching, and server validation. Terrain chunks need world-scale streaming, meshing,
lighting, saving, and replication.

Alternatives considered:
Store craft shapes as terrain voxels; represent every craft object as a high-detail
mesh only.

Consequences:
Workpiece save/load, networking, and inspection become a dedicated engine subsystem.

When to revisit:
After the workpiece sandbox proves grid size, operation history, and template matching
requirements.
