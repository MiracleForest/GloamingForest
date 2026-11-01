// Neutral stub of the Java-parity random sources (F0). F5 replaces this file (rule R18).
#include "core/jrand/JRand.h"

#include "core/Stub.h"

namespace gf::core::jrand {

namespace {
/// A factory that hands out stub sources.
class StubFactory final : public PositionalRandomFactory {
public:
    std::unique_ptr<RandomSource> at(int, int, int) override { return std::make_unique<LegacyRandom>(0); }
    std::unique_ptr<RandomSource> fromHashOf(std::string_view) override { return std::make_unique<LegacyRandom>(0); }
};
} // namespace

void RandomSource::consumeCount(int count) {
    for (int i = 0; i < count; ++i) (void)nextInt();
}

LegacyRandom::LegacyRandom(std::int64_t seed) : mSeed(seed) {}
void         LegacyRandom::setSeed(std::int64_t seed) { mSeed = seed; }
int          LegacyRandom::next(int) { GF_STUB("jrand.LegacyRandom"); return 0; }
int          LegacyRandom::nextInt() { return next(32); }
int          LegacyRandom::nextInt(int) { return next(31); }
std::int64_t LegacyRandom::nextLong() { return next(32); }
bool         LegacyRandom::nextBoolean() { return next(1) != 0; }
float        LegacyRandom::nextFloat() { return static_cast<float>(next(24)); }
double       LegacyRandom::nextDouble() { return static_cast<double>(next(26)); }
double       LegacyRandom::nextGaussian() { return static_cast<double>(next(26)); }
std::unique_ptr<RandomSource>            LegacyRandom::fork() { return std::make_unique<LegacyRandom>(0); }
std::unique_ptr<PositionalRandomFactory> LegacyRandom::forkPositional() { return std::make_unique<StubFactory>(); }

Xoroshiro128pp::Xoroshiro128pp(std::int64_t) {}
Xoroshiro128pp::Xoroshiro128pp(std::int64_t, std::int64_t) {}
void         Xoroshiro128pp::setSeed(std::int64_t) { GF_STUB("jrand.Xoroshiro128pp"); }
int          Xoroshiro128pp::nextInt() { GF_STUB("jrand.Xoroshiro128pp"); return 0; }
int          Xoroshiro128pp::nextInt(int) { return nextInt(); }
std::int64_t Xoroshiro128pp::nextLong() { return nextInt(); }
bool         Xoroshiro128pp::nextBoolean() { return false; }
float        Xoroshiro128pp::nextFloat() { return 0.f; }
double       Xoroshiro128pp::nextDouble() { return 0.0; }
double       Xoroshiro128pp::nextGaussian() { return 0.0; }
std::unique_ptr<RandomSource>            Xoroshiro128pp::fork() { return std::make_unique<Xoroshiro128pp>(0); }
std::unique_ptr<PositionalRandomFactory> Xoroshiro128pp::forkPositional() { return std::make_unique<StubFactory>(); }

WorldgenRandom::WorldgenRandom(std::unique_ptr<RandomSource> source) : mSource(std::move(source)) {}
std::int64_t WorldgenRandom::setDecorationSeed(std::int64_t, int, int) { GF_STUB("jrand.WorldgenRandom"); return 0; }
void         WorldgenRandom::setFeatureSeed(std::int64_t, int, int) { GF_STUB("jrand.WorldgenRandom"); }
void         WorldgenRandom::setLargeFeatureSeed(std::int64_t, int, int) { GF_STUB("jrand.WorldgenRandom"); }
void         WorldgenRandom::setLargeFeatureWithSalt(std::int64_t, int, int, int) { GF_STUB("jrand.WorldgenRandom"); }
std::unique_ptr<RandomSource> WorldgenRandom::seedSlimeChunk(int, int, std::int64_t, std::int64_t) {
    GF_STUB("jrand.WorldgenRandom");
    return std::make_unique<LegacyRandom>(0);
}
void         WorldgenRandom::setSeed(std::int64_t seed) { if (mSource) mSource->setSeed(seed); }
int          WorldgenRandom::nextInt() { ++mCount; return mSource ? mSource->nextInt() : 0; }
int          WorldgenRandom::nextInt(int bound) { ++mCount; return mSource ? mSource->nextInt(bound) : 0; }
std::int64_t WorldgenRandom::nextLong() { ++mCount; return mSource ? mSource->nextLong() : 0; }
bool         WorldgenRandom::nextBoolean() { ++mCount; return mSource && mSource->nextBoolean(); }
float        WorldgenRandom::nextFloat() { ++mCount; return mSource ? mSource->nextFloat() : 0.f; }
double       WorldgenRandom::nextDouble() { ++mCount; return mSource ? mSource->nextDouble() : 0.0; }
double       WorldgenRandom::nextGaussian() { ++mCount; return mSource ? mSource->nextGaussian() : 0.0; }
std::unique_ptr<RandomSource>            WorldgenRandom::fork() { return mSource ? mSource->fork() : std::make_unique<LegacyRandom>(0); }
std::unique_ptr<PositionalRandomFactory> WorldgenRandom::forkPositional() { return std::make_unique<StubFactory>(); }

int javaStringHash(std::string_view) {
    GF_STUB("jrand.javaStringHash");
    return 0;
}

std::int64_t mthGetSeed(int, int, int) {
    GF_STUB("jrand.mthGetSeed");
    return 0;
}

} // namespace gf::core::jrand
