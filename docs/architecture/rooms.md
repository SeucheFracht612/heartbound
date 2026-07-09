# Room Graph

Rooms are derived environmental spaces. They are rebuilt from terrain, build pieces,
doors, roofs, networks, smoke, light, heat, and other simulation sources; they are not
hand-authored boxes and they are not saved as authoritative world data.

The reusable engine layer stores a `RoomGraph` of `RoomRecord` values. Each record has a
runtime room id, source build-piece references, volume, measurable environmental metrics,
and human-readable descriptors.

`WorldState` owns the current `RoomGraph` as derived runtime state. It can be inspected and
queried by authoritative systems, but rooms remain rebuildable and are not saved as durable
world truth.

Current foundation metrics:

- enclosure, roof, and wall coverage
- warmth and dryness
- light, smoke, ventilation, safety, and spaciousness
- storage, cart, power, and ward access flags

`RoomEvaluator` converts metrics into descriptors such as `enclosed`, `warm`, `dry`,
`smoky`, `unsafe`, `crowded`, `storage_access`, `cart_access`, `power_access`, and
`warded`. Gameplay systems should query descriptors and modifiers instead of duplicating
room scoring rules.

Room descriptor metadata is data-driven through `room_descriptor` prototypes. Mods define
descriptor `code`, display `label`, `severity`, and optional tags, and content validation
materializes them into typed room descriptor definitions. This keeps the engine's derived
room graph separate from gameplay meaning while giving tools, overlays, and game systems a
stable catalog of descriptors to resolve against.

Process environment resolution now consumes these descriptors by owner source id and turns
them into generic room, power, and quality modifiers for timestamp-based processes. The
room graph remains derived data; processes store their own durable state and can recompute
room effects when the settlement context changes.

Room records and room graphs expose debug inspection data. Inspectors surface descriptor
counts, severity counts, access flags, aggregate problem-room counts, validation errors,
and warning descriptors so room behavior can be balanced from overlays and tools.

`RoomExtractionGrid` and `RoomExtractor` provide the first derived-room pass. The
extractor flood-fills passable cells, treats solid cells as blockers/walls, aggregates
source build-piece ids, computes room metrics, and emits descriptor-ready `RoomRecord`
values. This is a foundation for later terrain/build-piece extraction and dirty-region
updates, not a hand-authored room box system.

`RoomExtractionGridBuilder` is the first source adapter for derived rooms. It can apply:

- terrain voxels as solid world mass and light input
- build pieces as room blockers, roof coverage, source ids, and access flags
- build-piece network ports as storage, cart, power, ward, or ventilation inputs

This keeps world voxels and build pieces as separate authoritative representations while
still allowing room extraction to consume both. The extractor still operates on a compact
derived grid that can be rebuilt when terrain chunks or build pieces change.

Room rebuild scheduling can now use `RoomGraph::mark_dirty_region` to enqueue localized
room extraction work instead of treating every mutation as a full graph rebuild.

Future work:

- portal-aware room extraction
- smoke, heat, light, ventilation, ward, road, and storage graph inputs
- room overlays and extraction sandboxing from terrain/build-piece data
