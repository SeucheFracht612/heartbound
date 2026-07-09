# Items and Cargo Architecture

Items and cargo are separate representations.

Items are inventory and storage objects:

- stable prototype ids
- data-defined stack limits and tags
- stack counts
- max stack counts
- quality metadata
- deterministic split and merge rules

Cargo is for large, heavy, unstable, hazardous, or physically transported objects:

- data-defined mass and volume
- data-defined allowed transport modes
- stable save id
- prototype id
- world position
- mass and volume
- stability
- hazard tags
- allowed transport modes

The current foundation provides:

- `ItemDefinition`
  - materialized from item prototypes
  - creates validated `ItemStack` records with the prototype stack limit
- `ItemStack`
  - inventory/storage stack state
  - non-empty stack invariant for runtime inventories and saved inventories
  - exact split/merge transfer rules through owner-keyed `InventoryRecord` values
  - server-authoritative `inventory.transfer_items` command path for ordinary inventory moves
- `CargoDefinition`
  - materialized from cargo prototypes
  - creates persistent `CargoRecord` values with stable save ids
- `CargoRecord`
  - physical/bulk cargo state
  - validates stable id, prototype id, finite position, mass, volume, stability, known transport
    mode bits, and hazard tags
  - server-authoritative `cargo.create` command path for persistent cargo records with optional
    command position
  - kept separate from ordinary inventory transfer commands and item stacks

- `PhysicalResourceRecord`
  - models large dynamic resources before they become cargo
  - converts settled/frozen compound resources into `CargoRecord` values with the same mass,
    volume, stability, hazards, and transport modes

They intentionally do not share a storage model. A heavy log can become cargo for carts
and cranes; nails remain an ordinary item stack.
