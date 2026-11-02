// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/processors/CobbleVariants.java
//   src/main/java/twilightforest/world/components/processors/InfestBlocksProcessor.java
//   src/main/java/twilightforest/world/components/processors/NagastoneVariants.java
//   src/main/java/twilightforest/world/components/processors/SmoothStoneVariants.java
//   src/main/java/twilightforest/world/components/processors/StoneBricksVariants.java
//   src/main/java/twilightforest/world/components/processors/TargetedRotProcessor.java
//   src/main/java/twilightforest/world/components/processors/VerticalDecayProcessor.java
//   src/main/java/twilightforest/world/components/processors/SpawnerProcessor.java
//   src/main/java/twilightforest/world/components/feature/templates/GraveyardFeature.java (WebTemplateProcessor)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Java oracle of the shared-random cases of the structure.processors selftest
// (src/feature/structure/SelfTests.cpp): the fixture and the processor-targets templates placed
// with one random seeded 0x5eed per placement (StructurePlaceSettings#setRandom), the way
// Minecraft 1.21.1's StructureTemplate processes a loaded template:
//   - StructureTemplate#loadPalette / addToLists / buildInfoList: full collision blocks, then
//     other shapes, then blocks with block entity data, each list sorted by y, then x, then z;
//   - StructureTemplate#placeInWorld: settings.getRandomPalette(palettes, offset) draws
//     random.nextInt(1) before processBlockInfos, which runs every processor on every block;
// with the TF processors' process() bodies (java.util.Random is LegacyRandomSource bit for bit).
// Printed: for each processor the blocks it changed against the plain placement ("x,y,z:block",
// "-" where nothing is placed; TF's nagastone counts as air, as in the selftest's volume hashes),
// the same lines the selftest logs as "SELFTEST INFO structure.processors: diff processor_<name>"
// (the selftest prints Bedrock ids: minecraft:web is Java's cobweb). Run with
//   javac -d out tests/structure/oracle/ProcessorOracle.java && java -cp out ProcessorOracle
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.TreeMap;

public class ProcessorOracle {
	record Info(int x, int y, int z, String block) {}

	interface Processor {
		/** StructureProcessor#process on a block at world (x, y, z) of a placement at `offset`; null drops it. */
		Info process(Random random, Info info, int[] offset);
	}

	static Map<Character, String> palette(String... names) {
		Map<Character, String> p = new LinkedHashMap<>();
		for (int i = 0; i < names.length; i++) p.put(i < 10 ? (char) ('0' + i) : (char) ('a' + i - 10), names[i]);
		p.put('.', names[0]);
		return p;
	}

	static final Map<Character, String> FIXTURE_PALETTE = palette("air", "stone_bricks", "oak_stairs", "oak_log", "chest", "wall_torch", "cobblestone",
		"oak_slab", "spawner", "ladder", "oak_planks", "grass_block", "stone_brick_stairs", "mossy_cobblestone", "oak_fence", "structure_void");
	static final String[][] FIXTURE = {
		{"1111b", "1666b", "16a6b", "1666b", "11111"},
		{"1.4.1", "2...9", "3.8.c", "1...1", "1e7f1"},
		{"1...1", "5...1", "1...1", "1.d.1", "11111"}};
	/** The fixture's marker positions (loot chest, spawner, data marker): blocks with block entity data. */
	static final Set<List<Integer>> FIXTURE_NBT = Set.of(List.of(2, 1, 0), List.of(2, 1, 2), List.of(1, 2, 1));

	static final Map<Character, String> TARGETS_PALETTE = palette("air", "sandstone_slab", "sandstone_double_slab", "smooth_stone", "smooth_stone_slab",
		"grass_block", "spawner", "stone_bricks", "etched_nagastone", "nagastone_pillar", "cobblestone", "oak_planks", "stone_brick_stairs",
		"mossy_cobblestone");
	static final String[][] TARGETS = {
		{"12121", "34343", "55555", "7a6b7", "89c89"},
		{"12121", "3.4.3", "5.5.5", ".....", "d...d"}};
	static final Set<List<Integer>> TARGETS_NBT = Set.of(List.of(2, 0, 3));

	/** Block#isCollisionShapeFullBlock without a dynamic shape, for the fixtures' blocks. */
	static final Set<String> FULL = Set.of("stone_bricks", "oak_log", "cobblestone", "oak_planks", "grass_block", "mossy_cobblestone",
		"sandstone_double_slab", "smooth_stone", "etched_nagastone", "nagastone_pillar");
	static final Set<String> BLOCK_ENTITIES = Set.of("chest", "spawner");

	/**
	 * StructureTemplate#loadPalette: the blocks of a template in buildInfoList's order. A
	 * structure_void cell is not saved in a template, unless a data marker sits there (in `nbt`): that
	 * cell is the marker's DATA structure block, which has block entity data.
	 */
	static List<Info> load(String[][] layers, Map<Character, String> palette, Set<List<Integer>> nbt) {
		List<Info> full = new ArrayList<>(), other = new ArrayList<>(), withNbt = new ArrayList<>();
		for (int y = 0; y < layers.length; y++)
			for (int z = 0; z < layers[y].length; z++)
				for (int x = 0; x < layers[y][z].length(); x++) {
					String block = palette.get(layers[y][z].charAt(x));
					if (block.equals("structure_void")) {
						if (!nbt.contains(List.of(x, y, z))) continue; // not saved in a template
						block = "structure_block"; // a data marker: Java's DATA structure block
					}
					Info info = new Info(x, y, z, block);
					if (nbt.contains(List.of(x, y, z)) || BLOCK_ENTITIES.contains(block)) withNbt.add(info);
					else if (FULL.contains(block)) full.add(info);
					else other.add(info);
				}
		Comparator<Info> order = Comparator.comparingInt(Info::y).thenComparingInt(Info::x).thenComparingInt(Info::z);
		full.sort(order);
		other.sort(order);
		withNbt.sort(order);
		List<Info> list = new ArrayList<>(full);
		list.addAll(other);
		list.addAll(withNbt);
		return list;
	}

	/** StructureTemplate#placeInWorld with a shared random: what is placed where, in placement order. */
	static List<Info> place(List<Info> blocks, int[] offset, Processor processor) {
		Random random = new Random(0x5eedL);
		random.nextInt(1); // StructurePlaceSettings#getRandomPalette
		List<Info> placed = new ArrayList<>();
		for (Info b : blocks) {
			Info info = new Info(b.x() + offset[0], b.y() + offset[1], b.z() + offset[2], b.block());
			Info out = processor == null ? info : processor.process(random, info, offset);
			if (out != null) placed.add(out);
		}
		return placed;
	}

	static String variant(Random random, Info info, long factor, String[][] conversions) {
		random.setSeed(random.nextLong() * factor);
		for (String[] c : conversions)
			if (info.block().equals(c[0]) && random.nextBoolean()) return c[1];
		return null;
	}

	static Info with(Info info, String block) {
		return block == null ? info : new Info(info.x(), info.y(), info.z(), block);
	}

	static final Map<String, Processor> PROCESSORS = new LinkedHashMap<>();
	static {
		PROCESSORS.put("cobble_variants", (r, i, o) -> with(i, variant(r, i, 2, new String[][] {{"cobblestone", "mossy_cobblestone"},
			{"cobblestone_stairs", "mossy_cobblestone_stairs"}, {"cobblestone_slab", "mossy_cobblestone_slab"}, {"cobblestone_wall", "mossy_cobblestone_wall"}})));
		PROCESSORS.put("infest_blocks", (r, i, o) -> {
			// settings.getRandom(pos.below(-10)): the shared random
			r.setSeed(r.nextLong() * 2);
			Map<String, String> conversions = Map.of("stone", "infested_stone", "cobblestone", "infested_cobblestone", "stone_bricks", "infested_stone_bricks",
				"mossy_stone_bricks", "infested_mossy_stone_bricks", "cracked_stone_bricks", "infested_cracked_stone_bricks", "chiseled_stone_bricks",
				"infested_chiseled_stone_bricks");
			String to = conversions.get(i.block());
			if (to == null || r.nextFloat() > 1.0F / 12.0F) return i;
			return with(i, to);
		});
		PROCESSORS.put("smooth_stone_variants", (r, i, o) -> with(i, variant(r, i, 4,
			new String[][] {{"smooth_stone_slab", "cobblestone_slab"}, {"smooth_stone", "cobblestone"}})));
		PROCESSORS.put("stone_brick_variants", (r, i, o) -> {
			r.setSeed(r.nextLong() * 3);
			if (i.block().equals("stone_bricks") && r.nextBoolean()) return with(i, r.nextBoolean() ? "mossy_stone_bricks" : "cracked_stone_bricks");
			if (i.block().equals("stone_brick_stairs") && r.nextBoolean()) return with(i, "mossy_stone_brick_stairs");
			if (i.block().equals("stone_brick_slab") && r.nextBoolean()) return with(i, "mossy_stone_brick_slab");
			if (i.block().equals("stone_brick_wall") && r.nextBoolean()) return with(i, "mossy_stone_brick_wall");
			return i;
		});
		PROCESSORS.put("nagastone_variants", (r, i, o) -> {
			r.setSeed(r.nextLong() * 5);
			for (String n : new String[] {"etched_nagastone", "nagastone_pillar", "nagastone_stairs_left", "nagastone_stairs_right"})
				if (i.block().equals(n) && r.nextBoolean()) return with(i, (r.nextBoolean() ? "mossy_" : "cracked_") + n);
			return i;
		});
		// the selftest's configurations: blocks_to_rot stone_bricks, integrity 0.5; decay_blocks stone_bricks, decay_chance 0.3
		PROCESSORS.put("targeted_rot", (r, i, o) -> !i.block().equals("stone_bricks") || r.nextFloat() <= 0.5F ? i : null);
		PROCESSORS.put("vertical_decay", (r, i, o) -> i.block().equals("stone_bricks") && r.nextFloat() < 0.3F ? null : i);
		// WebTemplateProcessor: the cobweb goes to the placement's offset
		PROCESSORS.put("web", (r, i, o) -> i.block().equals("grass_block") ? i : r.nextInt(5) == 0 ? new Info(o[0], o[1], o[2], "cobweb") : i);
		// SimpleWeightedRandomList#getRandomValue over skeleton 3 + spider 1 for every spawner
		PROCESSORS.put("spawner_processor", (r, i, o) -> {
			if (i.block().equals("spawner")) r.nextInt(4);
			return i;
		});
	}

	static String asHashed(String block) {
		return block.contains("nagastone") ? "air" : block; // TF blocks hash as air
	}

	/** Where the two placements leave which block (the later block at a position wins), by y, z, x. */
	static TreeMap<List<Integer>, String> world(Processor processor) {
		TreeMap<List<Integer>, String> world = new TreeMap<>(Comparator.<List<Integer>>comparingInt(k -> k.get(1)).thenComparingInt(k -> k.get(2))
			.thenComparingInt(k -> k.get(0)));
		for (Info b : place(load(FIXTURE, FIXTURE_PALETTE, FIXTURE_NBT), new int[] {0, 64, 0}, processor)) world.put(List.of(b.x(), b.y(), b.z()), asHashed(b.block()));
		for (Info b : place(load(TARGETS, TARGETS_PALETTE, TARGETS_NBT), new int[] {10, 64, 0}, processor)) world.put(List.of(b.x(), b.y(), b.z()), asHashed(b.block()));
		return world;
	}

	// structure.shared_random: TemplateFeature#place's druid hut and basement sharing the feature's random
	static final Map<Character, String> HUT_PALETTE = palette("air", "stone_bricks", "structure_void", "chest", "cobblestone");
	static final String[][] HUT = {{"111", "141", "111"}, {"1.2", "...", "1.3"}};
	static final String[][] BASEMENT = {{"111", "111", "111"}};
	/** The RandomizableContainer block entities among the blocks above. */
	static final Set<String> RANDOMIZABLE = Set.of("chest");

	/**
	 * The hut placed with settings.setRandom(random) and placeInWorld(..., random, ...): getRandomPalette,
	 * processBlockInfos (StoneBricksVariants on every block), then the placement loop drawing a
	 * LootTableSeed for every RandomizableContainer with block entity data; filterBlocks for the data
	 * markers (getRandomPalette again); the basement 4 blocks down with the same random. Returns the
	 * stone brick variants left, by y, then z, then x.
	 */
	static String sharedRandom(boolean marker) {
		Random random = new Random(0x5eedL);
		Processor variants = PROCESSORS.get("stone_brick_variants");
		TreeMap<List<Integer>, String> world = new TreeMap<>(Comparator.<List<Integer>>comparingInt(k -> k.get(1)).thenComparingInt(k -> k.get(2))
			.thenComparingInt(k -> k.get(0)));
		int[] hut = {30, 64, 0}, basement = {30, 60, 0};
		random.nextInt(1); // the hut's StructurePlaceSettings#getRandomPalette
		List<Info> processed = new ArrayList<>();
		for (Info b : load(HUT, HUT_PALETTE, marker ? Set.of(List.of(2, 1, 0)) : Set.of()))
			processed.add(variants.process(random, new Info(b.x() + hut[0], b.y() + hut[1], b.z() + hut[2], b.block()), hut));
		for (Info b : processed) {
			world.put(List.of(b.x(), b.y(), b.z()), b.block());
			if (RANDOMIZABLE.contains(b.block())) random.nextLong(); // its LootTableSeed
		}
		random.nextInt(1); // filterBlocks(placementPos, settings, STRUCTURE_BLOCK): getRandomPalette
		random.nextInt(1); // the basement's getRandomPalette
		for (Info b : load(BASEMENT, HUT_PALETTE, Set.of())) {
			Info out = variants.process(random, new Info(b.x() + basement[0], b.y() + basement[1], b.z() + basement[2], b.block()), basement);
			world.put(List.of(out.x(), out.y(), out.z()), out.block());
		}
		StringBuilder out = new StringBuilder();
		for (Map.Entry<List<Integer>, String> e : world.entrySet())
			if (e.getValue().equals("mossy_stone_bricks") || e.getValue().equals("cracked_stone_bricks"))
				out.append(e.getKey().get(0)).append(',').append(e.getKey().get(1)).append(',').append(e.getKey().get(2)).append(':').append(e.getValue()).append(';');
		return out.toString();
	}

	public static void main(String[] args) {
		TreeMap<List<Integer>, String> plain = world(null);
		for (Map.Entry<String, Processor> e : PROCESSORS.entrySet()) {
			TreeMap<List<Integer>, String> processed = world(e.getValue());
			TreeMap<List<Integer>, String> keys = new TreeMap<>(plain.comparator());
			keys.putAll(plain);
			keys.putAll(processed);
			StringBuilder out = new StringBuilder();
			for (List<Integer> k : keys.keySet()) {
				String a = plain.getOrDefault(k, "-"), b = processed.getOrDefault(k, "-");
				if (!a.equals(b)) out.append(k.get(0)).append(',').append(k.get(1)).append(',').append(k.get(2)).append(':').append(b).append(';');
			}
			System.out.println("diff processor_" + e.getKey() + ": " + out);
		}
		// nagastone_variants on the targets' nagastone itself (the selftest's nagastone_variants_blocks)
		StringBuilder nagastone = new StringBuilder();
		for (Info b : place(load(TARGETS, TARGETS_PALETTE, TARGETS_NBT), new int[] {10, 64, 0}, PROCESSORS.get("nagastone_variants")))
			if (b.block().contains("nagastone")) nagastone.append(b.x()).append(',').append(b.y()).append(',').append(b.z()).append(":twilightforest:").append(b.block()).append(';');
		System.out.println("nagastone_variants_blocks: " + nagastone);
		// the selftest structure.shared_random (its golden and its "shared_random:" info line)
		System.out.println("shared_random: " + sharedRandom(true));
		System.out.println("shared_random without the data marker: " + sharedRandom(false));
	}
}
