// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import it.unimi.dsi.fastutil.doubles.DoubleArrayList;
import it.unimi.dsi.fastutil.doubles.DoubleList;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.levelgen.LegacyRandomSource;
import net.minecraft.world.level.levelgen.XoroshiroRandomSource;
import net.minecraft.world.level.levelgen.synth.ImprovedNoise;
import net.minecraft.world.level.levelgen.synth.NormalNoise;
import net.minecraft.world.level.levelgen.synth.PerlinNoise;
import net.minecraft.world.level.levelgen.synth.SimplexNoise;

import java.lang.reflect.Field;
import java.util.List;
import java.util.Map;
import java.util.Random;

/**
 * Reference values of Minecraft 1.21.1's noise generators (the API of src/core/noise/Noise.h):
 * ImprovedNoise, SimplexNoise, PerlinNoise (modern and legacy seeding), NormalNoise. Each noise is
 * built from a fresh source ("legacy" = LegacyRandomSource(seed), "xoroshiro" =
 * XoroshiroRandomSource(seed)) and sampled at the listed points.
 */
final class NoiseDump {
	private NoiseDump() {
	}

	static final long[] SEEDS = {0L, 42L, -8346138912345678901L};
	static final String[] SOURCES = {"legacy", "xoroshiro"};

	record Octaves(int firstOctave, double[] amplitudes) {
	}

	static final Octaves[] OCTAVES = {
		new Octaves(0, new double[]{1.0}),
		new Octaves(-3, new double[]{1.0, 1.0, 1.0}),
		new Octaves(-7, new double[]{1.0, 0.0, 1.0, 0.5}),
		new Octaves(-9, new double[]{1.0, 1.0, 0.0, 1.0, 1.0}),
		new Octaves(-16, new double[]{1.5, 0.0, 1.0, 0.0, 0.0, 0.0}),
	};

	static Object dump(Ctx ctx) throws ReflectiveOperationException {
		Map<String, Object> out = Json.obj();
		double[][] points3 = points3();
		double[][] points2 = points2();
		out.put("points3", rows(points3));
		out.put("points2", rows(points2));
		out.put("note", "values[i] is the noise at points3[i] (points2[i] for 2D); maxValue is PerlinNoise's private maxValue field / NormalNoise#maxValue.");

		List<Object> improved = Json.arr();
		List<Object> simplex = Json.arr();
		for (String src : SOURCES) {
			for (long seed : SEEDS) {
				ImprovedNoise n = new ImprovedNoise(make(src, seed));
				Map<String, Object> m = head(src, seed);
				m.put("xo", n.xo);
				m.put("yo", n.yo);
				m.put("zo", n.zo);
				double[] v = new double[points3.length];
				for (int i = 0; i < v.length; i++) v[i] = n.noise(points3[i][0], points3[i][1], points3[i][2]);
				m.put("values", v);
				// the legacy y-scaled variant used by BlendedNoise: noise(x, y, z, yScale, yMax)
				double[][] ys = {{1.0, 1.0}, {0.5, 0.25}, {2.0, 4.0}};
				List<Object> scaled = Json.arr();
				for (double[] y : ys) {
					double[] sv = new double[points3.length];
					for (int i = 0; i < sv.length; i++) sv[i] = n.noise(points3[i][0], points3[i][1], points3[i][2], y[0], y[1]);
					Map<String, Object> s = Json.obj();
					s.put("yScale", y[0]);
					s.put("yMax", y[1]);
					s.put("values", sv);
					scaled.add(s);
				}
				m.put("yScaled", scaled);
				improved.add(m);

				SimplexNoise sn = new SimplexNoise(make(src, seed));
				Map<String, Object> sm = head(src, seed);
				sm.put("xo", sn.xo);
				sm.put("yo", sn.yo);
				sm.put("zo", sn.zo);
				double[] v2 = new double[points2.length];
				for (int i = 0; i < v2.length; i++) v2[i] = sn.getValue(points2[i][0], points2[i][1]);
				double[] v3 = new double[points3.length];
				for (int i = 0; i < v3.length; i++) v3[i] = sn.getValue(points3[i][0], points3[i][1], points3[i][2]);
				sm.put("values2", v2);
				sm.put("values3", v3);
				simplex.add(sm);
			}
		}
		out.put("improved", improved);
		out.put("simplex", simplex);

		Field maxValue = PerlinNoise.class.getDeclaredField("maxValue");
		maxValue.setAccessible(true);
		List<Object> perlin = Json.arr();
		List<Object> perlinLegacy = Json.arr();
		List<Object> normal = Json.arr();
		List<Object> normalLegacy = Json.arr();
		for (Octaves o : OCTAVES) {
			DoubleList amps = new DoubleArrayList(o.amplitudes);
			for (String src : SOURCES) {
				for (long seed : SEEDS) {
					PerlinNoise p = PerlinNoise.create(make(src, seed), o.firstOctave, amps);
					perlin.add(sample(head(src, seed, o), (double) maxValue.get(p), points3, p::getValue));
					NormalNoise nn = NormalNoise.create(make(src, seed), new NormalNoise.NoiseParameters(o.firstOctave, amps));
					normal.add(sample(head(src, seed, o), nn.maxValue(), points3, nn::getValue));
				}
			}
			// the legacy initialisation needs a LegacyRandomSource and octaves that end at or below 0
			if (o.firstOctave + o.amplitudes.length - 1 <= 0) {
				for (long seed : SEEDS) {
					PerlinNoise p = PerlinNoise.createLegacyForLegacyNetherBiome(make("legacy", seed), o.firstOctave, amps);
					perlinLegacy.add(sample(head("legacy", seed, o), (double) maxValue.get(p), points3, p::getValue));
					NormalNoise nn = NormalNoise.createLegacyNetherBiome(make("legacy", seed), new NormalNoise.NoiseParameters(o.firstOctave, amps));
					normalLegacy.add(sample(head("legacy", seed, o), nn.maxValue(), points3, nn::getValue));
				}
			}
		}
		out.put("perlin", perlin);
		out.put("perlinLegacyNetherBiome", perlinLegacy);
		out.put("normal", normal);
		out.put("normalLegacyNetherBiome", normalLegacy);
		return out;
	}

	interface Noise3 {
		double get(double x, double y, double z);
	}

	private static Map<String, Object> sample(Map<String, Object> m, double max, double[][] pts, Noise3 n) {
		m.put("maxValue", max);
		double[] v = new double[pts.length];
		for (int i = 0; i < v.length; i++) v[i] = n.get(pts[i][0], pts[i][1], pts[i][2]);
		m.put("values", v);
		return m;
	}

	private static Map<String, Object> head(String src, long seed) {
		Map<String, Object> m = Json.obj();
		m.put("source", src);
		m.put("seed", seed);
		return m;
	}

	private static Map<String, Object> head(String src, long seed, Octaves o) {
		Map<String, Object> m = head(src, seed);
		m.put("firstOctave", o.firstOctave);
		m.put("amplitudes", o.amplitudes);
		return m;
	}

	static RandomSource make(String src, long seed) {
		return src.equals("legacy") ? new LegacyRandomSource(seed) : new XoroshiroRandomSource(seed);
	}

	/** Small, fractional, lattice, negative and very large coordinates (wrap of PerlinNoise). */
	private static double[][] points3() {
		Random r = Ctx.sampler("noise.points3");
		double[][] p = new double[48][];
		int i = 0;
		p[i++] = new double[]{0.0, 0.0, 0.0};
		p[i++] = new double[]{1.0, 2.0, 3.0};
		p[i++] = new double[]{-1.0, -2.0, -3.0};
		p[i++] = new double[]{0.5, 0.5, 0.5};
		p[i++] = new double[]{1.0E-7, -1.0E-7, 0.25};
		p[i++] = new double[]{33554432.0, 64.0, -33554432.0};
		p[i++] = new double[]{1.0E7 + 0.3, -12.75, -1.0E7 - 0.6};
		p[i++] = new double[]{-29999984.0, 320.0, 29999984.0};
		while (i < 32) p[i++] = new double[]{(r.nextDouble() - 0.5) * 200.0, (r.nextDouble() - 0.5) * 200.0, (r.nextDouble() - 0.5) * 200.0};
		while (i < 48) p[i++] = new double[]{(r.nextDouble() - 0.5) * 6.0E7, (r.nextDouble() - 0.5) * 1000.0, (r.nextDouble() - 0.5) * 6.0E7};
		return p;
	}

	private static double[][] points2() {
		Random r = Ctx.sampler("noise.points2");
		double[][] p = new double[32][];
		int i = 0;
		p[i++] = new double[]{0.0, 0.0};
		p[i++] = new double[]{0.5, -0.5};
		p[i++] = new double[]{1.0E7 + 0.3, -1.0E7 - 0.6};
		while (i < 24) p[i++] = new double[]{(r.nextDouble() - 0.5) * 200.0, (r.nextDouble() - 0.5) * 200.0};
		while (i < 32) p[i++] = new double[]{(r.nextDouble() - 0.5) * 6.0E7, (r.nextDouble() - 0.5) * 6.0E7};
		return p;
	}

	private static List<Object> rows(double[][] pts) {
		List<Object> l = Json.arr();
		for (double[] p : pts) l.add(p);
		return l;
	}
}
