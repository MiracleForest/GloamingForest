// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/FireJetBlock.java
//   src/main/java/twilightforest/block/EncasedFireJetBlock.java
//   src/main/java/twilightforest/block/TFSmokerBlock.java
//   src/main/java/twilightforest/block/EncasedSmokerBlock.java
//   src/main/java/twilightforest/block/entity/FireJetBlockEntity.java
//   src/main/java/twilightforest/block/entity/TFSmokerBlockEntity.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Fire jets and smokers. Custom blocks have no block entities (S3): the jets' FireJetBlockEntity
// (twilightforest:flame_jet) ticks every popping / flaming jet from a registry of positions, the
// smokers' TFSmokerBlockEntity (twilightforest:smoker) every smoker near a player. The registry is
// filled when a jet starts (random tick, redstone) and by a palette scan around the players (a jet
// still popping or flaming after a reload, the jets and smokers the fire_jet feature generates);
// every other placed one (a player's, /setblock's, a structure's) is remembered in the world DB
// (gf:hydra:jets) and comes back with its chunk, as Java's block entities do (watches are dropped while the chunk is unloaded). The encased blocks
// read redstone through gf::redstone (custom blocks get no neighbour updates): F11's neighbour
// changes plus a watch on every encased block the scan or a placement found. TF's client-side
// effects (particles, the jet's active / start sounds) go out through gf::fx.

#include "feature/hydra/Hydra.h"
#include "feature/hydra/World.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/dimension/World.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::hydra {

namespace fe = gf::feature::hydra::world;
using core::GlobalPos;

namespace {

constexpr int kScanRadius = 32; ///< horizontal blocks around a player the palette scan covers
constexpr int kScanHeight = 24; ///< blocks above and below
constexpr int kScanInterval = 20; ///< ticks between two scans
/// Java's minecraft:large_smoke for the jets (assets/pack/rp/particles/hydra/large_smoke.json; until
/// F10's fx maps minecraft:large_smoke to a smoke effect).
constexpr char kLargeSmokeStandIn[] = "gf_hydra:large_smoke";

struct Jet {
    int counter = 0;
};
struct Smoker {
    long long counter = 0;
};

struct JetState {
    std::map<GlobalPos, Jet>                          jets;    ///< popping / flaming jets
    std::map<GlobalPos, Smoker>                       smokers; ///< smokers near players
    std::map<GlobalPos, gf::redstone::WatchId>        watches; ///< encased blocks
    int                                               tickId = 0; ///< world::onLevelTick
    fe::JavaRandom                                    random{fe::freshSeed()};
    int                                               smokeParticles = 0;
    std::uint64_t                                     tick           = 0;
    bool                                              hooked         = false;
    /// Jets and smokers placed in any dimension (Java keeps their block entities with the chunk):
    /// kept in the world DB (gf:hydra:jets), re-attached whenever their chunk is loaded.
    std::set<GlobalPos>                               known;
    bool                                              knownDirty  = false;
    bool                                              knownLoaded = false;
};
JetState& J() {
    static JetState s;
    return s;
}

/// The positions the twilightforest:fire_jet feature placed a jet or smoker at, until their
/// placement is seen (world generation may run off the server thread). Bounded: the oldest are
/// dropped beyond kGeneratedMax (a placement the game never reports).
struct Generated {
    std::mutex                                                lock;
    std::map<std::tuple<int, int, int>, std::uint64_t>        at;
    std::uint64_t                                             seq = 0;
};
constexpr std::uint64_t kGeneratedMax = 4096;
Generated&              G() {
    static Generated g;
    return g;
}

/// Whether a placement at block (x, y, z) came from the fire_jet feature (consumes the note).
bool takeGenerated(int x, int y, int z) {
    auto&            g = G();
    std::scoped_lock l(g.lock);
    return g.at.erase({x, y, z}) > 0;
}

std::string idAt(BlockSource& region, BlockPos const& pos) { return game::blockIdAt(region, pos); }

std::string stateValue(Block const& block, std::string const& name) {
    auto states = game::statesOf(block);
    auto it     = states.find(name);
    return it == states.end() ? std::string{} : it->second;
}

bool isJet(std::string const& id) { return id == kFireJet || id == kEncasedJet; }
bool isSmoker(std::string const& id) { return id == kSmoker || id == kEncasedSmoker; }

void setJetState(BlockSource& region, BlockPos const& pos, std::string const& id, char const* value) {
    if (auto const* b = game::blockWithStates(id, {{"twilightforest:state", value}})) game::setBlock(region, pos, *b);
}

void sound(BlockSource& region, BlockPos const& pos, double dx, double dy, double dz, char const* event, float volume, float pitch) {
    gf::fx::sound(region.getDimension(), event, Vec3{static_cast<float>(pos.x + dx), static_cast<float>(pos.y + dy), static_cast<float>(pos.z + dz)}, volume, pitch);
}

void track(GlobalPos const& gp) { J().jets.try_emplace(gp); }

/// FireJetBlock#isLava: #twilightforest:fire_jet_fuel blocks or a fluid of the fluid tag (lava).
bool isLava(BlockSource& region, BlockPos const& pos) {
    auto id = idAt(region, pos);
    if (id.empty()) return false;
    if (gf::tags::has(gf::tags::Registry::Block, "twilightforest:fire_jet_fuel", id)) return true;
    // the fluid tag twilightforest:fire_jet_fuel = #minecraft:lava (still and flowing lava)
    return id == "minecraft:lava" || id == "minecraft:flowing_lava"
        || gf::tags::has(gf::tags::Registry::Fluid, "twilightforest:fire_jet_fuel", id);
}

/// FireJetBlock#findLavaAround: the block itself, else 3 tries on its layer (intentionally unreliable).
BlockPos findLavaAround(BlockSource& region, BlockPos const& pos, core::jrand::RandomSource& random) {
    if (isLava(region, pos)) return pos;
    for (int i = 0; i < 3; i++) {
        int      dx = random.nextInt(3) - 1;
        int      dz = random.nextInt(3) - 1;
        BlockPos p{pos.x + dx, pos.y, pos.z + dz};
        if (isLava(region, p)) return p;
    }
    return pos;
}

void jetRandomTick(BlockSource& region, BlockPos const& pos, Block const& block, core::jrand::RandomSource& random) {
    if (stateValue(block, "twilightforest:state") != "idle") return;
    BlockPos below{pos.x, pos.y - 1, pos.z};
    BlockPos lavaPos = findLavaAround(region, below, random);
    if (!isLava(region, lavaPos)) return;
    if (auto const* air = game::blockWith("minecraft:air")) game::setBlock(region, lavaPos, *air);
    setJetState(region, pos, kFireJet, "popping");
    track(game::globalPos(region, pos));
    log().info("hydra: fire jet at {} {} {} took lava and pops", pos.x, pos.y, pos.z);
}

/// EncasedFireJetBlock#neighborChanged / EncasedSmokerBlock#neighborChanged.
void encasedUpdate(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    auto         id    = game::blockId(block);
    if (id == kEncasedJet) {
        auto variant = stateValue(block, "twilightforest:state");
        bool powered = gf::redstone::powered(region, pos);
        if (variant == "idle" && powered) {
            setJetState(region, pos, kEncasedJet, "popping");
            sound(region, pos, 0.5, 0.5, 0.5, "block.twilightforest.jet.start", 0.3F, 0.6F);
            track(game::globalPos(region, pos));
            log().info("hydra: encased fire jet at {} {} {} powered: popping", pos.x, pos.y, pos.z);
        } else if (variant == "timeout" && !powered) {
            setJetState(region, pos, kEncasedJet, "idle");
            log().info("hydra: encased fire jet at {} {} {} unpowered: idle", pos.x, pos.y, pos.z);
        }
    } else if (id == kEncasedSmoker) {
        bool powered = gf::redstone::powered(region, pos);
        bool active  = stateValue(block, "twilightforest:active") == "true";
        if (active != powered) {
            if (auto const* b = game::blockWithStates(kEncasedSmoker, {{"twilightforest:active", powered}})) game::setBlock(region, pos, *b);
            sound(region, pos, 0.5, 0.5, 0.5, "block.twilightforest.smoker.start", 0.3F, 0.6F);
            J().smokers.try_emplace(game::globalPos(region, pos));
            log().info("hydra: encased smoker at {} {} {} {}", pos.x, pos.y, pos.z, powered ? "active" : "inactive");
        }
    }
}

/// Watches the redstone power of an encased block (once per position).
void watch(BlockSource& region, BlockPos const& pos) {
    auto gp = game::globalPos(region, pos);
    if (J().watches.contains(gp)) return;
    // the first poll reports the power the block already had: no neighbour changed then (a block
    // placed next to a powered one waits for the next change, as Java's onPlace does nothing)
    auto first = std::make_shared<int>(gf::redstone::power(region, pos));
    J().watches[gp] = gf::redstone::watch(gp, [first](GlobalPos const& at, int power) {
        if (*first >= 0) {
            bool same = power == *first;
            *first    = -1;
            if (same) return;
        }
        auto* r = game::regionOf(at.dim);
        if (!r) return;
        BlockPos p = game::blockPos(at);
        auto     id = idAt(*r, p);
        if (id != kEncasedJet && id != kEncasedSmoker) {
            // the block is gone: drop the watch on the next tick (not inside the poll)
            gf::interactions::nextTick([at] {
                auto it = J().watches.find(at);
                if (it == J().watches.end()) return;
                gf::redstone::unwatch(it->second);
                J().watches.erase(it);
            });
            return;
        }
        encasedUpdate(*r, p);
    });
}

void jetHurtEntities(BlockSource& region, BlockPos const& pos) {
    // AABB between the centres of pos + (-2, 0, -2) and pos + (2, 4, 2): every entity that is not
    // fire immune (items burn, boats and minecarts take the damage)
    AABB box{Vec3{pos.x - 1.5F, pos.y + 0.5F, pos.z - 1.5F}, Vec3{pos.x + 2.5F, pos.y + 4.5F, pos.z + 2.5F}};
    std::vector<std::int64_t> ids;
    for (auto* e : fe::actorsIn(region, static_cast<int>(region.getDimensionId()), box))
        if (e && e->isAlive() && !e->isFireImmune()) ids.push_back(fe::uidOf(*e));
    // by id: a hit may remove an actor (a burnt item, a broken boat) while the others wait
    for (auto id : ids) {
        Actor* e = fe::actorById(id);
        if (!e || !e->isAlive()) continue;
        gf::combat::hurt(*e, 2.0F, "twilightforest:fire_jet");
        if ((e = fe::actorById(id)) && e->isAlive()) e->setOnFire(15, true); // setRemainingFireTicks(300)
    }
}

/// FireJetBlockEntity#tick: false when the jet stops ticking.
bool tickJet(GlobalPos const& gp, Jet& jet) {
    auto* region = game::regionOf(gp.dim);
    if (!region) return false;
    BlockPos pos = game::blockPos(gp);
    if (!region->hasChunksAt(pos, 0, false)) return false;
    Block const& block = region->getBlock(pos);
    auto         id    = game::blockId(block);
    if (!isJet(id)) return false;
    auto  variant = stateValue(block, "twilightforest:state");
    auto& r       = J().random;
    if (variant == "popping") {
        if (++jet.counter >= 80) {
            jet.counter = 0;
            // turn to flame
            setJetState(*region, pos, id, "flame");
            log().info("hydra: jet at {} {} {} flames", pos.x, pos.y, pos.z);
        } else if (jet.counter % 20 == 0) {
            for (int i = 0; i < 8; i++)
                gf::fx::particle(region->getDimension(), "minecraft:lava", Vec3{pos.x + 0.5F, pos.y + 1.5F, pos.z + 0.5F});
            sound(*region, pos, 0.5, 0.5, 0.5, "block.twilightforest.jet.pop", 0.2F + r.nextFloat() * 0.2F, 0.9F + r.nextFloat() * 0.15F);
        }
        return true;
    }
    if (variant == "flame") {
        double x = pos.x, y = pos.y + 1, z = pos.z;
        bool   done = false;
        if (++jet.counter > 60) {
            jet.counter = 0;
            done        = true;
            // idle again (the rest of the tick still runs, with the counter at 0); an encased jet
            // waits in timeout for a neighbour change without power (EncasedFireJetBlock)
            setJetState(*region, pos, id, id == kFireJet ? "idle" : "timeout");
            log().info("hydra: jet at {} {} {} {}", pos.x, pos.y, pos.z, id == kFireJet ? "idle" : "timeout");
        }
        auto& dim = region->getDimension();
        if (jet.counter % 2 == 0) {
            auto at = [&](double dx, double dz) { return Vec3{static_cast<float>(x + dx), static_cast<float>(y + 1.0), static_cast<float>(z + dz)}; };
            // ParticleTypes.LARGE_SMOKE: F10's fx maps minecraft:large_smoke to Bedrock's
            // large_explosion (a pale explosion puff, routed to F10); the unit's stand-in effect
            // gf_hydra:large_smoke is Java's SmokeParticle at scale 2.5 (the huge smoke's maths)
            gf::fx::particle(dim, kLargeSmokeStandIn, at(0.5, 0.5));
            gf::fx::particle(dim, "twilightforest:large_flame", at(0.5, 0.5), Vec3{0.0F, 0.5F, 0.0F});
            gf::fx::particle(dim, "twilightforest:large_flame", at(-0.5, 0.5), Vec3{0.05F, 0.5F, 0.0F});
            gf::fx::particle(dim, "twilightforest:large_flame", at(0.5, -0.5), Vec3{0.0F, 0.5F, 0.05F});
            gf::fx::particle(dim, "twilightforest:large_flame", at(1.5, 0.5), Vec3{-0.05F, 0.5F, 0.0F});
            gf::fx::particle(dim, "twilightforest:large_flame", at(0.5, 1.5), Vec3{0.0F, 0.5F, -0.05F});
        }
        // sounds
        if (jet.counter % 4 == 0)
            gf::fx::sound(dim, "block.twilightforest.jet.active", Vec3{static_cast<float>(x + 0.5), static_cast<float>(y + 0.5), static_cast<float>(z + 0.5)}, 1.0F + r.nextFloat(),
                          r.nextFloat() * 0.7F + 0.3F);
        else if (jet.counter == 1)
            gf::fx::sound(dim, "block.twilightforest.jet.start", Vec3{static_cast<float>(x + 0.5), static_cast<float>(y + 0.5), static_cast<float>(z + 0.5)}, 1.0F + r.nextFloat(),
                          r.nextFloat() * 0.7F + 0.3F);
        // actual fire effects
        if (jet.counter % 5 == 0) jetHurtEntities(*region, pos);
        return !done;
    }
    jet.counter = 0;
    return false;
}

/// TFSmokerBlockEntity#tick: false when the block is no smoker any more.
bool tickSmoker(GlobalPos const& gp, Smoker& s) {
    auto* region = game::regionOf(gp.dim);
    if (!region) return false;
    BlockPos pos = game::blockPos(gp);
    if (!region->hasChunksAt(pos, 0, false)) return false;
    Block const& block = region->getBlock(pos);
    auto         id    = game::blockId(block);
    if (!isSmoker(id)) return false;
    if (++s.counter % 4 == 0) {
        bool on = id == kSmoker || stateValue(block, "twilightforest:active") == "true";
        if (on) {
            double c = static_cast<double>(s.counter) / 10.0;
            gf::fx::particle(region->getDimension(), "twilightforest:huge_smoke", Vec3{pos.x + 0.5F, pos.y + 0.95F, pos.z + 0.5F},
                             Vec3{static_cast<float>(std::cos(c) * 0.05), 0.25F, static_cast<float>(std::sin(c) * 0.05)});
            ++J().smokeParticles;
        }
    }
    return true;
}

/// Finds the jets and smokers the world generation placed (and the ones a reload left popping or
/// flaming) around the players in the Twilight Forest, once a second. Blocks placed in any
/// dimension are known from their placement (onPlaced), jets that start from a random tick or
/// redstone register themselves.
void scan() {
    static std::unordered_set<BlockType const*> types;
    if (types.empty())
        for (auto const* id : {kFireJet, kEncasedJet, kSmoker, kEncasedSmoker})
            if (auto const* b = game::blockWith(id)) types.insert(&b->getBlockType());
    if (types.empty()) return;
    auto lv = ll::service::getLevel();
    if (!lv) return;
    std::vector<GlobalPos> found;
    lv->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (!gf::world::isTwilight(p.getDimension())) return true;
        int   dim    = static_cast<int>(p.getDimensionId());
        auto* region = game::regionOf(dim);
        if (!region) return true;
        BlockPos c  = p.getFeetBlockPos();
        int      ys = std::max<int>(c.y - kScanHeight, region->getMinHeight());
        int      ye = std::min<int>(c.y + kScanHeight, region->getMaxHeight() - 1);
        if (ys > ye) return true;
        int r = kScanRadius;
        for (int cx = (c.x - r) >> 4; cx <= (c.x + r) >> 4; ++cx)
            for (int cz = (c.z - r) >> 4; cz <= (c.z + r) >> 4; ++cz) {
                if (!region->hasChunksAt(BlockPos{cx * 16, ys, cz * 16}, 0, false)) continue;
                BoundingBox box{BlockPos{std::max(cx * 16, c.x - r), ys, std::max(cz * 16, c.z - r)},
                                BlockPos{std::min(cx * 16 + 15, c.x + r), ye, std::min(cz * 16 + 15, c.z + r)}};
                auto hits = region->fetchBlocksInBox(box, [&](Block const& b) { return types.contains(&b.getBlockType()); });
                for (auto const& hit : hits) found.push_back(game::globalPos(dim, hit.mDataPos));
            }
        return true;
    }));
    for (auto const& gp : found) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        BlockPos     pos   = game::blockPos(gp);
        Block const& block = region->getBlock(pos);
        auto         id    = game::blockId(block);
        if (isJet(id)) {
            auto v = stateValue(block, "twilightforest:state");
            if (v == "popping" || v == "flame") track(gp);
        }
        if (id == kEncasedJet || id == kEncasedSmoker) watch(*region, pos);
        if (isSmoker(id)) J().smokers.try_emplace(gp);
    }
}

constexpr char kKnownKey[] = "gf:hydra:jets";

void loadKnown() {
    auto& s = J();
    if (s.knownLoaded || !game::db::available()) return;
    s.knownLoaded = true;
    if (auto j = game::db::get(kKnownKey); j && j->is_array())
        for (auto const& e : *j)
            if (e.is_array() && e.size() == 4) s.known.insert(GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
}

void saveKnown() {
    auto& s = J();
    if (!s.knownDirty || !game::db::available()) return;
    auto arr = nlohmann::json::array();
    for (auto const& gp : s.known) arr.push_back({gp.dim, gp.x, gp.y, gp.z});
    game::db::put(kKnownKey, arr);
    s.knownDirty = false;
}

/// A jet or smoker placed at `gp` (any dimension): remembered across reloads.
void remember(GlobalPos const& gp) {
    loadKnown();
    if (J().known.insert(gp).second) J().knownDirty = true;
}

void dropWatch(GlobalPos const& gp) {
    auto it = J().watches.find(gp);
    if (it == J().watches.end()) return;
    gf::redstone::unwatch(it->second);
    J().watches.erase(it);
}

bool loaded(BlockSource* region, BlockPos const& pos) { return region && region->hasChunksAt(pos, 0, false); }

/// The block entities of the remembered jets and smokers whose chunk is loaded come back (the
/// encased blocks' redstone watch, a smoker's ticking, a jet still popping or flaming); the ones
/// whose block is gone are forgotten. Watches whose chunk unloaded are dropped (they come back with
/// the chunk).
void revisit() {
    auto& s = J();
    loadKnown();
    for (auto it = s.known.begin(); it != s.known.end();) {
        auto*    region = game::regionOf(it->dim);
        BlockPos pos    = game::blockPos(*it);
        if (!loaded(region, pos)) {
            ++it;
            continue;
        }
        Block const& block = region->getBlock(pos);
        auto         id    = game::blockId(block);
        if (!isJet(id) && !isSmoker(id)) {
            dropWatch(*it);
            it           = s.known.erase(it);
            s.knownDirty = true;
            continue;
        }
        if (id == kEncasedJet || id == kEncasedSmoker) watch(*region, pos);
        if (isSmoker(id)) s.smokers.try_emplace(*it);
        if (isJet(id)) {
            auto v = stateValue(block, "twilightforest:state");
            if (v == "popping" || v == "flame") track(*it);
        }
        ++it;
    }
    std::vector<GlobalPos> gone;
    for (auto const& [gp, id] : s.watches)
        if (!loaded(game::regionOf(gp.dim), game::blockPos(gp))) gone.push_back(gp);
    for (auto const& gp : gone) dropWatch(gp);
    saveKnown();
}

void onLevelTick() {
    auto& s = J();
    ++s.tick;
    if (s.tick % kScanInterval == 0) {
        scan();
        revisit();
    }
    // a placement is written at the end of its tick (not only at the next autosave): the client
    // leaves a world without a final save of the mod's data (generated jets are not remembered,
    // so this stays rare)
    saveKnown();
    for (auto it = s.jets.begin(); it != s.jets.end();) it = tickJet(it->first, it->second) ? std::next(it) : s.jets.erase(it);
    // a smoker ticks while its chunk is loaded (it is forgotten with the chunk; the scan finds it again)
    for (auto it = s.smokers.begin(); it != s.smokers.end();) it = tickSmoker(it->first, it->second) ? std::next(it) : s.smokers.erase(it);
}

} // namespace

std::string jetState(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    if (!isJet(game::blockId(block))) return {};
    return stateValue(block, "twilightforest:state");
}

void randomTickJet(BlockSource& region, BlockPos const& pos) { jetRandomTick(region, pos, region.getBlock(pos), J().random); }

void neighborChanged(BlockSource& region, BlockPos const& pos) { encasedUpdate(region, pos); }

int jetCounter(GlobalPos const& pos) {
    auto it = J().jets.find(pos);
    return it == J().jets.end() ? -1 : it->second.counter;
}

int smokeParticlesForTest() { return J().smokeParticles; }

bool encasedWatched(GlobalPos const& pos) { return J().watches.contains(pos); }

bool smokerTracked(GlobalPos const& pos) { return J().smokers.contains(pos); }

void noteGeneratedJet(int x, int y, int z) {
    auto&            g = G();
    std::scoped_lock l(g.lock);
    g.at[{x, y, z}] = ++g.seq;
    if (g.at.size() > kGeneratedMax)
        for (auto it = g.at.begin(); it != g.at.end();) it = it->second + kGeneratedMax / 2 < g.seq ? g.at.erase(it) : std::next(it);
}

bool jetRemembered(GlobalPos const& pos) {
    loadKnown();
    return J().known.contains(pos);
}

void reloadJetsForTest() {
    auto& s = J();
    s.knownDirty = true;
    saveKnown();
    for (auto const& [gp, id] : s.watches) gf::redstone::unwatch(id);
    s.watches.clear();
    s.jets.clear();
    s.smokers.clear();
    s.known.clear();
    s.knownLoaded = false;
}

void installJets() {
    auto& s = J();
    if (!s.hooked) {
        // the block event registrations have no removal: once per process
        s.hooked = true;
        gf::blockevents::onRandomTick(kFireJet, jetRandomTick);
        for (auto const* id : {kEncasedJet, kEncasedSmoker}) {
            gf::blockevents::onNeighborChanged(id, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
                watch(region, pos);
                encasedUpdate(region, pos);
            });
            // Block#onPlace does nothing for the encased blocks: only the watch, the next change of the
            // power around them (neighborChanged) switches them
            gf::blockevents::onPlaced(id, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { watch(region, pos); });
        }
        // a jet placed already popping or flaming has a ticking block entity at once (any dimension)
        for (auto const* id : {kFireJet, kEncasedJet})
            gf::blockevents::onPlaced(id, [](BlockSource& region, BlockPos const& pos, Block const& block, Actor*) {
                auto v = stateValue(block, "twilightforest:state");
                if (v == "popping" || v == "flame") track(game::globalPos(region, pos));
            });
        for (auto const* id : {kSmoker, kEncasedSmoker})
            gf::blockevents::onPlaced(id, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { J().smokers.try_emplace(game::globalPos(region, pos)); });
        // the jets and smokers placed are remembered with the world: their "block entity" comes back
        // with its chunk however far it is from the players, whoever placed them (a player,
        // /setblock, /fill, a structure, another unit's code). Only the fire swamp's generated ones
        // (the twilightforest:fire_jet feature notes them) are not: block changes of any cause reach
        // onPlaced, so they would grow the list with exploration (the palette scan around the
        // players finds those)
        for (auto const* id : {kFireJet, kEncasedJet, kSmoker, kEncasedSmoker})
            gf::blockevents::onPlaced(id, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) {
                if (!takeGenerated(pos.x, pos.y, pos.z)) remember(game::globalPos(region, pos));
            });
        // a generated jet's note whose placement was never reported (or that a later write of the
        // feature covered) goes with the block: a jet placed there later is remembered
        for (auto const* id : {kFireJet, kEncasedJet, kSmoker, kEncasedSmoker})
            gf::blockevents::onRemoved(id, [](BlockSource&, BlockPos const& pos, Block const&) { (void)takeGenerated(pos.x, pos.y, pos.z); });
        gf::game::db::onWorldReady([] { loadKnown(); });
        gf::game::db::onWorldSave([] { saveKnown(); });
        gf::game::db::onWorldClosed([] {
            saveKnown();
            J().jets.clear();
            J().smokers.clear();
            J().watches.clear(); // dropped with the world (Redstone.h)
            J().known.clear();
            J().knownLoaded = false;
            J().knownDirty  = false;
        });
    }
    if (!s.tickId) s.tickId = fe::onLevelTick(onLevelTick);
}

void uninstallJets() {
    auto& s = J();
    if (s.tickId) fe::removeLevelTick(s.tickId);
    s.tickId = 0;
}

} // namespace gf::feature::hydra
