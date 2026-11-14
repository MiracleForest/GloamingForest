// Music discs whose song is a named sound event (ext/item/Records.h, spike S6). The jukebox's own
// start of a record (JukeboxBlockActor::_startRecordSound) plays the registered sound through gf::fx
// at Java's jukebox volume; stopping the record or taking it out of the jukebox stops the sound.
#include <algorithm>
#include <map>
#include <mutex>
#include <variant>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/StopSoundPacket.h"
#include "mc/sound/SoundEventIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/RecordItemComponent.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/JukeboxBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/GlobalPos.h"
#include "feature/ext/item/Records.h"
#include "feature/fx/Fx.h"
#include "feature/item/DataFiles.h"
#include "feature/item/ItemInternal.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::item {

namespace {

struct Playing {
    std::string disc;  ///< the record's item id
    std::string sound; ///< the sound event played
};

struct Store {
    std::mutex                                 mutex;
    std::map<std::string, std::string>         sounds;  ///< disc item id -> sound event
    std::map<core::GlobalPos, Playing>         playing; ///< jukeboxes playing a registered sound
    internal::RecordCounts                     counts;
};

Store& store() {
    static Store s;
    return s;
}

/// Java's jukebox song volume (JukeboxSongPlayer: 4, i.e. heard within 64 blocks).
constexpr float kVolume = 4.0f;

/// The sound definition name fx plays for an upstream event ("gloamingforest:music_disc.x" -> "music_disc.x").
std::string definitionName(std::string const& event) {
    auto colon = event.find(':');
    return colon == std::string::npos ? event : event.substr(colon + 1);
}

/// Stops a sound for every player of a dimension (StopSoundPacket by name).
void stopFor(int dim, std::string const& sound) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::StopSound);
    if (!packet) return;
    auto& stop            = static_cast<StopSoundPacket&>(*packet);
    stop.mName            = definitionName(sound);
    stop.mStopAll         = false;
    stop.mStopMusicLegacy = false;
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) == dim && !p.isSimulatedPlayer()) packet->sendTo(p);
        return true;
    });
}

} // namespace

namespace ext {
void setRecordSound(std::string const& itemId, std::string const& soundEvent) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            id = feature::item::data::withNamespace(itemId);
    if (soundEvent.empty()) s.sounds.erase(id);
    else s.sounds[id] = soundEvent;
}

std::string recordSound(std::string const& itemId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            it = s.sounds.find(feature::item::data::withNamespace(itemId));
    return it == s.sounds.end() ? std::string() : it->second;
}
} // namespace ext

namespace internal {
RecordCounts recordCounts() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return s.counts;
}

bool startRecord(BlockSource& region, BlockPos const& pos, std::string const& disc) {
    auto sound = ext::recordSound(disc);
    if (sound.empty()) return false;
    Vec3 center{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    fx::sound(region.getDimension(), sound, center, kVolume, 1.0f);
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        s.playing[core::GlobalPos{static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z}] = Playing{disc, sound};
        ++s.counts.started;
        s.counts.lastSound = sound;
    }
    log().info("item: jukebox at {} {} {} plays {} for {}", pos.x, pos.y, pos.z, sound, disc);
    return true;
}

bool stopRecord(int dim, BlockPos const& pos, std::string const& unlessDisc) {
    Playing was;
    int     stopIn = dim;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        auto it = std::find_if(s.playing.begin(), s.playing.end(), [&](auto const& e) {
            auto const& at = e.first;
            return (dim < 0 || at.dim == dim) && at.x == pos.x && at.y == pos.y && at.z == pos.z;
        });
        if (it == s.playing.end() || (!unlessDisc.empty() && it->second.disc == unlessDisc)) return false;
        was    = it->second;
        stopIn = it->first.dim;
        s.playing.erase(it);
        ++s.counts.stopped;
    }
    stopFor(stopIn, was.sound);
    return true;
}
} // namespace internal

} // namespace gf::item

namespace {
/// Whether a jukebox block actor is one of the server's (a local world's client has its own).
bool serverSide(BlockSource& region) { return !region.getLevel().isClientSide(); }
} // namespace

// The jukebox starts its record: a registered disc plays its named sound instead of the record
// component's (a LevelSoundEvent the BP had to name is not played; without one the game's own start
// still runs for its bookkeeping).
LL_TYPE_INSTANCE_HOOK(
    ItemRecordStartHook,
    ll::memory::HookPriority::Normal,
    JukeboxBlockActor,
    &JukeboxBlockActor::_startRecordSound,
    void,
    BlockSource&               region,
    RecordItemComponent const& record
) {
    bool named = false;
    try {
        if (serverSide(region)) named = gf::item::internal::startRecord(region, getPosition(), gf::game::items::idOf(*mRecord));
    } catch (std::exception const& e) {
        gf::log().error("item: jukebox record sound failed: {}", e.what());
    }
    if (!named || std::holds_alternative<std::monostate>(record.mSoundEvent->mSound.get())) origin(region, record);
}

LL_TYPE_INSTANCE_HOOK(
    ItemRecordStopHook,
    ll::memory::HookPriority::Normal,
    JukeboxBlockActor,
    &JukeboxBlockActor::stopPlayingRecord,
    void,
    BlockSource& region
) {
    origin(region);
    if (serverSide(region)) gf::item::internal::stopRecord(static_cast<int>(region.getDimensionId()), getPosition(), {});
}

// A record taken out (or replaced by another disc): its song stops. The jukebox actor has no region
// here: every dimension's entry at this position whose disc is not the new record.
LL_TYPE_INSTANCE_HOOK(
    ItemRecordSetHook,
    ll::memory::HookPriority::Normal,
    JukeboxBlockActor,
    &JukeboxBlockActor::setRecord,
    void,
    ItemStack const& record,
    bool             startPlaying
) {
    try {
        gf::item::internal::stopRecord(-1, getPosition(), gf::game::items::idOf(record));
    } catch (...) {
    }
    origin(record, startPlaying);
}

namespace gf::item {
namespace {
class RecordsModule {
public:
    void install() {
        ItemRecordStartHook::hook();
        ItemRecordStopHook::hook();
        ItemRecordSetHook::hook();
    }
    void uninstall() {
        ItemRecordSetHook::unhook();
        ItemRecordStopHook::unhook();
        ItemRecordStartHook::unhook();
    }
};
} // namespace
} // namespace gf::item

GF_REGISTER_MODULE(gf::item::RecordsModule);
