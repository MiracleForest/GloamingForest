// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import com.google.gson.JsonObject;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.core.QuartPos;
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
import gloamingforest.util.landmarks.LegacyLandmarkPlacements;
import gloamingforest.world.components.biomesources.TFBiomeProvider;
import gloamingforest.world.components.chunkgenerators.TerrainColumn;
import gloamingforest.world.components.layer.BiomeDensitySource;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Random;
import java.util.TreeMap;
import java.util.TreeSet;

/** Seed-dependent dumps of the gloamingforest:gloaming_forest level: biomes, heights, surface, density, landmarks. */
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
		throw new IllegalStateException("the Gloaming Forest biome source is not gloamingforest:gloaming_biomes");
	}

	// -----------------------------------------------------------------------------------------
	// biomes

	static Object biomes(Ctx ctx) {
		ServerLevel level = ctx.gloaming();
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
		ServerLevel level = ctx.gloaming();
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
		throw new IllegalStateException("the Gloaming Forest generator is not minecraft:noise but " + g.getClass().getName());
	}

	// -----------------------------------------------------------------------------------------
	// surface (generated chunks at minecraft:surface: noise + structure terraforming + surface rules)

	/** One column of a chunk generated to minecraft:surface. */
	private record Column(int worldSurface, int oceanFloor, String top, String[] profile, String biome) {
	}

	/**
	 * Generated chunks at minecraft:surface, read without generating them further: every chunk must
	 * still be before minecraft:carvers (nothing generated features into it before the dump).
	 */
	private static final class SurfaceChunks {
		final ServerLevel level;
		final Map<Long, ChunkAccess> chunks = new HashMap<>();
		final int minY, maxY;
		final BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();

		SurfaceChunks(ServerLevel level) {
			this.level = level;
			this.minY = level.getMinBuildHeight();
			this.maxY = level.getMaxBuildHeight();
		}

		ChunkAccess chunk(int cx, int cz) {
			return this.chunks.computeIfAbsent(ChunkPos.asLong(cx, cz), k -> {
				ChunkAccess got = this.level.getChunkSource().getChunk(cx, cz, ChunkStatus.SURFACE, true);
				if (got == null || got.getPersistedStatus().isOrAfter(ChunkStatus.CARVERS)) {
					throw new IllegalStateException("chunk " + cx + ", " + cz + " is past minecraft:surface ("
						+ (got == null ? "missing" : got.getPersistedStatus()) + "): something generated it further before the surface dump");
				}
				return got;
			});
		}

		String block(ChunkAccess c, int x, int y, int z) {
			if (y < this.minY || y >= this.maxY) return "minecraft:void_air";
			return BuiltInRegistries.BLOCK.getKey(c.getBlockState(this.pos.set(x, y, z)).getBlock()).toString();
		}

		/** The column at (x, z): heights, top block, depth blocks from the floor down, the floor's quart biome (depth > 0). */
		Column column(int x, int z, int depth) {
			ChunkAccess c = this.chunk(x >> 4, z >> 4);
			int ws = this.minY, of = this.minY;
			String topId = "minecraft:air";
			for (int y = this.maxY - 1; y >= this.minY; y--) {
				BlockState st = c.getBlockState(this.pos.set(x, y, z));
				if (ws == this.minY && Heightmap.Types.WORLD_SURFACE_WG.isOpaque().test(st)) {
					ws = y + 1;
					topId = BuiltInRegistries.BLOCK.getKey(st.getBlock()).toString();
				}
				if (Heightmap.Types.OCEAN_FLOOR_WG.isOpaque().test(st)) {
					of = y + 1;
					break;
				}
			}
			String[] profile = new String[depth];
			for (int d = 0; d < depth; d++) profile[d] = this.block(c, x, of - 1 - d, z);
			String biome = null; // only the sampled chunks record it (depth > 0)
			if (depth > 0) {
				int by = Math.max(this.minY, of - 1);
				biome = Ctx.id(c.getNoiseBiome(QuartPos.fromBlock(x), QuartPos.fromBlock(by), QuartPos.fromBlock(z)));
			}
			return new Column(ws, of, topId, profile, biome);
		}
	}

	static Object surface(Ctx ctx) {
		ServerLevel level = ctx.gloaming();
		JsonObject s = ctx.section();
		Grid g = Grid.of(s, -128, -128, 128, 128, 2);
		SurfaceChunks sc = new SurfaceChunks(level);
		TreeSet<String> ids = new TreeSet<>();
		String[][] top = new String[g.height][g.width];
		List<Object> surface = Json.arr();
		List<Object> floor = Json.arr();
		for (int j = 0; j < g.height; j++) {
			int[] a = new int[g.width];
			int[] b = new int[g.width];
			for (int i = 0; i < g.width; i++) {
				Column col = sc.column(g.x(i), g.z(j), 0);
				a[i] = col.worldSurface();
				b[i] = col.oceanFloor();
				top[j][i] = col.top();
				ids.add(col.top());
			}
			surface.add(a);
			floor.add(b);
		}

		// sparse samples: whole chunks inside each main biome of the biome search grid
		JsonObject ss = Ctx.getObj(s, "samples");
		Grid search = Grid.of(Ctx.getObj(ss, "grid"), -1024, -1024, 256, 256, 8);
		int perBiome = Ctx.getInt(ss, "perBiome", 2);
		int minApart = Ctx.getInt(ss, "minApart", 512);
		int depth = Ctx.getInt(ss, "depth", 8);
		TFBiomeProvider src = biomeSource(level);
		String[][] biomeGrid = mainBiomes(src, search, new TreeSet<>());
		TreeMap<String, List<int[]>> candidates = new TreeMap<>(); // biome -> [i, j, score]
		for (int j = 0; j < search.height; j++) {
			for (int i = 0; i < search.width; i++) {
				String b = biomeGrid[j][i];
				int score = 0;
				for (int dj = -1; dj <= 1; dj++) {
					for (int di = -1; di <= 1; di++) {
						if (di == 0 && dj == 0) continue;
						int ni = i + di, nj = j + dj;
						if (ni >= 0 && nj >= 0 && ni < search.width && nj < search.height && biomeGrid[nj][ni].equals(b)) score++;
					}
				}
				candidates.computeIfAbsent(b, k -> new ArrayList<>()).add(new int[]{i, j, score});
			}
		}
		TreeSet<String> biomeIds = new TreeSet<>();
		List<Map<String, Object>> samples = new ArrayList<>();
		List<Column[][]> sampleColumns = new ArrayList<>();
		for (Map.Entry<String, List<int[]>> e : candidates.entrySet()) {
			List<int[]> cells = e.getValue();
			// the most interior cells first, then the nearest to the origin, then by z and x
			cells.sort((p, q) -> {
				if (p[2] != q[2]) return Integer.compare(q[2], p[2]);
				long dp = dist2(search, p), dq = dist2(search, q);
				if (dp != dq) return Long.compare(dp, dq);
				if (p[1] != q[1]) return Integer.compare(p[1], q[1]);
				return Integer.compare(p[0], q[0]);
			});
			List<int[]> picked = new ArrayList<>();
			for (int[] cell : cells) {
				if (picked.size() >= perBiome) break;
				int bx = search.x(cell[0]) << 2, bz = search.z(cell[1]) << 2;
				boolean apart = true;
				for (int[] p : picked) {
					long dx = bx - (search.x(p[0]) << 2), dz = bz - (search.z(p[1]) << 2);
					if (dx * dx + dz * dz < (long) minApart * minApart) apart = false;
				}
				if (apart) picked.add(cell);
			}
			for (int[] cell : picked) {
				int bx = search.x(cell[0]) << 2, bz = search.z(cell[1]) << 2;
				int cx = bx >> 4, cz = bz >> 4;
				Column[][] cols = new Column[16][16];
				for (int dz = 0; dz < 16; dz++) {
					for (int dx = 0; dx < 16; dx++) {
						Column col = sc.column((cx << 4) + dx, (cz << 4) + dz, depth);
						cols[dz][dx] = col;
						ids.add(col.top());
						for (String p : col.profile()) ids.add(p);
						biomeIds.add(col.biome());
					}
				}
				Map<String, Object> m = Json.obj();
				m.put("biome", e.getKey());
				m.put("score", cell[2]);
				m.put("cellX", search.x(cell[0]));
				m.put("cellZ", search.z(cell[1]));
				m.put("chunkX", cx);
				m.put("chunkZ", cz);
				m.put("x0", cx << 4);
				m.put("z0", cz << 4);
				samples.add(m);
				sampleColumns.add(cols);
			}
		}
		// encode once every palette is known
		List<String> palette = new ArrayList<>(ids);
		List<String> biomePalette = new ArrayList<>(biomeIds);
		if (palette.size() > PALETTE_CHARS.length() || biomePalette.size() > PALETTE_CHARS.length()) throw new IllegalStateException("palette too large");
		List<Object> chunkList = Json.arr();
		for (int n = 0; n < samples.size(); n++) {
			Map<String, Object> m = samples.get(n);
			Column[][] cols = sampleColumns.get(n);
			List<Object> ws = Json.arr(), of = Json.arr(), topRows = Json.arr(), biomeRows = Json.arr(), profileRows = Json.arr();
			for (Column[] row : cols) {
				int[] a = new int[16], b = new int[16];
				StringBuilder t = new StringBuilder(), bi = new StringBuilder(), pr = new StringBuilder();
				for (int dx = 0; dx < 16; dx++) {
					Column col = row[dx];
					a[dx] = col.worldSurface();
					b[dx] = col.oceanFloor();
					t.append(PALETTE_CHARS.charAt(palette.indexOf(col.top())));
					bi.append(PALETTE_CHARS.charAt(biomePalette.indexOf(col.biome())));
					if (dx > 0) pr.append(' ');
					for (String p : col.profile()) pr.append(PALETTE_CHARS.charAt(palette.indexOf(p)));
				}
				ws.add(a);
				of.add(b);
				topRows.add(t.toString());
				biomeRows.add(bi.toString());
				profileRows.add(pr.toString());
			}
			m.put("worldSurfaceWg", ws);
			m.put("oceanFloorWg", of);
			m.put("top", topRows);
			m.put("biomes", biomeRows);
			m.put("profile", profileRows);
			chunkList.add(m);
		}
		// key biomes (with a terrain column) that the search grid never reaches
		List<Object> unsampled = Json.arr();
		Registry<Biome> biomes = level.registryAccess().registryOrThrow(Registries.BIOME);
		TreeSet<String> keyBiomes = new TreeSet<>();
		for (ResourceKey<Biome> k : biomes.registryKeySet()) {
			if (src.getBiomeTerrain().getTerrainColumn(k).isPresent()) keyBiomes.add(Ctx.id(k));
		}
		for (String b : keyBiomes) if (!candidates.containsKey(b)) unsampled.add(b);

		Map<String, Object> out = Json.obj();
		out.put("note", "Block coordinates of chunks generated to minecraft:surface (noise terrain with landmark terraforming and surface rules; no carvers or features). "
			+ "worldSurfaceWg / oceanFloorWg are the y above the highest block matching the heightmap predicate; top is the block below worldSurfaceWg (palette characters). "
			+ "Grid: x0/z0/width/height/step around the origin. samples.chunks: whole 16x16 chunks (rows = z, columns = x from x0/z0) picked per main biome of the search grid "
			+ "(quart units, TFBiomeProvider#getMainBiome): the cells with the most same-biome neighbours (score, of 8) first, then the nearest to the origin; at most perBiome per biome, "
			+ "minApart blocks apart. profile: per column the depth blocks from the block below oceanFloorWg downwards (one palette character each, columns separated by spaces); "
			+ "biomes: the column's quart biome (ChunkAccess#getNoiseBiome) at that block (biomePalette characters). unsampled: key biomes the search grid never reaches.");
		out.put("status", "minecraft:surface");
		out.put("minY", sc.minY);
		out.put("maxY", sc.maxY);
		out.putAll(g.head());
		out.put("palette", palette);
		out.put("paletteChars", PALETTE_CHARS.substring(0, palette.size()));
		out.put("worldSurfaceWg", surface);
		out.put("oceanFloorWg", floor);
		out.put("top", encodeRows(top, ids));
		Map<String, Object> sm = Json.obj();
		sm.put("grid", search.head());
		sm.put("perBiome", perBiome);
		sm.put("minApart", minApart);
		sm.put("depth", depth);
		sm.put("biomePalette", biomePalette);
		sm.put("biomePaletteChars", PALETTE_CHARS.substring(0, biomePalette.size()));
		sm.put("unsampled", unsampled);
		sm.put("chunks", chunkList);
		out.put("samples", sm);
		return out;
	}

	/** Squared block distance of a search grid cell from the origin. */
	private static long dist2(Grid g, int[] cell) {
		long x = (long) g.x(cell[0]) << 2, z = (long) g.z(cell[1]) << 2;
		return x * x + z * z;
	}

	// -----------------------------------------------------------------------------------------
	// density (the seeded noise router of gloamingforest:gloaming_noise_gen at single points)

	static Object density(Ctx ctx) {
		ServerLevel level = ctx.gloaming();
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
			+ "interpolation, no blending); a function with min == max is listed as constant. terrain is BiomeDensitySource#sampleTerrain(x, z) (upstream's biome depth/scale blend).");
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
	// landmarks (upstream's LegacyLandmarkPlacements and the structure starts it leads to)

	static Object landmarks(Ctx ctx) {
		ServerLevel level = ctx.gloaming();
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
