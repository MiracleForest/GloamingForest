import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;

/** Prints Java values of Minecraft 1.21.1's random sources and noises (goldens for tests/worldgen). */
public class Oracle {
    static Map<String, String> cls = new HashMap<>();            // deobf -> obf class
    static Map<String, String> meth = new HashMap<>();           // deobfClass#name(params) -> obf
    static ClassLoader loader;

    static void loadMappings(Path p) throws IOException {
        String cur = null;
        for (String line : Files.readAllLines(p)) {
            if (line.startsWith("#")) continue;
            if (!line.startsWith(" ")) {
                String[] s = line.split(" -> ");
                cur = s[0];
                cls.put(cur, s[1].substring(0, s[1].length() - 1));
            } else if (line.contains("(")) {
                String t = line.trim();
                String[] s = t.split(" -> ");
                String sig = s[0];
                sig = sig.replaceFirst("^\\d+:\\d+:", "");
                String name = sig.substring(sig.indexOf(' ') + 1);
                name = name.replaceFirst(":\\d+:\\d+$", "");
                meth.put(cur + "#" + name, s[1]);
            }
        }
    }

    static Class<?> c(String deobf) throws Exception {
        switch (deobf) {
            case "int": return int.class;
            case "long": return long.class;
            case "double": return double.class;
            case "float": return float.class;
            case "boolean": return boolean.class;
        }
        String o = cls.getOrDefault(deobf, deobf);
        return Class.forName(o, true, loader);
    }

    static Object call(Object target, String deobfClass, String name, String[] params, Object... args) throws Exception {
        String key = deobfClass + "#" + name + "(" + String.join(",", params) + ")";
        String obf = meth.get(key);
        if (obf == null) throw new RuntimeException("no mapping " + key);
        Class<?>[] pt = new Class<?>[params.length];
        for (int i = 0; i < params.length; i++) pt[i] = c(params[i]);
        Class<?> k = c(deobfClass);
        Method m = null;
        for (Class<?> kk = k; kk != null && m == null; kk = kk.getSuperclass()) {
            try { m = kk.getDeclaredMethod(obf, pt); } catch (NoSuchMethodException e) { }
        }
        if (m == null) {
            for (Class<?> itf : k.getInterfaces()) { try { m = itf.getDeclaredMethod(obf, pt); break; } catch (NoSuchMethodException e) { } }
        }
        m.setAccessible(true);
        return m.invoke(target, args);
    }

    static Object ctor(String deobfClass, String[] params, Object... args) throws Exception {
        Class<?>[] pt = new Class<?>[params.length];
        for (int i = 0; i < params.length; i++) pt[i] = c(params[i]);
        Constructor<?> ct = c(deobfClass).getDeclaredConstructor(pt);
        ct.setAccessible(true);
        return ct.newInstance(args);
    }

    static final String RS = "net.minecraft.util.RandomSource";
    static final String PRF = "net.minecraft.world.level.levelgen.PositionalRandomFactory";
    static String[] P(String... s) { return s; }

    static void dumpSource(String label, Object r) throws Exception {
        StringBuilder sb = new StringBuilder(label + " ints:");
        for (int i = 0; i < 4; i++) sb.append(" ").append(call(r, RS, "nextInt", P()));
        sb.append(" bounded:");
        for (int b : new int[]{1, 2, 16, 100, 1000, 7, Integer.MAX_VALUE}) sb.append(" ").append(call(r, RS, "nextInt", P("int"), b));
        sb.append(" longs:");
        for (int i = 0; i < 2; i++) sb.append(" ").append(call(r, RS, "nextLong", P()));
        sb.append(" floats:");
        for (int i = 0; i < 2; i++) sb.append(" ").append(Float.floatToRawIntBits((Float) call(r, RS, "nextFloat", P())));
        sb.append(" doubles:");
        for (int i = 0; i < 2; i++) sb.append(" ").append(Double.doubleToRawLongBits((Double) call(r, RS, "nextDouble", P())));
        sb.append(" gauss:");
        for (int i = 0; i < 3; i++) sb.append(" ").append(Double.doubleToRawLongBits((Double) call(r, RS, "nextGaussian", P())));
        sb.append(" bools:");
        for (int i = 0; i < 3; i++) sb.append(" ").append(call(r, RS, "nextBoolean", P()));
        System.out.println(sb);
    }

    public static void main(String[] a) throws Exception {
        loadMappings(Paths.get(a[0]));
        List<java.net.URL> urls = new ArrayList<>();
        for (int i = 1; i < a.length; i++) urls.add(new File(a[i]).toURI().toURL());
        loader = new java.net.URLClassLoader(urls.toArray(new java.net.URL[0]), Oracle.class.getClassLoader());

        String LRS = "net.minecraft.world.level.levelgen.LegacyRandomSource";
        String XRS = "net.minecraft.world.level.levelgen.XoroshiroRandomSource";
        String WGR = "net.minecraft.world.level.levelgen.WorldgenRandom";
        String MTH = "net.minecraft.util.Mth";
        long[] seeds = {0L, 42L, -123456789L, 8675309123456789L};

        // java.util.Random, for reference
        Random jr = new Random(0);
        System.out.println("jur0 " + jr.nextInt() + " " + jr.nextInt(100) + " " + jr.nextLong() + " " + Double.doubleToRawLongBits(jr.nextGaussian()));

        for (long s : seeds) {
            dumpSource("legacy " + s, ctor(LRS, P("long"), s));
            dumpSource("xoro " + s, ctor(XRS, P("long"), s));
            Object w = ctor(WGR, P(RS), ctor(XRS, P("long"), s));
            long deco = (Long) call(w, WGR, "setDecorationSeed", P("long", "int", "int"), s, 160, -320);
            System.out.println("wgx " + s + " deco " + deco);
            dumpSource("wgx-after-deco " + s, w);
            call(w, WGR, "setFeatureSeed", P("long", "int", "int"), deco, 3, 9);
            dumpSource("wgx-feature " + s, w);
            call(w, WGR, "setLargeFeatureSeed", P("long", "int", "int"), s, -7, 12);
            dumpSource("wgx-large " + s, w);
            call(w, WGR, "setLargeFeatureWithSalt", P("long", "int", "int", "int"), s, 3, -4, 10387312);
            dumpSource("wgx-salt " + s, w);
            Object wl = ctor(WGR, P(RS), ctor(LRS, P("long"), s));
            long decoL = (Long) call(wl, WGR, "setDecorationSeed", P("long", "int", "int"), s, -48, 32);
            System.out.println("wgl " + s + " deco " + decoL);
            dumpSource("wgl-after-deco " + s, wl);
            Object slime = call(null, WGR, "seedSlimeChunk", P("int", "int", "long", "long"), 5, -9, s, 987234911L);
            dumpSource("slime " + s, slime);

            Object xf = call(ctor(XRS, P("long"), s), RS, "forkPositional", P());
            dumpSource("xoro-at " + s, call(xf, PRF, "at", P("int", "int", "int"), 13, -64, -200));
            dumpSource("xoro-hash " + s, call(xf, PRF, "fromHashOf", P("java.lang.String"), "octave_-3"));
            dumpSource("xoro-fork " + s, call(ctor(XRS, P("long"), s), RS, "fork", P()));
            Object lf = call(ctor(LRS, P("long"), s), RS, "forkPositional", P());
            dumpSource("legacy-at " + s, call(lf, PRF, "at", P("int", "int", "int"), 13, -64, -200));
            dumpSource("legacy-hash " + s, call(lf, PRF, "fromHashOf", P("java.lang.String"), "minecraft:offset"));
            dumpSource("xoro-lohi " + s, ctor(XRS, P("long", "long"), s, ~s));
            Object cc = ctor(XRS, P("long"), s);
            call(cc, RS, "consumeCount", P("int"), 262);
            dumpSource("xoro-consume " + s, cc);
            Object cl = ctor(LRS, P("long"), s);
            call(cl, RS, "consumeCount", P("int"), 262);
            dumpSource("legacy-consume " + s, cl);
        }
        int[][] pts = {{0, 0, 0}, {1, 2, 3}, {-1000, 64, 2500}, {30000000, -64, -30000000}, {123456, 255, -98765}};
        StringBuilder sb = new StringBuilder("getSeed");
        for (int[] p : pts) sb.append(" ").append(call(null, MTH, "getSeed", P("int", "int", "int"), p[0], p[1], p[2]));
        System.out.println(sb);
        sb = new StringBuilder("hash");
        for (String s : new String[]{"", "octave_-3", "minecraft:offset", "example:dark_forest", "日本"}) sb.append(" ").append(s.hashCode());
        System.out.println(sb);
        sb = new StringBuilder("log");
        for (double d : new double[]{0.5, 1e-300, 0.999999, 1.0000001, 0.123456789, 4.9e-324, 0.25}) sb.append(" ").append(Double.doubleToRawLongBits(StrictMath.log(d)));
        System.out.println(sb);

        // HashSet<BlockPos> iteration order (insertion indices), BlockPos#hashCode values
        {
            List<int[]> ps = new ArrayList<>();
            for (int y = 0; y < 6; y++) for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz += 2) ps.add(new int[]{100 + dx, 64 + y, -37 + dz});
            String BP = "net.minecraft.core.BlockPos";
            Constructor<?> bpc = c(BP).getDeclaredConstructor(int.class, int.class, int.class);
            Map<Object, Integer> idx = new HashMap<>();
            Set<Object> set = new HashSet<>();
            sb = new StringBuilder("hashset");
            for (int i = 0; i < ps.size(); i++) { Object p = bpc.newInstance(ps.get(i)[0], ps.get(i)[1], ps.get(i)[2]); idx.put(p, i); set.add(p); }
            for (Object p : set) sb.append(" ").append(idx.get(p));
            System.out.println(sb);
        }
        sb = new StringBuilder("mthsin");
        for (float f : new float[]{0f, 0.5f, 1.25f, -2.7f, 3.14159f, 100.75f, -1234.5f}) {
            sb.append(" ").append(Float.floatToRawIntBits((Float) call(null, MTH, "sin", P("float"), f)));
            sb.append(" ").append(Float.floatToRawIntBits((Float) call(null, MTH, "cos", P("float"), f)));
        }
        System.out.println(sb);
        {
            String PSN = "net.minecraft.world.level.levelgen.synth.PerlinSimplexNoise";
            Object rs = ctor(WGR, P(RS), ctor(LRS, P("long"), 1234L));
            Object psn = c(PSN).getDeclaredConstructor(c(RS), List.class).newInstance(rs, List.of(0));
            Object rs2 = ctor(WGR, P(RS), ctor(LRS, P("long"), 3456L));
            Object psn2 = c(PSN).getDeclaredConstructor(c(RS), List.class).newInstance(rs2, List.of(-2, -1, 0));
            sb = new StringBuilder("perlinsimplex");
            double[][] q = {{0, 0}, {12.5, -3.25}, {-1000.125, 77.75}, {31.0, 5.0}};
            for (double[] p : q) {
                sb.append(" ").append(Double.doubleToRawLongBits((Double) call(psn, PSN, "getValue", P("double", "double", "boolean"), p[0], p[1], false)));
                sb.append(" ").append(Double.doubleToRawLongBits((Double) call(psn2, PSN, "getValue", P("double", "double", "boolean"), p[0], p[1], false)));
            }
            System.out.println(sb);
        }

        // noises
        String IN ="net.minecraft.world.level.levelgen.synth.ImprovedNoise";
        String SN = "net.minecraft.world.level.levelgen.synth.SimplexNoise";
        String PN = "net.minecraft.world.level.levelgen.synth.PerlinNoise";
        String NN = "net.minecraft.world.level.levelgen.synth.NormalNoise";
        String NP = "net.minecraft.world.level.levelgen.synth.NormalNoise$NoiseParameters";
        double[][] np = {{0, 0, 0}, {0.5, 1.25, -3.75}, {1234.567, -89.01, 2345.678}, {-0.001, 100.5, 7.25}, {1e6, 3.3, -1e6}};
        for (long s : seeds) {
            Object in = ctor(IN, P(RS), ctor(XRS, P("long"), s));
            sb = new StringBuilder("improved " + s);
            for (double[] p : np) sb.append(" ").append(Double.doubleToRawLongBits((Double) call(in, IN, "noise", P("double", "double", "double"), p[0], p[1], p[2])));
            for (double[] p : np) sb.append(" ").append(Double.doubleToRawLongBits((Double) call(in, IN, "noise", P("double", "double", "double", "double", "double"), p[0], p[1], p[2], 0.5, 0.75)));
            System.out.println(sb);
            Object sn = ctor(SN, P(RS), ctor(LRS, P("long"), s));
            sb = new StringBuilder("simplex " + s);
            for (double[] p : np) sb.append(" ").append(Double.doubleToRawLongBits((Double) call(sn, SN, "getValue", P("double", "double"), p[0], p[2])));
            for (double[] p : np) sb.append(" ").append(Double.doubleToRawLongBits((Double) call(sn, SN, "getValue", P("double", "double", "double"), p[0], p[1], p[2])));
            System.out.println(sb);

            Class<?> dal = Class.forName("it.unimi.dsi.fastutil.doubles.DoubleArrayList", true, loader);
            Object list = dal.getConstructor(double[].class).newInstance((Object) new double[]{1.0, 0.0, 2.0, 1.0});
            Class<?> dl = Class.forName("it.unimi.dsi.fastutil.doubles.DoubleList", true, loader);
            Method create = null;
            for (Method m : c(PN).getDeclaredMethods()) {
                if (m.getName().equals(meth.get(PN + "#create(net.minecraft.util.RandomSource,int,it.unimi.dsi.fastutil.doubles.DoubleList)"))
                    && Arrays.equals(m.getParameterTypes(), new Class<?>[]{c(RS), int.class, dl})) create = m;
            }
            create.setAccessible(true);
            Object pn = create.invoke(null, ctor(XRS, P("long"), s), -4, list);
            sb = new StringBuilder("perlin " + s);
            for (double[] p : np) sb.append(" ").append(Double.doubleToRawLongBits((Double) call(pn, PN, "getValue", P("double", "double", "double"), p[0], p[1], p[2])));
            sb.append(" max ").append(Double.doubleToRawLongBits((Double) call(pn, PN, "maxValue", P())));
            System.out.println(sb);
            Method legacy = null;
            for (Method m : c(PN).getDeclaredMethods()) {
                if (m.getName().equals(meth.get(PN + "#createLegacyForLegacyNetherBiome(net.minecraft.util.RandomSource,int,it.unimi.dsi.fastutil.doubles.DoubleList)"))
                    && Arrays.equals(m.getParameterTypes(), new Class<?>[]{c(RS), int.class, dl})) legacy = m;
            }
            legacy.setAccessible(true);
            Object pl = legacy.invoke(null, ctor(LRS, P("long"), s), -5, list);
            sb = new StringBuilder("perlinlegacy " + s);
            for (double[] p : np) sb.append(" ").append(Double.doubleToRawLongBits((Double) call(pl, PN, "getValue", P("double", "double", "double"), p[0], p[1], p[2])));
            sb.append(" max ").append(Double.doubleToRawLongBits((Double) call(pl, PN, "maxValue", P())));
            System.out.println(sb);

            Object params = c(NP).getDeclaredConstructor(int.class, dl).newInstance(-7, dal.getConstructor(double[].class).newInstance((Object) new double[]{1.0, 1.0, 0.0, 0.5}));
            Object nn = call(null, NN, "create", P(RS, NP), ctor(XRS, P("long"), s), params);
            sb = new StringBuilder("normal " + s);
            for (double[] p : np) sb.append(" ").append(Double.doubleToRawLongBits((Double) call(nn, NN, "getValue", P("double", "double", "double"), p[0], p[1], p[2])));
            sb.append(" max ").append(Double.doubleToRawLongBits((Double) call(nn, NN, "maxValue", P())));
            System.out.println(sb);

            // RandomState-style noise: Xoroshiro(seed).forkPositional().fromHashOf(key) -> NormalNoise.create
            Object pf = call(ctor(XRS, P("long"), s), RS, "forkPositional", P());
            Object keyed = call(null, NN, "create", P(RS, NP), call(pf, PRF, "fromHashOf", P("java.lang.String"), "minecraft:gf_test"), params);
            sb = new StringBuilder("keyednoise " + s);
            for (double[] p : np) sb.append(" ").append(Double.doubleToRawLongBits((Double) call(keyed, NN, "getValue", P("double", "double", "double"), p[0] * 0.25, p[1] * 0.5, p[2] * 0.25)));
            System.out.println(sb);
        }

        // BiomeManager: obfuscateSeed, getFiddledDistance and the quart getBiome(BlockPos) reads (a
        // NoiseBiomeSource proxy records the quart it is asked for)
        String BM = "net.minecraft.world.level.biome.BiomeManager";
        String NBS = "net.minecraft.world.level.biome.BiomeManager$NoiseBiomeSource";
        int[][] zp = {{0, 0, 0}, {1, 2, 3}, {-1, -1, -1}, {7, 64, -9}, {-123, 17, 456}, {1000, -31, -2000}, {15, 255, 15}, {-4097, 100, 8191}, {2, 2, 2}, {-30000000, -64, 29999999}};
        for (long s : seeds) {
            long zoomSeed = (Long) call(null, BM, "obfuscateSeed", P("long"), s);
            final int[] last = new int[3];
            Object src = Proxy.newProxyInstance(loader, new Class<?>[]{c(NBS)}, (proxy, m, args) -> {
                if (args != null && args.length == 3) { last[0] = (Integer) args[0]; last[1] = (Integer) args[1]; last[2] = (Integer) args[2]; }
                return null;
            });
            Object bm = ctor(BM, P(NBS, "long"), src, zoomSeed);
            StringBuilder zb = new StringBuilder("biomezoom " + s + " " + zoomSeed);
            for (int[] p : zp) {
                call(bm, BM, "getBiome", P("net.minecraft.core.BlockPos"), ctor("net.minecraft.core.BlockPos", P("int", "int", "int"), p[0], p[1], p[2]));
                zb.append(" ").append(last[0]).append(",").append(last[1]).append(",").append(last[2]);
            }
            zb.append(" fiddled ").append(Double.doubleToRawLongBits((Double) call(null, BM, "getFiddledDistance", P("long", "int", "int", "int", "double", "double", "double"), zoomSeed, 3, -2, 5, 0.25, 0.5, -0.75)));
            System.out.println(zb);
        }
    }
}
