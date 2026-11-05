// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Java oracle dump for unit U3 (trees): original code, LGPL-3.0-or-later (see COPYING.LESSER).
//
// The source of assets/data/java_goldens/trees.json (U3's Java goldens), a dump kind for F13's oracle
// dumper (tools/oracle, to be folded in by F13): copy this file to tools/oracle/dumper/src/gf/oracle/,
// add Map.entry("trees", TreeDump::dump) to OracleMod.DUMPS, add "trees" to globalKinds and the
// "trees" section of tests/trees/oracle/spec.trees.json to tools/oracle/spec.json, then
//     pwsh tools/oracle/run.ps1 -AcceptEula -Kinds trees -Seeds 0 -Out <dir>
// and copy <dir>/trees.json to assets/data/java_goldens/trees.json. The positions are the server e2e
// world's (its spawn): the trees selftests log theirs ("<id> at x y z (box ...)").
package gf.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.SaplingBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.Property;
import net.minecraft.world.level.levelgen.LegacyRandomSource;
import net.minecraft.world.level.levelgen.feature.ConfiguredFeature;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Trees placed and grown by The Twilight Forest in a flat overworld, block by block, at the absolute
 * positions the Bedrock selftests use (trees.java_*): each case clears its area as the selftest's
 * flatArea does (grass under the box, air in it), places a configured feature with
 * {@code LegacyRandomSource(seed)} or plants saplings and advances them with one, dumps the box
 * (palette + run-length indices, x outermost, then y, then z) and puts the world back.
 */
final class TreeDump {
	private TreeDump() {}

	static Object dump(Ctx ctx) {
		ServerLevel level = ctx.overworld();
		JsonObject sec = ctx.section();
		List<Object> out = Json.arr();
		for (JsonElement e : Ctx.getArr(sec, "cases")) {
			JsonObject c = e.getAsJsonObject();
			runCase(level, c, out);
		}
		Map<String, Object> body = Json.obj();
		body.put("note", "Each case: the flat area (grass at box.minY - 1, air in the box, the world's flat layers elsewhere), then a configured feature placed with LegacyRandomSource(seed) at pos, or saplings planted (stage 0) and advanced with SaplingBlock#advanceTree twice per sapling with one LegacyRandomSource(seed) per sapling; blocks of the box as palette indices, run-length encoded [count, index], x outermost, then y, then z.");
		body.put("cases", out);
		return body;
	}

	private static int[] ints(JsonObject o, String key) {
		JsonArray a = Ctx.getArr(o, key);
		int[] r = new int[a.size()];
		for (int i = 0; i < r.length; i++) r[i] = a.get(i).getAsInt();
		return r;
	}

	private static void runCase(ServerLevel level, JsonObject c, List<Object> out) {
		String name = Ctx.getString(c, "name", "?");
		int[] area = ints(c, "area"); // the flat area: minX minY minZ maxX maxY maxZ
		int margin = 40;
		// load the chunks around
		for (int cx = (area[0] - margin) >> 4; cx <= (area[3] + margin) >> 4; cx++)
			for (int cz = (area[2] - margin) >> 4; cz <= (area[5] + margin) >> 4; cz++) level.getChunk(cx, cz);
		// the world as it was (put back afterwards)
		int sx0 = area[0] - margin, sx1 = area[3] + margin, sz0 = area[2] - margin, sz1 = area[5] + margin;
		int sy0 = level.getMinBuildHeight(), sy1 = Math.min(level.getMaxBuildHeight() - 1, area[4] + 48);
		// a fresh flat world: bedrock, dirt, dirt, grass block, air (anything else is a structure or
		// what an earlier case left: counted, and the area is reset to the layers before and after)
		BlockPos.MutableBlockPos m = new BlockPos.MutableBlockPos();
		int foreign = 0;
		for (int x = sx0; x <= sx1; x++)
			for (int y = sy0; y <= sy1; y++)
				for (int z = sz0; z <= sz1; z++) {
					m.set(x, y, z);
					if (level.getBlockState(m) != flat(level, y)) {
						foreign++;
						level.setBlock(m, flat(level, y), 2 | 16);
					}
				}
		// the flat area
		for (int x = area[0]; x <= area[3]; x++)
			for (int z = area[2]; z <= area[5]; z++) {
				level.setBlock(new BlockPos(x, area[1] - 1, z), Blocks.GRASS_BLOCK.defaultBlockState(), 2 | 16);
				for (int y = area[1]; y <= area[4]; y++) level.setBlock(new BlockPos(x, y, z), Blocks.AIR.defaultBlockState(), 2 | 16);
			}
		Map<String, Object> r = Json.obj();
		r.put("name", name);
		String kind = Ctx.getString(c, "kind", "feature");
		r.put("kind", kind);
		if (kind.equals("feature")) {
			String id = Ctx.getString(c, "id", "");
			long seed = Ctx.getLong(c, "seed", 0L);
			int[] pos = ints(c, "pos");
			ConfiguredFeature<?, ?> f = level.registryAccess().registryOrThrow(Registries.CONFIGURED_FEATURE).get(ResourceLocation.parse(id));
			if (f == null) throw new IllegalStateException("no configured feature " + id);
			boolean ok = f.place(level, level.getChunkSource().getGenerator(), new LegacyRandomSource(seed), new BlockPos(pos[0], pos[1], pos[2]));
			r.put("id", id);
			r.put("seed", seed);
			r.put("pos", Json.arr(pos[0], pos[1], pos[2]));
			r.put("placed", ok);
		} else {
			// saplings: [{id, pos:[x,y,z], seed, pedestal?}] grown one after the other
			for (JsonElement se : Ctx.getArr(c, "saplings")) {
				JsonObject s = se.getAsJsonObject();
				int[] pos = ints(s, "pos");
				BlockPos p = new BlockPos(pos[0], pos[1], pos[2]);
				if (s.has("pedestal") && s.get("pedestal").getAsBoolean()) {
					// the selftests' pedestal: a 3x4x3 dirt column topped with grass, the sapling on it
					for (int dx = -1; dx <= 1; dx++)
						for (int dy = 0; dy < 4; dy++)
							for (int dz = -1; dz <= 1; dz++)
								level.setBlock(p.offset(dx, dy, dz), (dy == 3 ? Blocks.GRASS_BLOCK : Blocks.DIRT).defaultBlockState(), 2 | 16);
					p = p.above(4);
				}
				Block b = BuiltInRegistries.BLOCK.get(ResourceLocation.parse(Ctx.getString(s, "id", "")));
				level.setBlock(p, b.defaultBlockState(), 3);
				LegacyRandomSource random = new LegacyRandomSource(Ctx.getLong(s, "seed", 0L));
				for (int step = 0; step < 2; step++) {
					BlockState st = level.getBlockState(p);
					if (st.getBlock() instanceof SaplingBlock sap) sap.advanceTree(level, p, st, random);
				}
				Map<String, Object> pl = Json.obj();
				pl.put("name", Ctx.getString(s, "name", "?"));
				pl.put("kind", "sapling");
				pl.put("id", Ctx.getString(s, "id", ""));
				pl.put("seed", Ctx.getLong(s, "seed", 0L));
				pl.put("pos", Json.arr(p.getX(), p.getY(), p.getZ()));
				pl.put("grown", !(level.getBlockState(p).getBlock() instanceof SaplingBlock));
				dumpBox(level, ints(s, "box"), pl);
				pl.put("foreignBefore", foreign);
				out.add(pl);
			}
		}
		if (kind.equals("multi")) {
			// features placed one after the other in one flat area (the selftests' placeAll), the whole
			// area dumped at the end
			List<Object> list = Json.arr();
			for (JsonElement fe : Ctx.getArr(c, "features")) {
				JsonObject s = fe.getAsJsonObject();
				int[] pos = ints(s, "pos");
				BlockPos p = new BlockPos(pos[0], pos[1], pos[2]);
				if (s.has("pedestal") && s.get("pedestal").getAsBoolean()) {
					for (int dx = -1; dx <= 1; dx++)
						for (int dy = 0; dy < 4; dy++)
							for (int dz = -1; dz <= 1; dz++)
								level.setBlock(p.offset(dx, dy, dz), (dy == 3 ? Blocks.GRASS_BLOCK : Blocks.DIRT).defaultBlockState(), 2 | 16);
					p = p.above(4);
				}
				String id = Ctx.getString(s, "id", "");
				ConfiguredFeature<?, ?> f = level.registryAccess().registryOrThrow(Registries.CONFIGURED_FEATURE).get(ResourceLocation.parse(id));
				if (f == null) throw new IllegalStateException("no configured feature " + id);
				boolean ok = f.place(level, level.getChunkSource().getGenerator(), new LegacyRandomSource(Ctx.getLong(s, "seed", 0L)), p);
				Map<String, Object> pl = Json.obj();
				pl.put("id", id);
				pl.put("pos", Json.arr(p.getX(), p.getY(), p.getZ()));
				pl.put("placed", ok);
				list.add(pl);
			}
			r.put("features", list);
			dumpBox(level, ints(c, "box"), r);
			r.put("foreignBefore", foreign);
			out.add(r);
		}
		if (kind.equals("feature")) {
			dumpBox(level, ints(c, "box"), r);
			r.put("foreignBefore", foreign);
			out.add(r);
		}
		// put the flat layers back (no updates)
		for (int x = sx0; x <= sx1; x++)
			for (int y = sy0; y <= sy1; y++)
				for (int z = sz0; z <= sz1; z++) {
					m.set(x, y, z);
					if (level.getBlockState(m) != flat(level, y)) level.setBlock(m, flat(level, y), 2 | 16);
				}
	}

	private static void dumpBox(ServerLevel level, int[] box, Map<String, Object> r) {
		List<String> keys = new ArrayList<>();
		Map<String, Integer> index = new LinkedHashMap<>();
		List<Object> palette = Json.arr();
		List<Object> rle = Json.arr();
		int last = -1, run = 0;
		for (int x = box[0]; x <= box[3]; x++)
			for (int y = box[1]; y <= box[4]; y++)
				for (int z = box[2]; z <= box[5]; z++) {
					BlockState s = level.getBlockState(new BlockPos(x, y, z));
					String key = key(s);
					Integer i = index.get(key);
					if (i == null) {
						i = keys.size();
						keys.add(key);
						index.put(key, i);
						palette.add(state(s));
					}
					if (i == last) {
						run++;
					} else {
						if (run > 0) rle.add(Json.arr(run, last));
						last = i;
						run = 1;
					}
				}
		if (run > 0) rle.add(Json.arr(run, last));
		r.put("box", Json.arr(box[0], box[1], box[2], box[3], box[4], box[5]));
		r.put("palette", palette);
		r.put("rle", rle);
	}

	private static BlockState flat(ServerLevel level, int y) {
		int min = level.getMinBuildHeight();
		if (y == min) return Blocks.BEDROCK.defaultBlockState();
		if (y == min + 1 || y == min + 2) return Blocks.DIRT.defaultBlockState();
		if (y == min + 3) return Blocks.GRASS_BLOCK.defaultBlockState();
		return Blocks.AIR.defaultBlockState();
	}

	private static String key(BlockState s) {
		return s.toString();
	}

	private static Map<String, Object> state(BlockState s) {
		Map<String, Object> o = Json.obj();
		o.put("Name", BuiltInRegistries.BLOCK.getKey(s.getBlock()).toString());
		Map<String, Object> props = Json.obj();
		for (Property<?> p : s.getProperties()) props.put(p.getName(), value(s, p));
		if (!props.isEmpty()) o.put("Properties", props);
		return o;
	}

	private static <T extends Comparable<T>> String value(BlockState s, Property<T> p) {
		return p.getName(s.getValue(p));
	}
}
