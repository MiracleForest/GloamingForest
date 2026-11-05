// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/TravellersGearEvents.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/twilightforest/network/GogglesZoomPacket.java
//   src/main/java/twilightforest/network/SwapHotbarPacket.java
//   src/main/java/twilightforest/network/CycleMapSlotPacket.java
//   src/main/java/twilightforest/block/RedThreadBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U11's server module: the per-tick modifiers (haste, stealth, auto repair, the item display's map,
// arrow magnetism), perfect dodge (combat), the never-break rule and the last armour damage time
// (ext/tgear armour hook), efficient eater (ext/tgear movement exhaustion), the companion channels
// tgear.goggles_zoom / tgear.swap_hotbar / tgear.cycle_map_slot, the vanilla fallbacks (sneak-use the
// goggles: item display form; sneak-use a belt / wings: hotbar swap), the add_modifier trigger of
// crafted gear and the red thread's multiface placement.
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/projectile/AbstractArrow.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/tgear/Logic.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/dimension/World.h"
#include "feature/ext/tgear/PlayerHooks.h"
#include "feature/fx/Fx.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/tgear/Gear.h"
#include "feature/tgear/Internal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::tgear {

namespace {

namespace items = game::items;
namespace ia    = interactions;
using Slot      = SharedTypes::Legacy::ArmorSlot;

constexpr unsigned kHasteEffect        = 3;  // minecraft:haste
constexpr unsigned kInvisibilityEffect = 14; // minecraft:invisibility

struct State {
    std::mutex                             mutex;
    std::mt19937_64                        rng{std::random_device{}()};
    std::unordered_map<std::int64_t, std::int64_t> lastArmorDamage; ///< player uid -> game time
    std::unordered_map<std::int64_t, bool>         zoom;            ///< player uid -> zooming
    struct Arrow {
        std::int64_t firstTick = 0;
        Vec3         last{};
        bool         moved = false;
    };
    std::unordered_map<std::int64_t, Arrow> arrows;
};

State& state() {
    static State s;
    return s;
}

float nextFloat() {
    std::lock_guard lock(state().mutex);
    return std::uniform_real_distribution<float>(0.f, 1.f)(state().rng);
}

double nextDouble() {
    std::lock_guard lock(state().mutex);
    return std::uniform_real_distribution<double>(0.0, 1.0)(state().rng);
}

std::int64_t uidOf(Actor const& a) { return a.getOrCreateUniqueID().rawID; }

std::int64_t gameTime() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::int64_t>(level->getCurrentTick().tickID) : 0;
}

/// Mob#getVoicePitch: (r1 - r2) * 0.2 + 1.
float voicePitch() { return (nextFloat() - nextFloat()) * 0.2f + 1.f; }

/// MobEffectInstance(effect, 2, amplifier, false, false, false): hidden.
void hiddenEffect(Player& player, unsigned id, int amplifier) {
    MobEffectInstance inst(id, EffectDuration{2});
    inst.mAmplifier                       = amplifier;
    inst.mAmbient                         = false;
    inst.mEffectVisible                   = false;
    inst.mDisplayOnScreenTextureAnimation = false;
    player.addEffect(inst);
}

/// Level#isDay for the auto repair boost: !fixed time and skyDarken < 4 (Java's formula over the
/// dimension's time of day; the weather terms are left out).
bool isDay(Dimension& dim) {
    float  t  = dim.getTimeOfDay(1.f);
    double d2 = 0.5 + 2.0 * std::clamp(std::cos(t * 6.283185307179586), -0.25, 0.25);
    int    darken = static_cast<int>((1.0 - d2) * 11.0);
    return darken < 4;
}

// ---- per-tick modifiers ------------------------------------------------------------------------------

void tickPlayer(Player& player, std::int64_t now) {
    if (!player.isAlive()) return;
    // TravellersGearLogic#travellersVestHaste
    if (isActive(player, ids::kHaste)) hiddenEffect(player, kHasteEffect, static_cast<int>(numberOf(ids::kHaste, 1)));
    // travellersStealth: crouching -> Invisibility (2 ticks, hidden); TF only clears the client-side
    // invisible flag when the effect runs out, which Bedrock does by itself
    if (isActive(player, ids::kStealth) && player.isSneaking()) hiddenEffect(player, kInvisibilityEffect, 0);
    autoRepair(player, now);
    tickDisplayMap(player);
}

/// TravellersGearEvents#magnetizeArrows for vanilla arrows: an arrow of a player wearing the modifier
/// that stops in a block within 200 ticks of its flight goes back to the inventory (pickup allowed,
/// survival) and is removed.
void tickArrows(Level& level, std::int64_t now) {
    bool anyone = false;
    level.forEachPlayer([&](Player& p) {
        anyone = anyone || (!p.isClientSide() && isActive(p, ids::kArrowMagnetism));
        return !anyone;
    });
    auto& s = state();
    if (!anyone) {
        std::lock_guard lock(s.mutex);
        s.arrows.clear();
        return;
    }
    std::unordered_map<std::int64_t, State::Arrow> next;
    std::vector<Actor*>                            stuck;
    for (auto* a : level.getRuntimeActorList()) {
        if (!a || a->getTypeName() != "minecraft:arrow" || !a->isAlive()) continue;
        auto            id = uidOf(*a);
        State::Arrow    rec;
        {
            std::lock_guard lock(s.mutex);
            auto            it = s.arrows.find(id);
            rec = it == s.arrows.end() ? State::Arrow{now, a->getPosition(), false} : it->second;
        }
        auto const& pos   = a->getPosition();
        bool        still = std::abs(pos.x - rec.last.x) < 1e-4f && std::abs(pos.y - rec.last.y) < 1e-4f && std::abs(pos.z - rec.last.z) < 1e-4f;
        if (!still) rec.moved = true;
        // a flight that ended in a block within 200 ticks (projectile.tickCount >= 200 returns)
        if (still && rec.moved && now - rec.firstTick < 200) stuck.push_back(a);
        rec.last = pos;
        next[id] = rec;
    }
    {
        std::lock_guard lock(s.mutex);
        s.arrows.swap(next);
    }
    for (auto* a : stuck) {
        auto* owner = a->getOwner();
        if (!owner || !owner->isPlayer()) continue;
        auto& player = static_cast<Player&>(static_cast<Actor&>(*owner));
        if (!isActive(player, ids::kArrowMagnetism)) continue;
        auto& arrow   = static_cast<AbstractArrow&>(*a);
        bool  allowed = !arrow.mIsCreative; // Bedrock's creative / infinity arrows are CREATIVE_ONLY
        if (allowed && !player.isCreative()) {
            ItemStack pickup = arrow._getPickupItem();
            if (!items::isEmpty(pickup) && !player.add(pickup)) player.drop(pickup, false);
            player.sendInventory(false);
        }
        if (allowed || player.isCreative()) {
            {
                std::lock_guard lock(state().mutex);
                state().arrows.erase(uidOf(*a));
            }
            a->remove();
        }
    }
}

void onLevelTick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto now = gameTime();
    level->forEachPlayer([&](Player& p) {
        if (!p.isClientSide()) tickPlayer(p, now);
        return true;
    });
    tickArrows(*level, now);
}

// ---- armour damage ------------------------------------------------------------------------------------

/// TravellersGearEvents#stopDamagingTravellersGear (a piece that would break keeps one point; reaching
/// the last point plays item.break to the wearer) and #setLastDamageArmorTime.
void onArmorHurt(Player& player, std::array<ItemStack, 4> const& before) {
    bool damaged = false;
    for (int i = 0; i < 4; ++i) {
        auto const& was = before[i];
        auto const& now = player.getArmor(static_cast<Slot>(i));
        bool        broke = !items::isEmpty(was) && (items::isEmpty(now) || items::idOf(now) != items::idOf(was));
        if (!broke && !items::isEmpty(now) && now.getDamageValue() > was.getDamageValue()) damaged = true;
        if (items::isEmpty(was) || !isGear(items::idOf(was)) || !was.isDamageableItem()) continue;
        int max = maxDamageOf(was);
        if (broke || now.getDamageValue() >= max) {
            damaged       = true;
            ItemStack kept = was;
            kept.setDamageValue(static_cast<short>(max - 1));
            player.setArmor(static_cast<Slot>(i), kept);
            if (was.getDamageValue() < max - 1) fx::soundTo(player, "minecraft:entity.item.break", 1.f, voicePitch());
            log().info("tgear: {}'s {} is broken", player.getRealName(), items::idOf(was));
        } else if (now.getDamageValue() >= max - 1 && was.getDamageValue() < max - 1) {
            fx::soundTo(player, "minecraft:entity.item.break", 1.f, voicePitch());
            log().info("tgear: {}'s {} is broken", player.getRealName(), items::idOf(was));
        }
    }
    if (damaged) setLastArmorDamageTime(player, gameTime());
}

// ---- perfect dodge -------------------------------------------------------------------------------------

combat::Verdict onIncoming(combat::DamageInfo& d) {
    if (!d.direct || d.direct == &d.victim || !d.direct->hasCategory(ActorCategory::Projectile)) return combat::Verdict::Pass;
    return rollPerfectDodge(d.victim, d.direct) ? combat::Verdict::Cancel : combat::Verdict::Pass;
}

// ---- red thread (RedThreadBlock: a MultifaceBlock) -------------------------------------------------------

constexpr char const* kFaceStates[6] = {"twilightforest:down", "twilightforest:up", "twilightforest:north", "twilightforest:south", "twilightforest:west", "twilightforest:east"};
constexpr int         kFaceOffsets[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};

int oppositeFace(int f) { return f ^ 1; }

/// MultifaceBlock#canAttachTo: a solid neighbour on that side (Bedrock: not air or a liquid or another
/// thread; Java asks for a sturdy face).
bool canAttach(BlockSource& region, BlockPos const& pos, int face) {
    BlockPos n{pos.x + kFaceOffsets[face][0], pos.y + kFaceOffsets[face][1], pos.z + kFaceOffsets[face][2]};
    auto     id = game::blockIdAt(region, n);
    return !id.empty() && id != "minecraft:air" && id != ids::kRedThread && id.find("water") == std::string::npos && id.find("lava") == std::string::npos;
}

nlohmann::json facesOf(Block const& block) {
    nlohmann::json j = nlohmann::json::object();
    for (int f = 0; f < 6; ++f) j[kFaceStates[f]] = game::intState(block, kFaceStates[f]).value_or(0) != 0;
    return j;
}

/// Context#getNearestLookingDirections: the six faces by how much the player looks along them.
std::vector<int> lookingFaces(Player const& player) {
    auto                              v = player.getViewVector(1.f);
    std::vector<std::pair<float, int>> order;
    float                             comps[6] = {-v.y, v.y, -v.z, v.z, -v.x, v.x};
    for (int f = 0; f < 6; ++f) order.push_back({-comps[f], f});
    std::sort(order.begin(), order.end());
    std::vector<int> out;
    for (auto const& [_, f] : order) out.push_back(f);
    return out;
}

} // namespace

// ---- Internal.h ----------------------------------------------------------------------------------------

std::int64_t lastArmorDamageTime(Player const& player) {
    std::lock_guard lock(state().mutex);
    auto            it = state().lastArmorDamage.find(uidOf(player));
    return it == state().lastArmorDamage.end() ? std::numeric_limits<std::int64_t>::min() / 2 : it->second;
}

void setLastArmorDamageTime(Player const& player, std::int64_t time) {
    std::lock_guard lock(state().mutex);
    state().lastArmorDamage[uidOf(player)] = time;
}

void seedRandom(std::uint64_t seed) {
    std::lock_guard lock(state().mutex);
    state().rng.seed(seed);
}

int autoRepair(Player& player, std::int64_t now) {
    // TravellersGearLogic#travellersGearAutoRepair: nothing within 10 s of armour damage
    if (now - lastArmorDamageTime(player) <= core::tgear::kAutoRepairQuietTicks) return 0;
    int repaired = 0;
    for (int i = 0; i < 4; ++i) {
        ItemStack stack = player.getArmor(static_cast<Slot>(i));
        if (items::isEmpty(stack) || !hasModifier(stack, ids::kAutoRepair) || !isActiveOn(player, stack, ids::kAutoRepair)) continue;
        if (!stack.isDamageableItem() || stack.getDamageValue() <= 0) {
            (void)nextFloat(); // the roll happens either way in TF
            continue;
        }
        auto&    dim  = player.getDimension();
        BlockPos pos  = game::blockPosOf(player.getPosition());
        bool     sky  = player.getDimensionBlockSource().canSeeSky(pos);
        double   base = numberOf(ids::kAutoRepair, 0.001);
        double   p    = core::tgear::autoRepairChance(base, sky, world::isTwilight(dim), isDay(dim));
        if (p > nextFloat()) {
            stack.setDamageValue(static_cast<short>(std::max(stack.getDamageValue() - 1, 0)));
            player.setArmor(static_cast<Slot>(i), stack);
            ++repaired;
        }
    }
    return repaired;
}

bool rollPerfectDodge(Actor& victim, Actor* projectile) {
    if (!victim.isPlayer()) return false;
    auto& player = static_cast<Player&>(victim);
    if (!isActive(player, ids::kPerfectDodge)) return false;
    float probability = static_cast<float>(numberOf(ids::kPerfectDodge, 0.3));
    if (probability <= nextFloat()) return false;
    Vec3 hit = projectile ? projectile->getPosition() + projectile->getVelocity() : victim.getPosition();
    fx::sound(victim.getDimension(), "item.twilightforest.travellers_gear.perfect_dodge", hit, 1.5f, voicePitch());
    for (int n = 0; n < 20; ++n) {
        Vec3 v{static_cast<float>(nextDouble() - 0.5), static_cast<float>(nextDouble() - 0.5), static_cast<float>(nextDouble() - 0.5)};
        fx::particle(victim.getDimension(), "twilightforest:perfect_dodge", hit, v, 1);
    }
    log().info("tgear: {} dodged a projectile", player.getRealName());
    return true;
}

bool usingZoom(Player const& player) {
    std::lock_guard lock(state().mutex);
    auto            it = state().zoom.find(uidOf(player));
    return it != state().zoom.end() && it->second;
}

void setZoom(Player& player, bool zoom) {
    // GogglesZoomPacket#handle on the server: only with an active zoom ability
    if (!isActive(player, ids::kZoom)) return;
    {
        std::lock_guard lock(state().mutex);
        state().zoom[uidOf(player)] = zoom;
    }
    fx::sound(player.getDimension(), zoom ? "item.twilightforest.travellers_goggles.zoom_in" : "item.twilightforest.travellers_goggles.zoom_out", player.getPosition(), 1.f, 1.f);
    // the trackers (texture flip of the goggles)
    auto level = ll::service::getLevel();
    if (!level) return;
    auto uid = uidOf(player);
    level->forEachPlayer([&](Player& other) {
        if (&other != &player && !other.isClientSide()) net::send(other, "tgear.goggles_zoom", {{"player", uid}, {"zoom", zoom}});
        return true;
    });
}

namespace {

class TgearModule {
public:
    void install() {
        ext::tgear::installHooks();
        static bool once = false;
        if (!once) {
            once = true;
            ext::tgear::onArmorHurt(&onArmorHurt);
            ext::tgear::setMovementExhaustionDivisor([](Player& p) {
                // PlayerHooks: movement exhaustion divided by the efficient eater component (2)
                return isActive(p, ids::kEfficientEater) ? static_cast<float>(numberOf(ids::kEfficientEater, 2.0)) : 1.f;
            });
            combat::onIncoming(combat::priority::High, &onIncoming);
            progression::events::onInventoryChanged([](Player& p) { fireCraftedTriggers(p); });
            game::db::onWorldReady([] { registerRecipes(); });
            net::on("tgear.swap_hotbar", [](Player& p, Json const&) { trySwapHotbar(p); });
            net::on("tgear.cycle_map_slot", [](Player& p, Json const&) { cycleMapSlot(p); });
            net::on("tgear.goggles_zoom", [](Player& p, Json const& msg) { setZoom(p, msg.value("zoom", false)); });
            registerInteractions();
        }
        registerRecipes();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onLevelTick(); });
        mLeave = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            std::lock_guard lock(state().mutex);
            state().zoom.erase(uidOf(ev.self()));
            state().lastArmorDamage.erase(uidOf(ev.self()));
        });
    }
    void uninstall() {
        ext::tgear::uninstallHooks();
        ll::event::EventBus::getInstance().removeListener(mTick);
        ll::event::EventBus::getInstance().removeListener(mLeave);
    }

private:
    static void registerInteractions() {
        // vanilla fallbacks (docs/units/tgear.md): sneak-use the goggles -> item display screen
        ia::onUse({.item = ids::kGoggles}, [](ia::UseContext const& c) {
            if (!c.sneaking || !displayOf(c.item)) return ia::Result::Pass;
            openDisplayForm(c.player);
            return ia::Result::Consume;
        });
        // sneak-use a belt / wings -> swap the hotbar with the worn legs piece's storage
        for (auto const* id : {ids::kBelt, ids::kWings}) {
            ia::onUse({.item = id}, [](ia::UseContext const& c) {
                if (!c.sneaking || !hasSwapHotbar(c.player.getArmor(Slot::Legs))) return ia::Result::Pass;
                trySwapHotbar(c.player);
                return ia::Result::Consume;
            });
        }
        // red thread: the clicked face of the placement; using thread on a thread adds a face
        ia::onPlace({.block = ids::kRedThread}, [](ia::PlaceContext const& c) {
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return;
            auto     pos  = game::blockPos(c.pos);
            int      face = oppositeFace(c.face);
            auto     j    = facesOf(c.block);
            j[kFaceStates[face]] = true;
            if (auto const* b = game::blockWithStates(ids::kRedThread, j)) game::setBlock(*region, pos, *b);
        });
        ia::onUseOn({.block = ids::kRedThread, .item = ids::kRedThread}, [](ia::UseOnContext const& c) {
            if (!c.firstEvent) return ia::Result::Consume;
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return ia::Result::Pass;
            auto pos = game::blockPos(c.pos);
            auto j   = facesOf(c.block);
            for (int f : lookingFaces(c.player)) {
                if (j[kFaceStates[f]].get<bool>() || !canAttach(*region, pos, f)) continue;
                j[kFaceStates[f]] = true;
                if (auto const* b = game::blockWithStates(ids::kRedThread, j)) {
                    game::setBlock(*region, pos, *b);
                    if (!c.player.isCreative()) {
                        auto& hand = ia::mutableItem(c.item);
                        c.player.setSelectedItem(items::withCount(hand, items::countOf(hand) - 1));
                    }
                }
                return ia::Result::Consume;
            }
            return ia::Result::Pass;
        });
    }

    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mLeave;
};

} // namespace

} // namespace gf::feature::tgear

GF_REGISTER_MODULE(gf::feature::tgear::TgearModule);
