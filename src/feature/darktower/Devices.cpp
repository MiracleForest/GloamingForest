// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/VanishingBlock.java
//   src/main/java/twilightforest/block/ReappearingBlock.java
//   src/main/java/twilightforest/block/LockedVanishingBlock.java
//   src/main/java/twilightforest/block/BuilderBlock.java
//   src/main/java/twilightforest/block/TranslucentBuiltBlock.java
//   src/main/java/twilightforest/block/AntibuilderBlock.java
//   src/main/java/twilightforest/block/CarminiteReactorBlock.java
//   src/main/java/twilightforest/block/ReactorDebrisBlock.java
//   src/main/java/twilightforest/block/GhastTrapBlock.java
//   src/main/java/twilightforest/block/InfestedTowerwoodBlock.java
//   src/main/java/twilightforest/block/entity/CarminiteBuilderBlockEntity.java
//   src/main/java/twilightforest/block/entity/AntibuilderBlockEntity.java
//   src/main/java/twilightforest/block/entity/CarminiteReactorBlockEntity.java
//   src/main/java/twilightforest/block/entity/ReactorDebrisBlockEntity.java
//   src/main/java/twilightforest/block/entity/GhastTrapBlockEntity.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/darktower/Devices.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/darktower/Runtime.h"
#include "feature/darktower/UrGhast.h"
#include "feature/darktower/Util.h"
#include "feature/families/BlockEvents.h"
#include "feature/progression/Progression.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::darktower::devices {

namespace dt = core::darktower;
namespace ia = gf::interactions;
using Facing = dt::Facing;

namespace {

constexpr char kActive[]   = "twilightforest:active";
constexpr char kVanished[] = "twilightforest:vanished";
constexpr char kLocked[]   = "twilightforest:locked";
constexpr char kState[]    = "twilightforest:state";

bool isVanishingFamily(std::string const& id) {
    return id == id::VanishingBlock || id == id::ReappearingBlock || id == id::UnbreakableVanish || id == id::LockedVanish;
}
bool hasVanished(std::string const& id) { return id == id::ReappearingBlock; }

BlockPos rel(BlockPos const& p, int f) { return offset(p, static_cast<Facing>(f)); }

Json statesOf(Block const& block) {
    Json j = Json::object();
    for (auto const& [k, v] : game::statesOf(block)) {
        if (v == "true" || v == "false") j[k] = v == "true";
        else {
            char* end = nullptr;
            long  n   = std::strtol(v.c_str(), &end, 10);
            if (end && *end == '\0' && !v.empty()) j[k] = static_cast<int>(n);
            else j[k] = v;
        }
    }
    return j;
}

/// setBlockAndUpdate(pos, state.setValue(...)) for the block now at `pos`.
void setStates(BlockSource& region, BlockPos const& pos, Json const& changes) {
    Block const& b  = region.getBlock(pos);
    auto         id = game::blockId(b);
    Json         s  = statesOf(b);
    for (auto const& [k, v] : changes.items()) s[k] = v;
    set(region, pos, id, s);
}

// ---- vanishing family ---------------------------------------------------------------------------

bool vanishedState(Block const& b) { return boolState(b, kVanished); }

void vanishingTick(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto id     = game::blockId(block);
    bool active = boolState(block, kActive);
    if (hasVanished(id) && vanishedState(block)) {
        if (active) {
            setStates(region, pos, {{kVanished, false}, {kActive, false}});
            sound(region, pos, "block.twilightforest.reappearing_block.reappear", 0.3f, 0.3f);
        } else {
            sound(region, pos, "block.twilightforest.reappearing_block.vanish", 0.3f, 0.5f);
            setStates(region, pos, {{kActive, true}});
            runtime::schedule(region, pos, 15);
        }
        return;
    }
    if (!active) return;
    if (hasVanished(id)) {
        setStates(region, pos, {{kActive, false}, {kVanished, true}});
        runtime::schedule(region, pos, 80);
    } else {
        remove(region, pos);
    }
    sound(region, pos, id == id::ReappearingBlock ? "block.twilightforest.reappearing_block.vanish" : "block.twilightforest.vanish.vanish", 0.3f, 0.5f);
    for (int f = 0; f < 6; ++f) activateVanishing(region, rel(pos, f));
}

void vanishingNeighbor(BlockSource& region, BlockPos const& pos) {
    Block const& b = region.getBlock(pos);
    if (!vanishedState(b) && !boolState(b, kActive) && powered(region, pos) && !areBlocksLocked(region, pos)) activateVanishing(region, pos);
}

ia::Result vanishingUse(ia::UseOnContext const& c) {
    // useWithoutItem: reached unless the player sneaks with an item (isSecondaryUseActive)
    if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    auto bp = game::blockPos(c.pos);
    if (vanishedState(c.block) || boolState(c.block, kActive)) return ia::Result::Pass;
    if (!c.firstEvent) return ia::Result::Consume;
    if (areBlocksLocked(*region, bp)) sound(*region, bp, "block.twilightforest.vanish.locked", 1.0f, 0.3f);
    else activateVanishing(*region, bp);
    return ia::Result::Consume;
}

ia::Result lockedUseWithKey(ia::UseOnContext const& c) {
    if (!boolState(c.block, kLocked)) return ia::Result::Pass;
    if (!c.firstEvent) return ia::Result::Consume;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    if (c.player.getPlayerGameType() != GameType::Creative) {
        auto& stack = ia::mutableItem(c.item);
        stack.remove(1);
    }
    auto bp = game::blockPos(c.pos);
    setStates(*region, bp, {{kLocked, false}});
    sound(*region, bp, "block.twilightforest.vanish.unlock", 0.3f, 0.6f);
    gf::log().info("darktower: {} unlocked a locked vanishing block at {}", c.player.getRealName(), c.pos.key());
    return ia::Result::Consume;
}

// ---- carminite builder and built blocks ---------------------------------------------------------------

std::map<GlobalPos, dt::BuilderState>& builders() {
    static std::map<GlobalPos, dt::BuilderState> m;
    return m;
}
std::map<GlobalPos, bool>& lastPower() {
    static std::map<GlobalPos, bool> m;
    return m;
}

dt::BuilderState& builderAt(BlockSource& region, BlockPos const& pos) { return builders()[game::globalPos(region, pos)]; }

std::array<int, 3> arr(BlockPos const& p) { return {p.x, p.y, p.z}; }

void builderNeighbor(BlockSource& region, BlockPos const& pos) {
    Block const& b       = region.getBlock(pos);
    auto         variant = textState(b, kState);
    bool         pw      = powered(region, pos);
    if (variant == "builder_inactive" && pw) {
        setStates(region, pos, {{kState, "builder_active"}});
        sound(region, pos, "block.twilightforest.builder.on", 0.3f, 0.6f);
        runtime::schedule(region, pos, 4);
    }
    if (variant == "builder_active" && !pw) {
        setStates(region, pos, {{kState, "builder_inactive"}});
        sound(region, pos, "block.twilightforest.builder.off", 0.3f, 0.6f);
        runtime::schedule(region, pos, 4);
    }
    if (variant == "builder_timeout" && !pw) setStates(region, pos, {{kState, "builder_inactive"}});
}

void builderPlaced(BlockSource& region, BlockPos const& pos, Block const& block) {
    runtime::track(region, pos);
    if (textState(block, kState) == "builder_inactive" && powered(region, pos)) {
        setStates(region, pos, {{kState, "builder_active"}});
        sound(region, pos, "block.twilightforest.builder.on", 0.3f, 0.6f);
    }
}

void builderTick(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto variant = textState(block, kState);
    if (variant == "builder_active" && powered(region, pos)) {
        auto& s = builderAt(region, pos);
        if (!s.makingBlocks) s.startBuilding(arr(pos));
    }
    if (variant == "builder_inactive" || variant == "builder_timeout") {
        builderAt(region, pos).resetStats(arr(pos));
        for (int f = 0; f < 6; ++f) activateBuiltBlocks(region, rel(pos, f));
    }
}

/// The tracked player of each builder (R16: kept by unique id).
std::map<GlobalPos, std::int64_t>& builderPlayers() {
    static std::map<GlobalPos, std::int64_t> m;
    return m;
}

void builderDevice(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto gp = game::globalPos(region, pos);
    // neighbour changes the game does not report to custom blocks (power through solid blocks)
    bool pw = powered(region, pos);
    auto lp = lastPower().find(gp);
    if (lp == lastPower().end() || lp->second != pw) {
        lastPower()[gp] = pw;
        if (lp != lastPower().end()) builderNeighbor(region, pos);
    }
    Block const& now = region.getBlock(pos);
    if (textState(now, kState) != "builder_active") return; // the ticker exists only while active
    auto& s   = builders()[gp];
    int   dim = dimOf(region);
    auto  r   = dt::tickBuilder(
        s,
        [&] {
            auto* p = nearestPlayer(dim, center(pos), 16.0);
            builderPlayers()[gp] = p ? uidOf(*p) : 0;
            return p != nullptr;
        },
        [&](Facing& f) {
            auto* a = actorById(builderPlayers()[gp]);
            if (!a || !a->isAlive()) return false;
            auto rot = a->getRotation(); // x pitch, y yaw
            f        = dt::builderFacing(rot.x, dt::horizontalFacing(rot.y));
            return true;
        },
        [&](std::array<int, 3> const& p) { return isAir(region, BlockPos{p[0], p[1], p[2]}); }
    );
    if (!s.tracking) builderPlayers().erase(gp);
    if (r.placed) {
        set(region, BlockPos{r.at[0], r.at[1], r.at[2]}, id::BuiltBlock, {{kActive, false}});
        sound(region, pos, "block.twilightforest.builder.create", 0.75f, 1.2f);
    }
    if (r.timeout) {
        setStates(region, pos, {{kState, "builder_timeout"}});
        runtime::schedule(region, pos, 4);
    }
}

void builtTick(BlockSource& region, BlockPos const& pos, Block const& block) {
    if (!boolState(block, kActive)) return;
    remove(region, pos);
    sound(region, pos, "block.twilightforest.builder.replace", 0.3f, 0.5f);
    for (int f = 0; f < 6; ++f) activateBuiltBlocks(region, rel(pos, f));
}

// ---- antibuilder ------------------------------------------------------------------------------------

std::map<GlobalPos, dt::AntibuilderState>& antibuilders() {
    static std::map<GlobalPos, dt::AntibuilderState> m;
    return m;
}

core::jrand::LegacyRandom& antibuilderRandom() {
    static core::jrand::LegacyRandom r(0x5DEECE66DLL ^ static_cast<std::int64_t>(now()));
    return r;
}

/// A stable identity per block type for the snapshot (blocks, not states, are compared).
int blockIdentity(Block const& b) {
    static std::unordered_map<std::string, int> ids;
    auto [it, fresh] = ids.try_emplace(game::blockId(b), static_cast<int>(ids.size()) + 1);
    return it->second;
}

void antibuilderDevice(BlockSource& region, BlockPos const& pos, Block const&) {
    auto gp      = game::globalPos(region, pos);
    auto& s      = antibuilders()[gp];
    int  dim     = dimOf(region);
    bool inRange = !playersNear(dim, center(pos), dt::kAntibuilderPlayerRange, true).empty();
    auto read    = [&](int i) {
        auto         o = dt::antibuilderOffset(i);
        BlockPos     p{pos.x + o[0], pos.y + o[1], pos.z + o[2]};
        Block const& b  = region.getBlock(p);
        auto         id = game::blockId(b);
        dt::AntiBlock a;
        a.id           = blockIdentity(b);
        a.air          = b.isAir();
        a.blocksMotion = !a.air && blocksMotion(region, p);
        a.unbreakable  = !a.air && unbreakable(b);
        a.ignored      = !a.air && tags::has(tags::Registry::Block, "twilightforest:antibuilder_ignores", id);
        return a;
    };
    auto reverts = dt::tickAntibuilder(s, inRange, loaded(region, pos, dt::kAntibuilderRadius), read, antibuilderRandom());
    for (auto const& r : reverts) {
        auto     o = dt::antibuilderOffset(r.index);
        BlockPos p{pos.x + o[0], pos.y + o[1], pos.z + o[2]};
        if (r.currentAir) {
            if (auto const* ab = game::blockWithStates(r.toAntibuilt ? id::Antibuilt : "minecraft:air", Json::object())) breakEffect(region, p, *ab);
        }
        if (r.toAntibuilt) {
            if (idAt(region, p) != id::Antibuilt) set(region, p, id::Antibuilt); // flag 2: no neighbour updates
        } else {
            destroy(region, p, true); // updateOrDestroy to air: destroyBlock(pos, true)
        }
    }
    // client effects: redstone dust in the block, and 1/10 three outline lines
    if (inRange) {
        auto& rnd = random();
        redstoneDust(region, Vec3{pos.x + rnd.nextFloat(), pos.y + rnd.nextFloat(), pos.z + rnd.nextFloat()});
    }
}

// ---- carminite reactor -------------------------------------------------------------------------------

struct Reactor {
    int               counter = 0;
    dt::ReactorBursts bursts;
};
std::map<GlobalPos, Reactor>& reactors() {
    static std::map<GlobalPos, Reactor> m;
    return m;
}

bool reactorImmune(Block const& b) {
    return tags::has(tags::Registry::Block, "twilightforest:carminite_reactor_immune", game::blockId(b)) || unbreakable(b);
}

void createFakeBlock(BlockSource& region, BlockPos const& p, bool diamond) {
    Block const& there = region.getBlock(p);
    if (!there.isAir() && reactorImmune(there)) return;
    if (there.isAir() || !reactorImmune(there)) set(region, p, diamond ? id::FakeDiamond : id::FakeGold);
}

void transformBlock(BlockSource& region, BlockPos const& p, char const* stateId, int fuzz, bool nether) {
    Block const& there = region.getBlock(p);
    if (!there.isAir() && reactorImmune(there)) return;
    if (fuzz == 0 && !there.isAir()) breakEffect(region, p, there);
    if (nether && !there.isAir()) {
        auto& r    = random();
        auto  ores = tags::members(tags::Registry::Block, "twilightforest:carminite_reactor_ores");
        bool  ore  = r.nextInt(8) == 0 && !ores.empty();
        std::string pick = ore ? ores[r.nextInt(static_cast<int>(ores.size()))] : std::string("minecraft:netherrack");
        if (pick == "minecraft:nether_quartz_ore") pick = "minecraft:quartz_ore"; // the Bedrock id
        if (!set(region, p, pick)) set(region, p, "minecraft:netherrack");
        BlockPos above{p.x, p.y + 1, p.z};
        if (isAir(region, above) && fuzz % 3 == 0) set(region, above, "minecraft:fire");
    } else {
        if (std::string(stateId) == "minecraft:air") {
            if (!there.isAir()) remove(region, p);
        } else {
            set(region, p, stateId);
        }
    }
}

void reactorNeighbor(BlockSource& region, BlockPos const& pos) {
    Block const& b = region.getBlock(pos);
    if (boolState(b, kActive)) return;
    for (int f = 0; f < 6; ++f)
        if (idAt(region, rel(pos, f)) != "minecraft:redstone_block") return;
    setStates(region, pos, {{kActive, true}});
    runtime::track(region, pos);
}

void reactorDevice(BlockSource& region, BlockPos const& pos, Block const& block) {
    if (!boolState(block, kActive)) return;
    auto gp = game::globalPos(region, pos);
    auto it = reactors().find(gp);
    if (it == reactors().end()) {
        Reactor r;
        r.bursts = dt::reactorBursts(random());
        it       = reactors().emplace(gp, r).first;
    }
    auto& te = it->second;
    te.counter++;
    if (te.counter % 5 == 0) {
        if (te.counter == 5)
            for (auto const& f : dt::reactorFakeBlocks()) createFakeBlock(region, BlockPos{pos.x + f.dx, pos.y + f.dy, pos.z + f.dz}, f.diamond);
        for (auto const& call : dt::reactorBlobCalls(te.counter)) {
            std::array<int, 3> c{0, 0, 0};
            if (call.centre == 1) c = te.bursts.secondary;
            if (call.centre == 2) c = te.bursts.tertiary;
            for (auto const& b : dt::reactorBlob(call.rad, call.fuzz))
                transformBlock(region, BlockPos{pos.x + c[0] + b.dx, pos.y + c[1] + b.dy, pos.z + c[2] + b.dz}, call.debris ? id::ReactorDebris : "minecraft:air", b.fuzz, call.nether);
        }
        // client: the ambient whoosh every 5 ticks up to 250, volume and pitch counter / 100
        if (te.counter <= 250) sound(region, pos, "block.twilightforest.reactor.ambient", te.counter / 100.0f, te.counter / 100.0f);
    }
    if (te.counter >= dt::kReactorExplodeAt) {
        auto bursts = te.bursts;
        reactors().erase(gp);
        destroy(region, pos, false);
        region.getLevel().explode(region, nullptr, Vec3{float(pos.x), float(pos.y), float(pos.z)}, 4.0f, true, true, 3.40282347e38f, false);
        auto& r = random();
        for (int i = 0; i < 3; i++) {
            for (auto const* c : {&bursts.secondary, &bursts.tertiary}) {
                Vec3 at{
                    float(pos.x + (*c)[0] - 1.5 + r.nextFloat() * 3.0),
                    float(pos.y + (*c)[1] - 1.5 + r.nextFloat() * 3.0),
                    float(pos.z + (*c)[2] - 1.5 + r.nextFloat() * 3.0)
                };
                spawnMob(region, id::Ghastling, at, r.nextFloat() * 360.0f);
            }
        }
        gf::log().info("darktower: carminite reactor at {} exploded", gp.key());
    }
}

void reactorRemoved(BlockSource& region, BlockPos const& pos, Block const&) {
    reactors().erase(game::globalPos(region, pos));
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                BlockPos p{pos.x + dx, pos.y + dy, pos.z + dz};
                auto     id = idAt(region, p);
                if (id == id::FakeGold || id == id::FakeDiamond) destroy(region, p, false);
            }
}

// ---- reactor debris ------------------------------------------------------------------------------------

std::map<GlobalPos, int>& debrisAge() {
    static std::map<GlobalPos, int> m;
    return m;
}

void debrisDevice(BlockSource& region, BlockPos const& pos, Block const&) {
    auto gp  = game::globalPos(region, pos);
    int& age = debrisAge()[gp];
    ++age;
    if (age >= 60) { // will_disappear
        debrisAge().erase(gp);
        destroy(region, pos, false);
    }
}

// ---- ghast trap ------------------------------------------------------------------------------------------

struct Trap {
    std::vector<std::int64_t> dying; ///< dyingGhasts (by unique id)
    int                       counter = 0;
    bool                      lastPower = false;
    bool                      seen      = false;
};
std::map<GlobalPos, Trap>& traps() {
    static std::map<GlobalPos, Trap> m;
    return m;
}

void makeParticlesTo(BlockSource& region, BlockPos const& pos, Vec3 const& to) {
    Vec3 top{pos.x + 0.5f, float(pos.y + 1), pos.z + 0.5f};
    Vec3 diff{top.x - to.x, top.y - to.y, top.z - to.z};
    particle(region, "twilightforest:ghast_trap", top, Vec3{-diff.x, -diff.y, -diff.z}, 5);
}

void trapNeighbor(BlockSource& region, BlockPos const& pos) {
    Block const& b  = region.getBlock(pos);
    auto         gp = game::globalPos(region, pos);
    auto&        te = traps()[gp];
    if (boolState(b, kActive) || te.dying.size() < 3 || !powered(region, pos)) return;
    AABB box{Vec3{pos.x - 6.0f, pos.y - 6.0f, pos.z - 6.0f}, Vec3{pos.x + 7.0f, pos.y + 7.0f, pos.z + 7.0f}};
    for (auto* a : actorsIn(region, box))
        if (a->isPlayer()) progression::trigger(*static_cast<Player*>(a), "twilightforest:activate_ghast_trap");
    setStates(region, pos, {{kActive, true}});
    sound(region, pos, "block.twilightforest.jet.start", 0.3f, 0.6f);
    te.counter = 0; // block event ACTIVATE
    te.dying.clear();
    gf::log().info("darktower: ghast trap at {} activated", gp.key());
}

void trapDevice(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto  gp = game::globalPos(region, pos);
    auto& te = traps()[gp];
    bool  pw = powered(region, pos);
    if (!te.seen || te.lastPower != pw) {
        bool first   = !te.seen;
        te.seen      = true;
        te.lastPower = pw;
        if (!first || pw) trapNeighbor(region, pos);
    }
    Block const& b = region.getBlock(pos);
    if (game::blockId(b) != id::GhastTrap) return;
    auto& rnd = random();
    if (!boolState(b, kActive)) {
        AABB box{Vec3{pos.x - 10.0f, pos.y - 16.0f, pos.z - 10.0f}, Vec3{pos.x + 11.0f, pos.y + 17.0f, pos.z + 11.0f}};
        std::vector<Actor*> nearby;
        for (auto* a : region.getEntities(box, 0.0f))
            if (a && typeOf(*a) == id::Ghastling) nearby.push_back(a);
        for (auto* g : nearby) {
            if (deathTime(*g) > 0) {
                makeParticlesTo(region, pos, g->getEyePos());
                auto uid = uidOf(*g);
                if (std::find(te.dying.begin(), te.dying.end(), uid) == te.dying.end()) te.dying.push_back(uid);
            }
        }
        int charge = std::min<int>(3, static_cast<int>(te.dying.size()));
        te.counter++;
        // the client effects of TF (every player near sees them through fx)
        if (te.counter % 20 == 0 && !nearby.empty()) makeParticlesTo(region, pos, nearby[rnd.nextInt(static_cast<int>(nearby.size()))]->getEyePos());
        Vec3 snd{pos.x + 0.5f, pos.y + 1.5f, pos.z + 0.5f};
        if (charge >= 1 && te.counter % 10 == 0) {
            sparkle(region, pos);
            soundAt(region, snd, "block.twilightforest.ghast_trap.ambient", 1.0f, 1.0f);
        }
        if (charge >= 2) {
            particle(region, "minecraft:smoke", Vec3{pos.x + 0.1f + rnd.nextFloat() * 0.8f, pos.y + 1.05f, pos.z + 0.1f + rnd.nextFloat() * 0.8f},
                     Vec3{(rnd.nextFloat() - rnd.nextFloat()) * 0.05f, 0.0f, (rnd.nextFloat() - rnd.nextFloat()) * 0.05f});
            if (te.counter % 10 == 0) soundAt(region, snd, "block.twilightforest.ghast_trap.ambient", 1.2f, 0.8f);
        }
        if (charge == 3) {
            particle(region, "minecraft:large_smoke", Vec3{pos.x + 0.1f + rnd.nextFloat() * 0.8f, pos.y + 1.05f, pos.z + 0.1f + rnd.nextFloat() * 0.8f},
                     Vec3{(rnd.nextFloat() - rnd.nextFloat()) * 0.05f, 0.05f, (rnd.nextFloat() - rnd.nextFloat()) * 0.05f});
            sparkle(region, pos);
            if (te.counter % 5 == 0) soundAt(region, snd, "block.twilightforest.ghast_trap.ambient", 1.5f, 2.0f);
        }
        return;
    }
    // active
    ++te.counter;
    Vec3 base{pos.x + 0.5f, float(pos.y + 1), pos.z + 0.5f};
    if (te.counter > 100 && te.counter % 4 == 0) {
        particle(region, "twilightforest:huge_smoke", Vec3{pos.x + 0.5f, pos.y + 0.95f, pos.z + 0.5f},
                 Vec3{float(std::cos(te.counter / 10.0) * 0.05), 0.25f, float(std::sin(te.counter / 10.0) * 0.05)});
    } else if (te.counter < 100) {
        float dx = float(std::cos(te.counter / 10.0) * 2.5), dy = 20.0f, dz = float(std::sin(te.counter / 10.0) * 2.5);
        for (auto const& v : {Vec3{dx, dy, dz}, Vec3{-dx, dy, -dz}, Vec3{-dx, dy / 2, dz}, Vec3{dx, dy / 2, -dz}, Vec3{dx / 2, dy / 4, dz / 2}, Vec3{-dx / 2, dy / 4, -dz / 2}})
            particle(region, "twilightforest:ghast_trap", base, v);
    }
    Vec3 snd{pos.x + 0.5f, pos.y + 1.5f, pos.z + 0.5f};
    soundAt(region, snd, te.counter < 30 ? "block.twilightforest.ghast_trap.warmup" : te.counter < 80 ? "block.twilightforest.ghast_trap.on" : "block.twilightforest.ghast_trap.spindown", 1.0f, 4.0f);
    // trap nearby ghasts: AABB(above(16).center, above(16).offset(1,1,1).center).inflate(6, 16, 6)
    AABB box{Vec3{pos.x + 0.5f - 6.0f, pos.y + 16.5f - 16.0f, pos.z + 0.5f - 6.0f}, Vec3{pos.x + 1.5f + 6.0f, pos.y + 17.5f + 16.0f, pos.z + 1.5f + 6.0f}};
    for (auto* a : region.getEntities(box, 0.0f)) {
        if (!a || !a->isAlive() || !ghasts::isTrappable(*a)) continue;
        auto* mob = asMob(*a);
        if (!mob) continue;
        auto p = a->getFeetPos();
        if (ghasts::isUrGhast(*a)) {
            ghasts::trapUrGhast(*mob);
            setMotion(*a, Vec3{float((p.x - pos.x - 0.5) * -0.1), float((p.y - pos.y - 2.5) * -0.1), float((p.z - pos.z - 0.5) * -0.1)});
            if (rnd.nextInt(10) == 0) {
                combat::hurt(*a, 7.0f, "minecraft:generic");
                if (auto* again = asMob(*a)) ghasts::resetPhaseDamage(*again);
            }
        } else {
            setMotion(*a, Vec3{float((p.x - pos.x - 0.5) * -0.1), float((p.y - pos.y - 1.5) * -0.1), float((p.z - pos.z - 0.5) * -0.1)});
            if (rnd.nextInt(10) == 0) combat::hurt(*a, 10.0f, "minecraft:generic");
        }
    }
    if (te.counter >= 120) {
        setStates(region, pos, {{kActive, false}});
        te.counter = 0; // block event DEACTIVATE
        gf::log().info("darktower: ghast trap at {} spun down", gp.key());
    }
}

// ---- infested towerwood -----------------------------------------------------------------------------

void infestedBroken(ia::DestroyContext const& c, bool destroyed) {
    if (!destroyed) return;
    if (c.player.getPlayerGameType() == GameType::Creative) return; // no drops in creative
    // the tool lacks an enchantment of #minecraft:prevents_infested_spawns (silk touch)
    if (!c.itemId.empty() && EnchantUtils::getEnchantLevel(Enchant::Type::SilkTouch, c.item) > 0) return;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return;
    auto bp = game::blockPos(c.pos);
    if (auto* borer = spawnMob(*region, id::Borer, Vec3{bp.x + 0.5f, float(bp.y), bp.z + 0.5f})) {
        (void)borer;
        particle(*region, "minecraft:poof", center(bp)); // spawnAnim
    }
}

// ---- placement and removal --------------------------------------------------------------------------

void onDevicePlaced(BlockSource& region, BlockPos const& pos, Block const& block, Actor*) {
    auto id = game::blockId(block);
    if (id == id::Builder) builderPlaced(region, pos, block);
    else if (id == id::Antibuilder || id == id::GhastTrap || id == id::Reactor || id == id::ReactorDebris) {
        runtime::track(region, pos);
        if (id == id::Reactor) reactorNeighbor(region, pos);
    }
}

} // namespace

bool areBlocksLocked(BlockSource& region, BlockPos const& start) {
    int const                    limit = 512;
    std::deque<BlockPos>         queue{start};
    std::set<std::array<int, 3>> checked;
    for (int iter = 0; !queue.empty() && iter < limit; iter++) {
        BlockPos cur = queue.front(); // Deque#pop takes the head
        queue.pop_front();
        Block const& b  = region.getBlock(cur);
        auto         id = game::blockId(b);
        if (id == id::LockedVanish && boolState(b, kLocked)) return true;
        checked.insert(arr(cur));
        if (isVanishingFamily(id))
            for (int f = 0; f < 6; ++f) {
                auto n = rel(cur, f);
                if (!checked.contains(arr(n))) queue.push_back(n);
            }
    }
    return false;
}

void activateVanishing(BlockSource& region, BlockPos const& pos) {
    Block const& b  = region.getBlock(pos);
    auto         id = game::blockId(b);
    if (!isVanishingFamily(id) || vanishedState(b) || boolState(b, kActive)) return;
    setStates(region, pos, {{kActive, true}});
    runtime::schedule(region, pos, 2 + random().nextInt(5));
}

void activateBuiltBlocks(BlockSource& region, BlockPos const& pos) {
    Block const& b = region.getBlock(pos);
    if (game::blockId(b) != id::BuiltBlock || boolState(b, kActive)) return;
    setStates(region, pos, {{kActive, true}});
    sound(region, pos, "block.twilightforest.builder.replace", 0.3f, 0.6f);
    runtime::schedule(region, pos, 10);
}

dt::BuilderState const* builderState(GlobalPos const& pos) {
    auto it = builders().find(pos);
    return it == builders().end() ? nullptr : &it->second;
}

dt::AntibuilderState const* antibuilderState(GlobalPos const& pos) {
    auto it = antibuilders().find(pos);
    return it == antibuilders().end() ? nullptr : &it->second;
}

std::optional<int> reactorCounter(GlobalPos const& pos) {
    auto it = reactors().find(pos);
    return it == reactors().end() ? std::nullopt : std::optional<int>(it->second.counter);
}

int trapDyingGhasts(GlobalPos const& pos) {
    auto it = traps().find(pos);
    return it == traps().end() ? 0 : static_cast<int>(it->second.dying.size());
}

int trapCounter(GlobalPos const& pos) {
    auto it = traps().find(pos);
    return it == traps().end() ? 0 : it->second.counter;
}

std::vector<BlockPos> ghastTraps(int dim, BlockPos const& c, int radius) {
    std::vector<BlockPos> out;
    auto*                 region = game::regionOf(dim);
    if (!region) return out;
    // the traps the runtime ticks (placed, generated or seen)
    for (auto const& [gp, te] : traps()) {
        if (gp.dim != dim) continue;
        double dx = gp.x - c.x, dy = gp.y - c.y, dz = gp.z - c.z;
        if (dx * dx + dy * dy + dz * dz > double(radius) * radius) continue;
        BlockPos bp{gp.x, gp.y, gp.z};
        if (loaded(*region, bp) && idAt(*region, bp) == id::GhastTrap) out.push_back(bp);
    }
    return out;
}

void install() {
    // vanishing family
    for (auto const* id : {id::VanishingBlock, id::ReappearingBlock, id::UnbreakableVanish, id::LockedVanish}) {
        runtime::onTick(id, vanishingTick);
        blockevents::onNeighborChanged(id, [](BlockSource& r, BlockPos const& p, BlockPos const&) { vanishingNeighbor(r, p); });
        ia::onUseOn({.block = id}, vanishingUse);
    }
    ia::onUseOn({.block = id::LockedVanish, .item = id::TowerKey}, lockedUseWithKey, ia::priority::High);
    // builder
    runtime::onTick(id::Builder, builderTick);
    runtime::onDeviceTick(id::Builder, builderDevice);
    runtime::onForget(id::Builder, [](GlobalPos const& p) {
        builders().erase(p);
        lastPower().erase(p);
        builderPlayers().erase(p);
    });
    blockevents::onNeighborChanged(id::Builder, [](BlockSource& r, BlockPos const& p, BlockPos const&) {
        runtime::track(r, p);
        lastPower()[game::globalPos(r, p)] = powered(r, p);
        builderNeighbor(r, p);
    });
    blockevents::onRemoved(id::Builder, [](BlockSource& r, BlockPos const& p, Block const&) {
        for (int f = 0; f < 6; ++f) activateBuiltBlocks(r, rel(p, f));
    });
    runtime::onTick(id::BuiltBlock, builtTick);
    // antibuilder
    runtime::onDeviceTick(id::Antibuilder, antibuilderDevice);
    runtime::onForget(id::Antibuilder, [](GlobalPos const& p) { antibuilders().erase(p); });
    // reactor
    runtime::onDeviceTick(id::Reactor, reactorDevice);
    runtime::onForget(id::Reactor, [](GlobalPos const& p) { reactors().erase(p); });
    blockevents::onNeighborChanged(id::Reactor, [](BlockSource& r, BlockPos const& p, BlockPos const&) { reactorNeighbor(r, p); });
    blockevents::onRemoved(id::Reactor, reactorRemoved);
    runtime::onDeviceTick(id::ReactorDebris, debrisDevice);
    runtime::onForget(id::ReactorDebris, [](GlobalPos const& p) { debrisAge().erase(p); });
    runtime::onTick(id::ReactorDebris, [](BlockSource& r, BlockPos const& p, Block const&) { destroy(r, p, false); });
    // ghast trap
    runtime::onDeviceTick(id::GhastTrap, trapDevice);
    runtime::onForget(id::GhastTrap, [](GlobalPos const& p) { traps().erase(p); });
    blockevents::onNeighborChanged(id::GhastTrap, [](BlockSource& r, BlockPos const& p, BlockPos const&) {
        runtime::track(r, p);
        trapNeighbor(r, p);
    });
    // placement of every ticking device
    for (auto const* id : {id::Builder, id::Antibuilder, id::GhastTrap, id::Reactor, id::ReactorDebris})
        blockevents::onPlaced(id, onDevicePlaced);
    // infested towerwood
    ia::onDestroyed({.block = id::InfestedTowerwood}, infestedBroken);
}

} // namespace gf::feature::darktower::devices
