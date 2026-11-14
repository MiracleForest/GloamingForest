#pragma once

// Internal to unit F4: the per-player clock of vanilla clients (Clock.cpp).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

class Packet;
class Player;

namespace gf::feature::dimension {

/// The time-of-day carrying packets a client's clock follows.
enum class TimePacket { None, SetTime, WorldClocks, DaylightRule };

/// The packet's kind (WorldClocks only when it carries the overworld clock, DaylightRule only when
/// it sets doDaylightCycle).
[[nodiscard]] TimePacket  timePacketKind(Packet const& packet);
[[nodiscard]] char const* timePacketName(TimePacket kind);
/// Time of day 0..23999 of a level / clock time.
[[nodiscard]] int timeOfDay(int time);
/// The time a Gloaming player's clock shows: the same day, time of day 13000 (fixed_time).
[[nodiscard]] int gloamingClock(int time);
/// The id of the overworld world clock (the one the sky follows), 0 while unknown.
[[nodiscard]] std::uint64_t overworldClockId();
/// The overworld clock's time and paused flag.
[[nodiscard]] std::optional<std::pair<int, bool>> overworldClockState();
/// Corrects a time packet in place for a player in the Gloaming Forest; returns how to undo it
/// (empty: nothing changed).
[[nodiscard]] std::function<void()> fixForGloaming(Packet& packet);

/// The clock a client was last sent over the network (remote clients only), and the checks of it.
struct ClientClock {
    int         timeOfDay     = -1; ///< of the last SetTime / overworld clock packet (-1: none yet)
    bool        clockPaused   = false;
    bool        daylightCycle = true;
    int         packets       = 0; ///< time packets since the last reset
    int         wrongTicks    = 0; ///< ticks in the Gloaming Forest that ended with another time of day
    std::string lastPacket;        ///< the packet that set timeOfDay
    std::string wrongPacket;       ///< the packet behind the first wrong tick
};
[[nodiscard]] std::optional<ClientClock> clientClock(Player const& player);
/// Starts a new check window (packets, wrongTicks).
void resetClientClockChecks(Player const& player);

} // namespace gf::feature::dimension
