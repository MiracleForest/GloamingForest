#pragma once

// Contract "worldgen", Java-parity random numbers (header owned by F0, implemented by F5 with
// gtest goldens against Java values; spike S2). Pure C++ (gf-core), usable on worker threads.
//
// Bit-exact equivalents of Minecraft 1.21.1's random sources, so generation driven by the same
// seed makes the same choices as TF on Java:
//   - LegacyRandom      net.minecraft.world.level.levelgen.LegacyRandomSource (java.util.Random LCG)
//   - Xoroshiro128pp    net.minecraft.world.level.levelgen.XoroshiroRandomSource (Xoroshiro128++)
//   - WorldgenRandom    net.minecraft.world.level.levelgen.WorldgenRandom (decoration / feature /
//                       large-feature / carver seeding over another source)
//   - PositionalRandomFactory  RandomSource#forkPositional (at(x, y, z), fromHashOf(name))
//
//     gf::core::jrand::LegacyRandom r(seed);
//     int n = r.nextInt(16);

#include <cstdint>
#include <memory>
#include <string_view>

namespace gf::core::jrand {

class PositionalRandomFactory;

/// net.minecraft.util.RandomSource.
class RandomSource {
public:
    virtual ~RandomSource() = default;

    virtual void          setSeed(std::int64_t seed) = 0;
    [[nodiscard]] virtual int          nextInt()                 = 0;
    /// [0, bound), bound > 0 (Java's rejection sampling).
    [[nodiscard]] virtual int          nextInt(int bound)        = 0;
    /// [origin, bound) (RandomSource#nextIntBetweenInclusive is `nextInt(max - min + 1) + min`).
    [[nodiscard]] int                  nextIntBetweenInclusive(int min, int max) { return nextInt(max - min + 1) + min; }
    [[nodiscard]] virtual std::int64_t nextLong()                = 0;
    [[nodiscard]] virtual bool         nextBoolean()             = 0;
    [[nodiscard]] virtual float        nextFloat()               = 0;
    [[nodiscard]] virtual double       nextDouble()              = 0;
    [[nodiscard]] virtual double       nextGaussian()            = 0;
    /// RandomSource#triangle: mode + spread * (nextDouble() - nextDouble()).
    [[nodiscard]] double               triangle(double mode, double spread) { return mode + spread * (nextDouble() - nextDouble()); }
    virtual void                       consumeCount(int count);
    /// A new independent source seeded from this one (RandomSource#fork).
    [[nodiscard]] virtual std::unique_ptr<RandomSource>            fork()           = 0;
    [[nodiscard]] virtual std::unique_ptr<PositionalRandomFactory> forkPositional() = 0;
};

/// Java's java.util.Random LCG (LegacyRandomSource, also used by TF's landmark placement).
class LegacyRandom final : public RandomSource {
public:
    explicit LegacyRandom(std::int64_t seed = 0);

    void                                     setSeed(std::int64_t seed) override;
    [[nodiscard]] int                        nextInt() override;
    [[nodiscard]] int                        nextInt(int bound) override;
    [[nodiscard]] std::int64_t               nextLong() override;
    [[nodiscard]] bool                       nextBoolean() override;
    [[nodiscard]] float                      nextFloat() override;
    [[nodiscard]] double                     nextDouble() override;
    [[nodiscard]] double                     nextGaussian() override;
    [[nodiscard]] std::unique_ptr<RandomSource>            fork() override;
    [[nodiscard]] std::unique_ptr<PositionalRandomFactory> forkPositional() override;
    /// The next `bits` random bits (java.util.Random#next).
    [[nodiscard]] int next(int bits);

private:
    std::int64_t mSeed                 = 0;
    double       mNextNextGaussian     = 0;
    bool         mHaveNextNextGaussian = false;
};

/// Xoroshiro128++ (XoroshiroRandomSource, seeded through RandomSupport#upgradeSeedTo128bit).
class Xoroshiro128pp final : public RandomSource {
public:
    explicit Xoroshiro128pp(std::int64_t seed = 0);
    Xoroshiro128pp(std::int64_t seedLo, std::int64_t seedHi);

    void                                     setSeed(std::int64_t seed) override;
    [[nodiscard]] int                        nextInt() override;
    [[nodiscard]] int                        nextInt(int bound) override;
    [[nodiscard]] std::int64_t               nextLong() override;
    [[nodiscard]] bool                       nextBoolean() override;
    [[nodiscard]] float                      nextFloat() override;
    [[nodiscard]] double                     nextDouble() override;
    [[nodiscard]] double                     nextGaussian() override;
    [[nodiscard]] std::unique_ptr<RandomSource>            fork() override;
    [[nodiscard]] std::unique_ptr<PositionalRandomFactory> forkPositional() override;

private:
    std::uint64_t mLo = 0, mHi = 0;
    double        mNextNextGaussian     = 0;
    bool          mHaveNextNextGaussian = false;
};

/// RandomSource#forkPositional: sources derived from a position or a name.
class PositionalRandomFactory {
public:
    virtual ~PositionalRandomFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<RandomSource> at(int x, int y, int z) = 0;
    /// fromHashOf(ResourceLocation / String): seeded from the Java String#hashCode / MD5 of `name`.
    [[nodiscard]] virtual std::unique_ptr<RandomSource> fromHashOf(std::string_view name) = 0;
};

/// net.minecraft.world.level.levelgen.WorldgenRandom over another source.
class WorldgenRandom final : public RandomSource {
public:
    explicit WorldgenRandom(std::unique_ptr<RandomSource> source);

    /// setDecorationSeed(levelSeed, minBlockX, minBlockZ); returns the decoration seed.
    std::int64_t setDecorationSeed(std::int64_t levelSeed, int minBlockX, int minBlockZ);
    void         setFeatureSeed(std::int64_t decorationSeed, int index, int step);
    void         setLargeFeatureSeed(std::int64_t baseSeed, int chunkX, int chunkZ);
    void         setLargeFeatureWithSalt(std::int64_t levelSeed, int regionX, int regionZ, int salt);
    /// WorldgenRandom.seedSlimeChunk.
    [[nodiscard]] static std::unique_ptr<RandomSource> seedSlimeChunk(int chunkX, int chunkZ, std::int64_t levelSeed, std::int64_t salt);
    [[nodiscard]] int getCount() const { return mCount; }

    void                                     setSeed(std::int64_t seed) override;
    [[nodiscard]] int                        nextInt() override;
    [[nodiscard]] int                        nextInt(int bound) override;
    [[nodiscard]] std::int64_t               nextLong() override;
    [[nodiscard]] bool                       nextBoolean() override;
    [[nodiscard]] float                      nextFloat() override;
    [[nodiscard]] double                     nextDouble() override;
    [[nodiscard]] double                     nextGaussian() override;
    [[nodiscard]] std::unique_ptr<RandomSource>            fork() override;
    [[nodiscard]] std::unique_ptr<PositionalRandomFactory> forkPositional() override;

private:
    std::unique_ptr<RandomSource> mSource;
    int                           mCount = 0;
};

/// Java's String#hashCode of UTF-8 text interpreted as UTF-16 (ASCII ids are identical).
[[nodiscard]] int javaStringHash(std::string_view text);
/// net.minecraft.util.Mth#getSeed(x, y, z).
[[nodiscard]] std::int64_t mthGetSeed(int x, int y, int z);

} // namespace gf::core::jrand
