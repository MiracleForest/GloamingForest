// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import com.google.gson.JsonObject;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.util.random.WeightedEntry;
import net.minecraft.world.level.ChunkPos;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.biome.Climate;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.ChunkAccess;
import net.minecraft.world.level.chunk.ChunkGenerator;
import net.minecraft.world.level.chunk.status.ChunkStatus;
import net.minecraft.world.level.levelgen.DensityFunction;
import net.minecraft.world.level.levelgen.Heightmap;
import net.minecraft.world.level.levelgen.NoiseBasedChunkGenerator;
import net.minecraft.world.level.levelgen.NoiseRouter;
import net.minecraft.world.level.levelgen.RandomState;
import net.minecraft.world.level.levelgen.structure.Structure;
import net.minecraft.world.level.levelgen.structure.StructureStart;
import twilightforest.util.landmarks.LegacyLandmarkPlacements;
import twilightforest.world.components.biomesources.TFBiomeProvider;
import twilightforest.world.components.chunkgenerators.TerrainColumn;
import twilightforest.world.components.layer.BiomeDensitySource;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Random;
import java.util.TreeMap;
import java.util.TreeSet;

/** Seed-dependent dumps of the twilightforest:twilight_forest level: biomes, heights, surface, density, landmarks. */
final class WorldDumps {
	private WorldDumps() {
	}

	static final String PALETTE_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

	/** A rectangular sample grid: origin, step, size (in quart or block units as documented). */
	record Grid(int x0, int z0, int width, int height, int step) {
		static Grid of(JsonObject o, int x0, int z0, int w, int h, int step) {
			return new Grid(Ctx.getInt(o, "x0", x0), Ctx.getInt(o, "z0", z0), Ctx.getInt(o, "width", w), Ctx.getInt(o, "height", h), Ctx.getInt(o, "step", step));
		}

		int x(int i) {
			return this.x0 + i * this.step;
		}

		int z(int j) {
			return this.z0 + j * this.step;
		}

		Map<String, Object> head() {
			Map<String, Object> m = Json.obj();
			m.put("x0", this.x0);
			m.put("z0", this.z0);
			m.put("width", this.width);
			m.put("height", this.height);
			m.put("step", this.step);
			return m;
		}
	}

	/** rows[j][i] ids -> one string per row, one palette character per cell (palette sorted by id). */
	private static List<Object> encodeRows(String[][] rows, TreeSet<String> paletteIds) {
		List<String> palette = new ArrayList<>(paletteIds);
		if (palette.size() > PALETTE_CHARS.length()) throw new IllegalStateException("palette too large: " + palette.size());
		Map<String, Character> code = new HashMap<>();
		for (int i = 0; i < palette.size(); i++) code.put(palette.get(i), PALETTE_CHARS.charAt(i));
		List<Object> out = Json.arr();
		for (String[] row : rows) {
			StringBuilder sb = new StringBuilder(row.length);
			for (String id : row) sb.append(code.get(id));
			out.add(sb.toString());
		}
		return out;
	}

	private static TFBiomeProvider biomeSource(ServerLevel level) {
		if (level.getChunkSource().getGenerator().getBiomeSource() instanceof TFBiomeProvider p) return p;
		throw new IllegalStateException("the Twilight Forest biome source is not twilightforest:twilight_biomes");
	}

	// -----------------------------------------------------------------------------------------
	// biomes

	static Object biomes(Ctx ctx) {
		ServerLevel level = ctx.twilight();
		TFBiomeProvider src = biomeSource(level);
		Climate.Sampler sampler = level.getChunkSource().randomState().sampler();
		JsonObject s = ctx.section();
		Grid main = Grid.of(Ctx.getObj(s, "main"), -128, -128, 256, 256, 1);
		Grid wide = Grid.of(Ctx.getObj(s, "wide"), -1024, -1024, 256, 256, 8);
		int noiseSamples = Ctx.getInt(s, "noiseSamples", 512);
		int zoomSamples = Ctx.getInt(s, "zoomSamples", 256);

		TreeSet<String> ids = new TreeSet<>();
		String[][] mainRows = mainBiomes(src, main, ids);
		String[][] wideRows = mainBiomes(src, wide, ids);

		int minQy = level.getMinBuildHeight() >> 2;
		int maxQy = (level.getMaxBuildHeight() - 1) >> 2;
		Random r = Ctx.sampler("biomes.noise");
		List<Object> noise = Json.arr();
		for (int n = 0; n < noiseSamples; n++) {
			int qx = main.x0 + r.nextInt(main.width * main.step);
			int qz = main.z0 + r.nextInt(main.height * main.step);
			int qy = minQy + r.nextInt(maxQy - minQy + 1);
			String id = Ctx.id(src.getNoiseBiome(qx, qy, qz, sampler));
			noise.add(Json.arr(qx, qy, qz, id));
		}
		Random zr = Ctx.sampler("biomes.zoom");
		List<Object> zoom = Json.arr();
		for (int n = 0; n < zoomSamples; n++) {
			int x = (main.x0 << 2) + zr.nextInt(main.width * main.step << 2);
			int z = (main.z0 << 2) + zr.nextInt(main.height * main.step << 2);
			int y = level.getMinBuildHeight() + zr.nextInt(level.getMaxBuildHeight() - level.getMinBuildHeight());
			zoom.add(Json.arr(x, y, z, Ctx.id(level.getBiome(new BlockPos(x, y, z)))));
		}

		Map<String, Object> columns = Json.obj();
		Registry<Biome> biomes = level.registryAccess().registryOrThrow(Registries.BIOME);
		TreeMap<String, ResourceKey<Biome>> keys = new TreeMap<>();
		for (ResourceKey<Biome> k : biomes.registryKeySet()) keys.put(Ctx.id(k), k);
		for (Map.Entry<String, ResourceKey<Biome>> e : keys.entrySet()) {
			Optional<TerrainColumn> col = src.getBiomeTerrain().getTerrainColumn(e.getValue());
			if (col.isEmpty()) continue;
			List<Object> layers = Json.arr();
			col.get().getBiomesDebug(line -> {
				int colon = line.indexOf(": ");
				layers.add(Json.arr(Double.parseDouble(line.substring(0, colon)), line.substring(colon + 2)));
			});
			columns.put(e.getKey(), layers);
		}

		Map<String, Object> out = Json.obj();
		out.put("note", "Grids are in quart (4-block) units: cell (i, j) is getMainBiome(x0 + i*step, z0 + j*step), one palette character per cell; "
			+ "columns lists each key biome's [elevation in quarts, biome] layers (getNoiseBiome picks the nearest layer to quart y); "
			+ "noise holds [qx, qy, qz, getNoiseBiome]; zoom holds [x, y, z, Level#getBiome] (block coordinates, BiomeManager zoom).");
		out.put("minY", level.getMinBuildHeight());
		out.put("maxY", level.getMaxBuildHeight());
		out.put("palette", new ArrayList<>(ids));
		out.put("paletteChars", PALETTE_CHARS.substring(0, ids.size()));
		Map<String, Object> m = main.head();
		m.put("rows", encodeRows(mainRows, ids));
		out.put("main", m);
		Map<String, Object> w = wide.head();
		w.put("rows", encodeRows(wideRows, ids));
		out.put("wide", w);
		out.put("columns", columns);
		out.put("noise", noise);
		out.put("zoom", zoom);
		return out;
	}

	private static String[][] mainBiomes(TFBiomeProvider src, Grid g, TreeSet<String> ids) {
		String[][] rows = new String[g.height][g.width];
		for (int j = 0; j < g.height; j++) {
			for (int i = 0; i < g.width; i++) {
				String id = Ctx.id(src.getMainBiome(g.x(i), g.z(j)));
				rows[j][i] = id;
				ids.add(id);
			}
		}
		return rows;
	}

	// -----------------------------------------------------------------------------------------
	// heights (noise only: NoiseBasedChunkGenerator#getBaseHeight, no structures, carvers or features)

	static Object heights(Ctx ctx) {
		ServerLevel level = ctx.twilight();
		NoiseBasedChunkGenerator gen = noiseGenerator(level);
		RandomState rs = level.getChunkSource().randomState();
		Grid g = Grid.of(ctx.section(), -512, -512, 128, 128, 8);
		List<Object> surface = Json.arr();
		List<Object> floor = Json.arr();
		for (int j = 0; j < g.height; j++) {
			int[] a = new int[g.width];
			int[] b = new int[g.width];
			for (int i = 0; i < g.width; i++) {
				a[i] = gen.getBaseHeight(g.x(i), g.z(j), Heightmap.Types.WORLD_SURFACE_WG, level, rs);
				b[i] = gen.getBaseHeight(g.x(i), g.z(j), Heightmap.Types.OCEAN_FLOOR_WG, level, rs);
			}
			surface.add(a);
			floor.add(b);
		}
		Map<String, Object> out = Json.obj();
		out.put("note", "Block coordinates: cell (i, j) is column (x0 + i*step, z0 + j*step); values are NoiseBasedChunkGenerator#getBaseHeight "
			+ "(the y above the highest block of the noise terrain, before surface rules, structures, carvers and features).");
		out.put("minY", level.getMinBuildHeight());
		out.put("maxY", level.getMaxBuildHeight());
		out.put("seaLevel", gen.getSeaLevel());
		out.putAll(g.head());
		out.put("worldSurfaceWg", surface);
		out.put("oceanFloorWg", floor);
		return out;
	}

	private static NoiseBasedChunkGenerator noiseGenerator(ServerLevel level) {
		ChunkGenerator g = level.getChunkSource().getGenerator();
		if (g instanceof NoiseBasedChunkGenerator n) return n;
		throw new IllegalStateException("the Twilight Forest generator is not minecraft:noise but " + g.getClass().getName());
	}

	// -----------------------------------------------------------------------------------------
	// surface (generated chunks at minecraft:surface: noise + structure terraforming + surface rules)

	static Object surface(Ctx ctx) {
		ServerLevel level = ctx.twilight();
		Grid g = Grid.of(ctx.section(), -128, -128, 128, 128, 2);
		Map<Long, ChunkAccess> chunks = new HashMap<>();
		int minY = level.getMinBuildHeight();
		int maxY = level.getMaxBuildHeight();
		String[][] top = new String[g.height][g.width];
		TreeSet<String> ids = new TreeSet<>();
		List<Object> surface = Json.arr();
		List<Object> floor = Json.arr();
		BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
		for (int j = 0; j < g.height; j++) {
			int[] a = new int[g.width];
			int[] b = new int[g.width];
			for (int i = 0; i < g.width; i++) {
				int x = g.x(i), z = g.z(j);
				ChunkAccess c = chunks.computeIfAbsent(ChunkPos.asLong(x >> 4, z >> 4), k -> {
					ChunkAccess got = level.getChunkSource().getChunk(x >> 4, z >> 4, ChunkStatus.SURFACE, true);
					if (got == null || got.getPersistedStatus().isOrAfter(ChunkStatus.CARVERS)) {
						throw new IllegalStateException("chunk " + (x >> 4) + ", " + (z >> 4) + " is past minecraft:surface ("
							+ (got == null ? "missing" : got.getPersistedStatus()) + "): something generated it further before the surface dump");
					}
					return got;
				});
				int ws = minY, of = minY;
				String topId = "minecraft:air";
				for (int y = maxY - 1; y >= minY; y--) {
					BlockState st = c.getBlockState(pos.set(x, y, z));
					if (ws == minY && Heightmap.Types.WORLD_SURFACE_WG.isOpaque().test(st)) {
						ws = y + 1;
						topId = BuiltInRegistries.BLOCK.getKey(st.getBlock()).toString();
					}
					if (Heightmap.Types.OCEAN_FLOOR_WG.isOpaque().test(st)) {
						of = y + 1;
						break;
					}
				}
				a[i] = ws;
				b[i] = of;
				top[j][i] = topId;
				ids.add(topId);
			}
			surface.add(a);
			floor.add(b);
		}
		Map<String, Object> out = Json.obj();
		out.put("note", "Block coordinates of chunks generated to minecraft:surface (noise terrain with landmark terraforming and surface rules; no carvers or features). "
			+ "worldSurfaceWg / oceanFloorWg are the y above the highest block matching the heightmap predicate; top is the block below worldSurfaceWg (palette characters).");
		out.put("status", "minecraft:surface");
		out.put("minY", minY);
		out.put("maxY", maxY);
		out.putAll(g.head());
		out.put("palette", new ArrayList<>(ids));
		out.put("worldSurfaceWg", surface);
		out.put("oceanFloorWg", floor);
		out.put("top", encodeRows(top, ids));
		return out;
	}

	// -----------------------------------------------------------------------------------------
	// density (the seeded noise router of twilightforest:twilight_noise_gen at single points)

	static Object density(Ctx ctx) {
		ServerLevel level = ctx.twilight();
		RandomState rs = level.getChunkSource().randomState();
		NoiseRouter router = rs.router();
		JsonObject s = ctx.section();
		int n = Ctx.getInt(s, "samples", 256);
		int range = Ctx.getInt(s, "range", 4096);
		int minY = level.getMinBuildHeight(), maxY = level.getMaxBuildHeight();
		Random r = Ctx.sampler("density.points");
		int[][] pts = new int[n][];
		for (int i = 0; i < n; i++) {
			pts[i] = new int[]{r.nextInt(2 * range) - range, minY + r.nextInt(maxY - minY), r.nextInt(2 * range) - range};
		}
		Map<String, DensityFunction> fns = new LinkedHashMap<>();
		fns.put("barrier", router.barrierNoise());
		fns.put("fluid_level_floodedness", router.fluidLevelFloodednessNoise());
		fns.put("fluid_level_spread", router.fluidLevelSpreadNoise());
		fns.put("lava", router.lavaNoise());
		fns.put("temperature", router.temperature());
		fns.put("vegetation", router.vegetation());
		fns.put("continents", router.continents());
		fns.put("erosion", router.erosion());
		fns.put("depth", router.depth());
		fns.put("ridges", router.ridges());
		fns.put("initial_density_without_jaggedness", router.initialDensityWithoutJaggedness());
		fns.put("final_density", router.finalDensity());
		fns.put("vein_toggle", router.veinToggle());
		fns.put("vein_ridged", router.veinRidged());
		fns.put("vein_gap", router.veinGap());

		Map<String, Object> router_ = Json.obj();
		for (Map.Entry<String, DensityFunction> e : fns.entrySet()) {
			DensityFunction f = e.getValue();
			Map<String, Object> m = Json.obj();
			m.put("min", f.minValue());
			m.put("max", f.maxValue());
			if (f.minValue() == f.maxValue()) {
				m.put("constant", f.minValue());
			} else {
				double[] v = new double[n];
				for (int i = 0; i < n; i++) v[i] = f.compute(new DensityFunction.SinglePointContext(pts[i][0], pts[i][1], pts[i][2]));
				m.put("values", v);
			}
			router_.put(e.getKey(), m);
		}
		BiomeDensitySource terrain = biomeSource(level).getBiomeTerrain();
		double[] depth = new double[n], scale = new double[n];
		for (int i = 0; i < n; i++) {
			BiomeDensitySource.DensityData d = terrain.sampleTerrain(pts[i][0], pts[i][2], new DensityFunction.SinglePointContext(pts[i][0], 0, pts[i][2]));
			depth[i] = d.depth;
			scale[i] = d.scale;
		}
		List<Object> points = Json.arr();
		for (int[] p : pts) points.add(p);
		Map<String, Object> out = Json.obj();
		out.put("note", "points are block positions [x, y, z]; router.<name>.values[i] is DensityFunction#compute at points[i] (SinglePointContext: no "
			+ "interpolation, no blending); a function with min == max is listed as constant. terrain is BiomeDensitySource#sampleTerrain(x, z) (TF's biome depth/scale blend).");
		out.put("minY", minY);
		out.put("maxY", maxY);
		out.put("points", points);
		out.put("router", router_);
		Map<String, Object> t = Json.obj();
		t.put("depth", depth);
		t.put("scale", scale);
		out.put("terrain", t);
		return out;
	}

	// -----------------------------------------------------------------------------------------
	// landmarks (TF's LegacyLandmarkPlacements and the structure starts it leads to)

	static Object landmarks(Ctx ctx) {
		ServerLevel level = ctx.twilight();
		TFBiomeProvider src = biomeSource(level);
		JsonObject s = ctx.section();
		int rMin = Ctx.getInt(s, "regionMin", -8), rMax = Ctx.getInt(s, "regionMax", 7);
		int chunkSamples = Ctx.getInt(s, "chunkSamples", 2048);
		int chunkRange = Ctx.getInt(s, "chunkRange", 1875000);
		Registry<Structure> structures = level.registryAccess().registryOrThrow(Registries.STRUCTURE);

		List<Object> centers = Json.arr();
		for (int rz = rMin; rz <= rMax; rz++) {
			for (int rx = rMin; rx <= rMax; rx++) {
				BlockPos c = LegacyLandmarkPlacements.getNearestCenterXZ(rx * 16, rz * 16);
				int cx = c.getX() >> 4, cz = c.getZ() >> 4;
				// pickLandmarkForChunk reads the biome at the region's grid point, not at the center
				int gx = Math.round(cx / 16F) * 16, gz = Math.round(cz / 16F) * 16;
				BlockPos biomePos = new BlockPos((gx << 4) + 8, 0, (gz << 4) + 8);
				ChunkAccess chunk = level.getChunkSource().getChunk(cx, cz, ChunkStatus.STRUCTURE_STARTS, true);
				TreeSet<String> starts = new TreeSet<>();
				for (Map.Entry<Structure, StructureStart> e : chunk.getAllStarts().entrySet()) {
					if (e.getValue().isValid()) starts.add(String.valueOf(structures.getKey(e.getKey())));
				}
				Map<String, Object> m = Json.obj();
				m.put("regionX", rx);
				m.put("regionZ", rz);
				m.put("x", c.getX());
				m.put("z", c.getZ());
				m.put("chunkX", cx);
				m.put("chunkZ", cz);
				m.put("hasCenter", LegacyLandmarkPlacements.chunkHasLandmarkCenter(cx, cz));
				m.put("variety", Ctx.id(LegacyLandmarkPlacements.pickVarietyLandmark(cx, cz)));
				m.put("pick", Ctx.id(LegacyLandmarkPlacements.pickLandmarkForChunk(cx, cz, level)));
				m.put("pickBiomeX", biomePos.getX());
				m.put("pickBiomeZ", biomePos.getZ());
				m.put("pickBiome", Ctx.id(level.getBiome(biomePos)));
				m.put("mainBiome", Ctx.id(src.getMainBiome(c.getX() >> 2, c.getZ() >> 2)));
				m.put("starts", new ArrayList<>(starts));
				centers.add(m);
			}
		}

		Random r = Ctx.sampler("landmarks.chunks");
		List<Object> chunks = Json.arr();
		for (int i = 0; i < chunkSamples; i++) {
			int cx, cz;
			if (i < chunkSamples / 2) { // near the origin: every case of the region arithmetic
				cx = r.nextInt(1024) - 512;
				cz = r.nextInt(1024) - 512;
			} else {
				cx = r.nextInt(2 * chunkRange) - chunkRange;
				cz = r.nextInt(2 * chunkRange) - chunkRange;
			}
			BlockPos c = LegacyLandmarkPlacements.getNearestCenterXZ(cx, cz);
			Map<String, Object> m = Json.obj();
			m.put("chunkX", cx);
			m.put("chunkZ", cz);
			m.put("x", c.getX());
			m.put("z", c.getZ());
			m.put("hasCenter", LegacyLandmarkPlacements.chunkHasLandmarkCenter(cx, cz));
			m.put("manhattan", LegacyLandmarkPlacements.manhattanDistanceFromLandmarkCenter(cx, cz));
			m.put("variety", Ctx.id(LegacyLandmarkPlacements.pickVarietyLandmark(cx, cz)));
			chunks.add(m);
		}

		List<Object> weights = Json.arr();
		for (WeightedEntry.Wrapper<ResourceKey<Structure>> w : LegacyLandmarkPlacements.VARIETY_LANDMARKS.unwrap()) {
			weights.add(Json.arr(Ctx.id(w.data()), w.getWeight().asInt()));
		}
		Map<String, Object> out = Json.obj();
		out.put("note", "centers: one per region (regionX/Z = (chunk + 8) >> 4) from getNearestCenterXZ; variety = pickVarietyLandmark, pick = pickLandmarkForChunk "
			+ "(biome read at pickBiomeX/Z, y 0, through Level#getBiome), mainBiome = TFBiomeProvider#getMainBiome at the center, starts = the valid structure starts of the center chunk "
			+ "(minecraft:structure_starts). chunks: sampled chunks with their nearest center (x, z), chunkHasLandmarkCenter, manhattanDistanceFromLandmarkCenter and pickVarietyLandmark.");
		out.put("varietyWeights", weights);
		out.put("centers", centers);
		out.put("chunks", chunks);
		return out;
	}
}
