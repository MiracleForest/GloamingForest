// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/MagicBeansItem.java
//   src/main/java/twilightforest/block/GrowingBeanstalkBlock.java
//   src/main/java/twilightforest/block/entity/GrowingBeanstalkBlockEntity.java
//   src/main/java/twilightforest/block/UberousSoilBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The beanstalk. Custom blocks have no block entities: the grower's fields live in BlockData
// (key "grower", core::giants::BeanstalkState) and its position in gf:giants:growers, and the module
// ticks every loaded grower each server tick (GrowingBeanstalkBlockEntity#tick). The rumble (dust
// particles for 100 ticks, camera shake while ticker < 110) goes to every client: the dust as block
// particles through gf::fx, the shake as "giants.rumble" to companions (F12 camera::shake).
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/FertilizerType.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/giants/Giants.h"
#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/ext/giants/W0Internals.h"
#include "feature/families/BlockEvents.h"
#include "feature/features/Features.h"
#include "feature/fx/Fx.h"
#include "feature/giants/Giants.h"
#include "feature/net/Net.h"
#include "feature/stats/Stats.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::giants {

namespace cg = core::giants;
namespace ia = gf::interactions;
namespace fe = gf::feature::entity;

namespace {

constexpr char kGrowerKey[]  = "grower";
constexpr char kGrowersKey[] = "gf:giants:growers";

std::set<core::GlobalPos>& growerSet() {
    static std::set<core::GlobalPos> s;
    return s;
}
bool& growersDirty() {
    static bool d = false;
    return d;
}

Block const* defaultBlock(char const* blockId) { return game::blockWith(blockId); }

/// The block GrowingBeanstalkBlockEntity sees at a position.
cg::Cell classify(Block const& b) {
    if (b.isAir()) return cg::Cell::Air;
    auto const& bid = game::blockId(b);
    if (bid == id::BeanstalkGrower) return cg::Cell::Grower;
    if (bid == id::HugeStalk) return cg::Cell::Stalk;
    if (bid == id::WispyCloud) return cg::Cell::WispyCloud;
    if (bid == id::FluffyCloud) return cg::Cell::FluffyCloud;
    if (tags::has(tags::Registry::Block, "minecraft:leaves", bid)) return cg::Cell::Leaves;
    if (structure::blocks::canBeReplaced(b)) return cg::Cell::Replaceable;
    return cg::Cell::Solid;
}

/// The live world under a grower.
class LiveWorld final : public cg::BeanstalkWorld {
public:
    explicit LiveWorld(BlockSource& region) : mRegion(region) {}
    cg::Cell at(int x, int y, int z) override {
        BlockPos p{x, y, z};
        if (y < mRegion.getMinHeight() || y >= mRegion.getMaxHeight() || !mRegion.hasChunksAt(p, 0, false)) return cg::Cell::Solid;
        return classify(mRegion.getBlock(p));
    }
    void setStalk(int x, int y, int z) override {
        if (auto const* b = defaultBlock(id::HugeStalk)) set(x, y, z, *b);
    }
    void setAir(int x, int y, int z) override {
        if (auto const* b = defaultBlock("minecraft:air")) set(x, y, z, *b);
    }
    void setLeaves(int x, int y, int z, int distance) override {
        if (auto const* b = game::blockWithStates(id::BeanstalkLeaves, {{"twilightforest:distance", distance}})) set(x, y, z, *b);
    }

private:
    void set(int x, int y, int z, Block const& b) {
        BlockPos p{x, y, z};
        if (y < mRegion.getMinHeight() || y >= mRegion.getMaxHeight() || !mRegion.hasChunksAt(p, 0, false)) return;
        game::setBlock(mRegion, p, b);
    }
    BlockSource& mRegion;
};

void saveGrowers() {
    if (!game::db::available()) return;
    auto list = nlohmann::json::array();
    for (auto const& g : growerSet()) list.push_back({g.dim, g.x, g.y, g.z});
    game::db::put(kGrowersKey, list);
    growersDirty() = false;
}

void loadGrowers() {
    growerSet().clear();
    auto saved = game::db::get(kGrowersKey);
    if (!saved || !saved->is_array()) return;
    for (auto const& e : *saved)
        if (e.is_array() && e.size() == 4) growerSet().insert(core::GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
}

/// The client-side part of GrowingBeanstalkBlockEntity#tick for ticks < 100: dust of the solid
/// block below random points around the grower (TF: 20 a tick on each client; 5 a tick here,
/// sent as block particles).
void rumbleDust(BlockSource& region, BlockPos const& pos) {
    auto& r = random();
    for (int i = 0; i < 5; i++) {
        float x = (static_cast<float>(pos.x) + r.nextFloat()) + static_cast<float>(r.nextInt(5) * (r.nextBoolean() ? -1 : 1));
        float z = (static_cast<float>(pos.z) + r.nextFloat()) + static_cast<float>(r.nextInt(5) * (r.nextBoolean() ? -1 : 1));
        BlockPos under{static_cast<int>(std::floor(x)), pos.y - 1, static_cast<int>(std::floor(z))};
        if (!region.hasChunksAt(under, 0, false) || !fe::mobs::legacySolid(region, under)) continue;
        auto const& b = region.getBlock(under);
        gf::fx::particle(region.getDimension(), "minecraft:block", Vec3{x, static_cast<float>(pos.y), z}, Vec3{}, 1,
                         {{"block", game::blockId(b)}, {"states", nlohmann::json::object()}});
    }
}

/// ClientGameEvents#isBeanstalkRumbling: companions within 16 blocks shake (giants.rumble).
void sendRumble(BlockSource& region, BlockPos const& pos, int ticksLeft) {
    Vec3 c{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + .5f};
    for (auto* p : fe::playersNear(static_cast<int>(region.getDimensionId()), c, 32.0)) {
        if (!net::isCompanion(*p)) continue;
        net::send(*p, "giants.rumble", {{"pos", {pos.x, pos.y, pos.z}}, {"ticks", ticksLeft}});
    }
}

void tickGrower(core::GlobalPos const& gp, std::vector<core::GlobalPos>& done) {
    auto* region = game::regionOf(gp.dim);
    if (!region) return;
    BlockPos pos = game::blockPos(gp);
    if (!region->hasChunksAt(pos, 0, false)) return; // unloaded: resumes when it loads again
    if (game::blockId(region->getBlock(pos)) != id::BeanstalkGrower) {
        done.push_back(gp);
        return;
    }
    auto const* saved = blockdata::get(*region, pos, kGrowerKey);
    auto        state = saved ? cg::BeanstalkState::fromJson(*saved) : cg::BeanstalkState{};
    LiveWorld   world(*region);
    auto        step = cg::tickBeanstalk(state, world, random(), pos.x, pos.y, pos.z);
    if (state.ticker < 100) rumbleDust(*region, pos);
    if (state.ticker == 1) sendRumble(*region, pos, 110);
    if (step == cg::Step::Finished) {
        // level.setBlockAndUpdate(pos, HUGE_STALK); level.removeBlockEntity(pos)
        blockdata::set(*region, pos, kGrowerKey, nullptr);
        if (auto const* b = defaultBlock(id::HugeStalk)) game::setBlock(*region, pos, *b);
        gf::log().info("giants: beanstalk at {} {} {} finished at layer {} (max y {})", pos.x, pos.y, pos.z, state.layer, state.maxY);
        done.push_back(gp);
        return;
    }
    blockdata::set(*region, pos, kGrowerKey, state.toJson());
}

// ---- uberous soil ------------------------------------------------------------------------------------

bool isMushroom(std::string const& bid) {
    return bid == "minecraft:brown_mushroom" || bid == "minecraft:red_mushroom" || bid == "minecraft:crimson_fungus" || bid == "minecraft:warped_fungus";
}
bool isMushgloom(std::string const& bid) { return bid == "twilightforest:mushgloom"; }
bool isCrop(Block const& b, std::string const& bid) { return b.isCropBlock() || tags::has(tags::Registry::Block, "minecraft:crops", bid); }
/// Java's BushBlock subclasses that are bonemealable: saplings, flowers, grass, berry bushes.
bool isBush(std::string const& bid) {
    if (tags::has(tags::Registry::Block, "minecraft:saplings", bid) || tags::has(tags::Registry::Block, "minecraft:flowers", bid)) return true;
    if (bid == "minecraft:short_grass" || bid == "minecraft:tall_grass" || bid == "minecraft:fern" || bid == "minecraft:large_fern" || bid == "minecraft:sweet_berry_bush"
        || bid == "minecraft:sapling" || bid == "minecraft:grass")
        return true;
    return bid.rfind("twilightforest:", 0) == 0 && (bid.find("sapling") != std::string::npos || bid.find("bush") != std::string::npos);
}
bool isMoss(std::string const& bid) { return bid == "minecraft:moss_block"; }

/// `block instanceof BonemealableBlock`.
bool bonemealable(BlockSource& region, BlockPos const& pos, Block const& b) {
    auto bid = game::blockId(b);
    if (bid == id::UberousSoil) return true; // UberousSoilBlock itself (excluded by the callers)
    if (isCrop(b, bid) || isMushroom(bid) || isMushgloom(bid) || isBush(bid) || isMoss(bid)) return true;
    BlockPos above{pos.x, pos.y + 1, pos.z};
    try {
        return b.getBlockType().canBeFertilized(region, pos, region.getBlock(above));
    } catch (...) {
        return false;
    }
}

bool isDirtLike(std::string const& bid) {
    return bid == "minecraft:farmland" || tags::has(tags::Registry::Block, "minecraft:dirt", bid) || bid == "minecraft:dirt" || bid == "minecraft:grass_block";
}

/// FarmBlock.turnToDirt / setBlockAndUpdate.
void setTo(BlockSource& region, BlockPos const& pos, char const* blockId, nlohmann::json const& states = nlohmann::json::object()) {
    if (auto const* b = game::blockWithStates(blockId, states)) game::setBlock(region, pos, *b);
}

void bonemealParticles(BlockSource& region, BlockPos const& pos) {
    // LevelEvent.PARTICLES_AND_SOUND_PLANT_GROWTH (data 15): the composter / bone meal sparkles
    gf::fx::particle(region.getDimension(), "minecraft:happy_villager", Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, Vec3{}, 8,
                     {{"spread", {0.4, 0.4, 0.4}}});
    gf::fx::sound(region.getDimension(), "minecraft:item.bone_meal.use", Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f});
}

/// The server's next tick: 15 bone meals on the plant (with a fake player in TF).
void bonemealLater(int dim, BlockPos pos) {
    ia::nextTick([dim, pos] {
        auto* region = game::regionOf(dim);
        if (!region || !region->hasChunksAt(pos, 0, false)) return;
        for (int i = 0; i < 15; i++) applyBonemeal(*region, pos);
    });
}

/// MushroomBlock#growMushroom: the configured huge mushroom in place of the plant.
void growMushgloom(BlockSource& region, BlockPos const& pos) {
    auto const* before = &region.getBlock(pos);
    if (auto const* air = game::blockWith("minecraft:air")) game::setBlock(region, pos, *air);
    class Live final : public worldgen::Region {
    public:
        Live(BlockSource& r, BlockPos const& at) : mRegion(r), mCx(at.x >> 4), mCz(at.z >> 4) {}
        int          centerChunkX() const override { return mCx; }
        int          centerChunkZ() const override { return mCz; }
        int          minY() const override { return mRegion.getMinHeight(); }
        int          maxY() const override { return mRegion.getMaxHeight(); }
        bool         isInside(int x, int z) const override { return mRegion.hasChunksAt(BlockPos{x, minY(), z}, 0, false); }
        Block const& getBlock(BlockPos const& p) const override { return mRegion.getBlock(p); }
        bool         setBlock(BlockPos const& p, Block const& b, int) override {
            if (p.y < minY() || p.y >= maxY() || !mRegion.hasChunksAt(p, 0, false)) return false;
            return game::setBlock(mRegion, p, b);
        }
        int height(worldgen::Heightmap, int x, int z) const override {
            for (int y = maxY() - 1; y >= minY(); --y)
                if (!mRegion.getBlock(BlockPos{x, y, z}).isAir()) return y + 1;
            return minY();
        }
        std::string  biome(BlockPos const& p) const override { return worldgen::biomeAt(p); }
        std::int64_t seed() const override { return static_cast<std::int64_t>(mRegion.getLevel().getSeed()); }
        int          dimension() const override { return static_cast<int>(mRegion.getDimensionId()); }

    private:
        BlockSource& mRegion;
        int          mCx, mCz;
    } live(region, pos);
    if (!gf::features::place("twilightforest:mushroom/big_mushgloom", live, pos, random())) game::setBlock(region, pos, *before);
}

void spreadTo(BlockSource& region, BlockPos const& pos) {
    setTo(region, pos, id::UberousSoil);
    BlockPos above{pos.x, pos.y + 1, pos.z};
    if (!region.getBlock(above).isAir()) uberousNeighborChanged(region, pos, above);
}

} // namespace

// ---- magic beans -----------------------------------------------------------------------------------

bool useMagicBeans(Player& player, ItemStack& stack, BlockSource& region, BlockPos const& soil) {
    int      maxY = cg::beanstalkMaxY(soil.y);
    BlockPos above{soil.x, soil.y + 1, soil.z};
    if (!(soil.y < maxY && game::blockId(region.getBlock(soil)) == id::UberousSoil && region.getBlock(above).isAir())) return false;
    if (!player.isCreative()) {
        stack.remove(1);
        if (stack.mCount <= 0) stack = ItemStack::EMPTY_ITEM();
        player.sendInventory(false);
    }
    startGrower(region, above);
    gf::fx::sound(region.getDimension(), "block.twilightforest.beanstalk.grow", Vec3{soil.x + .5f, soil.y + .5f, soil.z + .5f}, 4.0f, 1.0f);
    gf::stats::award(player, std::string("minecraft:used:") + id::MagicBeans);
    gf::log().info("giants: magic beans planted at {} {} {}", above.x, above.y, above.z);
    return true;
}

void startGrower(BlockSource& region, BlockPos const& pos) {
    if (auto const* b = defaultBlock(id::BeanstalkGrower)) game::setBlock(region, pos, *b);
    blockdata::set(region, pos, kGrowerKey, cg::BeanstalkState{}.toJson());
    growerSet().insert(game::globalPos(region, pos));
    growersDirty() = true;
}

std::vector<core::GlobalPos> growers() { return {growerSet().begin(), growerSet().end()}; }

void tickGrowers() {
    if (growerSet().empty()) return;
    std::vector<core::GlobalPos> done;
    auto                         snapshot = growers();
    for (auto const& g : snapshot) tickGrower(g, done);
    for (auto const& g : done) growerSet().erase(g);
    if (!done.empty()) growersDirty() = true;
}

// ---- uberous soil ----------------------------------------------------------------------------------

void uberousNeighborChanged(BlockSource& region, BlockPos const& pos, BlockPos const& fromPos) {
    if (game::blockId(region.getBlock(pos)) != id::UberousSoil) return;
    int const dim = static_cast<int>(region.getDimensionId());
    if (fromPos.y == pos.y + 1 && fromPos.x == pos.x && fromPos.z == pos.z) {
        auto const& above = region.getBlock(fromPos);
        auto        aid   = game::blockId(above);
        if (!(bonemealable(region, fromPos, above) && aid != id::UberousSoil)) {
            if (!above.isAir() && fe::mobs::legacySolid(region, fromPos)) setTo(region, pos, "minecraft:dirt");
            return;
        }
        char const*    newState = "minecraft:dirt";
        nlohmann::json states   = nlohmann::json::object();
        if (isCrop(above, aid)) {
            newState = "minecraft:farmland";
            states   = {{"moisturized_amount", 7}};
        } else if (isMushroom(aid) || isMushgloom(aid)) newState = "minecraft:mycelium";
        else if (isBush(aid)) newState = "minecraft:grass_block";
        else if (isMoss(aid)) newState = "minecraft:moss_block";

        if (isMushgloom(aid)) {
            // only uberous soil grows a mushgloom (it takes no bone meal)
            setTo(region, pos, newState, states);
            growMushgloom(region, fromPos);
            return;
        }
        bonemealParticles(region, fromPos);
        // the soil changes before the plant is bone-mealed (no update loop)
        setTo(region, pos, newState, states);
        bonemealLater(dim, fromPos);
        gf::log().info("giants: uberous soil at {} {} {} feeds {} ({})", pos.x, pos.y, pos.z, aid, newState);
    } else if (fromPos.y + 1 == pos.y && fromPos.x == pos.x && fromPos.z == pos.z) {
        auto const& below = region.getBlock(fromPos);
        if (!bonemealable(region, fromPos, below)) return;
        setTo(region, pos, "minecraft:dirt");
        bonemealLater(dim, fromPos);
        bonemealParticles(region, fromPos);
    }
}

namespace {
/// One of UberousSoilBlock#isValidBonemealTarget / #performBonemeal's three candidate checks.
std::optional<BlockPos> spreadCandidate(BlockSource& region, BlockPos const& pos, int dx, int dz) {
    auto     idAt  = [&](BlockPos const& p) { return game::blockId(region.getBlock(p)); };
    auto     solid = [&](BlockPos const& p) { return fe::mobs::legacySolid(region, p); };
    BlockPos side{pos.x + dx, pos.y, pos.z + dz};
    BlockPos sideUp{side.x, side.y + 1, side.z};
    BlockPos sideUp2{side.x, side.y + 2, side.z};
    BlockPos sideDown{side.x, side.y - 1, side.z};
    if (!solid(sideUp) && isDirtLike(idAt(side)) && idAt(side) != id::UberousSoil) return side;
    if (!solid(sideUp2) && isDirtLike(idAt(sideUp)) && idAt(sideUp) != id::UberousSoil) return sideUp;
    if (!solid(side) && isDirtLike(idAt(sideDown)) && idAt(sideDown) != id::UberousSoil) return sideDown;
    return std::nullopt;
}
constexpr int kHorizontal[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}}; // N S W E (Direction order)
} // namespace

bool uberousBonemealTarget(BlockSource& region, BlockPos const& pos) {
    for (auto const& d : kHorizontal)
        if (spreadCandidate(region, pos, d[0], d[1])) return true;
    return false;
}

void uberousPerformBonemeal(BlockSource& region, BlockPos const& pos, core::jrand::RandomSource& r) {
    // Collections.shuffle(Direction.values()): the vertical two are skipped
    std::vector<int> dirs{0, 1, 2, 3, 4, 5};
    for (int i = static_cast<int>(dirs.size()); i > 1; --i) std::swap(dirs[static_cast<std::size_t>(i - 1)], dirs[static_cast<std::size_t>(r.nextInt(i))]);
    for (int d : dirs) {
        if (d < 2) continue; // DOWN, UP
        auto const& h = kHorizontal[d - 2];
        if (auto target = spreadCandidate(region, pos, h[0], h[1])) {
            spreadTo(region, *target);
            break;
        }
    }
}

bool applyBonemeal(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (b.isAir()) return false;
    if (game::blockId(b) == id::UberousSoil) {
        if (!uberousBonemealTarget(region, pos)) return false;
        uberousPerformBonemeal(region, pos, random());
        return true;
    }
    BlockPos above{pos.x, pos.y + 1, pos.z};
    try {
        if (!b.getBlockType().canBeFertilized(region, pos, region.getBlock(above))) return false;
        return b.getBlockType().onFertilized(region, pos, nullptr, FertilizerType::Bonemeal);
    } catch (...) {
        return false;
    }
}

void installBeanstalk() {
    // MagicBeansItem#useOn: only on uberous soil (any other block: PASS)
    ia::onUseOn({.block = id::UberousSoil, .item = id::MagicBeans}, [](ia::UseOnContext const& c) {
        if (!c.firstEvent) return ia::Result::Consume;
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return ia::Result::Pass;
        return useMagicBeans(c.player, ia::mutableItem(c.item), *region, game::blockPos(c.pos)) ? ia::Result::Consume : ia::Result::Pass;
    });
    // UberousSoilBlock#getStateForPlacement: dirt under a solid non-plant block
    ia::onPlace({.block = id::UberousSoil}, [](ia::PlaceContext const& c) {
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return;
        BlockPos pos = game::blockPos(c.pos);
        BlockPos above{pos.x, pos.y + 1, pos.z};
        auto const& a = region->getBlock(above);
        if (!a.isAir() && fe::mobs::legacySolid(*region, above) && !(bonemealable(*region, above, a) && game::blockId(a) != id::UberousSoil))
            setTo(*region, pos, "minecraft:dirt");
    });
    // UberousSoilBlock#onPlace: a new soil with a block above runs the neighbour logic for it
    gf::blockevents::onPlaced(id::UberousSoil, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) {
        BlockPos above{pos.x, pos.y + 1, pos.z};
        if (!region.getBlock(above).isAir()) uberousNeighborChanged(region, pos, above);
    });
    gf::blockevents::onNeighborChanged(id::UberousSoil, [](BlockSource& region, BlockPos const& pos, BlockPos const& from) { uberousNeighborChanged(region, pos, from); });
    // bone meal on the soil: BoneMealItem#useOn -> isValidBonemealTarget / performBonemeal
    ia::onUseOn({.block = id::UberousSoil, .item = "minecraft:bone_meal"}, [](ia::UseOnContext const& c) {
        if (!c.firstEvent) return ia::Result::Consume;
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return ia::Result::Pass;
        BlockPos pos = game::blockPos(c.pos);
        if (!uberousBonemealTarget(*region, pos)) return ia::Result::Deny;
        if (!c.player.isCreative()) {
            auto& held = ia::mutableItem(c.item);
            held.remove(1);
            if (held.mCount <= 0) held = ItemStack::EMPTY_ITEM();
            c.player.sendInventory(false);
        }
        bonemealParticles(*region, pos);
        uberousPerformBonemeal(*region, pos, random());
        return ia::Result::Consume;
    });

    // the grower: unbreakable (strength -1), driven every server tick
    fe::onLevelTick([] {
        tickGrowers();
        if (growersDirty()) saveGrowers();
    });
    game::db::onWorldReady([] { loadGrowers(); });
    game::db::onWorldSave([] { saveGrowers(); });
    game::db::onWorldClosed([] { growerSet().clear(); });
}

} // namespace gf::feature::giants
