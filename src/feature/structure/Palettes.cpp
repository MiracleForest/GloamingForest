// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/woods/WoodPalette.java
//   src/main/java/twilightforest/world/components/structures/util/SimpleRandomBlockSelector.java
//   src/main/java/twilightforest/world/components/structures/selectors/CastleRandomBlockSelectorFactory.java
//   src/main/java/twilightforest/world/components/structures/selectors/IceTowerRandomBlockSelectorFactory.java
//   src/main/java/twilightforest/world/components/structures/selectors/KnightStonesRandomBlockSelectorFactory.java
//   src/main/java/twilightforest/world/components/structures/selectors/MazestoneRandomBlockSelectoryFactory.java
//   src/main/java/twilightforest/world/components/structures/selectors/StrongholdStonesRandomBlockSelectorFactory.java
//   src/main/java/twilightforest/world/components/structures/selectors/TowerwoodRandomBlockSelectorFactory.java
//   src/main/java/twilightforest/util/WorldUtil.java (getRandomElementWithWeights)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/structure/Internal.h"

#include <array>
#include <mutex>
#include <shared_mutex>

#include "mc/world/level/block/Block.h"

namespace gf::structure {

// ---- wood palettes -------------------------------------------------------------------------------------

namespace palettes {

namespace {

/// WoodPalette.WoodShapes order: BLOCK, STAIRS, SLAB, BUTTON, FENCE, GATE, PLATE, BANISTER.
constexpr std::array<char const*, 8> kShapeKeys = {"planks", "stairs", "slab", "button", "fence", "gate", "plate", "banister"};

struct Palette {
    std::array<std::string, 8> javaIds;
    std::array<std::string, 8> bedrockIds;
};

std::shared_mutex              gMutex;
std::map<std::string, Palette> gPalettes;

int shapeOf(Palette const& p, Block const& block) {
    auto const& id = blocks::id(block);
    for (int i = 0; i < 8; i++)
        if (!p.bedrockIds[static_cast<std::size_t>(i)].empty() && p.bedrockIds[static_cast<std::size_t>(i)] == id) return i;
    // a double slab of the palette's slab
    if (id.ends_with("_double_slab") && !p.bedrockIds[2].empty() && p.bedrockIds[2].ends_with("_slab") &&
        id == p.bedrockIds[2].substr(0, p.bedrockIds[2].size() - 5) + "_double_slab")
        return 2;
    return -1;
}

} // namespace

void load(std::map<std::string, Json> const& docs) {
    std::map<std::string, Palette> out;
    for (auto const& [id, doc] : docs) {
        Palette p;
        for (std::size_t i = 0; i < 8; i++) {
            p.javaIds[i]    = doc.value(kShapeKeys[i], std::string{});
            p.bedrockIds[i] = p.javaIds[i].empty() ? std::string{} : blocks::bedrockId(p.javaIds[i]);
        }
        out[id] = std::move(p);
    }
    std::unique_lock lock(gMutex);
    gPalettes = std::move(out);
}

std::vector<std::string> ids() {
    std::shared_lock         lock(gMutex);
    std::vector<std::string> out;
    for (auto const& [id, _] : gPalettes) out.push_back(id);
    return out;
}

Block const* swizzle(std::string const& target, std::string const& replacement, Block const& block) {
    std::shared_lock lock(gMutex);
    auto             t = gPalettes.find(target);
    auto             r = gPalettes.find(replacement);
    if (t == gPalettes.end() || r == gPalettes.end()) return nullptr;
    int shape = shapeOf(t->second, block);
    if (shape < 0) return nullptr;
    auto const& outId = r->second.javaIds[static_cast<std::size_t>(shape)];
    auto const* out   = blocks::fromJava(outId);
    if (!out) return nullptr;
    // BLOCK -> planks default state; the other shapes keep their states (transferAllStateKeys)
    return shape == 0 ? out : &blocks::transferStates(block, *out);
}

} // namespace palettes

Block const& palettes::apply(std::string const& paletteId, Block const& block, RandomSource&) {
    std::vector<std::string> sources;
    {
        std::shared_lock lock(gMutex);
        for (auto const& [id, p] : gPalettes)
            if (shapeOf(p, block) >= 0) sources.push_back(id);
    }
    for (auto const& source : sources)
        if (auto const* b = swizzle(source, paletteId, block)) return *b;
    return block;
}

// ---- random block selectors -------------------------------------------------------------------------------

namespace selectors {

namespace {

/// SimpleRandomBlockSelector: walls get a weighted pick, insides air.
class SimpleRandomBlockSelector final : public BlockSelector {
public:
    explicit SimpleRandomBlockSelector(std::vector<std::pair<std::string, float>> entries) : mEntries(std::move(entries)) {}

    Block const& next(RandomSource& random, int, int, int, bool wall) override {
        if (!wall) return blocks::air();
        // WorldUtil.getRandomElementWithWeights
        double sum = 0;
        for (auto const& [id, w] : mEntries) sum += static_cast<double>(w);
        auto  totalWeight = static_cast<float>(sum);
        float value       = random.nextFloat() * totalWeight;
        for (auto const& [id, w] : mEntries) {
            value -= w;
            if (value < 0) return blocks::java(id);
        }
        return blocks::java(mEntries[static_cast<std::size_t>(random.nextInt(static_cast<int>(mEntries.size())))].first);
    }

private:
    std::vector<std::pair<std::string, float>> mEntries;
};

std::map<std::string, std::unique_ptr<BlockSelector>>& all() {
    static std::map<std::string, std::unique_ptr<BlockSelector>> table = [] {
        std::map<std::string, std::unique_ptr<BlockSelector>> t;
        auto add = [&](char const* id, std::vector<std::pair<std::string, float>> e) { t[id] = std::make_unique<SimpleRandomBlockSelector>(std::move(e)); };
        add("twilightforest:castle",
            {{"twilightforest:worn_castle_brick", 0.1F}, {"twilightforest:cracked_castle_brick", 0.1F}, {"twilightforest:castle_brick", 0.8F}});
        add("twilightforest:ice_tower", {{"twilightforest:aurora_block", 1.0F}});
        add("twilightforest:knight_stones",
            {{"twilightforest:cracked_underbrick", 0.2F}, {"twilightforest:mossy_underbrick", 0.3F}, {"twilightforest:underbrick", 0.5F}});
        add("twilightforest:mazestone",
            {{"twilightforest:mossy_mazestone", 0.2F}, {"twilightforest:cracked_mazestone", 0.3F}, {"twilightforest:mazestone_brick", 0.5F}});
        add("twilightforest:stronghold_stones", {{"minecraft:cracked_stone_bricks", 0.2F},
                                                 {"minecraft:mossy_stone_bricks", 0.3F},
                                                 {"minecraft:infested_stone_bricks", 0.05F},
                                                 {"minecraft:stone_bricks", 0.45F}});
        add("twilightforest:towerwood", {{"twilightforest:cracked_towerwood", 0.1F},
                                         {"twilightforest:mossy_towerwood", 0.1F},
                                         {"twilightforest:infested_towerwood", 0.025F},
                                         {"twilightforest:towerwood", 0.775F}});
        return t;
    }();
    return table;
}

} // namespace

BlockSelector* get(std::string const& id) {
    auto& t  = all();
    auto  it = t.find(id);
    if (it == t.end()) {
        warnOnce("selector:" + id, "structure: unknown block selector " + id);
        return nullptr;
    }
    return it->second.get();
}

std::vector<std::string> ids() {
    std::vector<std::string> out;
    for (auto const& [id, _] : all()) out.push_back(id);
    return out;
}

} // namespace selectors
} // namespace gf::structure
