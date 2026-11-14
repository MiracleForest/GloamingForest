// Golden values of the structure selftests (rule R19), {"name", "value"}: volume hashes of
// in-memory placements at a fixed origin, so they do not depend on where the test area is. upstream
// blocks hash as air (their Bedrock states belong to F1's frozen table, which may still grow);
// what upstream blocks a test places is checked by name where it matters (nagastone_variants_blocks).
// Reviewed once with screenshots of the same placements in the world (tools/e2e/client/scenarios/structure.ps1).
{"template_none", "512ca9d6e76363cf"},
{"template_cw90", "174b60051e64a482"},
{"template_cw180", "eecc42f4bf847358"},
{"template_ccw90", "bde9c82ba5082307"},
{"template_left_right", "c2d51b80280c11d4"},
{"template_front_back", "7769faef2fd10a7d"},
{"template_world_cw90", "174b60051e64a482"},
// processors: the fixture at (0, 64, 0) and the processor targets at (10, 64, 0) over the prepared
// world, each placement sharing one random seeded 0x5eed between its blocks; spawner_processor,
// update_marking and nagastone_variants equal processor_none: spawn data is not part of a volume
// hash, update_marking changes nothing here and upstream's nagastone hashes as air.
// The shared-random goldens (cobble_variants, infest_blocks, smooth_stone_variants,
// stone_brick_variants, targeted_rot, vertical_decay, web) follow Java's processing order:
// StructureTemplate#loadPalette / buildInfoList (full blocks, other shapes, block entities; each
// by y, x, z) after the getRandomPalette draw, with upstream's nagastone processed like every block
// (F1 registers it). Their "diff processor_<name>" info lines (the blocks each processor changed)
// equal the output of tests/structure/oracle/TemplateOracle.java, which places the same fixtures
// with Minecraft 1.21.1's own StructureTemplate (load, getRandomPalette, processBlockInfos,
// filterBlocks; the server jar of the F13 oracle install) and upstream's processor bodies (translated,
// ProcessorOracle.java), as does nagastone_variants_blocks; ProcessorOracle.java models the same
// order by hand and prints the same lines (tests/structure/oracle/run.ps1 compares them).
{"processor_none", "615811ce73edfd6e"},
{"processor_box_cutting", "82c8e2920dc136fb"},
{"processor_cobble_variants", "c1f14db5d759e4b6"},
{"processor_courtyard_terrace", "f4331d95ca09e9bd"},
{"processor_infest_blocks", "ab07749d0267fe2d"},
{"processor_nagastone_variants", "615811ce73edfd6e"},
{"processor_smart_grass", "3b61e6873de495e0"},
{"processor_smooth_stone_variants", "9360f52caa7d62e0"},
{"processor_soft_replace", "c0574a48c1a016a5"},
{"processor_spawner_processor", "615811ce73edfd6e"},
{"processor_state_transfiguring", "a6107588ea10d6d2"},
{"processor_stone_brick_variants", "badb8edceeb91670"},
{"processor_targeted_rot", "8dfb8c136af26891"},
{"processor_update_marking", "615811ce73edfd6e"},
{"processor_vertical_decay", "dce644af72330f7a"},
{"processor_web", "b23c00a054536580"},
{"processor_wood_multiswizzle", "f281337035c1d93d"},
{"processor_wood_swizzle", "eb71fd97bd69212d"},
{"processor_rule", "e804d890c7414853"},
{"processor_gravity", "b12639ed4e113cff"},
{"processor_block_ignore", "06d114a20f41c792"},
// nagastone_variants on the targets' upstream nagastone (the placed blocks, Java's order)
{"nagastone_variants_blocks", "10,64,4:gloamingforest:etched_nagastone;11,64,4:gloamingforest:nagastone_pillar;13,64,4:gloamingforest:cracked_etched_nagastone;14,64,4:gloamingforest:nagastone_pillar;"},
// structure.shared_random: a hut whose data marker sits on a structure_void cell (Java's DATA
// structure block, processed like every block) and a chest (its LootTableSeed drawn while placing),
// then its data markers listed (filterBlocks) and a basement placed with the same random, under
// stone_brick_variants: the variants left, by y, z, x. The same as TemplateOracle's "shared_random:"
// line (without the data marker's structure block the basement comes out differently).
{"shared_random", "30,60,0:cracked_stone_bricks;31,60,0:mossy_stone_bricks;32,60,0:mossy_stone_bricks;30,60,2:cracked_stone_bricks;31,60,2:mossy_stone_bricks;32,60,2:cracked_stone_bricks;32,64,0:mossy_stone_bricks;30,64,1:mossy_stone_bricks;30,64,2:mossy_stone_bricks;31,64,2:cracked_stone_bricks;32,64,2:mossy_stone_bricks;30,65,0:mossy_stone_bricks;"},
{"jigsaw_layout", "gf_test:structure/jig/start@-2,1,-2r0;gf_test:structure/jig/room_a@-5,1,-1r3;gf_test:structure/jig/room_b@-9,1,-1r2;gf_test:structure/jig/room_a@-1,1,-5r0;gf_test:structure/jig/room_b@-1,1,-9r3;gf_test:structure/jig/room_b@3,1,-1r0;"},
{"jigsaw_blocks", "a0aeee07382be472"},
// the template definitions API: getShuffledSequence's first template for eight draws of one random,
// the terrain-adjusted assembly over the hilly test ground, and WorldUtil.adjustForTerrain(5, 5, 12, 3)
// (checked by hand: the 3 x 3 grid heights 66 66 65 64 62 62 61 61 60, rank weighted: 2786 / 45 -> 62)
{"shuffled_sequence", "room_a;room_a;room_a;room_a;room_b;room_b;room_a;room_b;"},
{"jigsaw_terrain_layout", "gf_test:structure/jig/terrain_start@-2,0,-2r0;gf_test:structure/jig/room_a@-1,2,-5r0;gf_test:structure/jig/room_b@-1,2,-9r3;gf_test:structure/jig/room_b@-6,0,-1r2;gf_test:structure/jig/room_a@3,-2,-1r1;gf_test:structure/jig/room_b@6,-2,-1r0;"},
{"adjust_for_terrain", "62"},
{"stronghold_stones_400", "minecraft:cracked_stone_bricks=77 minecraft:infested_stone_bricks=21 minecraft:mossy_stone_bricks=123 minecraft:stone_bricks=179 "},
{"speleothems", "c98ebbc5b764891c"},
{"lattice_chunk00", "0,0;-3,-1;0,3;-3,1;0,7;-3,5;0,10;-3,8;6,0;-3,12;6,3;3,-1;6,7;3,1;6,10;3,5;3,8;3,12;"},
{"maze_6x6", "a900f988d15e0ff5"},
{"hollow_hill_small", "7b9a9299709faca8"},
{"tower_layout", "gloamingforest:GloamingLTBA=34 gloamingforest:GloamingLTBea=12 gloamingforest:GloamingLTBri=7 gloamingforest:GloamingLTMai=1 gloamingforest:GloamingLTOut=4 gloamingforest:GloamingLTRAS=20 gloamingforest:GloamingLTRF=11 gloamingforest:GloamingLTRGF=3 gloamingforest:GloamingLTRPO=6 gloamingforest:GloamingLTRSF=4 gloamingforest:GloamingLTRSt=4 gloamingforest:GloamingLTRStO=3 gloamingforest:GloamingLTWin=46 -16,-21,39,144,39"},
{"tower_blocks", "7f419aede8b5cfc3"},
{"component_helpers", "c7caf7366f3b8f18"},
