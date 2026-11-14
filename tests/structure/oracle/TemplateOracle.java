// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// Java oracle of the structure kit's template order on Minecraft 1.21.1 itself: the server jar's
// StructureTemplate, StructurePlaceSettings and block classes do the work ProcessorOracle.java
// models by hand.
//   - "fixtures" (default): ProcessorOracle's fixtures loaded as real StructureTemplates
//     (StructureTemplate#load: loadPalette / addToLists / buildInfoList), the palette picked by
//     StructurePlaceSettings#getRandomPalette, every block processed by
//     StructureTemplate#processBlockInfos with the shared random (setRandom), the LootTableSeed
//     drawn for every processed block with block entity data whose block entity is a
//     RandomizableContainer (StructureTemplate#placeInWorld's placement loop), the data markers
//     listed by StructureTemplate#filterBlocks. Only upstream's processor bodies (ProcessorOracle.PROCESSORS,
//     translated from upstream 4.8) and upstream's two nagastone blocks are not the game's: vanilla 1.21.1 has
//     no upstream blocks, so they stand in as full blocks no processor of the fixtures touches
//     (lapis_block, emerald_block: addToLists' list 0, as the kit classifies upstream's nagastone). Prints
//     the lines ProcessorOracle prints; they must be equal.
//   - "lists": StructureTemplate#addToLists (the real private method) for every block state of
//     1.21.1, with block entity data exactly for the states with a block entity (a template saves it
//     for every block entity: StructureTemplate#fillFromWorld): the default state of every block,
//     then every state whose list differs from its block's default. Printed as the C++ table
//     src/feature/structure/JavaListsGolden.inc, which the selftest structure.java_lists checks the
//     kit's templates::javaList against.
// Run (the server jar and libraries of the F13 oracle install, %LOCALAPPDATA%\gf-oracle\server):
//   pwsh tests/structure/oracle/run.ps1 [-Lists]
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.nbt.IntTag;
import net.minecraft.nbt.ListTag;
import net.minecraft.nbt.NbtUtils;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.Bootstrap;
import net.minecraft.util.RandomSource;
import net.minecraft.world.RandomizableContainer;
import net.minecraft.world.level.LevelReader;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.EntityBlock;
import net.minecraft.world.level.block.SlabBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.Property;
import net.minecraft.world.level.block.state.properties.SlabType;
import net.minecraft.world.level.block.state.properties.StructureMode;
import net.minecraft.world.level.levelgen.structure.templatesystem.StructurePlaceSettings;
import net.minecraft.world.level.levelgen.structure.templatesystem.StructureProcessor;
import net.minecraft.world.level.levelgen.structure.templatesystem.StructureProcessorType;
import net.minecraft.world.level.levelgen.structure.templatesystem.StructureTemplate;
import net.minecraft.world.level.levelgen.structure.templatesystem.StructureTemplate.StructureBlockInfo;

public class TemplateOracle {
	/** upstream's blocks of the fixtures and the vanilla full blocks standing in for them. */
	static final Map<String, String> STAND_INS = Map.of("etched_nagastone", "lapis_block", "nagastone_pillar", "emerald_block");

	static Field palettes;
	static Method addToLists;

	static Block block(String path) {
		return BuiltInRegistries.BLOCK.get(ResourceLocation.withDefaultNamespace(path));
	}

	/** The 1.21.1 state of a fixture palette name (ProcessorOracle's names; upstream blocks by their stand-ins). */
	static BlockState state(String name) {
		if (name.equals("sandstone_double_slab")) return Blocks.SANDSTONE_SLAB.defaultBlockState().setValue(SlabBlock.TYPE, SlabType.DOUBLE);
		if (name.equals("structure_block")) return Blocks.STRUCTURE_BLOCK.defaultBlockState().setValue(net.minecraft.world.level.block.StructureBlock.MODE, StructureMode.DATA);
		Block b = block(STAND_INS.getOrDefault(name, name));
		if (b == Blocks.AIR && !name.equals("air")) throw new IllegalArgumentException("no block " + name);
		return b.defaultBlockState();
	}

	/** A state's fixture name (upstream's for the stand-ins). */
	static String nameOf(BlockState s) {
		String path = BuiltInRegistries.BLOCK.getKey(s.getBlock()).getPath();
		if (s.getBlock() == Blocks.SANDSTONE_SLAB && s.getValue(SlabBlock.TYPE) == SlabType.DOUBLE) return "sandstone_double_slab";
		for (Map.Entry<String, String> e : STAND_INS.entrySet())
			if (e.getValue().equals(path)) return e.getKey();
		return path;
	}

	/** Whether a fixture name is a block of the 1.21.1 registry. */
	static boolean vanilla(String name) {
		return !name.contains("nagastone") && (name.equals("air") || block(name) != Blocks.AIR);
	}

	/**
	 * A fixture as a real StructureTemplate (StructureTemplate#load of the NBT a template file
	 * holds): every cell but structure_void, a data marker's cell as the DATA structure block with its
	 * block entity data, block entity data for every block with a block entity.
	 */
	static StructureTemplate template(String[][] layers, Map<Character, String> palette, Set<List<Integer>> nbt) {
		List<BlockState> states = new ArrayList<>();
		ListTag blocks = new ListTag();
		for (int y = 0; y < layers.length; y++)
			for (int z = 0; z < layers[y].length; z++)
				for (int x = 0; x < layers[y][z].length(); x++) {
					String name = palette.get(layers[y][z].charAt(x));
					boolean marker = nbt.contains(List.of(x, y, z));
					if (name.equals("structure_void")) {
						if (!marker) continue; // not saved in a template
						name = "structure_block";
					}
					BlockState s = state(name);
					if (!states.contains(s)) states.add(s);
					CompoundTag b = new CompoundTag();
					ListTag pos = new ListTag();
					pos.add(IntTag.valueOf(x));
					pos.add(IntTag.valueOf(y));
					pos.add(IntTag.valueOf(z));
					b.put("pos", pos);
					b.putInt("state", states.indexOf(s));
					if (marker || s.hasBlockEntity()) {
						CompoundTag data = new CompoundTag();
						data.putString("id", BuiltInRegistries.BLOCK.getKey(s.getBlock()).toString());
						if (name.equals("structure_block")) {
							data.putString("mode", "DATA");
							data.putString("metadata", "gf_test:mark");
						}
						b.put("nbt", data);
					}
					blocks.add(b);
				}
		ListTag paletteTag = new ListTag();
		for (BlockState s : states) paletteTag.add(NbtUtils.writeBlockState(s));
		ListTag size = new ListTag();
		size.add(IntTag.valueOf(layers[0][0].length()));
		size.add(IntTag.valueOf(layers.length));
		size.add(IntTag.valueOf(layers[0].length));
		CompoundTag tag = new CompoundTag();
		tag.put("size", size);
		tag.put("palette", paletteTag);
		tag.put("blocks", blocks);
		tag.put("entities", new ListTag());
		StructureTemplate t = new StructureTemplate();
		t.load(BuiltInRegistries.BLOCK.asLookup(), tag);
		return t;
	}

	/**
	 * The game's RandomSource behind the java.util.Random the processor bodies take: every draw is
	 * the RandomSource's own (the shared LegacyRandomSource of StructurePlaceSettings#getRandom).
	 */
	static final class Draws extends java.util.Random {
		final RandomSource source;

		Draws(RandomSource source) {
			this.source = source;
		}

		@Override
		public void setSeed(long seed) {
			if (this.source != null) this.source.setSeed(seed); // (java.util.Random's constructor calls it first)
		}

		@Override
		public int nextInt() {
			return this.source.nextInt();
		}

		@Override
		public int nextInt(int bound) {
			return this.source.nextInt(bound);
		}

		@Override
		public long nextLong() {
			return this.source.nextLong();
		}

		@Override
		public boolean nextBoolean() {
			return this.source.nextBoolean();
		}

		@Override
		public float nextFloat() {
			return this.source.nextFloat();
		}

		@Override
		public double nextDouble() {
			return this.source.nextDouble();
		}

		@Override
		protected int next(int bits) {
			throw new UnsupportedOperationException("draw through the RandomSource");
		}
	}

	/** One of upstream's processor bodies (ProcessorOracle.PROCESSORS) as a StructureProcessor. */
	static final class Tf extends StructureProcessor {
		final ProcessorOracle.Processor body;
		final int[] offset;
		final Map<BlockPos, String> tfNames;

		Tf(ProcessorOracle.Processor body, int[] offset, Map<BlockPos, String> tfNames) {
			this.body = body;
			this.offset = offset;
			this.tfNames = tfNames;
		}

		@Override
		public StructureBlockInfo processBlock(LevelReader level, BlockPos offset, BlockPos pos, StructureBlockInfo original, StructureBlockInfo modified,
			StructurePlaceSettings settings) {
			BlockPos at = modified.pos();
			ProcessorOracle.Info in = new ProcessorOracle.Info(at.getX(), at.getY(), at.getZ(), tfNames.getOrDefault(at, nameOf(modified.state())));
			ProcessorOracle.Info out = body.process(new Draws(settings.getRandom(at)), in, this.offset);
			if (out == null) return null;
			if (out == in) return modified;
			BlockPos to = new BlockPos(out.x(), out.y(), out.z());
			if (!vanilla(out.block())) {
				// an upstream block (the nagastone variants): the stand-in stays, its upstream name is kept aside
				tfNames.put(to, out.block());
				return new StructureBlockInfo(to, modified.state(), modified.nbt());
			}
			return new StructureBlockInfo(to, state(out.block()), modified.nbt());
		}

		@Override
		protected StructureProcessorType<?> getType() {
			return null;
		}
	}

	/** Whether the block entity of a placed state is a RandomizableContainer (placeInWorld draws its LootTableSeed). */
	static boolean randomizable(BlockState s, BlockPos pos) {
		return s.getBlock() instanceof EntityBlock e && e.newBlockEntity(pos, s) instanceof RandomizableContainer;
	}

	/**
	 * StructureTemplate#placeInWorld(level, offset, offset, settings.setRandom(random), random, ...)
	 * up to the world writes: getRandomPalette, processBlockInfos, then, in placement order, the
	 * LootTableSeed of every block with block entity data whose block entity is a RandomizableContainer.
	 */
	@SuppressWarnings("unchecked")
	static List<ProcessorOracle.Info> place(StructureTemplate t, int[] offset, ProcessorOracle.Processor body, RandomSource random) throws Exception {
		Map<BlockPos, String> tfNames = new HashMap<>();
		StructurePlaceSettings settings = new StructurePlaceSettings().setRandom(random);
		if (body != null) settings.addProcessor(new Tf(body, offset, tfNames));
		BlockPos at = new BlockPos(offset[0], offset[1], offset[2]);
		List<StructureTemplate.Palette> all = (List<StructureTemplate.Palette>) palettes.get(t);
		List<StructureBlockInfo> blocks = settings.getRandomPalette(all, at).blocks();
		List<StructureBlockInfo> processed = StructureTemplate.processBlockInfos(null, at, at, settings, blocks);
		List<ProcessorOracle.Info> out = new ArrayList<>();
		for (StructureBlockInfo info : processed) {
			BlockPos p = info.pos();
			out.add(new ProcessorOracle.Info(p.getX(), p.getY(), p.getZ(), tfNames.getOrDefault(p, nameOf(info.state()))));
			if (info.nbt() != null && randomizable(info.state(), p)) random.nextLong();
		}
		return out;
	}

	static TreeMap<List<Integer>, String> newWorld() {
		return new TreeMap<>(Comparator.<List<Integer>>comparingInt(k -> k.get(1)).thenComparingInt(k -> k.get(2)).thenComparingInt(k -> k.get(0)));
	}

	/** ProcessorOracle#world: the fixture and the processor targets, each with its own random seeded 0x5eed. */
	static TreeMap<List<Integer>, String> world(ProcessorOracle.Processor body) throws Exception {
		TreeMap<List<Integer>, String> world = newWorld();
		StructureTemplate fixture = template(ProcessorOracle.FIXTURE, ProcessorOracle.FIXTURE_PALETTE, ProcessorOracle.FIXTURE_NBT);
		StructureTemplate targets = template(ProcessorOracle.TARGETS, ProcessorOracle.TARGETS_PALETTE, ProcessorOracle.TARGETS_NBT);
		for (ProcessorOracle.Info b : place(fixture, new int[] {0, 64, 0}, body, RandomSource.create(0x5eedL)))
			world.put(List.of(b.x(), b.y(), b.z()), ProcessorOracle.asHashed(b.block()));
		for (ProcessorOracle.Info b : place(targets, new int[] {10, 64, 0}, body, RandomSource.create(0x5eedL)))
			world.put(List.of(b.x(), b.y(), b.z()), ProcessorOracle.asHashed(b.block()));
		return world;
	}

	/** ProcessorOracle#sharedRandom on the real templates: the hut, filterBlocks, the basement, one random. */
	static String sharedRandom(boolean marker) throws Exception {
		RandomSource random = RandomSource.create(0x5eedL);
		ProcessorOracle.Processor variants = ProcessorOracle.PROCESSORS.get("stone_brick_variants");
		TreeMap<List<Integer>, String> world = newWorld();
		StructureTemplate hut = template(ProcessorOracle.HUT, ProcessorOracle.HUT_PALETTE, marker ? Set.of(List.of(2, 1, 0)) : Set.of());
		StructureTemplate basement = template(ProcessorOracle.BASEMENT, ProcessorOracle.HUT_PALETTE, Set.of());
		for (ProcessorOracle.Info b : place(hut, new int[] {30, 64, 0}, variants, random)) world.put(List.of(b.x(), b.y(), b.z()), b.block());
		List<StructureBlockInfo> markers = hut.filterBlocks(new BlockPos(30, 64, 0), new StructurePlaceSettings().setRandom(random), Blocks.STRUCTURE_BLOCK);
		if (markers.size() != (marker ? 1 : 0)) throw new IllegalStateException("filterBlocks found " + markers.size() + " data markers");
		for (ProcessorOracle.Info b : place(basement, new int[] {30, 60, 0}, variants, random)) world.put(List.of(b.x(), b.y(), b.z()), b.block());
		StringBuilder out = new StringBuilder();
		for (Map.Entry<List<Integer>, String> e : world.entrySet())
			if (e.getValue().equals("mossy_stone_bricks") || e.getValue().equals("cracked_stone_bricks"))
				out.append(e.getKey().get(0)).append(',').append(e.getKey().get(1)).append(',').append(e.getKey().get(2)).append(':').append(e.getValue()).append(';');
		return out.toString();
	}

	static void fixtures() throws Exception {
		// the stand-ins are classified as the kit classifies upstream's nagastone: full blocks (list 0)
		for (String standIn : STAND_INS.values())
			if (list(block(standIn).defaultBlockState()) != 0) throw new IllegalStateException(standIn + " is not a full block");
		TreeMap<List<Integer>, String> plain = world(null);
		for (Map.Entry<String, ProcessorOracle.Processor> e : ProcessorOracle.PROCESSORS.entrySet()) {
			TreeMap<List<Integer>, String> processed = world(e.getValue());
			TreeMap<List<Integer>, String> keys = newWorld();
			keys.putAll(plain);
			keys.putAll(processed);
			StringBuilder out = new StringBuilder();
			for (List<Integer> k : keys.keySet()) {
				String a = plain.getOrDefault(k, "-"), b = processed.getOrDefault(k, "-");
				if (!a.equals(b)) out.append(k.get(0)).append(',').append(k.get(1)).append(',').append(k.get(2)).append(':').append(b).append(';');
			}
			System.out.println("diff processor_" + e.getKey() + ": " + out);
		}
		StringBuilder nagastone = new StringBuilder();
		StructureTemplate targets = template(ProcessorOracle.TARGETS, ProcessorOracle.TARGETS_PALETTE, ProcessorOracle.TARGETS_NBT);
		for (ProcessorOracle.Info b : place(targets, new int[] {10, 64, 0}, ProcessorOracle.PROCESSORS.get("nagastone_variants"), RandomSource.create(0x5eedL)))
			if (b.block().contains("nagastone")) nagastone.append(b.x()).append(',').append(b.y()).append(',').append(b.z()).append(":gloamingforest:").append(b.block()).append(';');
		System.out.println("nagastone_variants_blocks: " + nagastone);
		System.out.println("shared_random: " + sharedRandom(true));
		System.out.println("shared_random without the data marker: " + sharedRandom(false));
	}

	/** StructureTemplate#addToLists of a state (block entity data exactly when it has a block entity): 0, 1 or 2 in buildInfoList's order. */
	static int list(BlockState s) throws Exception {
		List<StructureBlockInfo> full = new ArrayList<>(), withNbt = new ArrayList<>(), other = new ArrayList<>();
		addToLists.invoke(null, new StructureBlockInfo(BlockPos.ZERO, s, s.hasBlockEntity() ? new CompoundTag() : null), full, withNbt, other);
		return !full.isEmpty() ? 0 : !other.isEmpty() ? 1 : 2;
	}

	static String properties(BlockState s) {
		StringBuilder out = new StringBuilder("{");
		for (Property<?> p : s.getProperties()) {
			if (out.length() > 1) out.append(',');
			out.append("\\\"").append(p.getName()).append("\\\":\\\"").append(valueName(s, p)).append("\\\"");
		}
		return out.append('}').toString();
	}

	static <T extends Comparable<T>> String valueName(BlockState s, Property<T> p) {
		return p.getName(s.getValue(p));
	}

	static void lists() throws Exception {
		System.out.println("// Generated by tests/structure/oracle/TemplateOracle.java (run.ps1 -Lists) from Minecraft 1.21.1's server jar:");
		System.out.println("// StructureTemplate#addToLists of the default state of every block, then of every state whose list");
		System.out.println("// differs from its block's default (block entity data for exactly the states with a block entity).");
		System.out.println("// {Java id, Properties as JSON, list}. Do not edit by hand.");
		int blocks = 0, states = 0;
		for (Block b : BuiltInRegistries.BLOCK) {
			String id = BuiltInRegistries.BLOCK.getKey(b).toString();
			BlockState def = b.defaultBlockState();
			int defList = list(def);
			System.out.println("{\"" + id + "\", \"" + properties(def) + "\", " + defList + "},");
			blocks++;
			for (BlockState s : b.getStateDefinition().getPossibleStates()) {
				if (s == def) continue;
				int l = list(s);
				if (l == defList) continue;
				System.out.println("{\"" + id + "\", \"" + properties(s) + "\", " + l + "},");
				states++;
			}
		}
		System.err.println("lists: " + blocks + " blocks, " + states + " states whose list differs from their default");
	}

	/** The 1.21.1 blocks whose block entity is a RandomizableContainer (placeInWorld draws their LootTableSeed). */
	static void randomizable() {
		StringBuilder out = new StringBuilder("randomizable:");
		for (Block b : BuiltInRegistries.BLOCK)
			if (randomizable(b.defaultBlockState(), BlockPos.ZERO)) out.append(' ').append(BuiltInRegistries.BLOCK.getKey(b).getPath());
		System.out.println(out);
	}

	public static void main(String[] args) throws Exception {
		SharedConstants.tryDetectVersion();
		Bootstrap.bootStrap();
		palettes = StructureTemplate.class.getDeclaredField("palettes");
		palettes.setAccessible(true);
		addToLists = StructureTemplate.class.getDeclaredMethod("addToLists", StructureBlockInfo.class, List.class, List.class, List.class);
		addToLists.setAccessible(true);
		if (args.length > 0 && args[0].equals("lists")) lists();
		else if (args.length > 0 && args[0].equals("randomizable")) randomizable();
		else fixtures();
	}
}
