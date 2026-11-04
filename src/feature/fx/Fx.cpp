// The effect part of "rules+fx" (F10): particles and sounds by id (rule R10).
//
// Particles: TF's ids ("twilightforest:firefly") and the vanilla particle types TF spawns
// ("minecraft:flame", Java ParticleTypes) are played for the players near the position (Java's
// ServerLevel#sendParticles range, 32 blocks):
//   - companions (client mod) get "fx.particle" {id, pos, vel, count, params}: the client mod
//     spawns the native particle (F12's particles, through feature/ext/combat/FxNative.h) or, when
//     none is registered, the resource pack's effect;
//   - vanilla clients get SpawnParticleEffectPackets of the resource pack's particle effect with
//     the same identifier (vanilla particle types are mapped to Bedrock's effects): one per
//     particle up to 8 per call (kMaxVanillaParticles), offset by params "spread" (Java's
//     gaussian x distance), with the velocity as the molang variable `variable.velocity` {x, y, z}
//     (FxMolang.cpp; an effect that does not read it moves on its own) and the variables the
//     mapped vanilla effect reads (`variable.direction`, `variable.color`, `variable.note_color`;
//     see vanillaParticles). Java's item and block particles go as Bedrock's legacy particles.
// Sounds: TF's sound events ("entity.twilightforest.naga.hiss", the sound_definitions names F3
// generates) and the vanilla events TF plays ("minecraft:entity.ghast.shoot", mapped to Bedrock's
// names) go out as PlaySoundPacket to the players in range (16 blocks, more for volume > 1, as
// Java's Level#playSound).
// An id the resource pack does not define is silent, with one warning per id.

#include "feature/fx/Fx.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/comprehensive/ParticleType.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/network/packet/LevelEventPacket.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/fx/FxInternal.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::fx {

namespace {

// Java vanilla particle types TF spawns -> Bedrock particle effects (best match), with the molang
// variables the effect reads (Bedrock's vanilla effects of 1.26.51; an effect that reads a variable
// nobody set logs a content-log error per particle) and where Bedrock draws a Java type through a
// legacy particle (LevelEventPacket) instead of an effect.
//   - portal: portal_directional is Java's PortalParticle (the same lifetime, size and path:
//     start + direction * (1 - (2a^2 - a)) at age fraction a) except that Java adds (1 - a) to y
//     where Bedrock subtracts a: spawned one block higher, it follows Java's path exactly (from
//     pos + speed + (0, 1, 0) down to pos). Its colour is not Java's: with f = 0.4..1 Bedrock tints
//     (f, 0.3f, 0.9f) where Java tints (0.9f, 0.3f, f), so the specks are pinker than Java's
//     blue-violet. No vanilla effect has the portal path with a settable colour (mob_portal and
//     portal_east_west compute their own); exact colours need an own copy of the effect in the
//     resource pack, a path F10 does not own yet (owners.rules.json has no
//     assets/pack/rp/**/combat/** for F10).
//   - enchant: enchanting_table_particle flies from pos + direction to pos (FlyTowardsPositionParticle).
//   - electric_spark: the effect's shape direction is Java's velocity.
//   - splash: the direction is SplashParticle's motion (splashDirection: the x/z speed with y 0.1
//     when ySpeed is 0, WaterDropParticle's random motion otherwise).
//   - crit: the direction is CritParticle's motion (critDirection: Particle's random motion of a
//     still particle * 0.1 plus 0.4 * the speed), so a still crit scatters as in Java.
//   - poof: explosion_particle's speed is -variable.velocity * 10 blocks/s plus its own noise
//     (always set): it gets -2 * velocity, Java's ExplodeParticle speed of velocity blocks/tick.
//   - dust: DustParticleOptions' colour (params "color", red by default) picks the particle
//     (dustFor): redstone red is Bedrock's legacy RedDust (always red: its data is no colour, a
//     client check showed purple data drawn red), any other colour the falling_dust effect tinted
//     with it (a small square drifting for 1.4..7 s, Java's DustParticle lives 0.4..2 s and hovers).
//     The scale is not carried.
//   - note: note_particle's colour is NoteParticle's hue from the x speed.
//   - entity_effect / effect: mobspell_emitter's colour (params "color", white by default).
//   - falling_dust: the block's map colour (params "block") or params "color".
//   - crimson_spore / white_ash: Bedrock has no effect for them (its own are drawn by the engine
//     only for its biomes): falling_dust tinted with Java's colours, a small particle drifting down.
//   - item / block: Bedrock's item and block break particles take the texture from the engine
//     (legacy IconCrack and Terrain with params "item" / "block"); item_snowball and item_slime are
//     the legacy SnowballPoof and Slime. Legacy particles carry no velocity.
std::unordered_map<std::string, VanillaParticle> const& vanillaParticles() {
    static Vec3 const                                                   up{0.f, 1.f, 0.f};
    static std::unordered_map<std::string, VanillaParticle> const m{
        {"minecraft:smoke", {"minecraft:basic_smoke_particle"}},
        {"minecraft:large_smoke", {"minecraft:large_explosion"}},
        {"minecraft:flame", {"minecraft:basic_flame_particle"}},
        {"minecraft:small_flame", {"minecraft:candle_flame_particle"}},
        {"minecraft:poof", {"minecraft:explosion_particle", kVarVelocity | kVarExplosionVelocity}},
        {"minecraft:explosion", {"minecraft:large_explosion"}},
        {"minecraft:splash", {"minecraft:water_splash_particle", kVarDirection | kVarSplashDirection}},
        {"minecraft:rain", {"minecraft:rain_splash_particle"}},
        {"minecraft:falling_water", {"minecraft:water_drip_particle"}},
        {"minecraft:crit", {"minecraft:basic_crit_particle", kVarDirection | kVarCritDirection}},
        {"minecraft:cloud", {"minecraft:white_smoke_particle"}},
        {"minecraft:note", {"minecraft:note_particle", kVarNoteColor}},
        {"minecraft:item_snowball", {"", 0, {}, static_cast<int>(ParticleType::SnowballPoof)}},
        {"minecraft:item_slime", {"", 0, {}, static_cast<int>(ParticleType::Slime)}},
        {"minecraft:happy_villager", {"minecraft:villager_happy"}},
        {"minecraft:angry_villager", {"minecraft:villager_angry"}},
        {"minecraft:entity_effect", {"minecraft:mobspell_emitter", kVarColorRgba}},
        {"minecraft:effect", {"minecraft:mobspell_emitter", kVarColorRgba}},
        {"minecraft:witch", {"minecraft:witchspell_emitter"}},
        {"minecraft:crimson_spore", {"minecraft:falling_dust", kVarColorRgb, {}, 0, Rgba{0.9f, 0.4f, 0.5f, 1.f}}},
        {"minecraft:white_ash", {"minecraft:falling_dust", kVarColorRgb, {}, 0, Rgba{0.7294118f, 0.69411767f, 0.7607843f, 1.f}}},
        {"minecraft:portal", {"minecraft:portal_directional", kVarDirection, up}},
        {"minecraft:mycelium", {"minecraft:mycelium_dust_particle"}},
        {"minecraft:lava", {"minecraft:lava_particle"}},
        {"minecraft:enchant", {"minecraft:enchanting_table_particle", kVarDirection}},
        {"minecraft:electric_spark", {"minecraft:electric_spark_particle", kVarDirection}},
        {"minecraft:composter", {"minecraft:crop_growth_emitter"}},
        {"minecraft:block", {"", kVarBlock, {}, static_cast<int>(ParticleType::Terrain)}},
        {"minecraft:falling_dust", {"minecraft:falling_dust", kVarColorRgb}},
        {"minecraft:item", {"", kVarItem, {}, static_cast<int>(ParticleType::IconCrack)}},
        {"minecraft:heart", {"minecraft:heart_particle"}},
        {"minecraft:dust", {"", kVarDustColor, {}, static_cast<int>(ParticleType::RedDust), Rgba{1.f, 0.f, 0.f, 1.f}}},
        {"minecraft:end_rod", {"minecraft:endrod"}},
        {"minecraft:snowflake", {"minecraft:snowflake_particle"}},
    };
    return m;
}
// Java vanilla sound events TF plays -> Bedrock sound names (best match; every name is one of the
// game's sound_definitions of 1.26.51): the SoundEvents TF's code names, and the ones Java's own
// classes play for TF's subclasses and damage, which the W0 kits play through gf::fx
// (AbstractArrow's hits and pickup for TF's arrows, the DamageEffects sounds of a hurt player).
std::unordered_map<std::string, std::string> const& vanillaSounds() {
    static std::unordered_map<std::string, std::string> const m{
        {"block.anvil.land", "random.anvil_land"},
        {"item.armor.equip_generic", "armor.equip_generic"},
        {"item.armor.equip_leather", "armor.equip_leather"},
        {"item.armor.equip_netherite", "armor.equip_netherite"},
        {"entity.arrow.hit", "random.bowhit"},         // AbstractArrow: hitting a block or a mob
        {"entity.arrow.hit_player", "random.orb"},     // AbstractArrow#onHitEntity: the owner's "ding" (random/successful_hit)
        {"entity.arrow.shoot", "random.bow"},
        {"item.axe.strip", "use.wood"},
        {"entity.bat.takeoff", "mob.bat.takeoff"},
        {"entity.blaze.ambient", "mob.blaze.breathe"},
        {"entity.blaze.death", "mob.blaze.death"},
        {"entity.blaze.hurt", "mob.blaze.hit"},
        {"item.book.put", "item.book.put"},
        {"block.brewing_stand.brew", "random.potion.brewed"},
        {"item.bundle.insert", "bundle.insert"},
        {"item.bundle.remove_one", "bundle.remove_one"},
        {"block.candle.ambient", "ambient.candle"},
        {"block.candle.break", "dig.candle"}, // the candle block's break sound
        {"block.candle.extinguish", "extinguish.candle"},
        {"block.candle.place", "place.candle"},
        {"block.chest.locked", "random.door_close"},
        {"block.chest.open", "random.chestopen"},
        {"block.chest.close", "random.chestclosed"},
        {"block.barrel.open", "block.barrel.open"},
        {"block.barrel.close", "block.barrel.close"},
        {"item.chorus_fruit.teleport", "mob.endermen.portal"},
        {"block.comparator.click", "random.click"},
        {"entity.cow.ambient", "mob.cow.say"},
        {"entity.cow.death", "mob.cow.hurt"},
        {"entity.cow.hurt", "mob.cow.hurt"},
        {"entity.cow.step", "mob.cow.step"},
        {"block.dispenser.dispense", "random.click"},
        {"enchant.thorns.hit", "damage.thorns"}, // DamageEffects.THORNS
        {"block.end_portal.spawn", "block.end_portal.spawn"},
        {"entity.evoker.prepare_summon", "mob.evocation_illager.prepare_summon"},
        {"block.fire.ambient", "fire.fire"},
        {"block.fire.extinguish", "random.fizz"},
        {"item.flintandsteel.use", "fire.ignite"},
        {"entity.fox.bite", "mob.fox.bite"},
        {"block.furnace.fire_crackle", "block.furnace.lit"},
        {"entity.generic.burn", "random.fizz"},
        {"entity.generic.eat", "random.eat"},
        {"entity.generic.explode", "random.explode"},
        {"entity.ghast.ambient", "mob.ghast.moan"},
        {"entity.ghast.death", "mob.ghast.death"},
        {"entity.ghast.hurt", "mob.ghast.scream"},
        {"entity.ghast.shoot", "mob.ghast.fireball"},
        {"entity.ghast.warn", "mob.ghast.charge"},
        {"block.glass.break", "random.glass"},
        {"entity.iron_golem.attack", "mob.irongolem.throw"},
        {"entity.iron_golem.death", "mob.irongolem.death"},
        {"entity.iron_golem.hurt", "mob.irongolem.hit"},
        {"entity.iron_golem.step", "mob.irongolem.walk"},
        {"entity.item.break", "random.break"},
        {"entity.item.pickup", "random.pop"},
        {"entity.item_frame.add_item", "block.itemframe.add_item"},
        {"block.lava.pop", "liquid.lavapop"},
        {"entity.lightning_bolt.impact", "ambient.weather.lightning.impact"},
        {"block.lily_pad.place", "dig.grass"}, // Bedrock's lily pad sounds as grass (no lily pad sound)
        {"item.lodestone_compass.lock", "lodestone_compass.link_compass_to_lodestone"},
        {"block.note_block.harp", "note.harp"},
        {"entity.painting.break", "random.break"},
        {"entity.painting.place", "random.click"},
        {"entity.parrot.imitate.blaze", "mob.blaze.breathe"},
        {"entity.parrot.imitate.ghast", "mob.ghast.moan"},
        {"entity.parrot.imitate.silverfish", "mob.silverfish.say"},
        {"entity.parrot.imitate.skeleton", "mob.skeleton.say"},
        {"entity.parrot.imitate.slime", "mob.slime.small"},
        {"entity.parrot.imitate.spider", "mob.spider.say"},
        {"entity.parrot.imitate.zombie", "mob.zombie.say"},
        {"entity.pig.ambient", "mob.pig.say"},
        {"entity.pig.death", "mob.pig.death"},
        {"entity.pig.hurt", "mob.pig.say"},
        {"entity.pig.step", "mob.pig.step"},
        {"entity.player.attack.crit", "game.player.attack.strong"},
        {"entity.player.hurt", "game.player.hurt"},
        {"entity.player.hurt_drown", "mob.player.hurt_drown"},
        {"entity.player.hurt_freeze", "mob.player.hurt_freeze"},
        {"entity.player.hurt_on_fire", "mob.player.hurt_on_fire"},
        {"entity.player.hurt_sweet_berry_bush", "block.sweet_berry_bush.hurt"}, // DamageEffects.POKING
        {"entity.player.levelup", "random.levelup"},
        {"block.portal.ambient", "portal.portal"},
        {"entity.rabbit.ambient", "mob.rabbit.idle"},
        {"entity.rabbit.death", "mob.rabbit.death"},
        {"entity.rabbit.hurt", "mob.rabbit.hurt"},
        {"entity.sheep.ambient", "mob.sheep.say"},
        {"entity.sheep.death", "mob.sheep.say"},
        {"entity.sheep.hurt", "mob.sheep.say"},
        {"entity.sheep.shear", "mob.sheep.shear"},
        {"entity.sheep.step", "mob.sheep.step"},
        {"item.shield.block", "item.shield.block"},
        {"item.shield.break", "random.break"},
        {"entity.silverfish.ambient", "mob.silverfish.say"},
        {"entity.silverfish.death", "mob.silverfish.kill"},
        {"entity.silverfish.hurt", "mob.silverfish.hit"},
        {"entity.silverfish.step", "mob.silverfish.step"},
        {"entity.skeleton.step", "mob.skeleton.step"},
        {"entity.slime.death", "mob.slime.big"},
        {"entity.slime.death_small", "mob.slime.small"},
        {"entity.slime.hurt", "mob.slime.big"},
        {"entity.slime.hurt_small", "mob.slime.small"},
        {"entity.slime.jump", "mob.slime.jump"},
        {"entity.slime.squish", "mob.slime.big"},
        {"entity.slime.squish_small", "mob.slime.small"},
        {"entity.snowball.throw", "random.bow"},
        {"block.snow.place", "use.snow"},
        {"entity.spider.ambient", "mob.spider.say"},
        {"entity.spider.death", "mob.spider.death"},
        {"entity.spider.hurt", "mob.spider.say"},
        {"entity.spider.step", "mob.spider.step"},
        {"item.spyglass.stop_using", "item.spyglass.stop_using"},
        {"item.spyglass.use", "item.spyglass.use"},
        {"entity.stray.ambient", "mob.stray.ambient"},
        {"entity.stray.death", "mob.stray.death"},
        {"entity.stray.hurt", "mob.stray.hurt"},
        {"block.sweet_berry_bush.pick_berries", "block.sweet_berry_bush.pick"},
        {"item.totem.use", "random.totem"},
        {"ui.button.click", "random.click"},
        {"ui.cartography_table.take_result", "block.cartography_table.use"},
        {"weather.rain", "ambient.weather.rain"},
        {"weather.rain.above", "ambient.weather.rain"},
        {"entity.wolf.death", "mob.wolf.death"},
        {"entity.wolf.step", "mob.wolf.step"},
        {"entity.zombie.ambient", "mob.zombie.say"},
        {"entity.zombie.attack_iron_door", "random.anvil_land"}, // a metal clang (Bedrock has no zombie metal sound)
        {"entity.zombie.break_wooden_door", "mob.zombie.woodbreak"},
        {"entity.zombie.death", "mob.zombie.death"},
        {"entity.zombie.hurt", "mob.zombie.hurt"},
        {"entity.zombie.infect", "mob.zombie.say"},
        {"entity.zombie.step", "mob.zombie.step"},
        {"entity.zombie_villager.converted", "mob.zombie.unfect"},
        {"entity.zombie_villager.cure", "mob.zombie.remedy"},
    };
    return m;
}

struct Known {
    std::mutex                      mutex;
    bool                            loaded = false;
    std::unordered_set<std::string> particles; ///< resource pack particle identifiers
    std::unordered_set<std::string> sounds;    ///< resource pack sound_definitions names
    std::set<std::string>           warned;
};

Known& known() {
    static Known k;
    return k;
}

/// Reads the particle identifiers and sound names of the mod's resource pack(s).
void loadKnown(Known& k) {
    k.loaded = true;
    std::filesystem::path base;
    try {
        base = Mod::getInstance().getSelf().getModDir() / "resource_packs";
    } catch (...) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(base, ec)) return;
    for (auto const& pack : std::filesystem::directory_iterator(base, ec)) {
        auto particles = pack.path() / "particles";
        if (std::filesystem::is_directory(particles, ec)) {
            for (auto const& f : std::filesystem::recursive_directory_iterator(particles, ec)) {
                if (f.path().extension() != ".json") continue;
                try {
                    std::ifstream in(f.path());
                    auto          j  = nlohmann::json::parse(in, nullptr, true, true);
                    auto          id = j.value("particle_effect", nlohmann::json::object())
                                  .value("description", nlohmann::json::object())
                                  .value("identifier", std::string{});
                    if (!id.empty()) k.particles.insert(id);
                } catch (...) {}
            }
        }
        auto sounds = pack.path() / "sounds" / "sound_definitions.json";
        if (std::filesystem::is_regular_file(sounds, ec)) {
            try {
                std::ifstream in(sounds);
                auto          j    = nlohmann::json::parse(in, nullptr, true, true);
                auto const&   defs = j.contains("sound_definitions") ? j["sound_definitions"] : j;
                for (auto const& [name, _] : defs.items()) k.sounds.insert(name);
            } catch (...) {}
        }
    }
}

void warnOnce(Known& k, std::string const& what) {
    if (k.warned.insert(what).second) log().warn("fx: {} (silent)", what);
}

std::string stripNs(std::string const& id) {
    auto colon = id.find(':');
    return colon == std::string::npos ? id : id.substr(colon + 1);
}

} // namespace

std::vector<std::string> vanillaParticleIds() {
    std::vector<std::string> ids;
    for (auto const& [id, _] : vanillaParticles()) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

Resolved resolveParticle(std::string const& particleId) {
    auto&           k = known();
    std::lock_guard lock(k.mutex);
    if (!k.loaded) loadKnown(k);
    Resolved r;
    std::string id = particleId.find(':') == std::string::npos ? "minecraft:" + particleId : particleId;
    if (id.rfind("minecraft:", 0) == 0) {
        auto it = vanillaParticles().find(id);
        if (it != vanillaParticles().end()) {
            r.effect  = it->second.effect;
            r.vars    = it->second.vars;
            r.offset  = it->second.offset;
            r.legacy  = it->second.legacy;
            r.color   = it->second.color;
            r.vanilla = true;
        } else if (k.particles.contains(id)) {
            r.effect = id; // already a Bedrock effect name
            r.vanilla = true;
        } else {
            warnOnce(k, "unknown particle '" + particleId + "'");
        }
        return r;
    }
    r.native = true; // a mod particle: companions may draw it natively
    if (k.particles.contains(id)) r.effect = id;
    else warnOnce(k, "particle '" + particleId + "' has no resource pack effect (vanilla clients)");
    return r;
}

std::string resolveSound(std::string const& event) {
    auto&           k = known();
    std::lock_guard lock(k.mutex);
    if (!k.loaded) loadKnown(k);
    if (k.sounds.contains(event)) return event;
    auto bare = stripNs(event);
    if (k.sounds.contains(bare)) return bare;
    if (event.rfind("minecraft:", 0) == 0 || event.find(':') == std::string::npos) {
        auto it = vanillaSounds().find(bare);
        if (it != vanillaSounds().end()) return it->second;
        if (event.rfind("minecraft:", 0) != 0 && bare.find("twilightforest") == std::string::npos) return bare; // a Bedrock name
    }
    warnOnce(k, "unknown sound event '" + event + "'");
    return {};
}

void resetKnown() {
    auto&           k = known();
    std::lock_guard lock(k.mutex);
    k.loaded = false;
    k.particles.clear();
    k.sounds.clear();
}

namespace {

/// Calls `fn` for every player of `dimension` within `range` blocks of `pos`.
template <class Fn>
void forPlayersNear(Dimension& dimension, Vec3 const& pos, float range, Fn&& fn) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dimId = static_cast<int>(dimension.getDimensionId());
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dimId) return true;
        auto const& at = p.getPosition();
        float       dx = at.x - pos.x, dy = at.y - pos.y, dz = at.z - pos.z;
        if (dx * dx + dy * dy + dz * dz <= range * range) fn(p);
        return true;
    });
}

std::shared_ptr<Packet> soundPacket(std::string const& name, Vec3 const& pos, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return nullptr;
    auto& pkt   = static_cast<PlaySoundPacket&>(*packet);
    pkt.mName   = name;
    pkt.mPos    = BlockPos{static_cast<int>(std::floor(pos.x * 8)), static_cast<int>(std::floor(pos.y * 8)), static_cast<int>(std::floor(pos.z * 8))};
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    return packet;
}

nlohmann::json vecJson(Vec3 const& v) { return {v.x, v.y, v.z}; }

std::uint64_t gParticlePackets = 0;
std::uint64_t gSoundPackets    = 0;
std::uint64_t gMovingPackets   = 0;
std::uint64_t gVarPackets      = 0;
std::uint64_t gLegacyPackets   = 0;

void warnParticle(std::string const& what) {
    auto&           k = known();
    std::lock_guard lock(k.mutex);
    warnOnce(k, what);
}

/// The block of params "block" (+ "states"), nullptr when it names none.
Block const* paramBlock(nlohmann::json const& params) {
    if (!params.is_object()) return nullptr;
    auto it = params.find("block");
    if (it == params.end() || !it->is_string()) return nullptr;
    auto states = params.value("states", nlohmann::json::object());
    return game::blockWithStates(it->get<std::string>(), states.is_object() ? states : nlohmann::json::object());
}

/// The data of a legacy particle: IconCrack (item network id << 16 | aux), Terrain (block network
/// id), 0 for the others. nullopt (warned once) when params name no known
/// item / block.
std::optional<int> legacyData(Resolved const& r, std::string const& particleId, nlohmann::json const& params) {
    if (r.vars & kVarItem) {
        std::string id = params.is_object() && params.contains("item") && params["item"].is_string() ? params["item"].get<std::string>() : std::string{};
        int         aux = params.is_object() && params.contains("aux") && params["aux"].is_number_integer() ? params["aux"].get<int>() : 0;
        auto        stack = id.empty() ? std::nullopt : game::items::make(id, 1, aux);
        if (!stack || stack->isNull()) {
            warnParticle("particle '" + particleId + "' needs params \"item\" naming a known item (got '" + id + "')");
            return std::nullopt;
        }
        return (static_cast<int>(stack->getId()) << 16) | (static_cast<int>(stack->getAuxValue()) & 0xffff);
    }
    if (r.vars & kVarBlock) {
        auto const* block = paramBlock(params);
        if (!block) {
            warnParticle("particle '" + particleId + "' needs params \"block\" naming a known block");
            return std::nullopt;
        }
        return static_cast<int>(block->mNetworkId);
    }
    return 0;
}

/// The colour of a coloured effect: params "color", else the map colour of params "block"
/// (FallingDustParticle's colour), else the type's own colour, else white.
Rgba particleColor(Resolved const& r, nlohmann::json const& params) {
    if (params.is_object() && !params.contains("color")) {
        if (auto const* block = paramBlock(params)) {
            mce::Color const& c = block->getBlockType().mMapColor;
            return Rgba{c.r, c.g, c.b, 1.f};
        }
    }
    return colorOf(params, r.color);
}

} // namespace

Counters counters() { return {gParticlePackets, gSoundPackets, gMovingPackets, gVarPackets, gLegacyPackets}; }

Resolved dustFor(Resolved r, nlohmann::json const& params) {
    auto c = colorOf(params, r.color);
    r.vars &= ~static_cast<std::uint32_t>(kVarDustColor);
    if (c.r >= 0.995f && c.g <= 0.005f && c.b <= 0.005f) {
        r.effect.clear();
        r.legacy = static_cast<int>(ParticleType::RedDust);
    } else {
        r.effect = "minecraft:falling_dust";
        r.legacy = 0;
        r.vars |= kVarColorRgb;
        r.color  = c;
    }
    return r;
}

void particle(Dimension& dimension, std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, int count, nlohmann::json const& params) {
    auto r = resolveParticle(particleId);
    if (r.vars & kVarDustColor) r = dustFor(std::move(r), params);
    if (!r.shown() && !r.native) return;
    nlohmann::json msg;
    if (r.native) msg = {{"id", particleId}, {"pos", vecJson(pos)}, {"vel", vecJson(velocity)}, {"count", count}, {"params", params}, {"effect", r.effect}};
    // Vanilla clients: one SpawnParticleEffectPacket per effect instance (the packet has no
    // count), at most kMaxVanillaParticles per call; with params "spread" [dx, dy, dz] each one is
    // offset as ServerLevel#sendParticles offsets them (gaussian x distance). Each packet carries
    // the molang variables its effect reads (Fx.cpp's table, fillEffectVariables), and
    // `variable.velocity` whenever the particle moves. A legacy particle goes as a LevelEventPacket.
    int  n     = std::clamp(count, 1, kMaxVanillaParticles);
    auto dimId = static_cast<uchar>(static_cast<int>(dimension.getDimensionId()));
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::vector<Vec3>                at(static_cast<std::size_t>(n), pos);
    if (auto spread = spreadOf(params); spread && n > 1)
        for (auto& v : at) v = spreadAt(pos, *spread, rng);
    for (auto& v : at) v = Vec3{v.x + r.offset.x, v.y + r.offset.y, v.z + r.offset.z};
    std::optional<int> legacy;
    if (r.legacy != 0) {
        legacy = legacyData(r, particleId, params);
        if (!legacy && !r.native) return;
    }
    bool const moving = velocity.x != 0.f || velocity.y != 0.f || velocity.z != 0.f;
    bool const needsVars = moving || (r.vars & (kVarVelocity | kVarDirection | kVarColorRgb | kVarColorRgba | kVarNoteColor)) != 0;
    // the maps, one per particle (a still splash gets its own random motion), made for the first
    // vanilla client
    std::vector<std::unique_ptr<MolangMap>> vars;
    auto makeVars = [&] {
        Rgba const color = particleColor(r, params);
        for (int i = 0; i < n; ++i) {
            Vec3 direction = velocity;
            if (r.vars & kVarSplashDirection) direction = splashDirection(rng, velocity);
            else if (r.vars & kVarCritDirection) direction = critDirection(rng, velocity);
            auto& map = vars.emplace_back(std::make_unique<MolangMap>());
            fillEffectVariables(*map, r.vars, velocity, direction, color);
        }
    };
    forPlayersNear(dimension, pos, 32.f, [&](Player& p) {
        if (r.native && net::isCompanion(p)) {
            net::send(p, "fx.particle", msg);
            ++gParticlePackets;
            return;
        }
        if (legacy) {
            for (auto const& v : at) {
                auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::LevelEvent);
                if (!packet) return;
                auto& pkt    = static_cast<LevelEventPacket&>(*packet);
                pkt.mEventId = static_cast<int>(SharedTypes::Legacy::LevelEvent::ParticleLegacyEvent) | r.legacy;
                pkt.mPos     = v;
                pkt.mData    = *legacy;
                pkt.sendTo(p);
                ++gParticlePackets;
                ++gLegacyPackets;
            }
            return;
        }
        if (r.effect.empty()) return;
        if (needsVars && vars.empty()) makeVars();
        for (std::size_t i = 0; i < at.size(); ++i) {
            auto molang = needsVars ? std::optional<MolangVariableMap>(vars[i]->get()) : std::nullopt;
            SpawnParticleEffectPacket pkt(SpawnParticleEffectPacketPayload(at[i], r.effect, dimId, std::move(molang)));
            pkt.sendTo(p);
            ++gParticlePackets;
            if (needsVars) ++gVarPackets;
            if (moving || (r.vars & kVarVelocity)) ++gMovingPackets;
        }
    });
}
void sound(Dimension& dimension, std::string const& event, Vec3 const& pos, float volume, float pitch) {
    auto name = resolveSound(event);
    if (name.empty()) return;
    // Java: Level#playSound reaches 16 blocks, volume > 1 extends it (16 * volume)
    float range = 16.f * std::max(1.f, volume);
    auto  pkt   = soundPacket(name, pos, volume, pitch);
    if (!pkt) return;
    forPlayersNear(dimension, pos, range, [&](Player& p) {
        pkt->sendTo(p);
        ++gSoundPackets;
    });
}

void soundTo(Player& player, std::string const& event, float volume, float pitch) {
    auto name = resolveSound(event);
    if (name.empty()) return;
    if (auto pkt = soundPacket(name, player.getPosition(), volume, pitch)) {
        pkt->sendTo(player);
        ++gSoundPackets;
    }
}

namespace {
class FxModule {
public:
    void install() {
        game::db::onWorldReady([] { resetKnown(); });
    }
    void uninstall() {}
};
} // namespace

} // namespace gf::fx

GF_REGISTER_MODULE_ORDERED(gf::fx::FxModule, gf::ModuleOrder::Contracts);
