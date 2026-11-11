// Selftests of F10 (prefix "combat.", testPos dx 6000..6099): the hurt hook and damage types, death
// messages, the death pipeline (lethal, keepers, drops, respawn), the loot interpreter with TF's
// functions, containers (loot placement, the BlockData backend and its persistence over a world
// reload), the progression store / trigger bus / events, gamerules and fx.
//
// Handlers registered by a test cannot be removed (the contracts have no removal), so every test
// handler only acts while its test runs (a static "active" id) and is registered once.

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <random>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/comprehensive/ParticleType.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#ifndef GF_CLIENT
#include "mc/world/item/components/StorageItemComponentTags.h"
#endif
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainerBase.h"

#include "feature/combat/Combat.h"
#include "feature/combat/CombatInternal.h"
#include "feature/combat/CombatTestUtil.h"
#include "feature/common/Redstone.h"
#include "feature/death/Death.h"
#include "feature/death/DeathInternal.h"
#include "feature/ext/combat/InventoryViewers.h"
#include "feature/fx/Fx.h"
#include "feature/fx/FxInternal.h"
#include "feature/gamerules/GameRules.h"
#include "feature/gamerules/GameRulesInternal.h"
#include "feature/inventory/Inventory.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/loot/JavaRandom.h"
#include "feature/loot/Loot.h"
#include "feature/loot/LootInternal.h"
#include "feature/loot/LootRegistry.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Compat.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using JsonT = nlohmann::json;
using gf::core::GlobalPos;
namespace db    = gf::game::db;
namespace items = gf::game::items;

/// The actor a test's handlers act on (0: none, the handlers pass).
std::int64_t gActive = 0;

struct ActiveScope {
    explicit ActiveScope(Actor& a) { gActive = a.getOrCreateUniqueID().rawID; }
    ~ActiveScope() { gActive = 0; }
};

bool isActive(Actor const& a) { return gActive != 0 && const_cast<Actor&>(a).getOrCreateUniqueID().rawID == gActive; }

float healthOf(Player& p) { return static_cast<float>(p.getHealth()); }

using gf::combat::testutil::clearInventory;
using gf::combat::testutil::countIn;
using gf::combat::testutil::countInInventory;
using gf::combat::testutil::takeItemEntitiesNear;

/// Item entities within `radius` blocks of `pos` (same dimension).
int itemEntitiesNear(GlobalPos const& pos, float radius) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    int n = 0;
    for (auto* a : level->getRuntimeActorList()) {
        if (!a || !a->isType(ActorType::ItemEntity) || static_cast<int>(a->getDimensionId()) != pos.dim) continue;
        auto const& at = a->getPosition();
        float       dx = at.x - (pos.x + .5f), dy = at.y - (pos.y + .5f), dz = at.z - (pos.z + .5f);
        if (dx * dx + dy * dy + dz * dz <= radius * radius) ++n;
    }
    return n;
}

} // namespace

// ---- gamerules -----------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.gamerules", [](Context& ctx) {
    namespace gr = gf::gamerules;
    ctx.check(gr::getBool("tfEnforcedProgression") == gr::getBool("tfEnforcedProgression"), "tfEnforcedProgression is known");
    ctx.check(std::holds_alternative<int>(gr::get("playersTfPortalDefaultDelay")), "playersTfPortalDefaultDelay is an int rule");
    gr::registerBool("gfTestBoolRule", true);
    gr::registerInt("gfTestIntRule", 7);
    ctx.check(gr::getBool("gfTestBoolRule") && gr::getInt("gfTestIntRule") == 7, "defaults apply until set");
    ctx.check(gr::set("gfTestIntRule", 12) && gr::getInt("gfTestIntRule") == 12, "an int rule is set");
    ctx.check(!gr::set("gfTestIntRule", true), "a value of the other type is refused");
    ctx.check(!gr::set("noSuchRule", true) && !gr::getBool("noSuchRule"), "unknown rules are refused / false");
    ctx.check(gr::set("gfTestBoolRule", false) && !gr::getBool("gfTestBoolRule"), "a bool rule is set");
    auto stored = db::get("gf:gamerules:values");
    ctx.check(stored && stored->value("gfTestIntRule", 0) == 12, "values are stored in the world DB");
    ctx.check(gr::parse("gfTestIntRule", "34").has_value() && !gr::parse("gfTestIntRule", "3x").has_value(), "command values parse");
    ctx.check(gr::snapshot().contains("tfEnforcedProgression"), "the snapshot lists TF's rules");
    // the test rules leave no trace (registry and stored values)
    gr::unregisterRule("gfTestIntRule");
    gr::unregisterRule("gfTestBoolRule");
    ctx.check(!gr::snapshot().contains("gfTestIntRule") && !db::get("gf:gamerules:values")->contains("gfTestIntRule"), "the test rules are gone again");
});

// ---- fx ------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.fx", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace fx = gf::fx;
    ctx.check(fx::resolveParticle("minecraft:flame").effect == "minecraft:basic_flame_particle", "vanilla particle types map to Bedrock effects");
    ctx.check(fx::resolveParticle("minecraft:no_such_particle").effect.empty(), "unknown particles are silent");
    ctx.check(fx::resolveParticle("twilightforest:firefly").native, "TF particles go to companions natively");
    ctx.check(fx::resolveSound("minecraft:entity.ghast.shoot") == "mob.ghast.fireball", "vanilla sound events map to Bedrock names");
    ctx.check(fx::resolveSound("entity.twilightforest.no_such_sound").empty(), "unknown TF sound events are silent");
    auto pos = ctx.testPos(6000);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto  before = fx::counters();
    auto& dim    = p.getDimension();
    fx::particle(dim, "minecraft:flame", p.getPosition(), {}, 2);
    fx::sound(dim, "minecraft:entity.ghast.shoot", p.getPosition(), 1.f, 1.f);
    fx::soundTo(p, "minecraft:entity.generic.explode", 1.f, 1.f);
    auto after = fx::counters();
    ctx.check(after.particlePackets >= before.particlePackets + 2, "particles were sent to the player nearby");
    ctx.check(after.soundPackets >= before.soundPackets + 2, "sounds were sent (in range and to one player)");
    ctx.check(after.movingPackets == before.movingPackets, "a particle without velocity carries no molang variables");
    // a moving particle: its velocity goes to vanilla clients as `variable.velocity`
    fx::particle(dim, "minecraft:flame", p.getPosition(), Vec3{0, 0.05f, 0}, 3, JsonT{{"spread", {0.2, 0.2, 0.2}}});
    auto moving = fx::counters();
    ctx.check(moving.particlePackets >= after.particlePackets + 3 && moving.movingPackets >= after.movingPackets + 3,
              "each particle packet of a moving particle carries variable.velocity (" + std::to_string(moving.movingPackets - after.movingPackets) + ")");
    std::uint64_t near = 0; // the players in the 32 blocks fx reaches (a client e2e run has its real player too)
    ll::service::getLevel()->forEachPlayer([&](Player& q) {
        auto d = q.getPosition() - p.getPosition();
        if (static_cast<int>(q.getDimensionId()) == static_cast<int>(p.getDimensionId()) && d.x * d.x + d.y * d.y + d.z * d.z <= 32.f * 32.f) ++near;
        return true;
    });
    fx::particle(dim, "minecraft:flame", p.getPosition(), {}, 100);
    auto sent = fx::counters().particlePackets - moving.particlePackets;
    ctx.check(sent >= near && sent <= near * static_cast<std::uint64_t>(fx::kMaxVanillaParticles),
              "a player gets at most kMaxVanillaParticles packets per call (" + std::to_string(sent) + " for " + std::to_string(near) + " player(s))");
    // the spread helper both sides use (ServerLevel#sendParticles' gaussian offset)
    ctx.check(fx::spreadOf(JsonT{{"spread", {1, 2, 3}}}).has_value() && !fx::spreadOf(JsonT{{"spread", {1, "x", 3}}}) && !fx::spreadOf(JsonT{{"spread", {1, 2}}})
                  && !fx::spreadOf(JsonT::array()),
              "params spread: three numbers, anything else none");
    std::mt19937 rng{7};
    auto         off = fx::spreadAt(Vec3{10, 20, 30}, Vec3{0, 0, 0}, rng);
    ctx.check(off.x == 10 && off.y == 20 && off.z == 30, "no spread: the position itself");
    // a real molang variable map (constructed in place, the game's destructor)
    {
        fx::MolangMap vars;
        vars.setVelocity(Vec3{0.1f, 0.2f, 0.3f});
        ctx.check(!vars.get().mVariables->empty(), "the molang map holds variable.velocity");
    }
    // companions get TF particles over fx.particle
    gf::net::setTestCompanion(p, true);
    (void)gf::net::takeTestMessages(p);
    fx::particle(dim, "twilightforest:firefly", p.getPosition(), Vec3{0, 0.1f, 0}, 1, JsonT{{"colour", 1}});
    auto msgs = gf::net::takeTestMessages(p);
    gf::net::setTestCompanion(p, false);
    bool got = std::any_of(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == "fx.particle" && m.payload.value("id", std::string{}) == "twilightforest:firefly"; });
    ctx.check(got, "a companion got fx.particle for a TF particle");
});

// The molang variables of the vanilla effects gf::fx maps Java's particle types to: every effect
// that reads one gets it (a missing one is a content-log error per particle on the client), the
// portal follows Java's PortalParticle path, item and block particles go as legacy particles.
GF_REGISTER_SELFTEST("combat.fx_effect_variables", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace fx = gf::fx;
    struct Row {
        char const*   id;
        char const*   effect;
        std::uint32_t vars;
    };
    static constexpr Row rows[] = {
        {"minecraft:portal", "gf:portal", fx::kVarDirection},
        {"minecraft:dust", "gf:dust", fx::kVarColorRgb | fx::kVarDirection | fx::kVarDustDirection | fx::kVarDustLook},
        {"minecraft:enchant", "minecraft:enchanting_table_particle", fx::kVarDirection},
        {"minecraft:splash", "minecraft:water_splash_particle", fx::kVarDirection | fx::kVarSplashDirection},
        {"minecraft:crit", "minecraft:basic_crit_particle", fx::kVarDirection | fx::kVarCritDirection},
        {"minecraft:electric_spark", "minecraft:electric_spark_particle", fx::kVarDirection},
        {"minecraft:poof", "minecraft:explosion_particle", fx::kVarVelocity | fx::kVarExplosionVelocity},
        {"minecraft:note", "minecraft:note_particle", fx::kVarNoteColor},
        {"minecraft:entity_effect", "minecraft:mobspell_emitter", fx::kVarColorRgba},
        {"minecraft:effect", "minecraft:mobspell_emitter", fx::kVarColorRgba},
        {"minecraft:falling_dust", "minecraft:falling_dust", fx::kVarColorRgb},
        {"minecraft:crimson_spore", "minecraft:falling_dust", fx::kVarColorRgb},
        {"minecraft:white_ash", "minecraft:falling_dust", fx::kVarColorRgb},
        {"minecraft:flame", "minecraft:basic_flame_particle", 0},
    };
    for (auto const& row : rows) {
        auto r = fx::resolveParticle(row.id);
        ctx.check(r.effect == row.effect && r.vars == row.vars && r.legacy == 0,
                  std::string(row.id) + " -> " + row.effect + " with its variables (got '" + r.effect + "', vars " + std::to_string(r.vars) + ")");
        // the map the effect gets for a still particle: exactly the variables it reads
        fx::MolangMap map;
        fx::fillEffectVariables(map, r.vars, Vec3{}, Vec3{0.1f, 0.2f, 0.3f}, fx::Rgba{});
        std::size_t want = 0;
        for (auto bit : {fx::kVarVelocity, fx::kVarDirection, fx::kVarNoteColor}) want += (r.vars & bit) ? 1 : 0;
        want += (r.vars & (fx::kVarColorRgb | fx::kVarColorRgba)) ? 1 : 0;
        want += (r.vars & fx::kVarDustLook) ? 2 : 0; // variable.dust_size and variable.dust_ticks
        ctx.check(map.size() == want, std::string(row.id) + ": " + std::to_string(want) + " variable(s) set (got " + std::to_string(map.size()) + ")");
    }
    // portal and dust: F10's own effects (assets/pack/rp/particles/fx), shipped in the mod's pack
    auto portal = fx::resolveParticle("minecraft:portal");
    ctx.check(portal.offset.x == 0.f && portal.offset.y == 0.f && portal.offset.z == 0.f, "gf:portal follows Java's PortalParticle path from the position itself (no offset)");
    for (auto const* effect : {"gf:portal", "gf:dust"}) ctx.check(fx::packHasEffect(effect), std::string(effect) + " is in the mod's resource pack");
    for (auto const* id : {"minecraft:item", "minecraft:block", "minecraft:item_slime", "minecraft:item_snowball"}) {
        auto r = fx::resolveParticle(id);
        ctx.check(r.legacy != 0 && r.effect.empty() && r.shown(), std::string(id) + " is a legacy particle");
    }
    auto near = [](float a, float b) { return std::abs(a - b) < 1e-5f; };
    // poof: explosion_particle starts at -variable.velocity * 10 blocks/s; Java's ExplodeParticle
    // moves velocity blocks/tick (20 * velocity blocks/s), same direction
    {
        auto poof = fx::resolveParticle("minecraft:poof");
        Vec3 v{0.1f, 0.2f, -0.3f};
        auto ev = fx::effectVelocity(poof.vars, v);
        ctx.check(near(ev.x, -0.2f) && near(ev.y, -0.4f) && near(ev.z, 0.6f), "poof: variable.velocity is -2 * Java's velocity");
        ctx.check(near(-ev.x * 10.f, 20.f * v.x) && near(-ev.y * 10.f, 20.f * v.y) && near(-ev.z * 10.f, 20.f * v.z),
                  "poof: explosion_particle's speed is Java's 20 * velocity blocks/s in Java's direction");
        auto other = fx::effectVelocity(fx::resolveParticle("minecraft:flame").vars, v);
        ctx.check(other.x == v.x && other.y == v.y && other.z == v.z, "other effects get Java's velocity as is");
    }
    std::mt19937 rng{6001};
    // splash: SplashParticle keeps (xSpeed, 0.1, zSpeed) only for ySpeed 0 with an x/z speed
    {
        auto flat = fx::splashDirection(rng, Vec3{0.3f, 0.f, -0.2f});
        ctx.check(flat.x == 0.3f && flat.y == 0.1f && flat.z == -0.2f, "splash: ySpeed 0 with an x/z speed -> (xSpeed, 0.1, zSpeed)");
        bool        random = true;
        std::set<float> xs;
        for (Vec3 v : {Vec3{-0.4f, 0.5f, -0.4f}, Vec3{}, Vec3{0.f, 0.2f, 0.f}}) {
            for (int i = 0; i < 50; ++i) {
                auto d = fx::splashDirection(rng, v);
                random = random && std::abs(d.x) <= 0.0541f && std::abs(d.z) <= 0.0541f && d.y >= 0.1f && d.y <= 0.3f;
                xs.insert(d.x);
            }
        }
        ctx.check(random && xs.size() > 100, "splash: any other speed is dropped for WaterDropParticle's random motion");
    }
    // crit: CritParticle = 0.1 * Particle's random motion + 0.4 * speed; a still crit still scatters
    {
        std::set<float> xs;
        bool            ok = true;
        for (int i = 0; i < 50; ++i) {
            auto d = fx::critDirection(rng, Vec3{});
            ok     = ok && std::abs(d.x) <= 0.0181f && std::abs(d.z) <= 0.0181f && d.y >= -0.0081f && d.y <= 0.0281f && (d.x != 0.f || d.y != 0.f || d.z != 0.f);
            xs.insert(d.x);
        }
        ctx.check(ok && xs.size() > 40, "crit: a still crit gets Particle's random motion * 0.1");
        auto moving = fx::critDirection(rng, Vec3{1.f, 0.f, -0.5f});
        ctx.check(std::abs(moving.x - 0.4f) <= 0.0181f && std::abs(moving.z + 0.2f) <= 0.0181f && moving.y >= -0.0081f && moving.y <= 0.0281f,
                  "crit: a moving crit gets 0.4 * its speed plus the scatter");
    }
    // dust: every DustParticleOptions colour (REDSTONE's red, TrollsteinnBlock's purple) is gf:dust
    // with variable.color, DustParticleBase's motion, size and lifetime
    {
        auto dust = fx::resolveParticle("minecraft:dust");
        ctx.check(dust.effect == "gf:dust" && dust.legacy == 0 && dust.color && dust.color->r == 1.f && dust.color->g == 0.f && dust.color->b == 0.f,
                  "dust: gf:dust, red (DustParticleOptions.REDSTONE) without params");
        auto purple = fx::colorOf(JsonT{{"color", {0.25, 0.0, 0.5}}, {"scale", 1.0}}, dust.color);
        ctx.check(purple.r == 0.25f && purple.g == 0.f && purple.b == 0.5f, "dust: params color (0.25, 0, 0.5) is variable.color");
        ctx.check(fx::dustScale(JsonT::object()) == 1.f && fx::dustScale(JsonT{{"scale", 2.5}}) == 2.5f && fx::dustScale(JsonT{{"scale", 9.0}}) == 4.f &&
                      fx::dustScale(JsonT{{"scale", 0.0}}) == 0.01f,
                  "dust: the scale is params scale clamped to 0.01..4 (1 without)");
        // DustParticleBase: Particle's motion * 0.1: |x|, |z| <= 0.018, y 0.01 - 0.018 .. 0.01 + 0.018,
        // 0.01 blocks/tick up on average
        bool            ok = true;
        float           sumY = 0.f;
        std::set<float> xs;
        for (int i = 0; i < 200; ++i) {
            auto d = fx::dustDirection(rng, Vec3{});
            ok     = ok && std::abs(d.x) <= 0.0181f && std::abs(d.z) <= 0.0181f && d.y >= -0.0081f && d.y <= 0.0281f;
            sumY += d.y;
            xs.insert(d.x);
        }
        ctx.check(ok && sumY / 200.f > 0.007f && sumY / 200.f < 0.013f && xs.size() > 190, "dust: a still dust particle drifts up slowly (Particle's motion * 0.1)");
        // DustParticleBase's size 0.1..0.2 * 0.75 * scale and lifetime max((int)(8 / (0.2..1)) * scale, 1)
        bool  looks = true;
        float minTicks = 1e9f, maxTicks = 0.f;
        for (int i = 0; i < 200; ++i) {
            auto one = fx::dustLook(rng, 1.f);
            auto two = fx::dustLook(rng, 2.f);
            auto dot = fx::dustLook(rng, 0.01f);
            looks    = looks && one.size >= 0.075f - 1e-6f && one.size <= 0.15f + 1e-6f && two.size >= 0.15f - 1e-6f && two.size <= 0.3f + 1e-6f;
            looks    = looks && one.ticks >= 8.f && one.ticks <= 40.f && one.ticks == std::floor(one.ticks) && two.ticks >= 16.f && two.ticks <= 80.f && dot.ticks == 1.f;
            minTicks = std::min(minTicks, one.ticks);
            maxTicks = std::max(maxTicks, one.ticks);
        }
        ctx.check(looks && minTicks <= 10.f && maxTicks >= 20.f, "dust: size and lifetime follow DustParticleBase (scale 1: 8..40 ticks, 0.01: 1 tick)");
        fx::MolangMap map;
        fx::fillEffectVariables(map, dust.vars, Vec3{}, Vec3{0.f, 0.01f, 0.f}, *dust.color, fx::dustLook(rng, 2.f));
        ctx.check(map.size() == 4, "dust: variable.direction, variable.color, variable.dust_size and variable.dust_ticks (got " + std::to_string(map.size()) + ")");
    }
    for (auto const& id : fx::vanillaParticleIds()) ctx.check(fx::resolveParticle(id).shown(), id + " is shown");
    // NoteParticle's colour and the params colour
    auto note = fx::noteColor(0.f);
    ctx.check(std::abs(note.r - 0.35f) < 1e-4f && std::abs(note.g - 0.9129f) < 1e-3f && note.b == 0.f, "note 0 is NoteParticle's green");
    auto orange = fx::colorOf(JsonT{{"color", 0xFF8000}}, std::nullopt);
    ctx.check(orange.r == 1.f && std::abs(orange.g - 128 / 255.f) < 1e-4f && orange.b == 0.f && orange.a == 1.f, "params color 0xRRGGBB is opaque");
    // ColorParticleOption (entity_effect) colours are ARGB ints: the alpha byte is kept
    auto argb = fx::colorOf(JsonT{{"color", std::int64_t{0x80FF8000}}}, std::nullopt);
    ctx.check(argb.r == 1.f && std::abs(argb.g - 128 / 255.f) < 1e-4f && argb.b == 0.f && std::abs(argb.a - 128 / 255.f) < 1e-4f,
              "params color 0xAARRGGBB keeps alpha 0x80");
    auto opaqueArgb = fx::colorOf(JsonT{{"color", std::int64_t{0xFF7F33CC}}}, std::nullopt);
    ctx.check(opaqueArgb.a == 1.f && std::abs(opaqueArgb.r - 0x7F / 255.f) < 1e-4f, "params color 0xFF7F33CC is opaque");
    auto listed = fx::colorOf(JsonT{{"color", {0.1, 0.2, 0.3, 0.4}}}, std::nullopt);
    ctx.check(listed.r == 0.1f && listed.a == 0.4f && fx::colorOf(JsonT::object(), fx::Rgba{0.5f, 0.5f, 0.5f, 1.f}).r == 0.5f, "params color [r, g, b, a] and the fallback");
    // FallingDustParticle's block colour: dust colour, BlockColor without a level, map colour
    {
        auto none   = [](std::string_view) -> std::optional<int> { return std::nullopt; };
        auto colour = [&](std::string const& id) { return fx::fallingDustColor(id, none); };
        ctx.check(!colour("minecraft:snow") && !colour("minecraft:stone"), "snow and stone have no BlockColor (the map colour)");
        ctx.check(colour("minecraft:sand") == 0xDBD3A0 && colour("minecraft:red_sand") == 0xA95821 && colour("minecraft:gravel") == 0x807C7B, "sand, red sand and gravel use their dust colours");
        ctx.check(colour("minecraft:oak_leaves") == 0x48B518 && colour("minecraft:spruce_leaves") == 0x619961 && colour("minecraft:birch_leaves") == 0x80A755 &&
                      colour("minecraft:mangrove_leaves") == 0x92C648,
                  "vanilla leaves use FoliageColor's colours without a level");
        ctx.check(colour("minecraft:water") == -1 && fx::dustRgb(-1).r == 1.f && fx::dustRgb(-1).b == 1.f, "water's BlockColor needs a level: white");
        ctx.check(colour("twilightforest:canopy_leaves") == 0x619961 && colour("twilightforest:mangrove_leaves") == 0x80A755 && colour("twilightforest:twilight_oak_leaves") == 0x48B518 &&
                      colour("twilightforest:time_leaves") == 0x6A9C17 && colour("twilightforest:violet_force_field") == 0x5C1074,
                  "TF's ColorHandler colours without a level");
        ctx.check(colour("twilightforest:aurora_block") == 0x00FFB2, "the aurora block is hsvToRGB(0.45, 1, 1) (float: t = 0.6999999, 178)");
        auto pale = colour("twilightforest:aurora_pillar");
        ctx.check(pale && *pale != 0x00FFB2 && ((*pale >> 8) & 0xFF) == 0xE6, "the aurora pillar is the aurora colour paled (value 0.9)");
        auto signal = [](int power) { return [power](std::string_view s) -> std::optional<int> { return s == "redstone_signal" ? std::optional<int>(power) : std::nullopt; }; };
        auto lit = fx::fallingDustColor("minecraft:redstone_wire", signal(15)).value_or(0);
        // power 15: (255, floor(0.19999999f * 255) = 50, 0); power 0: (floor(0.3f * 255) = 76, 0, 0)
        ctx.check(lit == (255 << 16 | 50 << 8) && fx::fallingDustColor("minecraft:redstone_wire", signal(0)) == 0x4C0000,
                  "redstone wire: RedStoneWireBlock#getColorForPower");
        auto stem = [](int growth, int facing) {
            return [=](std::string_view s) -> std::optional<int> { return s == "growth" ? std::optional<int>(growth) : s == "facing_direction" ? std::optional<int>(facing) : std::nullopt; };
        };
        ctx.check(fx::fallingDustColor("minecraft:pumpkin_stem", stem(7, 0)) == (224 << 16 | 199 << 8 | 28) && fx::fallingDustColor("minecraft:melon_stem", stem(0, 0)) == 0x00FF00 &&
                      fx::fallingDustColor("minecraft:melon_stem", stem(7, 3)) == 14731036,
                  "stems: StemBlock's age colour, the attached stem's colour");
        // the cauldron: Java's water cauldron (white without a level), the empty / lava / powder snow
        // cauldrons have no BlockColor (the map colour)
        auto cauldron = [&](char const* liquid, int fill) {
            return fx::fallingDustColor(
                "minecraft:cauldron", [fill](std::string_view s) -> std::optional<int> { return s == "fill_level" ? std::optional<int>(fill) : std::nullopt; },
                [liquid](std::string_view s) -> std::optional<std::string> { return s == "cauldron_liquid" ? std::optional<std::string>(liquid) : std::nullopt; });
        };
        ctx.check(cauldron("water", 6) == -1 && cauldron("water", 1) == -1, "the water cauldron's BlockColor without a level: white");
        ctx.check(!cauldron("water", 0) && !cauldron("lava", 6) && !cauldron("powder_snow", 3), "the empty, lava and powder snow cauldrons: the map colour");
        ctx.check(colour("minecraft:anvil") == 0 && colour("minecraft:damaged_anvil") == 0 && colour("minecraft:dragon_egg") == 0,
                  "anvils and the dragon egg: FallingBlock#getDustColor (black)");
        // the same through the game's block states
        auto const* water = gf::game::blockWithStates("minecraft:cauldron", JsonT{{"cauldron_liquid", "water"}, {"fill_level", 6}});
        auto const* lava  = gf::game::blockWithStates("minecraft:cauldron", JsonT{{"cauldron_liquid", "lava"}, {"fill_level", 6}});
        ctx.require(water && lava, "the water and lava cauldron states exist");
        auto w = fx::blockDustColor(*water, nullptr, Vec3{});
        ctx.check(w.r == 1.f && w.g == 1.f && w.b == 1.f, "a water cauldron block state: falling dust in white");
        auto              l = fx::blockDustColor(*lava, nullptr, Vec3{});
        mce::Color const& m = lava->getBlockType().mMapColor;
        ctx.check(l.r == m.r && l.g == m.g && l.b == m.b && !(l.r == 1.f && l.g == 1.f && l.b == 1.f), "a lava cauldron block state: its map colour");
    }

    // the packets: every mapped type reaches the player, with variables or as a legacy particle
    auto pos = ctx.testPos(6001);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p   = ctx.player();
    auto& dim = p.getDimension();
    auto  at  = p.getPosition();
    auto  c0  = fx::counters();
    fx::particle(dim, "minecraft:portal", at, Vec3{0.1f, 0.5f, -0.1f});
    fx::particle(dim, "minecraft:note", at);
    fx::particle(dim, "minecraft:falling_dust", at, Vec3{}, 1, JsonT{{"block", "minecraft:snow"}});
    auto c1 = fx::counters();
    ctx.check(c1.variablePackets >= c0.variablePackets + 3, "portal, note and falling dust carry their variables");
    fx::particle(dim, "minecraft:item", at, Vec3{0, 0.1f, 0}, 2, JsonT{{"item", "minecraft:bone"}});
    fx::particle(dim, "minecraft:block", at, Vec3{}, 1, JsonT{{"block", "minecraft:oak_leaves"}});
    fx::particle(dim, "minecraft:item_slime", at);
    fx::particle(dim, "minecraft:item", at, Vec3{}, 1, JsonT{{"item", "twilightforest:twilight_oak_log"}}); // a block item (negative id)
    auto c2 = fx::counters();
    ctx.check(c2.legacyPackets >= c1.legacyPackets + 5, "item, block item, block and slime particles go as legacy particles (" + std::to_string(c2.legacyPackets - c1.legacyPackets) + ")");
    fx::particle(dim, "minecraft:item", at, Vec3{}, 1, JsonT{{"item", "minecraft:no_such_item"}});
    fx::particle(dim, "minecraft:block", at);
    auto c3 = fx::counters();
    ctx.check(c3.particlePackets == c2.particlePackets, "an item / block particle without a known item / block is silent");
    fx::particle(dim, "minecraft:dust", at, Vec3{}, 1, JsonT{{"color", {0.25, 0.0, 0.5}}, {"scale", 1.0}});
    fx::particle(dim, "minecraft:dust", at, Vec3{}, 1, JsonT{{"color", {1.0, 0.0, 0.0}}, {"scale", 1.0}});
    auto c4 = fx::counters();
    ctx.check(c4.variablePackets >= c3.variablePackets + 2 && c4.legacyPackets == c3.legacyPackets, "purple and red dust go as gf:dust with their variables");
    // falling dust of a water cauldron (white) and of a log state (its map colour at the position)
    fx::particle(dim, "minecraft:falling_dust", at, Vec3{}, 1, JsonT{{"block", "minecraft:cauldron"}, {"states", {{"cauldron_liquid", "water"}, {"fill_level", 6}}}});
    fx::particle(dim, "minecraft:falling_dust", at, Vec3{}, 1, JsonT{{"block", "minecraft:oak_log"}, {"states", {{"pillar_axis", "x"}}}});
    ctx.check(fx::counters().variablePackets >= c4.variablePackets + 2, "falling dust of block states carries variable.color");
    // colours within 1/255 of Java's 0xRRGGBB
    auto rgb     = [](int c) { return fx::Rgba{((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, 1.f}; };
    auto sameRgb = [](fx::Rgba const& a, fx::Rgba const& b) {
        return std::abs(a.r - b.r) <= 1.f / 255.f && std::abs(a.g - b.g) <= 1.f / 255.f && std::abs(a.b - b.b) <= 1.f / 255.f;
    };
    if (auto const* logX = gf::game::blockWithStates("minecraft:oak_log", JsonT{{"pillar_axis", "x"}}), *logY = gf::game::blockWithStates("minecraft:oak_log", JsonT{{"pillar_axis", "y"}});
        logX && logY) {
        auto& region = dim.getBlockSourceFromMainChunkSource();
        auto  x      = fx::blockDustColor(*logX, &region, at);
        auto  y      = fx::blockDustColor(*logY, &region, at);
        auto  show   = [](fx::Rgba const& c) { return "(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) + ")"; };
        // Java's oak log: MapColor WOOD (0x8F7748) on its ends (axis y), PODZOL (0x815631) on its sides
        ctx.check(!sameRgb(x, y), "the map colour fallback is the block state's (oak log x " + show(x) + " differs from y " + show(y) + ")");
        ctx.check(sameRgb(x, rgb(0x815631)), "oak log axis x: PODZOL 0x815631 " + show(x));
        ctx.check(sameRgb(y, rgb(0x8F7748)), "oak log axis y: WOOD 0x8F7748 " + show(y));
    } else {
        ctx.fail("the oak log axis states exist");
    }

    // Java's ids Bedrock folds into one block (paramBlockOf): the cauldrons and the beds
    {
        auto& region = dim.getBlockSourceFromMainChunkSource();
        auto  dust   = [&](JsonT const& params) { return fx::paramDustColor(fx::paramBlockOf(params), &region, at); };
        auto  water  = dust(JsonT{{"block", "minecraft:water_cauldron"}, {"states", {{"level", "2"}}}});
        auto  lava   = dust(JsonT{{"block", "minecraft:lava_cauldron"}});
        auto  snow   = dust(JsonT{{"block", "minecraft:powder_snow_cauldron"}, {"states", {{"level", 3}}}});
        ctx.check(water && sameRgb(*water, rgb(0xFFFFFF)), "minecraft:water_cauldron: white (its BlockColor without a level)");
        ctx.check(lava && sameRgb(*lava, rgb(0x707070)), "minecraft:lava_cauldron: the cauldron's map colour, STONE 0x707070");
        ctx.check(snow && sameRgb(*snow, rgb(0x707070)), "minecraft:powder_snow_cauldron: STONE 0x707070");
        auto const* waterBlock = fx::paramBlockOf(JsonT{{"block", "minecraft:water_cauldron"}, {"states", {{"level", 2}}}}).block;
        ctx.check(waterBlock && gf::game::statesOf(*waterBlock)["cauldron_liquid"] == "water" && gf::game::intState(*waterBlock, "fill_level") == 4,
                  "water_cauldron level 2 is minecraft:cauldron with water at fill_level 4");
        auto foot = dust(JsonT{{"block", "minecraft:red_bed"}});
        auto head = dust(JsonT{{"block", "minecraft:red_bed"}, {"states", {{"part", "head"}}}});
        auto blue = dust(JsonT{{"block", "minecraft:blue_bed"}, {"states", {{"part", "foot"}}}});
        ctx.check(foot && sameRgb(*foot, rgb(0x993333)), "red bed foot: COLOR_RED 0x993333");
        ctx.check(head && sameRgb(*head, rgb(0xC7C7C7)), "red bed head: WOOL 0xC7C7C7");
        ctx.check(blue && sameRgb(*blue, rgb(0x334CB2)), "blue bed foot: COLOR_BLUE 0x334CB2");
        auto const* bed = fx::paramBlockOf(JsonT{{"block", "minecraft:red_bed"}, {"states", {{"part", "head"}}}}).block;
        ctx.check(bed && gf::game::blockId(*bed) == "minecraft:bed" && gf::game::intState(*bed, "head_piece_bit").value_or(0) == 1, "a Java bed is minecraft:bed with its part");
        auto bedrockHead = dust(JsonT{{"block", "minecraft:bed"}, {"states", {{"part", "head"}}}});
        ctx.check(bedrockHead && sameRgb(*bedrockHead, rgb(0xC7C7C7)), "minecraft:bed with Java's part head: WOOL");
        ctx.check(fx::paramBlockOf(JsonT{{"block", "minecraft:red_bed"}, {"states", {{"part", 1}}}}).block != nullptr, "a malformed bed part is the foot, not an error");
        ctx.check(!dust(JsonT{{"block", "minecraft:no_such_block"}}), "an unknown block has no colour");
    }
});

// ---- combat --------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.damage_types", [](Context& ctx) {
    namespace c = gf::combat;
    using Cause = SharedTypes::Legacy::ActorDamageCause;
    int tf      = 0;
    for (auto const& id : c::damageTypeIds())
        if (id.rfind("twilightforest:", 0) == 0) ++tf;
    ctx.check(tf >= 40, "TF's 40 damage types are known (" + std::to_string(tf) + ")");
    auto const* bite = c::damageType("twilightforest:hydra_bite");
    ctx.require(bite != nullptr, "hydra_bite is known");
    ctx.check(bite->messageId == "twilightforest.hydraBite", "message ids follow TFDamageTypes");
    ctx.check(c::damageType("twilightforest:slam")->messageId == "twilightforest.axing", "slam reuses the axing message");
    ctx.check(c::damageType("twilightforest:thorns")->exhaustion > 0.09f, "exhaustion follows TFDamageTypes");
    ctx.check(c::causeFor("twilightforest:lich_bolt", true) == Cause::Magic, "bypasses_armor -> Magic");
    ctx.check(c::causeFor("twilightforest:hydra_fire", true) == Cause::Fire, "is_fire -> Fire");
    ctx.check(c::causeFor("twilightforest:yeeted", true) == Cause::Fall, "is_fall -> Fall");
    ctx.check(c::causeFor("twilightforest:expired", false) == Cause::Override, "bypasses_invulnerability -> Override");
    ctx.check(c::causeFor("twilightforest:thrown_axe", true) == Cause::Projectile, "is_projectile -> Projectile");
    ctx.check(c::causeFor("twilightforest:hydra_bite", true) == Cause::EntityAttack, "plain damage with an attacker -> EntityAttack");
    ctx.check(c::damageTypeHasTag("twilightforest:clamped", "minecraft:no_knockback"), "TF's tag memberships come from the tag data (F9)");
});

GF_REGISTER_SELFTEST("combat.hurt_hook", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace c = gf::combat;
    static std::string           sSeenType;
    static float                 sDealt   = -1;
    static bool                  sCancel  = false;
    static bool                  sDoubled = false;
    static bool                  registered = false;
    if (!registered) {
        registered = true;
        c::onIncoming(c::priority::High, [](c::DamageInfo& d) {
            if (!isActive(d.victim)) return c::Verdict::Pass;
            sSeenType = d.type;
            if (sCancel) return c::Verdict::Cancel;
            if (sDoubled) d.amount *= 2;
            return c::Verdict::Pass;
        });
        c::onDealt(c::priority::Normal, [](c::DamageInfo const& d) {
            if (isActive(d.victim)) sDealt = d.amount;
        });
    }
    auto pos = ctx.testPos(6001);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    ActiveScope active(p);
    float before = healthOf(p);
    sDoubled = true;
    sDealt   = -1;
    bool hit = c::hurt(p, 2.f, "twilightforest:hydra_bite");
    sDoubled = false;
    ctx.check(hit, "gf::combat::hurt applied the damage");
    ctx.check(sSeenType == "twilightforest:hydra_bite", "onIncoming saw the TF damage type (got " + sSeenType + ")");
    ctx.check(sDealt >= 3.9f, "onDealt heard the doubled amount (got " + std::to_string(sDealt) + ")");
    ctx.check(healthOf(p) <= before - 3.9f, "the doubled damage was applied");
    sCancel     = true;
    float mid   = healthOf(p);
    bool  again = c::hurt(p, 3.f, "twilightforest:squish");
    sCancel     = false;
    ctx.check(!again && healthOf(p) == mid, "a cancelled hurt does nothing");
    // death message of the last TF damage: TF's key with the victim's name
    auto msg = c::deathMessageFor(p, "Victim");
    ctx.check(msg.first == "death.attack.twilightforest.hydraBite" || msg.first == "death.attack.twilightforest.hydraBite.player", "TF death message key (got " + msg.first + ")");
    ctx.check(!msg.second.empty() && msg.second[0] == "Victim", "the victim's name is the first parameter");
    // vanilla damage gets Java's type id
    sSeenType.clear();
    {
        ActorDamageSource fall(SharedTypes::Legacy::ActorDamageCause::Fall, std::string{});
        HurtParameters    hp{};
        (void)p.hurt(fall, 1.f, hp);
    }
    ctx.check(sSeenType == "minecraft:fall", "vanilla fall damage is minecraft:fall (got " + sSeenType + ")");
});

// ---- death pipeline --------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.lethal", [](Context& ctx) -> ll::coro::CoroTask<> {
    static bool registered = false;
    static int  saves      = 0;
    if (!registered) {
        registered = true;
        // a charm-of-life like handler: survive with 8 health
        gf::death::onLethal(gf::combat::priority::High, [](Actor& victim, gf::combat::DamageInfo const&) {
            if (!isActive(victim)) return false;
            ++saves;
            gf::death::setHealth(victim, 8.f);
            return true;
        });
    }
    auto pos = ctx.testPos(6002);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p  = ctx.player();
    auto  id = p.getOrCreateUniqueID();
    {
        ActiveScope active(p);
        saves = 0;
        gf::combat::hurt(p, 1000.f, "twilightforest:expired");
    }
    co_await ctx.ticks(2);
    auto level = ll::service::getLevel();
    auto* again = level ? level->getPlayer(id) : nullptr;
    ctx.require(again != nullptr, "the player is still in the level");
    ctx.check(saves == 1, "the lethal handler ran once (" + std::to_string(saves) + ")");
    ctx.check(again->isAlive() && again->getHealth() == 8, "the fixture player survived lethal damage with the handler's health (" + std::to_string(again->getHealth()) + ")");
});

GF_REGISTER_SELFTEST("combat.keepers", [](Context& ctx) -> ll::coro::CoroTask<> {
    static bool registered = false;
    static int  dropsSeen  = 0;
    static int  respawns   = 0;
    if (!registered) {
        registered = true;
        // keeps the fixture item (as charms / the kept_on_death tag keep their stacks)
        gf::death::registerKeeper(gf::combat::priority::Normal, [](Player& p, ItemStack& s) {
            return isActive(p) && items::idOf(s) == "gf_test:fixture_item";
        });
        // takes the cobblestone out of the drops (as the keepsake casket does)
        gf::death::onDropsRemaining([](Player& p, std::vector<ItemStack>& drops) {
            if (!isActive(p)) return;
            for (auto& s : drops)
                if (items::idOf(s) == "minecraft:cobblestone") {
                    dropsSeen += items::countOf(s);
                    s = ItemStack{};
                }
        });
        gf::death::onRespawn([](Player& p) {
            if (isActive(p)) ++respawns;
        });
    }
    auto pos = ctx.testPos(6003);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = ctx.spawnPlayer("gf_combat_keeper");
    auto  id = sp.getOrCreateUniqueID();
    auto  fixture = items::make("gf_test:fixture_item", 5);
    auto  cobble  = items::make("minecraft:cobblestone", 9);
    ctx.require(fixture && cobble, "fixture item and cobblestone exist");
    ctx.give(sp, *fixture);
    ctx.give(sp, *cobble);
    dropsSeen = 0;
    respawns  = 0;
    {
        ActiveScope active(sp);
        gf::combat::hurt(sp, 1000.f, "twilightforest:expired");
    }
    co_await ctx.ticks(2);
    auto level = ll::service::getLevel();
    auto* p    = level ? level->getPlayer(id) : nullptr;
    ctx.require(p != nullptr, "the player is still in the level");
    auto kept = db::get("gf:combat:kept:" + p->getUuid().asString());
    bool keptFixture = kept && kept->is_array() && std::any_of(kept->begin(), kept->end(), [](JsonT const& e) {
        return e.value("item", JsonT::object()).value("id", std::string{}) == "gf_test:fixture_item";
    });
    ctx.check(keptFixture, "the keeper's stack is stored through death");
    ctx.check(countInInventory(*p, "gf_test:fixture_item") == 0, "the kept stack left the inventory");
    ctx.check(dropsSeen == 9, "the drops handler took the cobblestone (" + std::to_string(dropsSeen) + ")");
    // respawn: the kept items come back
    {
        ActiveScope active(*p);
        static_cast<SimulatedPlayer&>(*p).simulateRespawn();
    }
    co_await ctx.ticks(10);
    p = level->getPlayer(id);
    ctx.require(p != nullptr, "the player respawned");
    ctx.check(countInInventory(*p, "gf_test:fixture_item") == 5, "the respawn returned the kept stack (" + std::to_string(countInInventory(*p, "gf_test:fixture_item")) + ")");
    ctx.check(!db::get("gf:combat:kept:" + p->getUuid().asString()), "the kept record is gone");
    ctx.check(respawns >= 1, "onRespawn ran");
});

// ---- loot ----------------------------------------------------------------------------------------

namespace {

/// A table using every TF loot extension with constant numbers (exact expected counts).
JsonT tfFunctionsTable() {
    return JsonT::parse(R"({
      "type": "minecraft:entity",
      "pools": [
        { "rolls": { "type": "twilightforest:multiplayer_rolls", "default_rolls": 1, "per_player_rolls": 1 },
          "entries": [ { "type": "minecraft:item", "name": "minecraft:stick" } ] },
        { "rolls": 1,
          "entries": [ { "type": "minecraft:item", "name": "minecraft:diamond",
            "functions": [ { "function": "minecraft:set_count", "count": 2 },
                           { "function": "twilightforest:multiplayer_addition", "extra_count_per_player": 2 } ] } ] },
        { "rolls": { "type": "twilightforest:looting_rolls", "enchantment": "minecraft:looting", "base_value": 1 },
          "entries": [ { "type": "minecraft:item", "name": "minecraft:emerald" } ] },
        { "rolls": 1, "conditions": [ { "condition": "twilightforest:mod_exists", "mod_id": "twilightforest" } ],
          "entries": [ { "type": "minecraft:item", "name": "minecraft:gold_ingot" } ] },
        { "rolls": 1, "conditions": [ { "condition": "twilightforest:mod_exists", "mod_id": "examplemod" } ],
          "entries": [ { "type": "minecraft:item", "name": "minecraft:iron_ingot" } ] }
      ]
    })");
}

/// Vanilla structure of the interpreter: alternatives, nested tables, weights, conditions.
JsonT structureTable() {
    return JsonT::parse(R"({
      "type": "minecraft:chest",
      "pools": [
        { "rolls": 1, "entries": [ { "type": "minecraft:alternatives", "children": [
            { "type": "minecraft:item", "name": "minecraft:apple", "conditions": [ { "condition": "minecraft:killed_by_player" } ] },
            { "type": "minecraft:item", "name": "minecraft:bread" } ] } ] },
        { "rolls": 2, "entries": [ { "type": "minecraft:loot_table", "value": "gf_test:combat/nested" } ] },
        { "rolls": 1, "entries": [ { "type": "minecraft:item", "name": "minecraft:coal", "weight": 0 },
                                   { "type": "minecraft:item", "name": "minecraft:flint", "weight": 5,
                                     "functions": [ { "function": "minecraft:set_count", "count": { "type": "minecraft:uniform", "min": 3, "max": 5 } },
                                                    { "function": "minecraft:limit_count", "limit": { "max": 4 } } ] } ] },
        { "rolls": 1, "entries": [ { "type": "minecraft:item", "name": "minecraft:snowball",
            "functions": [ { "function": "minecraft:set_count", "count": 70 } ] } ] },
        { "rolls": 1, "conditions": [ { "condition": "minecraft:inverted", "term": { "condition": "minecraft:random_chance", "chance": 1.0 } } ],
          "entries": [ { "type": "minecraft:item", "name": "minecraft:iron_ingot" } ] }
      ]
    })");
}

} // namespace

GF_REGISTER_SELFTEST("combat.loot_tf_functions", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace loot = gf::loot;
    loot::registerTable("gf_test:combat/tf_functions", tfFunctionsTable());
    auto pos = ctx.testPos(6004);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    // the attacker holds a looting III sword
    auto sword = items::make("minecraft:iron_sword", 1);
    ctx.require(sword.has_value(), "iron sword");
    EnchantmentInstance looting;
    looting.mEnchantType = Enchant::Type::Looting;
    looting.mLevel       = 3;
    EnchantUtils::applyEnchant(*sword, looting, true);
    ctx.check(loot::enchantLevel(*sword, "minecraft:looting") == 3, "the sword has looting III");
    p.setCarriedItem(*sword);
    loot::LootContext lc;
    lc.region     = ctx.region();
    lc.origin     = p.getPosition();
    lc.thisEntity = &p;
    lc.attacker   = &p;
    lc.player     = &p;
    lc.seed       = 1234;
    // three qualified players with an adjuster that adds loot
    loot::setTestParticipants(3);
    loot::setTestAdjustsLoot(true);
    auto many = loot::roll("gf_test:combat/tf_functions", lc);
    loot::setTestAdjustsLoot(false);
    auto plain = loot::roll("gf_test:combat/tf_functions", lc);
    loot::setTestParticipants(-1);
    loot::setTestAdjustsLoot(std::nullopt);
    p.setCarriedItem(ItemStack{});
    ctx.check(countIn(many, "minecraft:stick") == 3, "multiplayer_rolls: 1 + (3 - 1) x 1 rolls (got " + std::to_string(countIn(many, "minecraft:stick")) + ")");
    ctx.check(countIn(many, "minecraft:diamond") == 6, "multiplayer_addition: 2 + (3 - 1) x 2 (got " + std::to_string(countIn(many, "minecraft:diamond")) + ")");
    ctx.check(countIn(many, "minecraft:emerald") == 4, "looting_rolls: looting 3 + base 1 (got " + std::to_string(countIn(many, "minecraft:emerald")) + ")");
    ctx.check(countIn(many, "minecraft:gold_ingot") == 1 && countIn(many, "minecraft:iron_ingot") == 0, "mod_exists: twilightforest yes, examplemod no");
    ctx.check(countIn(plain, "minecraft:stick") == 1 && countIn(plain, "minecraft:diamond") == 2, "without the loot adjuster the default rolls and counts apply");
    loot::registerTable("gf_test:combat/tf_functions", nullptr);
});

GF_REGISTER_SELFTEST("combat.loot_interpreter", [](Context& ctx) {
    namespace loot = gf::loot;
    loot::registerTable("gf_test:combat/nested", JsonT::parse(R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:string"}]}]})"));
    loot::registerTable("gf_test:combat/structure", structureTable());
    loot::LootContext lc;
    lc.seed  = 42;
    auto a   = loot::roll("gf_test:combat/structure", lc);
    auto b   = loot::roll("gf_test:combat/structure", lc);
    ctx.check(countIn(a, "minecraft:bread") == 1 && countIn(a, "minecraft:apple") == 0, "alternatives: the first child whose conditions pass");
    ctx.check(countIn(a, "minecraft:string") == 2, "nested loot_table entries roll the other table");
    int flint = countIn(a, "minecraft:flint");
    ctx.check(flint >= 3 && flint <= 4 && countIn(a, "minecraft:coal") == 0, "weights (0 never) and set_count / limit_count (got " + std::to_string(flint) + ")");
    int snow = 0, stacks = 0;
    for (auto const& s : a)
        if (items::idOf(s) == "minecraft:snowball") {
            snow += items::countOf(s);
            ++stacks;
        }
    ctx.check(snow == 70 && stacks == 5, "stacks over the max stack size are split (70 snowballs in 16-stacks: " + std::to_string(stacks) + " stacks)");
    ctx.check(countIn(a, "minecraft:iron_ingot") == 0, "inverted random_chance 1.0 never passes");
    bool same = a.size() == b.size();
    for (std::size_t i = 0; same && i < a.size(); ++i) same = items::idOf(a[i]) == items::idOf(b[i]) && items::countOf(a[i]) == items::countOf(b[i]);
    ctx.check(same, "a seeded roll is reproducible");
    // Java's LegacyRandomSource sequence (java.util.Random(42).nextInt(10) == 0, then 3)
    gf::loot::JavaRandom jr(42);
    int first = jr.nextInt(10), second = jr.nextInt(10);
    ctx.check(first == 0 && second == 3, "JavaRandom matches java.util.Random (got " + std::to_string(first) + ", " + std::to_string(second) + ")");
    // the F0 fixture chest table (TF number provider and function shapes)
    loot::LootContext fixtureCtx;
    fixtureCtx.seed = 7;
    auto fx         = loot::roll("gf_test:chests/fixture_chest", fixtureCtx);
    int  n          = countIn(fx, "gf_test:fixture_item") + countIn(fx, "minecraft:stick");
    ctx.check(!fx.empty() && n >= 1 && n <= 12, "the fixture chest table rolls (" + std::to_string(fx.size()) + " stacks)");
    // survives_explosion inside an explosion of radius 1000 almost never passes
    loot::registerTable("gf_test:combat/explosion", JsonT::parse(R"({"pools":[{"rolls":1,"conditions":[{"condition":"minecraft:survives_explosion"}],"entries":[{"type":"minecraft:item","name":"minecraft:dirt"}]}]})"));
    int survived = 0;
    {
        loot::ExplosionScope scope(1000.f);
        for (int i = 0; i < 20; ++i) {
            loot::LootContext e;
            e.seed = 100 + i;
            survived += countIn(loot::roll("gf_test:combat/explosion", e), "minecraft:dirt");
        }
    }
    ctx.check(survived <= 1, "survives_explosion uses 1 / radius (" + std::to_string(survived) + " of 20)");
    loot::registerTable("gf_test:combat/explosion", nullptr);
    loot::registerTable("gf_test:combat/structure", nullptr);
    loot::registerTable("gf_test:combat/nested", nullptr);
});

GF_REGISTER_SELFTEST("combat.loot_global_modifier", [](Context& ctx) {
    namespace loot = gf::loot;
    static bool active     = false;
    static bool registered = false;
    if (!registered) {
        registered = true;
        loot::addGlobalModifier("gf_test:combat_double", [](std::vector<ItemStack>& l, loot::LootContext const&, std::string const& tableId) {
            if (!active || tableId != "gf_test:combat/modified") return;
            std::vector<ItemStack> more = l;
            l.insert(l.end(), more.begin(), more.end());
        });
    }
    loot::registerTable("gf_test:combat/modified", JsonT::parse(R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:clay_ball"}]}]})"));
    active = true;
    auto l = loot::roll("gf_test:combat/modified", {});
    active = false;
    ctx.check(countIn(l, "minecraft:clay_ball") == 2, "a global modifier changed the roll");
    loot::registerTable("gf_test:combat/modified", nullptr);
});

GF_REGISTER_SELFTEST("combat.place_container", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace loot = gf::loot;
    loot::registerTable("gf_test:combat/chest", JsonT::parse(R"({"type":"minecraft:chest","pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond","functions":[{"function":"minecraft:set_count","count":12}]}]}]})"));
    auto pos = ctx.testPos(6005);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    auto  bp     = gf::game::blockPos(pos);
    ctx.cleanupBlock(pos);
    ctx.require(loot::placeContainer(*region, bp, "minecraft:chest", 2, "gf_test:combat/chest", 99), "a chest with a loot table was placed");
    ctx.check(ctx.blockAt(pos) == "minecraft:chest", "the chest is there");
    auto pending = loot::pendingVanilla(*region, bp);
    ctx.check(pending && pending->first == "gf_test:combat/chest" && pending->second == 99, "the table waits in the chest's loot field");
    // the game unpacks on first open: do what it does
    auto* actor = region->getBlockEntity(bp);
    ctx.require(actor && actor->getType() == BlockActorType::Chest, "a chest block actor");
    auto* chest = static_cast<ChestBlockActor*>(actor);
    auto* c     = chest->getContainer();
    ctx.require(c != nullptr, "the chest's container");
    auto& p = ctx.player();
    static_cast<RandomizableBlockActorContainerBase*>(actor)->unPackLootTable(p.getLevel(), *c, p.getDimensionId(), &p);
    int diamonds = 0, used = 0;
    for (int i = 0; i < c->getContainerSize(); ++i) {
        if (items::idOf(c->getItem(i)) == "minecraft:diamond") {
            diamonds += items::countOf(c->getItem(i));
            ++used;
        }
    }
    ctx.check(diamonds == 12, "the Java table filled the chest (" + std::to_string(diamonds) + " diamonds)");
    ctx.check(used >= 2, "LootTable#fill spread the stack over several slots (" + std::to_string(used) + ")");
    ctx.check(!loot::pendingVanilla(*region, bp), "the loot field was cleared");
    for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
    loot::registerTable("gf_test:combat/chest", nullptr);
});

// ---- inventory -----------------------------------------------------------------------------------

namespace {
constexpr char kContainerBlock[] = "gf_test:fixture_block";
constexpr char kPersistKey[]     = "gf:combat:container_probe";

gf::inventory::Declared fixtureDeclaration() { return {.block = kContainerBlock, .rows = 3, .slots = 27, .title = "container.chest", .skin = ""}; }

/// A container declaration for one test: it goes when the test ends, also when a `require` aborts
/// it midway (other units' tests of the fixture block in the same run must not have their use-on
/// open a screen or their breaks drop block data contents).
struct DeclaredScope {
    explicit DeclaredScope(gf::inventory::Declared const& d) : block(d.block) { gf::inventory::declare(d); }
    ~DeclaredScope() { gf::inventory::unregisterBlock(block); }
    DeclaredScope(DeclaredScope const&)            = delete;
    DeclaredScope& operator=(DeclaredScope const&) = delete;
    std::string block;
};

GlobalPos persistPos(Context& ctx, BlockSource const& region) {
    auto pos = ctx.testPos(6010);
    pos.y    = std::max(pos.y - 3, region.getMinHeight() + 1); // under the ground layer, as core.persist_*
    return pos;
}
} // namespace

GF_REGISTER_SELFTEST("combat.inventory_handler", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace inv = gf::inventory;
    DeclaredScope fixture(fixtureDeclaration());
    auto pos = ctx.testPos(6006);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, kContainerBlock), "fixture block placed");
    auto* region = ctx.region();
    auto  h      = inv::find(*region, gf::game::blockPos(pos));
    ctx.require(h.has_value() && h->slots == 27, "the declared block is a 27-slot container");
    auto cobble = items::make("minecraft:cobblestone", 100);
    ctx.require(cobble.has_value(), "cobblestone");
    auto rest = h->insert(pos, *items::make("minecraft:cobblestone", 64), false);
    rest      = h->insert(pos, *items::make("minecraft:cobblestone", 40), false);
    ctx.check(items::isEmpty(rest), "insert takes everything that fits");
    ctx.check(items::countOf(h->get(pos, 0)) == 64 && items::countOf(h->get(pos, 1)) == 40, "insert merges, then fills empty slots");
    auto taken = h->extract(pos, 1, 15, false);
    ctx.check(items::countOf(taken) == 15 && items::countOf(h->get(pos, 1)) == 25, "extract takes from a slot");
    auto sim = h->extract(pos, 0, 64, true);
    ctx.check(items::countOf(sim) == 64 && items::countOf(h->get(pos, 0)) == 64, "a simulated extract changes nothing");
    // a pending loot table rolls on first access
    gf::loot::registerTable("gf_test:combat/box", JsonT::parse(R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:emerald","functions":[{"function":"minecraft:set_count","count":3}]}]}]})"));
    h->set(pos, 0, ItemStack{});
    h->set(pos, 1, ItemStack{});
    h->setLootTable(pos, "gf_test:combat/box", 5);
    ctx.check(inv::pendingLoot(pos).has_value(), "the loot table is pending");
    int emeralds = 0;
    for (int i = 0; i < h->slots; ++i)
        if (items::idOf(h->get(pos, i)) == "minecraft:emerald") emeralds += items::countOf(h->get(pos, i));
    ctx.check(emeralds == 3 && !inv::pendingLoot(pos), "the table rolled into the container on access");
    gf::loot::registerTable("gf_test:combat/box", nullptr);
    for (int i = 0; i < h->slots; ++i) h->set(pos, i, ItemStack{});
});

// A container keeps its slots over a world reload: combat.container_persist_save fills the fixture
// container 3 blocks under testPos 6010 and writes a marker (gf:combat:container_probe);
// combat.container_persist_verify checks the slots, erases the marker and puts the block that was
// there back. The server scenario runs them around a restart; a glob run ("combat.*", "full")
// sorts save before verify, so verify cleans up in the same run and nothing stays in the world.
GF_REGISTER_SELFTEST("combat.container_persist_save", [](Context& ctx) -> ll::coro::CoroTask<> {
    DeclaredScope fixture(fixtureDeclaration());
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the overworld is available");
    auto pos = persistPos(ctx, *region);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    region = ctx.region();
    auto bp = gf::game::blockPos(pos);
    auto was = ctx.blockAt(pos);
    if (auto prev = db::get(kPersistKey)) was = prev->value("was", was);
    auto const* block = gf::game::blockWith(kContainerBlock);
    ctx.require(block != nullptr, "the fixture block exists");
    ctx.require(&region->getBlock(bp) == block || gf::game::setBlock(*region, bp, *block), "place the container");
    auto h = gf::inventory::find(*region, bp);
    ctx.require(h.has_value(), "the container handler");
    std::random_device rd;
    int                n = 2 + static_cast<int>(rd() % 60);
    h->set(pos, 13, *items::make("minecraft:gold_ingot", n));
    db::put(kPersistKey, {{"pos", {pos.dim, pos.x, pos.y, pos.z}}, {"count", n}, {"was", was}});
    ctx.info("container marker written count=" + std::to_string(n));
});

GF_REGISTER_SELFTEST("combat.container_persist_verify", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto marker = db::get(kPersistKey);
    if (!marker) {
        ctx.info("no container marker (combat.container_persist_save did not run before): nothing to check");
        co_return;
    }
    db::erase(kPersistKey);
    DeclaredScope fixture(fixtureDeclaration());
    auto      at = (*marker)["pos"];
    GlobalPos pos{at[0].get<int>(), at[1].get<int>(), at[2].get<int>(), at[3].get<int>()};
    int       n  = marker->value("count", -1);
    ctx.require(co_await ctx.ensureLoaded(pos), "marker area loaded");
    auto* region = ctx.region();
    auto  bp     = gf::game::blockPos(pos);
    auto  h      = gf::inventory::find(*region, bp);
    ctx.check(h.has_value(), "the container block survived the reload");
    if (h) {
        auto s = h->get(pos, 13);
        ctx.check(items::idOf(s) == "minecraft:gold_ingot" && items::countOf(s) == n, "the container kept its items (got " + items::idOf(s) + " x" + std::to_string(items::countOf(s)) + ")");
        if (items::countOf(s) == n) ctx.info("container marker verified count=" + std::to_string(n));
        h->set(pos, 13, ItemStack{});
    }
    auto was = marker->value("was", std::string{});
    if (auto const* original = was.empty() ? nullptr : gf::game::blockWith(was)) gf::game::setBlock(*region, bp, *original);
    else gf::game::clearBlock(*region, bp);
});

// ---- progression ---------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.progression", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace pr = gf::progression;
    static JsonT        sData;
    static int         sGranted  = 0;
    static int         sInvFired = 0;
    static int         sTicks    = 0;
    static bool        registered = false;
    if (!registered) {
        registered = true;
        pr::onTrigger("gf_test:fixture_trigger", [](Player& p, JsonT const& data) {
            if (isActive(p)) sData = data;
        });
        pr::onGranted([](Player& p, std::string const& adv) {
            if (isActive(p) && adv == "gf_test:fixture_advancement") ++sGranted;
        });
        pr::events::onInventoryChanged([](Player& p) {
            if (isActive(p)) ++sInvFired;
        });
        pr::events::onTick([](Player& p) {
            if (isActive(p)) ++sTicks;
        });
    }
    auto pos = ctx.testPos(6007);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p  = ctx.player();
    auto  id = p.getOrCreateUniqueID();
    ActiveScope active(p);
    sData = nullptr;
    sGranted = sInvFired = sTicks = 0;
    pr::trigger(p, "gf_test:fixture_trigger", {{"bug", "twilightforest:firefly"}});
    ctx.check(sData.value("bug", std::string{}) == "twilightforest:firefly", "the trigger reached the fixture listener with its data");
    pr::revoke(p, "gf_test:fixture_advancement");
    ctx.check(!pr::has(p, "gf_test:fixture_advancement"), "not granted yet");
    pr::grant(p, "gf_test:fixture_advancement", "fixture_criterion");
    ctx.check(pr::has(p, "gf_test:fixture_advancement") && pr::done(p, "gf_test:fixture_advancement", "fixture_criterion"), "grant completes an advancement without a definition");
    pr::grant(p, "gf_test:fixture_advancement");
    ctx.check(sGranted == 1, "onGranted fired once");
    auto stored = db::get("gf:combat:progress:" + p.getUuid().asString());
    ctx.check(stored && stored->dump().find("gf_test:fixture_advancement") != std::string::npos, "progress is stored in the world DB");
    pr::revoke(p, "gf_test:fixture_advancement");
    ctx.check(!pr::has(p, "gf_test:fixture_advancement"), "revoke takes it back");
    pr::showMissing(p, "gf_test:fixture_advancement"); // a vanilla toast for the simulated player
    ctx.give("minecraft:stick", 1);
    co_await ctx.ticks(25);
    auto* again = ll::service::getLevel()->getPlayer(id);
    ctx.require(again != nullptr, "the player is still there");
    ctx.check(sInvFired >= 1, "onInventoryChanged fired");
    ctx.check(sTicks >= 1, "onTick fired within a second");
});

// ---- entity drops --------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.entity_drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    static bool        registered = false;
    static int         sFixture   = 0;
    static bool        sKiller    = false;
    static std::string sType;
    static int         sAdded     = 0;
    if (!registered) {
        registered = true;
        // the death pipeline's drops: gf_test:boss rolls gf_test:entities/boss (the F0 fixture table)
        gf::loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const& lc) {
            if (gActive == 0 || dead.getTypeName() != "gf_test:boss") return;
            sFixture = countIn(drops, "gf_test:fixture_item");
            sKiller  = lc.player != nullptr && lc.player->getOrCreateUniqueID().rawID == gActive;
            sType    = lc.damageType;
            if (auto bonus = items::make("minecraft:stick", 1)) drops.push_back(*bonus); // handlers may add
            sAdded = countIn(drops, "minecraft:stick");
            drops.clear(); // and take everything: nothing is left in the shared test row
        });
    }
    auto pos = ctx.testPos(6008);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    auto& p      = ctx.player();
    Vec3  at{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + .5f};
    auto* mob = region->getLevel().getSpawner().spawnMob(*region, ActorDefinitionIdentifier("gf_test:boss"), nullptr, at, false, false, false);
    ctx.require(mob != nullptr, "the fixture boss spawned");
    sFixture = 0;
    sAdded   = 0;
    sKiller  = false;
    sType.clear();
    {
        ActiveScope active(p);
        gf::combat::hurt(*mob, 1000.f, "twilightforest:thrown_axe", &p);
        // LivingEntity#dropFromLootTable: the Java table's drops are in the world in the death tick
        ctx.check(itemEntitiesNear(pos, 4) >= 1, "the Java table dropped in the death tick");
        ctx.check(gf::loot::pendingDeathCount() >= 1, "the merge with the game's drops waits for the next tick");
        co_await ctx.ticks(2); // the merge runs on the next tick, while the test is active
    }
    ctx.check(gf::loot::pendingDeathCount() == 0, "no death waits any more");
    ctx.check(sFixture == 1, "the Java table gf_test:entities/boss dropped the fixture item (" + std::to_string(sFixture) + ")");
    ctx.check(sAdded == 1, "a handler added a stack to the drops");
    ctx.check(sKiller, "the loot context names the killing player");
    ctx.check(sType == "twilightforest:thrown_axe", "the loot context has the damage type (" + sType + ")");
    ctx.check(itemEntitiesNear(ctx.testPos(6008), 4) == 0, "the handler took every drop: no item entity is left");
});

// Vanilla mobs: the handlers see the game's own death loot (Bedrock's loot table) and may change it.
GF_REGISTER_SELFTEST("combat.entity_drops_vanilla", [](Context& ctx) -> ll::coro::CoroTask<> {
    static bool registered = false;
    static int  sBeef      = 0;
    static int  sCalls     = 0;
    if (!registered) {
        registered = true;
        gf::loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const&) {
            if (gActive == 0 || dead.getTypeName() != "minecraft:cow") return;
            ++sCalls;
            sBeef = countIn(drops, "minecraft:beef") + countIn(drops, "minecraft:cooked_beef");
            drops.clear(); // Java's LivingDropsEvent: the handler takes the drops
        });
    }
    auto pos = ctx.testPos(6013);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    auto& p      = ctx.player();
    Vec3  at{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + .5f};
    auto* cow = region->getLevel().getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:cow"), nullptr, at, false, false, false);
    ctx.require(cow != nullptr, "a cow spawned");
    if (cow->isBaby()) {
        cow->remove();
        ctx.info("the cow was a calf (no loot): nothing to check");
        co_return;
    }
    sBeef  = 0;
    sCalls = 0;
    {
        ActiveScope active(p);
        gf::combat::hurt(*cow, 1000.f, "twilightforest:thrown_axe", &p);
        co_await ctx.ticks(2); // the drop pass runs on the next tick, while the test is active
    }
    co_await ctx.ticks(2);
    ctx.check(sCalls == 1, "the drops handlers ran once for the vanilla mob (" + std::to_string(sCalls) + ")");
    ctx.check(sBeef >= 1, "the handler saw Bedrock's own loot (" + std::to_string(sBeef) + " beef)");
    ctx.check(itemEntitiesNear(pos, 4) == 0, "the drops the handler took are gone from the world");
});

// ---- loot: Java's number rounding, set_contents ---------------------------------------------------

GF_REGISTER_SELFTEST("combat.loot_number_rounding", [](Context& ctx) {
    namespace loot = gf::loot;
    // NumberProvider#getInt = Math.round(getFloat): half up
    ctx.check(loot::javaRound(0.5f) == 1 && loot::javaRound(2.5f) == 3 && loot::javaRound(-0.5f) == 0 && loot::javaRound(-2.5f) == -2,
              "Math.round rounds half up");
    ctx.check(loot::javaRound(0.49999997f) == 0 && loot::javaRound(1.4f) == 1, "Math.round below one half rounds down");
    // multiplayer_rolls with per_player_rolls 1.5 and 2 qualified players: 1 + 1.5 = 2.5 -> 3 rolls;
    // constant rolls 1.5 -> 2; binomial n 5.5 -> 6 trials (p 1); set_count 2.5 -> 3
    loot::registerTable("gf_test:combat/rounding", JsonT::parse(R"({
      "pools": [
        { "rolls": { "type": "twilightforest:multiplayer_rolls", "default_rolls": 1, "per_player_rolls": 1.5 },
          "entries": [ { "type": "minecraft:item", "name": "minecraft:stick" } ] },
        { "rolls": 1.5, "entries": [ { "type": "minecraft:item", "name": "minecraft:flint" } ] },
        { "rolls": { "type": "minecraft:binomial", "n": 5.5, "p": 1.0 }, "entries": [ { "type": "minecraft:item", "name": "minecraft:clay_ball" } ] },
        { "rolls": 1, "entries": [ { "type": "minecraft:item", "name": "minecraft:brick",
            "functions": [ { "function": "minecraft:set_count", "count": 2.5 } ] } ] }
      ]
    })"));
    loot::setTestParticipants(2);
    loot::setTestAdjustsLoot(true);
    loot::LootContext lc;
    lc.seed = 99;
    auto l  = loot::roll("gf_test:combat/rounding", lc);
    loot::setTestParticipants(-1);
    loot::setTestAdjustsLoot(std::nullopt);
    loot::registerTable("gf_test:combat/rounding", nullptr);
    ctx.check(countIn(l, "minecraft:stick") == 3, "per_player_rolls 1.5 with 2 players: 3 rolls (got " + std::to_string(countIn(l, "minecraft:stick")) + ")");
    ctx.check(countIn(l, "minecraft:flint") == 2, "constant rolls 1.5: 2 rolls (got " + std::to_string(countIn(l, "minecraft:flint")) + ")");
    ctx.check(countIn(l, "minecraft:clay_ball") == 6, "binomial n 5.5: 6 trials (got " + std::to_string(countIn(l, "minecraft:clay_ball")) + ")");
    ctx.check(countIn(l, "minecraft:brick") == 3, "set_count 2.5: 3 (got " + std::to_string(countIn(l, "minecraft:brick")) + ")");
});

GF_REGISTER_SELFTEST("combat.loot_set_contents", [](Context& ctx) {
    namespace loot = gf::loot;
    // the shape of TF's entities/questing_ram_rewards: a bundle filled by set_contents with an item
    // entry and a nested loot table entry (every pool of it rolls into the bundle)
    loot::registerTable("gf_test:combat/bundle_blocks", JsonT::parse(R"({"pools":[
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:coal_block"}]},
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:iron_block"}]}]})"));
    loot::registerTable("gf_test:combat/bundle", JsonT::parse(R"({"pools":[
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:bundle","functions":[
          {"function":"minecraft:set_contents","component":"minecraft:bundle_contents","entries":[
            {"type":"minecraft:item","name":"minecraft:emerald"},
            {"type":"minecraft:loot_table","value":"gf_test:combat/bundle_blocks"}]}]}]},
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:chest","functions":[
          {"function":"minecraft:set_contents","component":"minecraft:container","entries":[
            {"type":"minecraft:item","name":"minecraft:apple"}]}]}]}]})"));
    loot::LootContext lc;
    lc.seed = 5;
    auto l  = loot::roll("gf_test:combat/bundle", lc);
    loot::registerTable("gf_test:combat/bundle", nullptr);
    loot::registerTable("gf_test:combat/bundle_blocks", nullptr);
    ItemStack const* bundle = nullptr;
    ItemStack const* chest  = nullptr;
    for (auto const& s : l) {
        if (items::idOf(s) == "minecraft:bundle") bundle = &s;
        if (items::idOf(s) == "minecraft:chest") chest = &s;
    }
    ctx.require(bundle != nullptr, "the bundle dropped");
#ifndef GF_CLIENT
    ctx.check(std::string(StorageItemComponentTags::bundleIDTag()) == loot::kBundleIdTag,
              "the bundle id tag is the game's (" + std::string(StorageItemComponentTags::bundleIDTag()) + ")");
#endif
    auto inside = loot::bundleContents(*bundle);
    ctx.require(inside.has_value(), "the bundle has a Bedrock storage container");
    ctx.check(countIn(*inside, "minecraft:emerald") == 1 && countIn(*inside, "minecraft:coal_block") == 1 && countIn(*inside, "minecraft:iron_block") == 1,
              "the bundle holds the item entry and every pool of the nested table (" + std::to_string(inside->size()) + " stacks)");
    ctx.require(chest != nullptr, "the chest item dropped");
    auto comps = items::getData(*chest, "components");
    ctx.check(comps.is_object() && comps.value("minecraft:container", JsonT::array()).size() == 1, "other components keep their contents in the item data");
});

// ---- inventory: the server decides what a screen shows and changes ----------------------------------

GF_REGISTER_SELFTEST("combat.inventory_screen_auth", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace inv = gf::inventory;
    DeclaredScope fixture(fixtureDeclaration());
    auto a = ctx.testPos(6011);
    auto b = ctx.testPos(6012);
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    ctx.require(ctx.setBlock(a, kContainerBlock) && ctx.setBlock(b, kContainerBlock), "two containers placed");
    auto* region = ctx.region();
    auto  ha     = inv::find(*region, gf::game::blockPos(a));
    auto  hb     = inv::find(*region, gf::game::blockPos(b));
    ctx.require(ha && hb, "both are containers");
    ha->set(a, 0, *items::make("minecraft:emerald", 3));
    hb->set(b, 0, *items::make("minecraft:diamond", 5));
    auto& p = ctx.player();
    gf::net::setTestCompanion(p, true);
    (void)gf::net::takeTestMessages(p);
    ctx.require(inv::openChestLike(p, a, 3, "container.chest"), "the server opened container A");
    auto ctxOf = [](GlobalPos const& pos, int rows, std::string const& title) {
        return JsonT{{"pos", {pos.dim, pos.x, pos.y, pos.z}}, {"rows", rows}, {"title", title}};
    };
    auto click = JsonT{{"op", "click"}, {"slot", 0}, {"button", "left"}, {"shift", true}};
    // a forged context: container B was never opened for this player
    gf::net::injectFromClient(p, "screen.action", {{"type", "inventory.chest"}, {"context", ctxOf(b, 3, "container.chest")}, {"action", click}});
    ctx.check(items::countOf(hb->get(b, 0)) == 5 && countInInventory(p, "minecraft:diamond") == 0, "an action on a container the server did not open is refused");
    auto msgs    = gf::net::takeTestMessages(p);
    bool refused = std::any_of(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == "screen.close"; });
    ctx.check(refused, "the forged screen is closed");
    // the opened container, with forged rows and title: the server's own are used
    ctx.require(inv::openChestLike(p, a, 3, "container.chest"), "container A opened again");
    (void)gf::net::takeTestMessages(p);
    gf::net::injectFromClient(p, "screen.action", {{"type", "inventory.chest"}, {"context", ctxOf(a, 6, "forged.title")}, {"action", click}});
    ctx.check(items::isEmpty(ha->get(a, 0)) && countInInventory(p, "minecraft:emerald") == 3, "the opened container's quick move works");
    msgs             = gf::net::takeTestMessages(p);
    JsonT updateData = nullptr;
    for (auto const& m : msgs)
        if (m.channel == "screen.update") updateData = m.payload.value("data", JsonT{});
    ctx.check(updateData.is_object() && updateData.value("rows", 0) == 3 && updateData.value("title", std::string{}) != "forged.title",
              "rows and title come from the server's record (" + (updateData.is_object() ? updateData.dump() : std::string("no update")) + ")");
    gf::net::setTestCompanion(p, false);
    hb->set(b, 0, ItemStack{});
});


// ---- inventory: the vanilla chest screen proxy holds no second copy of the slots ---------------------

namespace {

int countAt(std::optional<gf::inventory::Handler> const& h, GlobalPos const& pos, int slot) { return items::countOf(h->get(pos, slot)); }

} // namespace

// While a vanilla player has the game's chest screen open through the proxy, the proxy and the
// storage are one copy: a change on either side shows on the other at once, the same slot changed
// through the storage and the screen in one tick yields its items once, and a block broken in the
// tick of a take drops only what the storage still holds.
GF_REGISTER_SELFTEST("combat.inventory_vanilla_proxy", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace inv = gf::inventory;
    DeclaredScope fixture(fixtureDeclaration());
    auto pos = ctx.testPos(6014);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, kContainerBlock), "a 27-slot container placed");
    auto* region = ctx.region();
    auto  h      = inv::find(*region, gf::game::blockPos(pos));
    ctx.require(h.has_value(), "the container handler");
    h->set(pos, 0, *items::make("minecraft:emerald", 4));
    auto& p      = ctx.player();
    auto  id     = p.getOrCreateUniqueID();
    int   before = inv::vanillaProxyCount();
    ctx.require(inv::openChestLike(p, pos, 3, "container.chest"), "a vanilla player opened it");
    ctx.check(inv::vanillaProxyCount() == before + 1, "a chest minecart proxy stands in for the container");
    auto* proxy = inv::vanillaProxyContainer(pos);
    ctx.require(proxy != nullptr, "the proxy's container");
    ctx.check(items::countOf(proxy->getItem(0)) == 4, "the proxy shows the stored slots");
    if (auto* actor = inv::vanillaProxyActor(pos)) {
        auto const& box = actor->getAABB();
        ctx.check(box.max.x - box.min.x < .05f && box.max.y - box.min.y < .05f, "the proxy has no hitbox (other players reach the block)");
    }
    co_await ctx.ticks(6); // the screen opens after the proxy reached the client
    proxy = inv::vanillaProxyContainer(pos);
    ctx.require(proxy != nullptr, "the proxy is still open after the screen opened (" + std::to_string(inv::vanillaProxyCount()) + " open)");
    ctx.check(inv::viewerCount(pos) == 1, "the vanilla screen counts as an opener");
    // one copy: a click in the screen reaches the storage at once, a write through the handler the screen
    proxy->setItem(5, *items::make("minecraft:gold_ingot", 7));
    ctx.check(countAt(h, pos, 5) == 7, "a change in the screen reaches the storage in the same tick");
    h->set(pos, 6, *items::make("minecraft:apple", 2));
    ctx.check(items::countOf(proxy->getItem(6)) == 2, "a write through the handler reaches the screen in the same tick");
    // the same slot through the storage (the client mod's screen, a unit) and the screen in one
    // tick: the items exist once
    h->set(pos, 1, *items::make("minecraft:diamond", 64));
    auto takenByCompanion = h->extract(pos, 1, 64, false);
    auto inScreen         = proxy->getItem(1); // what the vanilla player could still take
    proxy->setItem(1, ItemStack{});
    int total = items::countOf(takenByCompanion) + items::countOf(inScreen) + countAt(h, pos, 1) + items::countOf(proxy->getItem(1));
    ctx.check(total == 64, "storage first, then the screen: 64 diamonds in all (" + std::to_string(total) + ")");
    h->set(pos, 2, *items::make("minecraft:diamond", 64));
    auto takenInScreen = proxy->getItem(2);
    proxy->setItem(2, ItemStack{}); // the vanilla player takes the stack
    auto takenAfter = h->extract(pos, 2, 64, false);
    total           = items::countOf(takenInScreen) + items::countOf(takenAfter) + countAt(h, pos, 2) + items::countOf(proxy->getItem(2));
    ctx.check(total == 64, "the screen first, then the storage: 64 diamonds in all (" + std::to_string(total) + ")");
    // code that writes the storage behind the handler: the screen follows within a tick
    auto raw = inv::rawHandlerFor(kContainerBlock);
    ctx.require(raw.has_value(), "the raw handler");
    raw->set(pos, 7, *items::make("minecraft:stick", 3));
    co_await ctx.ticks(2);
    proxy = inv::vanillaProxyContainer(pos);
    ctx.require(proxy != nullptr, "the proxy is still open");
    ctx.check(items::countOf(proxy->getItem(7)) == 3, "a write behind the handler reaches the screen within a tick");
    // the block breaks in the tick a stack is taken in the screen: only the storage's copy drops
    h->set(pos, 3, *items::make("minecraft:iron_ingot", 10));
    auto taken = proxy->getItem(3);
    proxy->setItem(3, ItemStack{}); // the vanilla player takes the iron
    ctx.require(gf::game::clearBlock(*region, gf::game::blockPos(pos)), "the container is broken");
    ctx.check(inv::vanillaProxyContainer(pos) == nullptr && inv::vanillaProxyCount() == before, "the proxy goes with the block at once");
    auto dropped = takeItemEntitiesNear(pos, 4);
    int  iron = countIn(dropped, "minecraft:iron_ingot"), emeralds = countIn(dropped, "minecraft:emerald");
    ctx.check(items::countOf(taken) == 10 && iron == 0, "the taken iron did not drop again (" + std::to_string(iron) + " dropped)");
    ctx.check(emeralds == 4 && countIn(dropped, "minecraft:gold_ingot") == 7 && countIn(dropped, "minecraft:stick") == 3, "the rest of the storage dropped once");
    co_await ctx.ticks(1);
    auto* again = ll::service::getLevel()->getPlayer(id);
    if (again) static_cast<ServerPlayer&>(*again).doDeleteContainerManager(false);
    (void)takeItemEntitiesNear(pos, 4);
});

// ---- inventory: Java's ChestMenu clicks on the client mod's screen -----------------------------------

namespace {

/// Sends a click of the client mod's chest screen as the client does.
void clickScreen(Player& p, GlobalPos const& pos, JsonT action) {
    action["op"] = "click";
    gf::net::injectFromClient(p, "screen.action", {{"type", "inventory.chest"}, {"context", {{"pos", {pos.dim, pos.x, pos.y, pos.z}}, {"rows", 3}, {"title", "container.chest"}}}, {"action", action}});
}

} // namespace

GF_REGISTER_SELFTEST("combat.inventory_clicks", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace inv = gf::inventory;
    DeclaredScope fixture(fixtureDeclaration());
    auto pos = ctx.testPos(6015);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, kContainerBlock), "a 27-slot container placed");
    auto* region = ctx.region();
    auto  h      = inv::find(*region, gf::game::blockPos(pos));
    ctx.require(h.has_value(), "the container handler");
    auto& p = ctx.player();
    clearInventory(p);
    gf::net::setTestCompanion(p, true);
    ctx.require(inv::openChestLike(p, pos, 3, "container.chest"), "the client mod's screen opened");
    auto carried = [&] { return inv::carriedOf(p); };
    // QUICK_MOVE: ChestMenu#quickMoveStack fills the inventory from its end (the last hotbar slot)
    h->set(pos, 0, *items::make("minecraft:stick", 10));
    clickScreen(p, pos, {{"slot", 0}, {"button", 0}, {"type", "quick_move"}});
    ctx.check(items::countOf(p.getInventory().getItem(8)) == 10 && countAt(h, pos, 0) == 0, "quick move puts the stack in the last hotbar slot");
    // QUICK_MOVE back: into the container from its start
    clickScreen(p, pos, {{"slot", 27 + 27 + 8}, {"button", 0}, {"type", "quick_move"}});
    ctx.check(countAt(h, pos, 0) == 10 && items::isEmpty(p.getInventory().getItem(8)), "quick move from the hotbar fills the container from its start");
    h->set(pos, 0, ItemStack{});
    // PICKUP right: half (rounded up) on the cursor
    h->set(pos, 1, *items::make("minecraft:apple", 5));
    clickScreen(p, pos, {{"slot", 1}, {"button", 1}, {"type", "pickup"}});
    ctx.check(items::countOf(carried()) == 3 && countAt(h, pos, 1) == 2, "a right click picks up half");
    // QUICK_CRAFT left over three slots: 3 / 3 each
    clickScreen(p, pos, {{"slot", -999}, {"button", 0}, {"type", "quick_craft"}, {"slots", {2, 3, 4}}});
    ctx.check(items::isEmpty(carried()) && countAt(h, pos, 2) == 1 && countAt(h, pos, 3) == 1 && countAt(h, pos, 4) == 1, "dragging splits the stack evenly");
    // PICKUP_ALL: pick one up, double-click: every apple joins it
    clickScreen(p, pos, {{"slot", 2}, {"button", 0}, {"type", "pickup"}});
    clickScreen(p, pos, {{"slot", 2}, {"button", 0}, {"type", "pickup_all"}});
    ctx.check(items::countOf(carried()) == 5 && countAt(h, pos, 1) == 0 && countAt(h, pos, 3) == 0 && countAt(h, pos, 4) == 0, "a double click collects the same items (" + std::to_string(items::countOf(carried())) + ")");
    // QUICK_CRAFT right: one each
    clickScreen(p, pos, {{"slot", -999}, {"button", 1}, {"type", "quick_craft"}, {"slots", {10, 11}}});
    ctx.check(items::countOf(carried()) == 3 && countAt(h, pos, 10) == 1 && countAt(h, pos, 11) == 1, "a right drag puts one in each slot");
    // a click outside the panel drops the cursor stack (right: one)
    clickScreen(p, pos, {{"slot", -999}, {"button", 1}, {"type", "pickup"}});
    ctx.check(items::countOf(carried()) == 2, "a right click outside drops one");
    clickScreen(p, pos, {{"slot", -999}, {"button", 0}, {"type", "pickup"}});
    ctx.check(items::isEmpty(carried()), "a left click outside drops the stack");
    // SWAP with a hotbar key and the off-hand key
    h->set(pos, 5, *items::make("minecraft:gold_ingot", 7));
    clickScreen(p, pos, {{"slot", 5}, {"button", 0}, {"type", "swap"}});
    ctx.check(items::countOf(p.getInventory().getItem(0)) == 7 && countAt(h, pos, 5) == 0, "key 1 swaps the slot with the first hotbar slot");
    clickScreen(p, pos, {{"slot", 6}, {"button", 0}, {"type", "swap"}});
    ctx.check(countAt(h, pos, 6) == 7 && items::isEmpty(p.getInventory().getItem(0)), "key 1 on an empty slot puts the hotbar stack in");
    clickScreen(p, pos, {{"slot", 6}, {"button", 40}, {"type", "swap"}});
    ctx.check(items::countOf(p.getOffhandSlot()) == 7 && countAt(h, pos, 6) == 0, "F swaps with the off-hand");
    // THROW: Q one, Ctrl+Q the rest
    h->set(pos, 7, *items::make("minecraft:flint", 4));
    clickScreen(p, pos, {{"slot", 7}, {"button", 0}, {"type", "throw"}});
    ctx.check(countAt(h, pos, 7) == 3, "Q throws one");
    clickScreen(p, pos, {{"slot", 7}, {"button", 1}, {"type", "throw"}});
    ctx.check(countAt(h, pos, 7) == 0, "Ctrl+Q throws the stack");
    // CLONE needs creative
    h->set(pos, 8, *items::make("minecraft:coal", 1));
    clickScreen(p, pos, {{"slot", 8}, {"button", 2}, {"type", "clone"}});
    ctx.check(items::isEmpty(carried()), "clone does nothing outside creative");
    co_await ctx.ticks(2);
    auto  again   = ll::service::getLevel()->getPlayer(p.getOrCreateUniqueID());
    auto  dropped = takeItemEntitiesNear(gf::game::globalPos(*region, gf::game::blockPosOf(again ? again->getPosition() : Vec3{})), 8);
    ctx.check(countIn(dropped, "minecraft:apple") == 3 && countIn(dropped, "minecraft:flint") == 4, "the dropped and thrown items are in the world (" + std::to_string(countIn(dropped, "minecraft:apple")) + " apples, " + std::to_string(countIn(dropped, "minecraft:flint")) + " flint)");
    if (again) {
        clearInventory(*again);
        gf::net::setTestCompanion(*again, false);
    }
    for (int i = 0; i < h->slots; ++i) h->set(pos, i, ItemStack{});
});

// ---- inventory: ChestBlock's rules for declared chests ----------------------------------------------

GF_REGISTER_SELFTEST("combat.inventory_chest_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace inv = gf::inventory;
    static int  sViewerCalls = 0;
    static int  sLastCount   = -1;
    static bool registered   = false;
    if (!registered) {
        registered = true;
        inv::onViewersChanged(kContainerBlock, [](GlobalPos const&, int n) {
            ++sViewerCalls;
            sLastCount = n;
        });
    }
    DeclaredScope fixture(fixtureDeclaration());
    auto pos = ctx.testPos(6016);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, kContainerBlock), "a chest placed");
    auto above = pos;
    above.y += 1;
    auto& p = ctx.player();
    gf::net::setTestCompanion(p, true);
    // ChestBlock#isChestBlockedAt: a redstone conductor above
    ctx.require(ctx.setBlock(above, "minecraft:stone"), "stone above the chest");
    ctx.check(inv::chestBlocked(pos) && !inv::openChestLike(p, pos, 3, "container.chest"), "a chest under stone does not open");
    ctx.require(ctx.setBlock(above, "minecraft:glass"), "glass above the chest");
    ctx.check(!inv::chestBlocked(pos), "glass is no conductor: the chest is free");
    ctx.require(ctx.setBlock(above, "minecraft:air"), "nothing above the chest");
    // a sitting cat above
    auto* region = ctx.region();
    Vec3  catAt{static_cast<float>(above.x) + .5f, static_cast<float>(above.y), static_cast<float>(above.z) + .5f};
    auto* cat = region->getLevel().getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:cat"), nullptr, catAt, false, false, false);
    if (cat) {
        SynchedActorDataAccess::setActorFlag(cat->getEntityContext(), ActorFlags::Sitting, true);
        ctx.check(inv::chestBlocked(pos), "a sitting cat on the chest blocks it");
        cat->remove();
    } else {
        ctx.info("no cat could be spawned: the cat rule is not checked");
    }
    // the openers: the open sound at the first, the close sound after the last
    auto sounds = inv::soundCounters();
    sViewerCalls = 0;
    ctx.require(inv::openChestLike(p, pos, 3, "container.chest"), "the free chest opens");
    ctx.check(inv::viewerCount(pos) == 1 && inv::viewers(pos) == 1, "one opener");
    ctx.check(inv::soundCounters().opened == sounds.opened + 1, "the open sound played");
    ctx.check(sViewerCalls >= 1 && sLastCount == 1, "the viewers listener heard the opener");
    gf::net::injectFromClient(p, "screen.closed", {{"type", "inventory.chest"}});
    ctx.check(inv::viewerCount(pos) == 0 && inv::soundCounters().closed == sounds.closed + 1, "closing played the close sound");
    ctx.check(sLastCount == 0, "the viewers listener heard the last opener leave");
    // double chests (the probe chest of the combat scenario: gf_combat:chest with type / facing)
    if (gf::game::blockWithStates("gf_combat:chest", {{"gf_combat:type", "right"}, {"gf_combat:facing", "north"}})) {
        DeclaredScope chest({.block = "gf_combat:chest", .rows = 3, .slots = 27, .title = "container.chest", .skin = "", .pairs = true});
        auto right = ctx.testPos(6018);
        auto left  = ctx.testPos(6017);
        ctx.require(ctx.setBlock(right, "gf_combat:chest", {{"gf_combat:type", "right"}, {"gf_combat:facing", "north"}}), "the right half");
        ctx.require(ctx.setBlock(left, "gf_combat:chest", {{"gf_combat:type", "left"}, {"gf_combat:facing", "north"}}), "the left half");
        auto parts = inv::partsOf(left);
        ctx.check(parts.size() == 2 && parts[0] == right && parts[1] == left, "the halves pair, the right one first");
        auto hr = inv::find(*region, gf::game::blockPos(right));
        auto hl = inv::find(*region, gf::game::blockPos(left));
        ctx.require(hr && hl, "both halves are containers");
        hr->set(right, 0, *items::make("minecraft:redstone", 2));
        hl->set(left, 0, *items::make("minecraft:lapis_lazuli", 3));
        (void)gf::net::takeTestMessages(p);
        ctx.require(inv::openChestLike(p, left, 3, "container.chest"), "the double chest opens");
        auto  msgs = gf::net::takeTestMessages(p);
        JsonT data;
        for (auto const& m : msgs)
            if (m.channel == "screen.open") data = m.payload.value("data", JsonT{});
        ctx.check(data.value("rows", 0) == 6 && data.value("slots", 0) == 54, "a six-row screen of 54 slots");
        auto shown = data.value("items", JsonT::array());
        ctx.check(shown.size() == 54 && shown[0].is_object() && shown[0].value("id", std::string{}) == "minecraft:redstone" && shown[27].is_object()
                      && shown[27].value("id", std::string{}) == "minecraft:lapis_lazuli",
                  "the right half's slots first, then the left half's");
        ctx.check(inv::viewerCount(right) == 1 && inv::viewerCount(left) == 1, "both halves count the opener");
        // a click on slot 27 is the left half's slot 0
        auto pctx = JsonT{{"pos", {left.dim, left.x, left.y, left.z}}, {"rows", 6}, {"title", "container.chestDouble"}};
        gf::net::injectFromClient(p, "screen.action", {{"type", "inventory.chest"}, {"context", pctx}, {"action", {{"op", "click"}, {"slot", 27}, {"button", 0}, {"type", "quick_move"}}}});
        ctx.check(items::isEmpty(hl->get(left, 0)) && countInInventory(p, "minecraft:lapis_lazuli") == 3, "slot 27 is the left half's first slot");
        gf::net::injectFromClient(p, "screen.closed", {{"type", "inventory.chest"}});
        hr->set(right, 0, ItemStack{});
    } else {
        ctx.info("no gf_combat:chest probe block in this run (the combat scenario adds it): pairing is not checked");
    }
    clearInventory(p);
    gf::net::setTestCompanion(p, false);
});

// ---- death: the kept_on_death tag keeper, stale kept records -------------------------------------------

GF_REGISTER_SELFTEST("combat.keepers_tag", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace tags = gf::tags;
    auto pos = ctx.testPos(6020);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    // a real member of TF's tag: the keeper asks F9's tag data (gf::tags), no test lookup
    auto        members = tags::members(tags::Registry::Item, "twilightforest:kept_on_death");
    std::string kept;
    for (auto const& m : members)
        if (items::make(m, 1)) {
            kept = m;
            break;
        }
    ctx.require(!kept.empty(), "twilightforest:kept_on_death has an item member in F9's tag data (" + std::to_string(members.size()) + " members)");
    ctx.check(tags::has(tags::Registry::Item, "twilightforest:kept_on_death", kept), "gf::tags::has answers the member");
    auto& sp = ctx.spawnPlayer("gf_combat_tag_keeper");
    auto  id = sp.getOrCreateUniqueID();
    auto  stack = items::make(kept, 1);
    ctx.require(stack.has_value(), "the kept item exists");
    ctx.give(sp, *stack);
    gf::combat::hurt(sp, 1000.f, "twilightforest:expired");
    co_await ctx.ticks(2);
    auto* p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player is still in the level");
    auto record = db::get("gf:combat:kept:" + p->getUuid().asString());
    bool kept1  = record && record->is_array() && std::any_of(record->begin(), record->end(), [&](JsonT const& e) {
        return e.value("item", JsonT::object()).value("id", std::string{}) == kept;
    });
    ctx.check(kept1, "the twilightforest:kept_on_death keeper kept " + kept);
    ctx.check(itemEntitiesNear(pos, 6) == 0, "the kept item did not drop");
    static_cast<SimulatedPlayer&>(*p).simulateRespawn();
    co_await ctx.ticks(10);
    p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player respawned");
    ctx.check(countInInventory(*p, kept) == 1, "the respawn returned it");
});

// A kept record whose stacks are still in the inventory (the server stopped between the death and
// the player's next save) is a stale copy: it is dropped, never returned (no duplicate).
GF_REGISTER_SELFTEST("combat.keepers_stale", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(6021);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = ctx.spawnPlayer("gf_combat_stale");
    clearInventory(sp);
    auto stack = items::make("gf_test:fixture_item", 5);
    ctx.require(stack.has_value(), "the fixture item");
    sp.getInventory().setItem(3, *stack);
    auto key = "gf:combat:kept:" + sp.getUuid().asString();
    db::put(key, JsonT::array({{{"slot", "inv:3"}, {"item", items::toJson(*stack)}}}));
    ctx.check(!gf::death::returnKeptItems(sp), "a record of stacks still in their slots is not returned");
    ctx.check(countInInventory(sp, "gf_test:fixture_item") == 5, "no duplicate (" + std::to_string(countInInventory(sp, "gf_test:fixture_item")) + ")");
    ctx.check(!db::get(key), "the stale record is gone");
    // a real record (the slot is empty after the death) comes back
    sp.getInventory().setItem(3, ItemStack{});
    db::put(key, JsonT::array({{{"slot", "inv:3"}, {"item", items::toJson(*stack)}}}));
    ctx.check(gf::death::returnKeptItems(sp) && countInInventory(sp, "gf_test:fixture_item") == 5, "a record of emptied slots is returned");
    clearInventory(sp);
});

// ---- loot: slime size, enchanting, container luck --------------------------------------------------

GF_REGISTER_SELFTEST("combat.loot_slime_size", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace loot = gf::loot;
    auto pos = ctx.testPos(6019);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    Vec3  at{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + .5f};
    // slimes are hostile: no spawning on peaceful (the difficulty comes back right after)
    auto& level      = region->getLevel();
    auto  difficulty = level.getDifficulty();
    if (difficulty == SharedTypes::Legacy::Difficulty::Peaceful) level.setDifficulty(SharedTypes::Legacy::Difficulty::Easy);
    auto* small  = level.getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft", "slime", "minecraft:spawn_small"), nullptr, at, false, false, false);
    auto* medium = level.getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft", "slime", "minecraft:spawn_medium"), nullptr, at, false, false, false);
    auto* cow    = level.getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:cow"), nullptr, at, false, false, false);
    // the mobs go and the difficulty comes back at the end, whatever happens (a peaceful world
    // removes hostile mobs: they are looked up by id)
    auto idOf = [](Actor* a) { return a ? a->getOrCreateUniqueID().rawID : std::int64_t{0}; };
    struct Restore {
        Level&                          level;
        SharedTypes::Legacy::Difficulty difficulty;
        std::vector<std::int64_t>       ids;
        ~Restore() {
            for (auto raw : ids) {
                ActorUniqueID id;
                id.rawID = raw;
                if (auto* a = raw ? level.fetchEntity(id, false) : nullptr) a->remove();
            }
            if (level.getDifficulty() != difficulty) level.setDifficulty(difficulty);
        }
    } restore{level, difficulty, {idOf(small), idOf(medium), idOf(cow)}};
    ctx.require(small && medium && cow, std::string("a small and a medium slime and a cow (") + (small ? "small " : "") + (medium ? "medium " : "") + (cow ? "cow" : "") + ")");
    // the slime's own size events (what its splitting runs)
    auto sizeEvent = [](Actor& a, std::string const& size) {
        VariantParameterList params{};
        params.mSelf = &a;
        return gf::game::compat::executeEvent(a, "minecraft:spawn_" + size, params) || gf::game::compat::executeEvent(a, "spawn_" + size, params);
    };
    ctx.require(sizeEvent(*small, "small") && sizeEvent(*medium, "medium"), "the slimes took their size events");
    co_await ctx.ticks(2);
    auto fetch = [&](std::int64_t raw) {
        ActorUniqueID id;
        id.rawID = raw;
        return level.fetchEntity(id, false);
    };
    small  = static_cast<Mob*>(fetch(restore.ids[0]));
    medium = static_cast<Mob*>(fetch(restore.ids[1]));
    cow    = static_cast<Mob*>(fetch(restore.ids[2]));
    ctx.require(small && medium && cow, "the mobs are still there");
    ctx.check(loot::slimeSize(*small) == 1 && loot::slimeSize(*medium) == 2, "the slime sizes are read from the variant (" + std::to_string(loot::slimeSize(*small)) + ", " + std::to_string(loot::slimeSize(*medium)) + ")");
    auto sizeOne = JsonT::parse(R"({"type_specific": {"type": "minecraft:slime", "size": 1}})");
    auto range   = JsonT::parse(R"({"type_specific": {"type": "minecraft:slime", "size": {"min": 2, "max": 4}}})");
    ctx.check(loot::entityMatches(small, sizeOne) && !loot::entityMatches(medium, sizeOne), "maze_slime's size 1 condition: only the small slime");
    ctx.check(!loot::entityMatches(small, range) && loot::entityMatches(medium, range), "a size range");
    ctx.check(!loot::entityMatches(cow, sizeOne), "a cow is no slime");
});

GF_REGISTER_SELFTEST("combat.loot_enchant", [](Context& ctx) {
    namespace loot = gf::loot;
    // set_enchantments: 0 removes; a book turns into an enchanted book first
    loot::registerTable("gf_test:combat/unenchant", JsonT::parse(R"({"pools":[
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:book","functions":[
          {"function":"minecraft:set_enchantments","enchantments":{"minecraft:sharpness":0}}]}]},
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:iron_sword","functions":[
          {"function":"minecraft:set_enchantments","enchantments":{"minecraft:sharpness":3,"minecraft:looting":2}},
          {"function":"minecraft:set_enchantments","enchantments":{"minecraft:sharpness":0}}]}]}]})"));
    loot::LootContext lc;
    lc.seed = 11;
    auto l  = loot::roll("gf_test:combat/unenchant", lc);
    loot::registerTable("gf_test:combat/unenchant", nullptr);
    ItemStack const* book  = nullptr;
    ItemStack const* sword = nullptr;
    for (auto const& s : l) {
        if (items::idOf(s) == "minecraft:enchanted_book") book = &s;
        if (items::idOf(s) == "minecraft:iron_sword") sword = &s;
    }
    ctx.check(book != nullptr, "set_enchantments turns a book into an enchanted book even with level 0");
    ctx.require(sword != nullptr, "the sword dropped");
    // the levels set first (before the removal)
    loot::registerTable("gf_test:combat/enchant_set", JsonT::parse(R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:iron_sword","functions":[
          {"function":"minecraft:set_enchantments","enchantments":{"minecraft:sharpness":3,"minecraft:looting":2}}]}]}]})"));
    auto set = loot::roll("gf_test:combat/enchant_set", lc);
    loot::registerTable("gf_test:combat/enchant_set", nullptr);
    ctx.check(set.size() == 1 && loot::enchantLevel(set.front(), "minecraft:sharpness") == 3 && loot::enchantLevel(set.front(), "minecraft:looting") == 2,
              "set_enchantments sets the levels (sharpness " + std::to_string(set.empty() ? -1 : loot::enchantLevel(set.front(), "minecraft:sharpness")) + ")");
    ctx.check(loot::enchantLevel(*sword, "minecraft:sharpness") == 0 && loot::enchantLevel(*sword, "minecraft:looting") == 2,
              "level 0 removes sharpness and keeps looting (sharpness " + std::to_string(loot::enchantLevel(*sword, "minecraft:sharpness")) + ", looting "
                  + std::to_string(loot::enchantLevel(*sword, "minecraft:looting")) + ")");
    // enchant_with_levels / #minecraft:on_random_loot: Java's selection
    auto ironSword = items::make("minecraft:iron_sword", 1);
    auto bookStack = items::make("minecraft:book", 1);
    ctx.require(ironSword && bookStack, "an iron sword and a book");
    std::set<std::string> const pool{"minecraft:sharpness", "minecraft:smite", "minecraft:bane_of_arthropods", "minecraft:knockback", "minecraft:fire_aspect",
                                     "minecraft:looting", "minecraft:sweeping_edge", "minecraft:unbreaking", "minecraft:mending", "minecraft:vanishing_curse"};
    int  picks = 0, outside = 0, unseeded = 0;
    bool same  = true;
    for (int seed = 1; seed <= 200; ++seed) {
        gf::loot::JavaRandom r1(seed), r2(seed);
        auto                 a = loot::selectEnchantment(r1, *ironSword, 30, "#minecraft:on_random_loot");
        auto                 b = loot::selectEnchantment(r2, *ironSword, 30, "#minecraft:on_random_loot");
        same = same && a == b;
        if (a.empty()) ++unseeded;
        for (auto const& [id, level] : a) {
            ++picks;
            if (!pool.contains(id) || level < 1) ++outside;
        }
    }
    ctx.check(outside == 0, "an iron sword only gets the sword's on_random_loot enchantments (" + std::to_string(outside) + " outside)");
    ctx.check(unseeded == 0 && picks >= 200, "level 30 always enchants (" + std::to_string(picks) + " picks)");
    ctx.check(same, "a seeded selection is reproducible");
    std::set<std::string> bookPicks;
    for (int seed = 1; seed <= 300; ++seed) {
        gf::loot::JavaRandom r(seed);
        for (auto const& [id, _] : loot::selectEnchantment(r, *bookStack, 30, "#minecraft:on_random_loot")) bookPicks.insert(id);
    }
    ctx.check(!bookPicks.contains("minecraft:soul_speed") && !bookPicks.contains("minecraft:swift_sneak") && !bookPicks.contains("minecraft:wind_burst"),
              "soul speed, swift sneak and wind burst are not in #on_random_loot");
    ctx.check(bookPicks.contains("minecraft:mending") || bookPicks.contains("minecraft:frost_walker"), "a book can get the treasure enchantments the tag lists");
    // Java's numbers: a book at level 0 (enchantability 1: 0 + 1 + nextInt(1) + nextInt(1) = 1, the
    // +-15 % spread rounds back to 1): only enchantments with a level-1 cost range holding 1
    for (int seed = 1; seed <= 50; ++seed) {
        gf::loot::JavaRandom low(seed);
        for (auto const& [id, level] : loot::selectEnchantment(low, *bookStack, 0, "#minecraft:on_random_loot"))
            ctx.check(level == 1 && (id == "minecraft:protection" || id == "minecraft:efficiency" || id == "minecraft:power" || id == "minecraft:piercing"
                                     || id == "minecraft:impaling" || id == "minecraft:sharpness" || id == "minecraft:aqua_affinity"),
                      "a level-1 selection only has cost ranges holding 1 (" + id + " " + std::to_string(level) + ")");
    }
    // the function on a table
    loot::registerTable("gf_test:combat/enchanted", JsonT::parse(R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:iron_sword","functions":[
        {"function":"minecraft:enchant_with_levels","levels":30,"options":"#minecraft:on_random_loot"}]}]}]})"));
    lc.seed     = 77;
    auto rolled = loot::roll("gf_test:combat/enchanted", lc);
    loot::registerTable("gf_test:combat/enchanted", nullptr);
    ctx.check(rolled.size() == 1 && const_cast<ItemStack&>(rolled.front()).isEnchanted(), "enchant_with_levels enchants the sword");
});

GF_REGISTER_SELFTEST("combat.loot_container_luck", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace loot = gf::loot;
    // RandomizableContainer#unpackLootTable: no killer player in the context
    loot::registerTable("gf_test:combat/opened", JsonT::parse(R"({"type":"minecraft:chest","pools":[
        {"rolls":1,"conditions":[{"condition":"minecraft:killed_by_player"}],"entries":[{"type":"minecraft:item","name":"minecraft:stick"}]},
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:dirt"}]}]})"));
    auto pos = ctx.testPos(6022);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    auto  bp     = gf::game::blockPos(pos);
    ctx.cleanupBlock(pos);
    ctx.require(loot::placeContainer(*region, bp, "minecraft:chest", 2, "gf_test:combat/opened", 5), "a chest with the table");
    auto& p = ctx.player();
    ctx.check(loot::luckOf(&p) == 0.f, "a player's luck is the luck attribute (0)");
    ctx.require(loot::unpackVanilla(*region, bp, &p), "the opener unpacks it");
    auto* actor = region->getBlockEntity(bp);
    auto* c     = actor ? static_cast<ChestBlockActor*>(actor)->getContainer() : nullptr;
    ctx.require(c != nullptr, "the chest's container");
    int sticks = 0, dirt = 0;
    for (int i = 0; i < c->getContainerSize(); ++i) {
        sticks += items::idOf(c->getItem(i)) == "minecraft:stick" ? items::countOf(c->getItem(i)) : 0;
        dirt += items::idOf(c->getItem(i)) == "minecraft:dirt" ? items::countOf(c->getItem(i)) : 0;
        c->setItem(i, ItemStack{});
    }
    ctx.check(sticks == 0 && dirt == 1, "killed_by_player fails for a chest opener");
    loot::registerTable("gf_test:combat/opened", nullptr);
});

// ---- combat: a damage type's effect sound replaces the hurt sound ---------------------------------------

GF_REGISTER_SELFTEST("combat.hurt_sound", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(6023);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp     = ctx.spawnPlayer("gf_combat_hurt_sound");
    int   before = gf::combat::hurtSoundsReplaced();
    gf::combat::hurt(sp, 1.f, "twilightforest:chilling_breath"); // freezing
    ctx.check(gf::combat::hurtSoundsReplaced() == before + 1, "the Hurt event of the freezing damage carries the freezing cause (the client's freezing hurt sound)");
    co_await ctx.ticks(11); // the hurt cooldown
    auto* again = ll::service::getLevel()->getPlayer(sp.getOrCreateUniqueID());
    ctx.require(again != nullptr, "the player is still there");
    auto packets = gf::fx::counters().soundPackets;
    gf::combat::hurt(*again, 1.f, "twilightforest:clamped"); // TF's pinch
    ctx.check(gf::combat::hurtSoundsReplaced() == before + 1, "the pinch has no cause of its own: the event stays");
    if (!gf::fx::resolveSound("twilightforest:entity.twilightforest.pinch_beetle.attack").empty())
        ctx.check(gf::fx::counters().soundPackets > packets, "the pinch beetle's sound plays through fx");
    else ctx.info("the pinch beetle's sound is not in this build's resource pack (F3's audio): its packet is not checked");
    co_await ctx.ticks(11);
    again = ll::service::getLevel()->getPlayer(sp.getOrCreateUniqueID());
    ctx.require(again != nullptr, "the player is still there");
    gf::combat::hurt(*again, 1.f, "twilightforest:hydra_bite"); // "hurt": the game's own sound
    ctx.check(gf::combat::hurtSoundsReplaced() == before + 1, "plain damage keeps the game's hurt sound");
});

// ---- inventory: the vanilla screen proxy against writes behind the handler, a screen opened again -----

// A unit with its own storage changes a slot behind its handler while a vanilla player looks at the
// proxy: the player's next item request first takes in the storage's slots; a take of the stale stack
// in the same tick keeps the storage's stack (nothing exists twice); a player who closed the screen
// and uses the block again in the same tick gets the screen again.
GF_REGISTER_SELFTEST("combat.inventory_vanilla_conflict", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace inv = gf::inventory;
    DeclaredScope fixture(fixtureDeclaration());
    auto          pos = ctx.testPos(6024);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, kContainerBlock), "a 27-slot container placed");
    auto* region = ctx.region();
    auto  h      = inv::find(*region, gf::game::blockPos(pos));
    auto  raw    = inv::rawHandlerFor(kContainerBlock);
    ctx.require(h.has_value() && raw.has_value(), "the container handlers");
    auto& p      = ctx.player();
    auto  id     = p.getOrCreateUniqueID();
    int   before = inv::vanillaProxyCount();
    ctx.require(inv::openChestLike(p, pos, 3, "container.chest"), "a vanilla player opened it");
    co_await ctx.ticks(6); // the screen opens after the proxy reached the client
    auto* proxy = inv::vanillaProxyContainer(pos);
    ctx.require(proxy != nullptr, "the proxy is open");
    // the storage changes behind the handler and the screen takes the stale stack in the same tick
    h->set(pos, 0, *items::make("minecraft:diamond", 64));
    raw->set(pos, 0, *items::make("minecraft:emerald", 10)); // the unit's own storage (the diamonds handed out)
    proxy->setItem(0, ItemStack{});                          // the vanilla player takes the stale diamonds
    auto stored = h->get(pos, 0);
    ctx.check(items::idOf(stored) == "minecraft:emerald" && items::countOf(stored) == 10,
              "the storage's stack stays (" + items::idOf(stored) + " x" + std::to_string(items::countOf(stored)) + ")");
    ctx.check(items::idOf(proxy->getItem(0)) == "minecraft:emerald" && items::countOf(proxy->getItem(0)) == 10, "the proxy slot is reset to the storage's stack");
    // a change behind the handler reaches the proxy before the viewer's next item request
    raw->set(pos, 1, *items::make("minecraft:stick", 5));
    ctx.check(items::isEmpty(proxy->getItem(1)), "a write behind the handler is not mirrored by itself");
    inv::proxyBeforeScreenRequest(p);
    ctx.check(items::countOf(proxy->getItem(1)) == 5, "the item request hook pulls the storage in first, in the same tick");
    proxy->setItem(1, *items::make("minecraft:stick", 2));
    ctx.check(countAt(h, pos, 1) == 2, "a screen change after the pull reaches the storage");
    // the player closes the screen and uses the block again before the next tick
    static_cast<ServerPlayer&>(p).doDeleteContainerManager(false);
    ctx.require(inv::openChestLike(p, pos, 3, "container.chest"), "used again in the tick it closed");
    co_await ctx.ticks(4);
    auto* again = ll::service::getLevel()->getPlayer(id);
    ctx.require(again != nullptr, "the player is still there");
    ctx.check(inv::vanillaProxyCount() == before + 1 && inv::proxyViewers(pos).contains(id.rawID), "the session goes on with the player in it");
    ctx.check(again->mContainerManager.get() != nullptr, "the chest screen opened again");
    for (int i = 0; i < h->slots; ++i) h->set(pos, i, ItemStack{});
    static_cast<ServerPlayer&>(*again).doDeleteContainerManager(false);
    co_await ctx.ticks(2);
    ctx.check(inv::vanillaProxyCount() == before, "the proxy goes when the screen closes");
});

// ---- inventory: comparator output of declared containers ---------------------------------------------

// ChestBlock#getAnalogOutputSignal: AbstractContainerMenu#getRedstoneSignalFromContainer, read by a
// vanilla comparator through the redstone contract.
GF_REGISTER_SELFTEST("combat.inventory_comparator", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace inv = gf::inventory;
    namespace rs  = gf::redstone;
    DeclaredScope fixture(fixtureDeclaration());
    auto          pos  = ctx.testPos(6025);
    auto          comp = ctx.testPos(6026);
    auto          out  = ctx.testPos(6027);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, kContainerBlock), "a 27-slot container placed");
    // a vanilla comparator east of it (its rear faces west, towards the container), stone in front
    bool placed = ctx.setBlock(comp, "minecraft:unpowered_comparator", {{"minecraft:cardinal_direction", "west"}})
               || ctx.setBlock(comp, "minecraft:unpowered_comparator", {{"direction", 1}});
    ctx.require(placed, "a comparator behind the container");
    ctx.require(ctx.setBlock(out, "minecraft:stone"), "a block in front of the comparator");
    auto h = inv::find(*ctx.region(), gf::game::blockPos(pos));
    ctx.require(h.has_value(), "the container handler");
    auto signal = [&] { return rs::analogOutput(*ctx.region(), gf::game::blockPos(pos)).value_or(-1); };
    ctx.check(signal() == 0, "an empty container gives 0 (" + std::to_string(signal()) + ")");
    // Mth.lerpDiscrete(sum(count / max stack) / slots, 0, 15); one reading per position and tick
    h->set(pos, 0, *items::make("minecraft:cobblestone", 64));
    co_await ctx.ticks(1);
    ctx.check(signal() == 1, "one full stack in 27 slots gives 1 (" + std::to_string(signal()) + ")");
    for (int i = 0; i < 14; ++i) h->set(pos, i, *items::make("minecraft:cobblestone", 64));
    co_await ctx.ticks(1);
    ctx.check(signal() == 8, "14 full stacks give 8 (" + std::to_string(signal()) + ")");
    for (int i = 0; i < 27; ++i) h->set(pos, i, *items::make("minecraft:ender_pearl", 16));
    co_await ctx.ticks(1);
    ctx.check(signal() == 15, "27 stacks of 16 pearls (their own limit) give 15 (" + std::to_string(signal()) + ")");
    ctx.check(rs::comparatorInput(*ctx.region(), gf::game::blockPos(comp)).value_or(-1) == 15, "the comparator behind it reads 15");
    co_await ctx.ticks(10);
    int output = rs::signalFrom(*ctx.region(), gf::game::blockPos(comp), gf::game::blockPos(out));
    ctx.check(output == 15, "the vanilla comparator re-evaluated and puts out 15 (" + std::to_string(output) + ")");
    // ChestBlock#getContainer finds none while the chest is blocked: 0
    auto above = pos;
    above.y += 1;
    ctx.require(ctx.setBlock(above, "minecraft:stone"), "stone above the chest");
    co_await ctx.ticks(1);
    ctx.check(signal() == 0, "a blocked chest gives 0 (" + std::to_string(signal()) + ")");
    ctx.require(ctx.setBlock(above, "minecraft:air"), "nothing above the chest");
    // RandomizableContainer#getItem: reading the slots rolls a pending table first
    for (int i = 0; i < 27; ++i) h->set(pos, i, ItemStack{});
    gf::loot::registerTable("gf_test:combat/comparator", JsonT::parse(R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:emerald","functions":[{"function":"minecraft:set_count","count":3}]}]}]})"));
    h->setLootTable(pos, "gf_test:combat/comparator", 9);
    co_await ctx.ticks(1);
    ctx.check(signal() == 1 && !inv::pendingLoot(pos), "reading the signal unpacked the table (" + std::to_string(signal()) + ")");
    gf::loot::registerTable("gf_test:combat/comparator", nullptr);
    for (int i = 0; i < 27; ++i) h->set(pos, i, ItemStack{});
    // a double chest: both halves read the 54 slots together
    if (gf::game::blockWithStates("gf_combat:chest", {{"gf_combat:type", "right"}, {"gf_combat:facing", "north"}})) {
        DeclaredScope chest({.block = "gf_combat:chest", .rows = 3, .slots = 27, .title = "container.chest", .skin = "", .pairs = true});
        auto          left  = ctx.testPos(6028);
        auto          right = ctx.testPos(6029);
        ctx.require(ctx.setBlock(right, "gf_combat:chest", {{"gf_combat:type", "right"}, {"gf_combat:facing", "north"}}), "the right half");
        ctx.require(ctx.setBlock(left, "gf_combat:chest", {{"gf_combat:type", "left"}, {"gf_combat:facing", "north"}}), "the left half");
        auto hr = inv::find(*ctx.region(), gf::game::blockPos(right));
        ctx.require(hr.has_value(), "the right half is a container");
        for (int i = 0; i < 27; ++i) hr->set(right, i, *items::make("minecraft:cobblestone", 64));
        co_await ctx.ticks(1);
        auto l = inv::comparatorSignal(left).value_or(-1), r = inv::comparatorSignal(right).value_or(-1);
        ctx.check(l == 8 && r == 8, "27 full stacks in a double chest give 8 at both halves (" + std::to_string(l) + ", " + std::to_string(r) + ")");
        for (int i = 0; i < 27; ++i) hr->set(right, i, ItemStack{});
    } else {
        ctx.info("no gf_combat:chest probe block in this run (the combat scenario adds it): the double chest is not checked");
    }
});

// ---- death: a mob's handled death is forgotten once it is gone -----------------------------------------

GF_REGISTER_SELFTEST("combat.death_entries", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(6030);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    Vec3  at{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + .5f};
    auto* mob = region->getLevel().getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:bat"), nullptr, at, false, false, false);
    ctx.require(mob != nullptr, "a bat spawned (no drops; the test world is peaceful)");
    auto id = mob->getOrCreateUniqueID().rawID;
    gf::combat::hurt(*mob, 1000.f, "twilightforest:thrown_axe");
    ctx.check(gf::death::deathHandled(id), "the death is kept while the dead mob is in the level");
    co_await ctx.ticks(70); // the death animation (20 ticks), then the sweep (every 20 ticks)
    ctx.check(!gf::death::deathHandled(id), "the entry went with the mob (" + std::to_string(gf::death::handledDeathCount()) + " kept)");
    (void)takeItemEntitiesNear(pos, 4);
});