// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// A worldgen::Region over a live BlockSource (commands, selftests, landmark::generateAt): pieces
// and templates placed through it write to the loaded world directly, and block entity work
// (loot, spawners, signs) happens at once instead of being queued for generated chunks.

#include "feature/structure/Internal.h"

namespace gf::structure {

class BlockSourceRegion final : public worldgen::Region {
public:
    BlockSourceRegion(BlockSource& source, BlockPos const& center);
    ~BlockSourceRegion() override;
    BlockSourceRegion(BlockSourceRegion const&)            = delete;
    BlockSourceRegion& operator=(BlockSourceRegion const&) = delete;

    [[nodiscard]] BlockSource& source() const { return mSource; }

    [[nodiscard]] int          centerChunkX() const override;
    [[nodiscard]] int          centerChunkZ() const override;
    [[nodiscard]] int          minY() const override;
    [[nodiscard]] int          maxY() const override;
    [[nodiscard]] bool         isInside(int x, int z) const override;
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const override;
    bool                       setBlock(BlockPos const& pos, Block const& block, int flags = 3) override;
    [[nodiscard]] int          height(worldgen::Heightmap type, int x, int z) const override;
    [[nodiscard]] std::string  biome(BlockPos const& pos) const override;
    [[nodiscard]] std::int64_t seed() const override;
    [[nodiscard]] int          dimension() const override;

private:
    BlockSource& mSource;
    BlockPos     mCenter;
};

} // namespace gf::structure
