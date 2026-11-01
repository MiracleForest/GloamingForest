// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/Registry.h), LGPL-3.0-or-later.
#pragma once

#include <vector>

/// Token pasting helpers for unique static registration objects.
#define GF_CONCAT_IMPL(a, b) a##b
#define GF_CONCAT(a, b)      GF_CONCAT_IMPL(a, b)
#define GF_UNIQUE(prefix)    GF_CONCAT(prefix, __COUNTER__)

namespace gf {

/// A process-wide list of `Entry`, filled by static registrar objects before `main`/mod load.
/// The function-local static avoids static-initialization-order problems.
///
/// Registrations live in translation units linked directly into the mod DLL (never into a
/// static library), so the linker can't drop them.
template <class Entry>
class StaticRegistry {
public:
    static std::vector<Entry>& entries() {
        static std::vector<Entry> v;
        return v;
    }
    static void add(Entry e) { entries().push_back(std::move(e)); }
};

} // namespace gf
