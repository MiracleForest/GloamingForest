// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import net.minecraft.util.Mth;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.levelgen.LegacyRandomSource;
import net.minecraft.world.level.levelgen.PositionalRandomFactory;
import net.minecraft.world.level.levelgen.RandomSupport;
import net.minecraft.world.level.levelgen.WorldgenRandom;
import net.minecraft.world.level.levelgen.XoroshiroRandomSource;

import java.util.List;
import java.util.Map;
import java.util.function.LongFunction;

/**
 * Reference values of Minecraft 1.21.1's random sources (the API of src/core/jrand/JRand.h):
 * LegacyRandomSource, XoroshiroRandomSource, WorldgenRandom seeding, positional factories,
 * RandomSupport seed upgrades, String#hashCode and Mth#getSeed. Every sequence starts from a fresh
 * source, so each list is independent of the others.
 */
final class RngDump {
	private RngDump() {
	}

	static final long[] SEEDS = {0L, 1L, -1L, 42L, 123456789L, -8346138912345678901L, Long.MAX_VALUE, Long.MIN_VALUE};
	static final int[] BOUNDS = {1, 2, 3, 7, 10, 16, 100, 1000, 1 << 30, (1 << 30) + 1, Integer.MAX_VALUE};
	static final int N = 8;
	static final int[][] POSITIONS = {{0, 0, 0}, {1, 2, 3}, {-1, -64, -1}, {12345, 70, -54321}, {-30000000, 319, 30000000}, {7, -7, 7}};
	static final String[] NAMES = {"", "a", "minecraft:stone", "example:lich_tower", "minecraft:offset", "example:dimension", "Example Forest", "café 林"};

	static Object dump(Ctx ctx) {
		Map<String, Object> out = Json.obj();
		out.put("note", "Each list starts from a fresh source built from 'seed' (or 'seedLo'/'seedHi'); floats are widened to double.");

		List<Object> legacy = Json.arr();
		for (long seed : SEEDS) {
			Map<String, Object> m = Json.obj();
			m.put("seed", seed);
			source(m, s -> new LegacyRandomSource(seed), seed, true);
			legacy.add(m);
		}
		out.put("legacy", legacy);

		List<Object> xoro = Json.arr();
		for (long seed : SEEDS) {
			Map<String, Object> m = Json.obj();
			m.put("seed", seed);
			RandomSupport.Seed128bit s128 = RandomSupport.upgradeSeedTo128bit(seed);
			m.put("seedLo", s128.seedLo());
			m.put("seedHi", s128.seedHi());
			source(m, s -> new XoroshiroRandomSource(seed), seed, false);
			xoro.add(m);
		}
		long[][] pairs = {{0L, 0L}, {1L, 2L}, {-1L, 123456789L}, {RandomSupport.GOLDEN_RATIO_64, RandomSupport.SILVER_RATIO_64}};
		for (long[] p : pairs) {
			Map<String, Object> m = Json.obj();
			m.put("seedLo", p[0]);
			m.put("seedHi", p[1]);
			source(m, s -> new XoroshiroRandomSource(p[0], p[1]), 0L, false);
			xoro.add(m);
		}
		out.put("xoroshiro", xoro);

		out.put("worldgen", worldgen());

		List<Object> slime = Json.arr();
		int[][] chunks = {{0, 0}, {1, -1}, {-12, 34}, {1000, -1000}, {-117187, 117187}};
		for (long seed : new long[]{0L, 42L, -8346138912345678901L}) {
			for (int[] c : chunks) {
				RandomSource r = WorldgenRandom.seedSlimeChunk(c[0], c[1], seed, 987234911L);
				slime.add(Map.of("chunkX", c[0], "chunkZ", c[1], "levelSeed", seed, "salt", 987234911L, "nextInt10", ints(r, 10, 4)));
			}
		}
		out.put("slimeChunk", ordered(slime, "chunkX", "chunkZ", "levelSeed", "salt", "nextInt10"));

		List<Object> support = Json.arr();
		for (long seed : SEEDS) {
			Map<String, Object> m = Json.obj();
			m.put("seed", seed);
			m.put("mixStafford13", RandomSupport.mixStafford13(seed));
			RandomSupport.Seed128bit s = RandomSupport.upgradeSeedTo128bit(seed);
			m.put("upgradeLo", s.seedLo());
			m.put("upgradeHi", s.seedHi());
			RandomSupport.Seed128bit u = RandomSupport.upgradeSeedTo128bitUnmixed(seed);
			m.put("unmixedLo", u.seedLo());
			m.put("unmixedHi", u.seedHi());
			support.add(m);
		}
		out.put("randomSupport", support);

		List<Object> hashes = Json.arr();
		for (String name : NAMES) {
			Map<String, Object> m = Json.obj();
			m.put("text", name);
			m.put("hash", name.hashCode());
			RandomSupport.Seed128bit s = RandomSupport.seedFromHashOf(name);
			m.put("md5Lo", s.seedLo());
			m.put("md5Hi", s.seedHi());
			hashes.add(m);
		}
		out.put("javaStringHash", hashes);

		List<Object> mth = Json.arr();
		for (int[] p : POSITIONS) {
			mth.add(ordered(Map.of("x", p[0], "y", p[1], "z", p[2], "seed", Mth.getSeed(p[0], p[1], p[2])), "x", "y", "z", "seed"));
		}
		out.put("mthGetSeed", mth);
		return out;
	}

	/** Every method of one source kind, each from a fresh source made by {@code make}. */
	private static void source(Map<String, Object> m, LongFunction<RandomSource> make, long seed, boolean legacy) {
		m.put("nextInt", ints(make.apply(seed), 0, N));
		List<Object> bounded = Json.arr();
		for (int b : BOUNDS) bounded.add(ordered(Map.of("bound", b, "values", ints(make.apply(seed), b, N)), "bound", "values"));
		m.put("nextIntBound", bounded);
		RandomSource r = make.apply(seed);
		List<Object> l = Json.arr();
		for (int i = 0; i < N; i++) l.add(r.nextLong());
		m.put("nextLong", l);
		r = make.apply(seed);
		l = Json.arr();
		for (int i = 0; i < N; i++) l.add(r.nextBoolean());
		m.put("nextBoolean", l);
		r = make.apply(seed);
		l = Json.arr();
		for (int i = 0; i < N; i++) l.add(r.nextFloat());
		m.put("nextFloat", l);
		r = make.apply(seed);
		l = Json.arr();
		for (int i = 0; i < N; i++) l.add(r.nextDouble());
		m.put("nextDouble", l);
		r = make.apply(seed);
		l = Json.arr();
		for (int i = 0; i < N; i++) l.add(r.nextGaussian());
		m.put("nextGaussian", l);
		r = make.apply(seed);
		l = Json.arr();
		for (int i = 0; i < 4; i++) l.add(r.nextIntBetweenInclusive(-5, 5));
		m.put("nextIntBetweenInclusiveM5To5", l);
		if (legacy) {
			List<Object> bits = Json.arr();
			for (int b : new int[]{1, 8, 16, 31, 32}) {
				LegacyRandomSource lr = (LegacyRandomSource) make.apply(seed);
				List<Object> v = Json.arr();
				for (int i = 0; i < 4; i++) v.add(lr.next(b));
				bits.add(ordered(Map.of("bits", b, "values", v), "bits", "values"));
			}
			m.put("next", bits);
		}
		r = make.apply(seed);
		r.consumeCount(5);
		m.put("consumeCount5ThenNextLong", List.of(r.nextLong(), r.nextLong()));

		// a mixed sequence: Gaussian caching interleaved with other calls
		r = make.apply(seed);
		List<Object> mixed = Json.arr();
		mixed.add(r.nextInt(10));
		mixed.add(r.nextGaussian());
		mixed.add(r.nextDouble());
		mixed.add(r.nextGaussian());
		mixed.add(r.nextGaussian());
		mixed.add(r.nextLong());
		mixed.add(r.nextFloat());
		mixed.add(r.nextBoolean());
		mixed.add(r.nextInt());
		m.put("mixed", mixed);
		m.put("mixedOps", List.of("nextInt(10)", "nextGaussian", "nextDouble", "nextGaussian", "nextGaussian", "nextLong", "nextFloat", "nextBoolean", "nextInt"));

		r = make.apply(seed);
		RandomSource fork = r.fork();
		List<Object> forked = Json.arr();
		for (int i = 0; i < 4; i++) forked.add(fork.nextLong());
		m.put("forkNextLong", forked);
		m.put("parentAfterForkNextLong", List.of(r.nextLong(), r.nextLong()));

		r = make.apply(seed);
		PositionalRandomFactory pf = r.forkPositional();
		List<Object> at = Json.arr();
		for (int[] p : POSITIONS) {
			RandomSource pr = pf.at(p[0], p[1], p[2]);
			at.add(ordered(Map.of("x", p[0], "y", p[1], "z", p[2], "nextLong", pr.nextLong(), "nextInt16", pr.nextInt(16)), "x", "y", "z", "nextLong", "nextInt16"));
		}
		List<Object> named = Json.arr();
		for (String name : NAMES) {
			RandomSource pr = pf.fromHashOf(name);
			named.add(ordered(Map.of("name", name, "nextLong", pr.nextLong(), "nextInt16", pr.nextInt(16)), "name", "nextLong", "nextInt16"));
		}
		List<Object> seeded = Json.arr();
		for (long s : new long[]{0L, 1L, -5L, 987654321L}) {
			RandomSource pr = pf.fromSeed(s);
			seeded.add(ordered(Map.of("seed", s, "nextLong", pr.nextLong()), "seed", "nextLong"));
		}
		Map<String, Object> pos = Json.obj();
		pos.put("at", at);
		pos.put("fromHashOf", named);
		pos.put("fromSeed", seeded);
		pos.put("parentAfterForkNextLong", r.nextLong());
		m.put("positional", pos);
	}

	private static List<Object> worldgen() {
		List<Object> out = Json.arr();
		int[][] blocks = {{0, 0}, {16, -32}, {-160, 4096}, {29999984, -29999984}};
		int[][] chunks = {{0, 0}, {1, -1}, {-12, 34}, {117187, -117187}};
		for (String kind : new String[]{"legacy", "xoroshiro"}) {
			for (long levelSeed : new long[]{0L, 42L, -8346138912345678901L}) {
				Map<String, Object> m = Json.obj();
				m.put("source", kind);
				m.put("levelSeed", levelSeed);
				List<Object> deco = Json.arr();
				for (int[] b : blocks) {
					WorldgenRandom wr = make(kind);
					long ds = wr.setDecorationSeed(levelSeed, b[0], b[1]);
					List<Object> after = List.of(wr.nextLong(), wr.nextLong());
					List<Object> features = Json.arr();
					for (int[] fs : new int[][]{{0, 0}, {3, 9}, {17, 4}}) {
						WorldgenRandom f = make(kind);
						f.setFeatureSeed(ds, fs[0], fs[1]);
						List<Object> v = ints(f, 16, 3);
						features.add(ordered(Map.of("index", fs[0], "step", fs[1], "nextInt16", v, "count", f.getCount()), "index", "step", "nextInt16", "count"));
					}
					Map<String, Object> d = Json.obj();
					d.put("minBlockX", b[0]);
					d.put("minBlockZ", b[1]);
					d.put("decorationSeed", ds);
					d.put("thenNextLong", after);
					d.put("features", features);
					deco.add(d);
				}
				m.put("decoration", deco);
				List<Object> large = Json.arr();
				for (int[] c : chunks) {
					WorldgenRandom wr = make(kind);
					wr.setLargeFeatureSeed(levelSeed, c[0], c[1]);
					List<Object> v = ints(wr, 16, 3);
					large.add(ordered(Map.of("chunkX", c[0], "chunkZ", c[1], "nextInt16", v, "nextLong", wr.nextLong()), "chunkX", "chunkZ", "nextInt16", "nextLong"));
				}
				m.put("largeFeature", large);
				List<Object> salted = Json.arr();
				for (int[] c : chunks) {
					for (int salt : new int[]{0, 10387312, 14357617}) {
						WorldgenRandom wr = make(kind);
						wr.setLargeFeatureWithSalt(levelSeed, c[0], c[1], salt);
						salted.add(ordered(Map.of("regionX", c[0], "regionZ", c[1], "salt", salt, "nextInt16", ints(wr, 16, 3)), "regionX", "regionZ", "salt", "nextInt16"));
					}
				}
				m.put("largeFeatureWithSalt", salted);
				out.add(m);
			}
		}
		return out;
	}

	/** A WorldgenRandom over a fresh source; its own seed is always overwritten before use. */
	private static WorldgenRandom make(String kind) {
		return new WorldgenRandom(kind.equals("legacy") ? new LegacyRandomSource(0L) : new XoroshiroRandomSource(0L));
	}

	/** n values of nextInt(bound), or nextInt() when bound == 0. */
	static List<Object> ints(RandomSource r, int bound, int n) {
		List<Object> l = Json.arr();
		for (int i = 0; i < n; i++) l.add(bound == 0 ? r.nextInt() : r.nextInt(bound));
		return l;
	}

	/** Map.of has no order: copy into an ordered map with the given key order. */
	static Map<String, Object> ordered(Map<String, ?> m, String... keys) {
		Map<String, Object> out = Json.obj();
		for (String k : keys) out.put(k, m.get(k));
		return out;
	}

	private static List<Object> ordered(List<Object> l, String... keys) {
		List<Object> out = Json.arr();
		for (Object o : l) {
			@SuppressWarnings("unchecked") Map<String, Object> m = (Map<String, Object>) o;
			out.add(ordered(m, keys));
		}
		return out;
	}
}
