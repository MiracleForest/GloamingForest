// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Java oracle of tests/structure/XoroshiroTest.cpp: runs Minecraft 1.21.1's own
// XoroshiroRandomSource and positional factories (the WorldGenRegion random of
// src/feature/structure/pure/Xoroshiro.h). Compile and run it against the 1.21.1 server jar and
// its libraries (F13's oracle install has them under %LOCALAPPDATA%\gf-oracle\server\libraries):
//   javac -cp <server-1.21.1-*.jar>;<guava>;<joml> -d out tests/structure/oracle/XoroshiroOracle.java
//   java -cp out;<same classpath> XoroshiroOracle > rows.txt   (then prepend the header line of XoroshiroGolden.inc)
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.levelgen.PositionalRandomFactory;
import net.minecraft.world.level.levelgen.XoroshiroRandomSource;

public class XoroshiroOracle {
    static String row(RandomSource r) {
        StringBuilder sb = new StringBuilder();
        sb.append(r.nextInt()).append(", ");
        sb.append(r.nextInt(10)).append(", ");
        sb.append(r.nextInt(1000)).append(", ");
        sb.append(r.nextLong()).append("LL, ");
        sb.append(r.nextBoolean() ? 1 : 0).append(", ");
        sb.append(Float.floatToRawIntBits(r.nextFloat())).append(", ");
        sb.append(Double.doubleToRawLongBits(r.nextDouble())).append("LL, ");
        sb.append(Double.doubleToRawLongBits(r.nextGaussian())).append("LL, ");
        sb.append(r.nextInt(3)).append(", ");
        sb.append(r.nextInt(Integer.MAX_VALUE));
        return sb.toString();
    }
    public static void main(String[] args) {
        long[] seeds = {0L, 1234567L, 20260521L, -5L, 4294967295L};
        int[][] chunks = {{0, 0}, {3, -7}, {-100, 250}};
        for (long seed : seeds) {
            System.out.println("{Kind::Plain, " + seed + "LL, 0, 0, {" + row(new XoroshiroRandomSource(seed)) + "}},");
            RandomSource s = new XoroshiroRandomSource(seed);
            s.setSeed(seed + 1);
            System.out.println("{Kind::Reseeded, " + seed + "LL, 0, 0, {" + row(s) + "}},");
            PositionalRandomFactory root = new XoroshiroRandomSource(seed).forkPositional();
            PositionalRandomFactory region = root.fromHashOf("minecraft:worldgen_region_random").forkPositional();
            for (int[] c : chunks) {
                System.out.println("{Kind::Region, " + seed + "LL, " + c[0] + ", " + c[1] + ", {" + row(region.at(c[0] * 16, 0, c[1] * 16)) + "}},");
            }
            RandomSource f = new XoroshiroRandomSource(seed).fork();
            System.out.println("{Kind::Fork, " + seed + "LL, 0, 0, {" + row(f) + "}},");
        }
    }
}
