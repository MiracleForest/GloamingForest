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
//     particle up to 8 per call, offset by params "spread" (Java's gaussian x distance); the
//     effect's own motion replaces the velocity (the server cannot build molang variables).
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
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/fx/FxInternal.h"
#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::fx {

namespace {

// Java vanilla particle types TF spawns -> Bedrock particle effects (best match).
std::unordered_map<std::string, std::string> const& vanillaParticles() {
    static std::unordered_map<std::string, std::string> const m{
        {"minecraft:smoke", "minecraft:basic_smoke_particle"},
        {"minecraft:large_smoke", "minecraft:large_explosion"},
        {"minecraft:flame", "minecraft:basic_flame_particle"},
        {"minecraft:small_flame", "minecraft:candle_flame_particle"},
        {"minecraft:poof", "minecraft:explosion_particle"},
        {"minecraft:explosion", "minecraft:large_explosion"},
        {"minecraft:splash", "minecraft:water_splash_particle"},
        {"minecraft:rain", "minecraft:rain_splash_particle"},
        {"minecraft:falling_water", "minecraft:water_drip_particle"},
        {"minecraft:crit", "minecraft:critical_hit_emitter"},
        {"minecraft:cloud", "minecraft:white_smoke_particle"},
        {"minecraft:note", "minecraft:note_particle"},
        {"minecraft:item_snowball", "minecraft:snowball_poof"},
        {"minecraft:item_slime", "minecraft:slime_particle"},
        {"minecraft:happy_villager", "minecraft:villager_happy"},
        {"minecraft:angry_villager", "minecraft:villager_angry"},
        {"minecraft:entity_effect", "minecraft:mobspell_emitter"},
        {"minecraft:effect", "minecraft:mobspell_emitter"},
        {"minecraft:witch", "minecraft:witchspell_emitter"},
        {"minecraft:crimson_spore", "minecraft:crimson_spore_particle"},
        {"minecraft:white_ash", "minecraft:white_ash_particle"},
        {"minecraft:portal", "minecraft:portal_directional"},
        {"minecraft:mycelium", "minecraft:mycelium_dust_particle"},
        {"minecraft:lava", "minecraft:lava_particle"},
        {"minecraft:enchant", "minecraft:enchanting_table_particle"},
        {"minecraft:electric_spark", "minecraft:electric_spark_particle"},
        {"minecraft:composter", "minecraft:crop_growth_emitter"},
        {"minecraft:block", "minecraft:breaking_item_terrain"},
        {"minecraft:falling_dust", "minecraft:falling_dust"},
        {"minecraft:item", "minecraft:breaking_item_icon"},
        {"minecraft:heart", "minecraft:heart_particle"},
        {"minecraft:dust", "minecraft:redstone_wire_dust_particle"},
        {"minecraft:end_rod", "minecraft:endrod"},
        {"minecraft:snowflake", "minecraft:snowflake_particle"},
    };
    return m;
}

// Java vanilla sound events TF plays (SoundEvents) -> Bedrock sound names (best match).
std::unordered_map<std::string, std::string> const& vanillaSounds() {
    static std::unordered_map<std::string, std::string> const m{
        {"block.anvil.land", "random.anvil_land"},
        {"item.armor.equip_generic", "armor.equip_generic"},
        {"item.armor.equip_leather", "armor.equip_leather"},
        {"item.armor.equip_netherite", "armor.equip_netherite"},
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
        {"block.candle.break", "break.candle"},
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
        {"block.lily_pad.place", "block.lily_pad.place"},
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
        {"entity.zombie.attack_iron_door", "mob.zombie.metal"},
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

Resolved resolveParticle(std::string const& particleId) {
    auto&           k = known();
    std::lock_guard lock(k.mutex);
    if (!k.loaded) loadKnown(k);
    Resolved r;
    std::string id = particleId.find(':') == std::string::npos ? "minecraft:" + particleId : particleId;
    if (id.rfind("minecraft:", 0) == 0) {
        auto it = vanillaParticles().find(id);
        if (it != vanillaParticles().end()) {
            r.effect = it->second;
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

} // namespace

Counters counters() { return {gParticlePackets, gSoundPackets}; }

void particle(Dimension& dimension, std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, int count, nlohmann::json const& params) {
    auto r = resolveParticle(particleId);
    if (r.effect.empty() && !r.native) return;
    nlohmann::json msg;
    if (r.native) msg = {{"id", particleId}, {"pos", vecJson(pos)}, {"vel", vecJson(velocity)}, {"count", count}, {"params", params}, {"effect", r.effect}};
    // Vanilla clients: one SpawnParticleEffectPacket per effect instance (the packet has no
    // count, and the server cannot build its molang variables), at most kMaxVanillaParticles per
    // call; with params "spread" [dx, dy, dz] each one is offset as ServerLevel#sendParticles
    // offsets them (gaussian x distance). The velocity is the effect's own.
    constexpr int kMaxVanillaParticles = 8;
    int           n     = std::clamp(count, 1, kMaxVanillaParticles);
    auto          dimId = static_cast<uchar>(static_cast<int>(dimension.getDimensionId()));
    std::vector<Vec3> at(static_cast<std::size_t>(n), pos);
    if (auto spread = params.is_object() ? params.value("spread", nlohmann::json{}) : nlohmann::json{};
        spread.is_array() && spread.size() == 3 && spread[0].is_number() && spread[1].is_number() && spread[2].is_number() && n > 1) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::normal_distribution<float>  gauss(0.f, 1.f);
        for (auto& v : at) {
            v.x += gauss(rng) * spread[0].get<float>();
            v.y += gauss(rng) * spread[1].get<float>();
            v.z += gauss(rng) * spread[2].get<float>();
        }
    }
    forPlayersNear(dimension, pos, 32.f, [&](Player& p) {
        if (r.native && net::isCompanion(p)) {
            net::send(p, "fx.particle", msg);
            ++gParticlePackets;
            return;
        }
        if (r.effect.empty()) return;
        for (auto const& v : at) {
            SpawnParticleEffectPacket pkt(SpawnParticleEffectPacketPayload(v, r.effect, dimId, std::nullopt));
            pkt.sendTo(p);
            ++gParticlePackets;
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
