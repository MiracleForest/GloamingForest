#pragma once

// Extension of contract "item" by F9 (rule R18; to be folded into feature/item/Item.h by A8):
// music discs whose song is a named sound event.
//
// upstream's discs (U10) play JukeboxSong sound events ("gloamingforest:music_disc.gloamingforest.radiance",
// F3's sound definitions). Bedrock's BP record component cannot name them: its "sound_event" is
// bound to the LevelSoundEvent enum (spike S6: "This value is bound to 'LevelSoundEvent' and expects
// one of the following strings ... Found: 'gf_s6.disc'"). So a disc's BP item keeps
// minecraft:record for the jukebox (duration, comparator signal) without "sound_event", and its song
// is registered here: when a jukebox starts the record, the server plays the sound through gf::fx at
// Java's jukebox volume (4: heard within 64 blocks), for vanilla clients and the client mod alike,
// and stops it (StopSoundPacket) when the record stops or leaves the jukebox. A record that also
// names a LevelSoundEvent does not play it.
//
//     gf::item::ext::setRecordSound("gloamingforest:music_disc_radiance", "gloamingforest:music_disc.gloamingforest.radiance");

#include <string>

namespace gf::item::ext {

/// The jukebox plays `soundEvent` for the disc `itemId` ("" removes the registration).
void setRecordSound(std::string const& itemId, std::string const& soundEvent);
/// The sound event registered for a disc ("" none).
[[nodiscard]] std::string recordSound(std::string const& itemId);

} // namespace gf::item::ext
