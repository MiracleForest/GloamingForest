// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/HolidayEvent.java
//   src/main/java/gloamingforest/client/event/ClientGameEvents.java (customizeSplashes)
//   src/main/java/gloamingforest/item/MoonDialItem.java (April Fools)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/Holiday.h"

#include <ctime>

namespace gf::core::holiday {

namespace {
/// Java's MonthDay ordering: month first, then day.
int ordinal(MonthDay d) { return d.month * 32 + d.day; }
} // namespace

MonthDay today() {
    std::time_t now = std::time(nullptr);
    std::tm     local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return {local.tm_mon + 1, local.tm_mday};
}

bool isHalloweenWeek(MonthDay date) {
    // now.isAfter(MonthDay.of(Month.OCTOBER, 19)) && now.isBefore(MonthDay.of(Month.NOVEMBER, 4))
    return ordinal(date) > ordinal({10, 19}) && ordinal(date) < ordinal({11, 4});
}

bool isHalloweenWeek() { return isHalloweenWeek(today()); }

bool isGloamingBirthday(MonthDay date) { return date.month == 8 && date.day == 19; }

int gloamingAge(int year) { return year - 2011; }

bool isAprilFools(MonthDay date) { return date.month == 4 && date.day == 1; }

} // namespace gf::core::holiday
