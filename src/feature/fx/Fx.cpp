// The effect part of "rules+fx" (F10): particles and sounds by id (rule R10).
//
// Particles: upstream's ids ("gloamingforest:firefly") and the vanilla particle types upstream spawns
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
//     mapped vanilla effect reads (`variable.direction`, `variable.color`, `variable.note_color`,
//     `variable.dust_size`, `variable.dust_ticks`; see vanillaParticles). Java's portal and dust particles are F10's own
//     effects gf:portal and gf:dust (assets/pack/rp/particles/fx), which every client gets with the
//     resource pack. Java's item and block particles go as Bedrock's legacy particles.
// Sounds: upstream's sound events ("entity.gloamingforest.naga.hiss", the sound_definitions names F3
// generates) and the vanilla events upstream plays ("minecraft:entity.ghast.shoot", mapped to Bedrock's
// names) go out as PlaySoundPacket to the players in range (16 blocks, more for volume > 1, as
// Java's Level#playSound).
// An id the resource pack does not define is silent, with one warning per id.

#include "feature/fx/Fx.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
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
#include "mc/world/level/BlockSource.h"
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

// Java vanilla particle types upstream spawns -> Bedrock particle effects (best match; gf: effects are
// F10's own, in the mod's resource pack), with the molang
// variables the effect reads (Bedrock's vanilla effects of 1.26.51; an effect that reads a variable
// nobody set logs a content-log error per particle) and where Bedrock draws a Java type through a
// legacy particle (LevelEventPacket) instead of an effect.
//   - portal: F10's own effect gf:portal (assets/pack/rp/particles/fx/portal.json), Java's
//     PortalParticle: the path pos + direction * (1 - (2a^2 - a)) + (0, 1 - a, 0) at age fraction a
//     (from pos + speed + (0, 1, 0) down to pos), the colour (0.9f, 0.3f, 1) * f with f = 0.4..1
//     (blue-violet; Bedrock's portal_directional tints (f, 0.3f, 0.9f), pink-magenta), the size
//     0.1 * (0.5..0.7) * (1 - (1 - a)^2), a lifetime of 40..49 ticks and a random generic sprite.
//     Deviation (kept on purpose): PortalParticle#getLightColor raises the packed SKY light by
//     a^4 * 240 (capped at 240) and keeps the block light, so a speck brightens towards its end only
//     where the sky light is low (under a roof); under the open sky (sky light 15, a portal pool's
//     usual place) it adds nothing, and at night the lightmap still darkens it. Bedrock's particle
//     JSON has no per-age lighting: the effect keeps particle_appearance_lighting (the world's light,
//     sky and block, as Java's base) all its life. Dropping the lighting, or switching to an unlit
//     copy late in the life, would draw the specks full bright at night under the open sky, where
//     Java's stay dark: further from Java than the covered-portal case this leaves out.
//   - enchant: enchanting_table_particle flies from pos + direction to pos (FlyTowardsPositionParticle).
//   - electric_spark: the effect's shape direction is Java's velocity.
//   - splash: the direction is SplashParticle's motion (splashDirection: the x/z speed with y 0.1
//     when ySpeed is 0, WaterDropParticle's random motion otherwise).
//   - crit: the direction is CritParticle's motion (critDirection: Particle's random motion of a
//     still particle * 0.1 plus 0.4 * the speed), so a still crit scatters as in Java.
//   - poof: explosion_particle's speed is -variable.velocity * 10 blocks/s plus its own noise
//     (always set): it gets -2 * velocity, Java's ExplodeParticle speed of velocity blocks/tick.
//   - dust: F10's own effect gf:dust (assets/pack/rp/particles/fx/dust.json), Java's DustParticle
//     for DustParticleOptions' colour (params "color", red by default: REDSTONE) and scale (params
//     "scale", 1 by default): the motion of Particle for the speed times 0.1 (dustDirection,
//     `variable.direction`: a slight upward drift; the effect's shape direction is the unit vector
//     and its speed |direction| * 20 blocks/s, right whether or not Bedrock normalizes the shape
//     direction) slowed by the friction 0.96 per tick (linear drag 20 * -ln 0.96 = 0.8164/s),
//     colliding with blocks without bouncing (Java zeroes the blocked motion); on the ground Java
//     multiplies x/z by 1.1 (speedUpWhenYMotionIsBlocked) and 0.7 per tick, about 0.74 with the
//     friction: the collision drag of 3 blocks/s^2 stops a 0.2..0.9 blocks/s particle in the same
//     5..6 ticks (deviation: Bedrock's collision drag cannot tell a ceiling from the ground, so a
//     speck held under a ceiling slows down too where Java's 1.1 * 0.96 speeds it up); each
//     channel (0.8..1) * colour * (0.6..1); the size 0.1..0.2 * 0.75 * scale,
//     growing over the first 1/32 of the life, and a lifetime of max(floor(8 / (0.2..1)) * scale, 1)
//     ticks, rolled here (dustLook: `variable.dust_size`, `variable.dust_ticks`); the generic
//     sprites 7 to 0 over the life.
//   - note: note_particle's colour is NoteParticle's hue from the x speed.
//   - entity_effect / effect: mobspell_emitter's colour (params "color", white by default).
//   - falling_dust: params "color", else FallingDustParticle's colour of params "block" (+ "states"):
//     its dust colour, its BlockColor without a level (FxBlockColor.cpp; the water cauldron's is
//     white), else the block state's map colour at each particle's own position (BlockColors#getColor:
//     state.getMapColor(level, pos)). Java's ids Bedrock folds into one block are translated
//     (paramBlockOf): water / lava / powder snow cauldrons, and the 16 beds with BedBlock's
//     state-based map colour (FOOT the dye's, HEAD WOOL; Bedrock keeps the colour in the block actor).
//   - colours (params "color"): an integer is 0xAARRGGBB when any of bits 24-31 is set, else an
//     opaque 0xRRGGBB (pass [r, g, b, 0] for alpha 0); an array is [r, g, b(, a)] in 0..1.
//   - crimson_spore / white_ash: Bedrock has no effect for them (its own are drawn by the engine
//     only for its biomes): falling_dust tinted with Java's colours, a small particle drifting down.
//   - item / block: Bedrock's item and block break particles take the texture from the engine
//     (legacy IconCrack and Terrain with params "item" / "block"); item_snowball and item_slime are
//     the legacy SnowballPoof and Slime. Legacy particles carry no velocity.
std::unordered_map<std::string, VanillaParticle> const& vanillaParticles() {
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
        {"minecraft:portal", {"gf:portal", kVarDirection}},
        {"minecraft:mycelium", {"minecraft:mycelium_dust_particle"}},
        {"minecraft:lava", {"minecraft:lava_particle"}},
        {"minecraft:enchant", {"minecraft:enchanting_table_particle", kVarDirection}},
        {"minecraft:electric_spark", {"minecraft:electric_spark_particle", kVarDirection}},
        {"minecraft:composter", {"minecraft:crop_growth_emitter"}},
        {"minecraft:block", {"", kVarBlock, {}, static_cast<int>(ParticleType::Terrain)}},
        {"minecraft:falling_dust", {"minecraft:falling_dust", kVarColorRgb}},
        {"minecraft:item", {"", kVarItem, {}, static_cast<int>(ParticleType::IconCrack)}},
        {"minecraft:heart", {"minecraft:heart_particle"}},
        {"minecraft:dust", {"gf:dust", kVarColorRgb | kVarDirection | kVarDustDirection | kVarDustLook, {}, 0, Rgba{1.f, 0.f, 0.f, 1.f}}},
        {"minecraft:end_rod", {"minecraft:endrod"}},
        {"minecraft:snowflake", {"minecraft:snowflake_particle"}},
    };
    return m;
}
// Java vanilla sound events upstream plays -> Bedrock sound names (best match; every name is one of the
// game's sound_definitions of 1.26.51): the SoundEvents upstream's code names, and the ones Java's own
// classes play for upstream's subclasses and damage, which the W0 kits play through gf::fx
// (AbstractArrow's hits and pickup for upstream's arrows, the DamageEffects sounds of a hurt player).
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
    // the pack's effects read variable.velocity unguarded (Molang's ?? only takes direct variables):
    // vanilla clients always get it, zero for a still particle
    r.vars |= kVarVelocity;
    if (k.particles.contains(id)) r.effect = id;
    else warnOnce(k, "particle '" + particleId + "' has no resource pack effect (vanilla clients)");
    return r;
}

bool packHasEffect(std::string const& effectId) {
    auto&           k = known();
    std::lock_guard lock(k.mutex);
    if (!k.loaded) loadKnown(k);
    return k.particles.contains(effectId);
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
        if (event.rfind("minecraft:", 0) != 0 && bare.find("gloamingforest") == std::string::npos) return bare; // a Bedrock name
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

/// An integer of a Java block state value (Java's states come as strings or numbers).
std::optional<int> stateInt(nlohmann::json const& states, char const* name) {
    if (!states.is_object()) return std::nullopt;
    auto it = states.find(name);
    if (it == states.end()) return std::nullopt;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_string()) {
        auto const& s     = it->get_ref<std::string const&>();
        int         value = 0;
        auto [end, ec]    = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec == std::errc{} && end == s.data() + s.size()) return value;
    }
    return std::nullopt;
}

/// A Java bed's "part" (BedPart: "head" or "foot", foot without).
bool bedHead(nlohmann::json const& states) {
    auto it = states.find("part");
    return it != states.end() && it->is_string() && it->get_ref<std::string const&>() == "head";
}

/// DyeColor#getMapColor of a Java dye name (MapColor's 0xRRGGBB).
std::optional<int> dyeMapColor(std::string_view dye) {
    static std::unordered_map<std::string_view, int> const m{
        {"white", 0xFFFFFF}, {"orange", 0xD87F33},     {"magenta", 0xB24CD8}, {"light_blue", 0x6699D8},
        {"yellow", 0xE5E533}, {"lime", 0x7FCC19},      {"pink", 0xF27FA5},    {"gray", 0x4C4C4C},
        {"light_gray", 0x999999}, {"cyan", 0x4C7F99},  {"purple", 0x7F3FB2},  {"blue", 0x334CB2},
        {"brown", 0x664C33}, {"green", 0x667F33},      {"red", 0x993333},     {"black", 0x191919},
    };
    auto it = m.find(dye);
    return it == m.end() ? std::nullopt : std::optional<int>(it->second);
}

Rgba rgbOf(int rgb) { return Rgba{((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, 1.f}; }

} // namespace

ParamBlock paramBlockOf(nlohmann::json const& params) {
    if (!params.is_object()) return {};
    auto it = params.find("block");
    if (it == params.end() || !it->is_string()) return {};
    std::string    id     = it->get<std::string>();
    nlohmann::json states = params.value("states", nlohmann::json::object());
    if (!states.is_object()) states = nlohmann::json::object();
    static constexpr int   kWool = 0xC7C7C7; // MapColor.WOOL
    std::string_view const name  = id.rfind("minecraft:", 0) == 0 ? std::string_view(id).substr(10) : std::string_view{};
    if (name == "water_cauldron" || name == "powder_snow_cauldron" || name == "lava_cauldron") {
        int const         fill   = name == "lava_cauldron" ? 6 : std::clamp(stateInt(states, "level").value_or(3), 1, 3) * 2;
        std::string const liquid = name == "water_cauldron" ? "water" : name == "lava_cauldron" ? "lava" : "powder_snow";
        return {game::blockWithStates("minecraft:cauldron", nlohmann::json{{"cauldron_liquid", liquid}, {"fill_level", fill}}), std::nullopt};
    }
    if (name.size() > 4 && name.substr(name.size() - 4) == "_bed") {
        if (auto dye = dyeMapColor(name.substr(0, name.size() - 4))) {
            bool const head = bedHead(states);
            return {game::blockWithStates("minecraft:bed", nlohmann::json{{"head_piece_bit", head}}), rgbOf(head ? kWool : *dye)};
        }
    }
    if (name == "bed" && states.contains("part")) {
        // Bedrock's bed with Java's part state: head_piece_bit
        bool const head = bedHead(states);
        states.erase("part");
        states["head_piece_bit"] = head;
    }
    auto const* block = game::blockWithStates(id, states);
    if (block && name == "bed" && game::intState(*block, "head_piece_bit").value_or(0) != 0) return {block, rgbOf(kWool)};
    return {block, std::nullopt};
}

namespace {

/// The block of params "block" (+ "states"), nullptr when it names none.
Block const* paramBlock(nlohmann::json const& params) { return paramBlockOf(params).block; }

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

} // namespace

Counters counters() { return {gParticlePackets, gSoundPackets, gMovingPackets, gVarPackets, gLegacyPackets}; }

Rgba blockDustColor(Block const& block, BlockSource* region, Vec3 const& pos) {
    auto const id   = game::blockId(block);
    auto       tint = fallingDustColor(
        id, [&block](std::string_view state) { return game::intState(block, state); },
        [&block, states = std::optional<std::map<std::string, std::string>>{}](std::string_view state) mutable -> std::optional<std::string> {
            if (!states) states = game::statesOf(block);
            auto it = states->find(std::string(state));
            if (it == states->end()) return std::nullopt;
            return it->second;
        });
    if (tint) return dustRgb(*tint);
    // BlockColors#getColor without a BlockColor: state.getMapColor(level, pos), the state's own map
    // colour (a log's by its axis, ...). The bed's reads its block actor at the position, which the
    // particle's position does not hold: the type's map colour.
    mce::Color c = block.getBlockType().mMapColor;
    if (region && id != "minecraft:bed") {
        BlockPos const at{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
        c = block.getBlockType().getMapColor(*region, at, block);
    }
    return Rgba{c.r, c.g, c.b, 1.f};
}

std::optional<Rgba> paramDustColor(ParamBlock const& pb, BlockSource* region, Vec3 const& pos) {
    if (!pb.block) return std::nullopt;
    if (pb.mapColor) return *pb.mapColor; // a bed: no BlockColor, its state's map colour
    return blockDustColor(*pb.block, region, pos);
}

void particle(Dimension& dimension, std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, int count, nlohmann::json const& params) {
    auto r = resolveParticle(particleId);
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
    bool const needsVars = moving || (r.vars & (kVarVelocity | kVarDirection | kVarColorRgb | kVarColorRgba | kVarNoteColor | kVarDustLook)) != 0;
    // the maps, one per particle (a still splash or dust gets its own random motion), made for the
    // first vanilla client
    std::vector<std::unique_ptr<MolangMap>> vars;
    auto makeVars = [&] {
        // the colour: params "color", else FallingDustParticle's colour of params "block" at each
        // particle's own position (BlockPos.containing of the spread position), else the type's
        ParamBlock const pb     = params.is_object() && !params.contains("color") ? paramBlockOf(params) : ParamBlock{};
        Rgba const       fixed  = colorOf(params, r.color);
        float const      scale  = dustScale(params);
        auto&            region = dimension.getBlockSourceFromMainChunkSource();
        for (int i = 0; i < n; ++i) {
            Rgba const color = paramDustColor(pb, &region, at[static_cast<std::size_t>(i)]).value_or(fixed);
            Vec3 direction = velocity;
            if (r.vars & kVarSplashDirection) direction = splashDirection(rng, velocity);
            else if (r.vars & kVarCritDirection) direction = critDirection(rng, velocity);
            else if (r.vars & kVarDustDirection) direction = dustDirection(rng, velocity);
            DustLook const dust = (r.vars & kVarDustLook) ? dustLook(rng, scale) : DustLook{};
            auto&          map  = vars.emplace_back(std::make_unique<MolangMap>());
            fillEffectVariables(*map, r.vars, velocity, direction, color, dust);
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
