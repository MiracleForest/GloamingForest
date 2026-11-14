// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/HolidayEvent.java
//   src/main/java/gloamingforest/client/event/ClientGameEvents.java (customizeSplashes)
//   src/main/java/gloamingforest/item/MoonDialItem.java (April Fools)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

namespace gf::core::holiday {

/// A calendar day (month 1..12, day 1..31).
struct MonthDay {
    int month = 1;
    int day   = 1;
};

/// Today's date in the local time zone (as Java's MonthDay.now()).
[[nodiscard]] MonthDay today();

/// upstream's HolidayEvent#isHalloweenWeek: strictly after October 19 and strictly before November 4.
[[nodiscard]] bool isHalloweenWeek(MonthDay date);
[[nodiscard]] bool isHalloweenWeek();

/// The title screen splash day (August 19, the Gloaming Forest's birthday).
[[nodiscard]] bool isGloamingBirthday(MonthDay date);
/// The ordinal of the birthday in `year` ("Happy 15th birthday ..." in 2026: year - 2011).
[[nodiscard]] int gloamingAge(int year);

/// April 1 (MoonDialItem shows a random phase).
[[nodiscard]] bool isAprilFools(MonthDay date);

} // namespace gf::core::holiday
