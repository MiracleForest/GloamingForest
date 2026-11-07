# Structure kit (F7)

## Audit round 1

Checked: the Java -> Bedrock block table of the structure kit against the warnings of a `full`
server run (`unknown block minecraft:lime_bed / light_gray_bed / rooted_dirt`).

Fixed:

- Beds: Java's 16 `<colour>_bed` blocks are Bedrock's one `minecraft:bed`, whose colour is the bed
  block actor's (`BedBlockActor::mDyeColor`, DyeColor numbering). Pieces placing a bed by id,
  templates (the palette's colour) and processors (rule / state_transfiguring outputs naming a bed,
  e.g. CampStructureDefinitionGenerator's tent bed colours) carry the colour to the placed bed.
  Selftest `structure.bed_color`.
- `minecraft:rooted_dirt` -> `minecraft:dirt_with_roots` (graveyard templates).

Open:

- A bed processor rule test `block_match minecraft:red_bed` matches a bed of any colour (the
  Bedrock block is the same); TF's templates only hold red beds.
