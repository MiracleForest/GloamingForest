// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/equipment/Enchants.cpp, src/core/equipment/Gear.cpp), LGPL-3.0-or-later.
//
// The enchantment part of contract "enchant+effects" (F9; spike S6).
//
// Bedrock's enchantment registry is a closed enum (Enchant::Type, the 42 ids vanilla clients know),
// so TF's enchantments (fire_react, chill_aura, destruction, renewal: data/twilightforest/enchantment,
// registered from the data at load, an owner's registerType replaces the data's spec) live in the
// item's user data (`gf:enchant` {id: level}, books too: Java's stored enchantments). Clients see
// them as gray lore lines named like enchantments (written through the item runtime's lore) and
// the glint (an empty vanilla enchantment list, spike S6). The engine's enchanting paths behave as
// Java's:
// - anvil (EnchantUtils::combineEnchantedItems): the second item's custom enchantments join the
//   first as AnvilMenu does (equal levels + 1 up to the max, else the higher; exclusive sets and
//   supported items checked, books apply to anything), at anvilCost * level (books: half);
// - grindstone (EnchantUtils::removeEnchants / CraftHandlerGrindstone::_getExperienceFromItem):
//   every custom enchantment goes (none is a curse), giving its min cost as experience;
// - enchanting table: only enchantments in #minecraft:in_enchanting_table are offered; TF 4.8 puts
//   none of its four there, so the table never gives them (the roll below serves registered specs
//   with `discoverable`).
#include "feature/enchant/Enchant.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <set>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/managers/models/EnchantingContainerManagerModel.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/crafting/CraftHandlerEnchant.h"
#include "mc/world/inventory/network/crafting/CraftHandlerGrindstone.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchantOption.h"
#include "mc/world/item/enchanting/ItemEnchants.h"

#include "feature/enchant/EnchantInternal.h"
#include "feature/item/DataFiles.h"
#include "feature/item/ItemInternal.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::enchant {

namespace {

namespace data = feature::item::data;
using Json     = nlohmann::json;

constexpr char kDataKey[] = "enchant"; ///< "gf:enchant" in the user data

struct Entry {
    Spec spec;
    bool fromData = false;
};

struct Store {
    std::recursive_mutex         mutex;
    std::map<std::string, Entry> specs;
};

Store& store() {
    static Store s;
    return s;
}

std::string fullId(std::string const& id) { return id.find(':') == std::string::npos ? "minecraft:" + id : id; }

bool isBook(std::string const& itemId) { return itemId == "minecraft:book" || itemId == "minecraft:enchanted_book"; }

/// Java ids of the vanilla enchantments whose Bedrock name differs.
std::string bedrockName(std::string const& javaId) {
    auto name = javaId.starts_with("minecraft:") ? javaId.substr(10) : javaId;
    if (name == "binding_curse") return "binding";
    if (name == "vanishing_curse") return "vanishing";
    if (name == "sweeping_edge") return "sweeping";
    return name;
}

std::optional<Enchant::Type> vanillaType(std::string const& id) {
    if (!id.starts_with("minecraft:")) return std::nullopt;
    auto& names = Enchant::mEnchantNameToType();
    auto  it    = names.find(HashedString(bedrockName(id)));
    if (it == names.end()) return std::nullopt;
    return it->second;
}

/// "fire_react" -> "Fire React" (the English fallback of a missing translation).
std::string englishName(std::string const& id) {
    auto        name = id.substr(id.find(':') + 1);
    std::string out;
    bool        up = true;
    for (char c : name) {
        if (c == '_') {
            out += ' ';
            up = true;
        } else {
            out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            up = false;
        }
    }
    return out;
}

// ---- data --------------------------------------------------------------------------------------------

std::string stringOrFirst(Json const& j) {
    if (j.is_string()) return j.get<std::string>();
    if (j.is_array() && !j.empty() && j[0].is_string()) return j[0].get<std::string>();
    return {};
}

Spec specOfJson(std::string const& id, Json const& j) {
    Spec s;
    s.maxLevel  = j.value("max_level", 1);
    s.weight    = j.value("weight", 1);
    s.anvilCost = j.value("anvil_cost", 1);
    if (auto it = j.find("min_cost"); it != j.end() && it->is_object()) s.minCost = {it->value("base", 1), it->value("per_level_above_first", 10)};
    if (auto it = j.find("max_cost"); it != j.end() && it->is_object()) s.maxCost = {it->value("base", 6), it->value("per_level_above_first", 10)};
    s.supportedItems = stringOrFirst(j.value("supported_items", Json()));
    s.primaryItems   = stringOrFirst(j.value("primary_items", Json()));
    if (auto it = j.find("exclusive_set"); it != j.end()) {
        if (it->is_string()) s.exclusiveWith.push_back(it->get<std::string>());
        else if (it->is_array())
            for (auto const& e : *it)
                if (e.is_string()) s.exclusiveWith.push_back(e.get<std::string>());
    }
    if (auto it = j.find("description"); it != j.end() && it->is_object()) s.nameKey = it->value("translate", std::string());
    if (s.nameKey.empty()) {
        auto ns   = id.substr(0, id.find(':'));
        s.nameKey = "enchantment." + ns + "." + id.substr(id.find(':') + 1);
    }
    // vanilla's enchantment tags decide the rest; TF 4.8 adds its enchantments to none of them
    s.treasure     = tags::has(tags::Registry::Enchantment, "minecraft:treasure", id);
    s.tradeable    = tags::has(tags::Registry::Enchantment, "minecraft:tradeable", id);
    s.discoverable = tags::has(tags::Registry::Enchantment, "minecraft:in_enchanting_table", id);
    return s;
}

void loadData() {
    auto&           st = store();
    std::lock_guard lock(st.mutex);
    std::erase_if(st.specs, [](auto const& e) { return e.second.fromData; });
    int n = 0;
    for (auto const& f : data::list("enchantment")) {
        if (f.ns == "minecraft") continue; // vanilla's are Bedrock's own
        auto id = f.id();
        if (st.specs.contains(id)) continue; // an owner's registration wins
        auto doc = data::read(f.path);
        if (!doc.is_object()) continue;
        st.specs[id] = {specOfJson(id, doc), true};
        ++n;
    }
    log().info("enchant: {} custom enchantments from data", n);
}

[[maybe_unused]] bool const kRegistered = (data::onReload(&loadData), true);

// ---- storage -----------------------------------------------------------------------------------------

Json storedOf(ItemStack const& stack) {
    auto j = game::items::getData(stack, kDataKey);
    return j.is_object() ? j : Json::object();
}

void writeStored(ItemStack& stack, Json const& j) { game::items::setData(stack, kDataKey, j.empty() ? Json() : j); }

int vanillaLevel(ItemStack const& stack, Enchant::Type type) { return EnchantUtils::getEnchantLevel(type, stack); }

/// Whether `id` (custom or vanilla, or a "#tag" of enchantments) matches `other`.
bool sameOrTagged(std::string const& entry, std::string const& other) {
    if (entry.starts_with('#')) return tags::has(tags::Registry::Enchantment, entry.substr(1), other);
    return fullId(entry) == other;
}

/// Java's Enchantment#areCompatible: neither lists the other in its exclusive set.
bool compatible(std::string const& a, std::string const& b) {
    if (a == b) return true;
    auto sa = internal::specOf(a), sb = internal::specOf(b);
    if (sa)
        for (auto const& e : sa->exclusiveWith)
            if (sameOrTagged(e, b)) return false;
    if (sb)
        for (auto const& e : sb->exclusiveWith)
            if (sameOrTagged(e, a)) return false;
    return true;
}

} // namespace

// ---- public ------------------------------------------------------------------------------------------

void registerType(std::string const& enchantmentId, Spec spec) {
    auto&           st = store();
    std::lock_guard lock(st.mutex);
    auto            id = fullId(enchantmentId);
    if (spec.nameKey.empty()) spec.nameKey = "enchantment." + id.substr(0, id.find(':')) + "." + id.substr(id.find(':') + 1);
    st.specs[id] = {std::move(spec), false};
}

int level(ItemStack const& stack, std::string const& enchantmentId) {
    if (game::items::isEmpty(stack)) return 0;
    auto id = fullId(enchantmentId);
    if (auto type = vanillaType(id)) return vanillaLevel(stack, *type);
    auto j  = storedOf(stack);
    auto it = j.find(id);
    return it != j.end() && it->is_number_integer() ? std::clamp(it->get<int>(), 0, 255) : 0;
}

void set(ItemStack& stack, std::string const& enchantmentId, int lvl) {
    if (game::items::isEmpty(stack)) return;
    auto id = fullId(enchantmentId);
    lvl     = std::clamp(lvl, 0, 255);
    if (auto type = vanillaType(id)) {
        auto enchants  = stack.constructItemEnchantsFromUserData();
        auto instances = enchants.getEnchantInstances();
        for (auto& list : instances) std::erase_if(list, [&](EnchantmentInstance const& e) { return e.mEnchantType == *type; });
        enchants.setEnchantInstances(instances);
        stack.saveEnchantsToUserData(enchants); // without the old level
        if (lvl > 0) {
            EnchantmentInstance inst;
            inst.mEnchantType = *type;
            inst.mLevel       = lvl;
            EnchantUtils::applyEnchant(stack, inst, true);
        }
    } else {
        auto j = storedOf(stack);
        if (lvl > 0) j[id] = lvl;
        else j.erase(id);
        writeStored(stack, j);
    }
    internal::updateGlint(stack);
    item::internal::refreshLore(stack, nullptr);
}

std::map<std::string, int> all(ItemStack const& stack) {
    std::map<std::string, int> out;
    if (game::items::isEmpty(stack)) return out;
    for (auto const& inst : stack.constructItemEnchantsFromUserData().getAllEnchants()) {
        auto type = static_cast<Enchant::Type>(inst.mEnchantType);
        for (auto const& [name, t] : Enchant::mEnchantNameToType()) {
            if (t != type) continue;
            out["minecraft:" + name.getString()] = inst.mLevel;
            break;
        }
    }
    for (auto const& [id, lvl] : internal::custom(stack)) out[id] = lvl;
    return out;
}

// ---- internals ---------------------------------------------------------------------------------------

namespace internal {

std::map<std::string, int> custom(ItemStack const& stack) {
    std::map<std::string, int> out;
    if (game::items::isEmpty(stack)) return out;
    auto stored = storedOf(stack); // a named copy: items() of a temporary would dangle
    for (auto& [id, v] : stored.items())
        if (v.is_number_integer() && v.get<int>() > 0) out[id] = v.get<int>();
    return out;
}

bool isCustom(std::string const& enchantmentId) {
    auto&           st = store();
    std::lock_guard lock(st.mutex);
    return st.specs.contains(fullId(enchantmentId));
}

std::optional<Spec> specOf(std::string const& enchantmentId) {
    auto&           st = store();
    std::lock_guard lock(st.mutex);
    auto            it = st.specs.find(fullId(enchantmentId));
    if (it == st.specs.end()) return std::nullopt;
    return it->second.spec;
}

bool supports(std::string const& enchantmentId, std::string const& itemId) {
    if (isBook(itemId)) return true;
    auto spec = specOf(enchantmentId);
    if (!spec) return false;
    auto const& s = spec->supportedItems;
    if (s.empty()) return true;
    if (s.starts_with('#')) return tags::has(tags::Registry::Item, s.substr(1), itemId);
    return fullId(s) == fullId(itemId);
}

std::string levelName(int lvl) {
    static constexpr char const* names[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    if (lvl >= 1 && lvl <= 10) return names[lvl - 1];
    return std::to_string(lvl);
}

std::vector<std::string> loreLines(ItemStack const& stack, Player const* viewer) {
    std::vector<std::string> out;
    for (auto const& [id, lvl] : custom(stack)) {
        auto spec = specOf(id);
        auto key  = spec ? spec->nameKey : "enchantment." + id.substr(0, id.find(':')) + "." + id.substr(id.find(':') + 1);
        auto name = viewer ? trFor(*viewer, key, englishName(id)) : tr(key, englishName(id));
        // Java's Enchantment#getFullname: the level unless it is I of a single-level enchantment
        if (lvl != 1 || !spec || spec->maxLevel != 1) {
            auto levelKey = "enchantment.level." + std::to_string(lvl);
            name += " " + (viewer ? trFor(*viewer, levelKey, levelName(lvl)) : tr(levelKey, levelName(lvl)));
        }
        out.push_back("\xC2\xA7" "7" + name);
    }
    return out;
}

void updateGlint(ItemStack& stack) {
    if (game::items::isEmpty(stack)) return;
    bool  hasCustom  = !custom(stack).empty();
    auto  enchants   = stack.constructItemEnchantsFromUserData().getAllEnchants();
    auto* tag        = stack.mUserData.get();
    bool  hasList    = tag && tag->contains(ItemStackBase::TAG_ENCHANTS());
    if (hasCustom && !hasList) {
        auto copy = tag ? tag->clone() : std::make_unique<CompoundTag>();
        copy->put(ItemStackBase::TAG_ENCHANTS(), std::make_unique<ListTag>());
        stack.setUserData(std::move(copy));
    } else if (!hasCustom && hasList && enchants.empty()) {
        // the marker only (no vanilla enchantment left): remove it
        auto copy = tag->clone();
        copy->erase(ItemStackBase::TAG_ENCHANTS());
        if (copy->empty()) stack.setUserData(nullptr);
        else stack.setUserData(std::move(copy));
    }
}

int mergeAnvil(ItemStack const& first, ItemStack const& second, ItemStack& out, bool bookEnchant) {
    if (game::items::isEmpty(out)) return 0;
    auto outId  = game::items::idOf(out);
    auto mine   = custom(first);
    auto theirs = custom(second);
    // the anvil's copy of the first item keeps the first item's custom enchantments
    auto stored = Json::object();
    for (auto const& [id, lvl] : mine) stored[id] = lvl;
    int cost = 0;
    for (auto const& [id, lvl2] : theirs) {
        auto spec = specOf(id);
        int  lvl1 = mine.contains(id) ? mine[id] : 0;
        int  lvl  = lvl1 == lvl2 ? lvl2 + 1 : std::max(lvl1, lvl2);
        bool ok   = supports(id, outId);
        // conflicts with the first item's other enchantments (custom and vanilla)
        for (auto const& [other, l] : all(first)) {
            if (other == id) continue;
            if (!compatible(id, other)) {
                ok = false;
                ++cost;
            }
        }
        if (!ok) continue;
        if (spec) lvl = std::min(lvl, spec->maxLevel);
        stored[id] = lvl;
        int anvil  = spec ? spec->anvilCost : 1;
        if (bookEnchant) anvil = std::max(1, anvil / 2);
        cost += anvil * lvl;
    }
    writeStored(out, stored);
    updateGlint(out);
    item::internal::refreshLore(out, nullptr);
    return cost;
}

bool grind(ItemStack& stack) {
    if (custom(stack).empty() && game::items::getData(stack, kDataKey).is_null()) return false;
    writeStored(stack, Json::object());
    updateGlint(stack);
    item::internal::refreshLore(stack, nullptr);
    return true;
}

int grindExperience(ItemStack const& stack) {
    int xp = 0;
    for (auto const& [id, lvl] : custom(stack))
        if (auto spec = specOf(id)) xp += spec->minCost.base + spec->minCost.perLevelAboveFirst * (lvl - 1);
    return xp;
}

} // namespace internal

// ---- the enchanting table (Java's EnchantmentHelper#selectEnchantment) --------------------------------

namespace {

/// java.util.Random (the table rolls with the player's enchantment seed + the option).
class JavaRandom {
public:
    explicit JavaRandom(std::int64_t seed) { setSeed(seed); }
    void setSeed(std::int64_t seed) { mSeed = static_cast<std::int64_t>((static_cast<std::uint64_t>(seed) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1)); }
    int  next(int bits) {
        auto s = (static_cast<std::uint64_t>(mSeed) * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
        mSeed  = static_cast<std::int64_t>(s);
        return static_cast<int>(static_cast<std::uint32_t>(s >> (48 - bits)));
    }
    int nextInt(int bound) {
        if (bound <= 0) return 0;
        if ((bound & -bound) == bound) return static_cast<int>((static_cast<std::int64_t>(bound) * next(31)) >> 31);
        int bits, val;
        do {
            bits = next(31);
            val  = bits % bound;
        } while (static_cast<std::int32_t>(static_cast<std::uint32_t>(bits) - static_cast<std::uint32_t>(val) + static_cast<std::uint32_t>(bound - 1)) < 0);
        return val;
    }
    float nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }

private:
    std::int64_t mSeed = 0;
};

struct Candidate {
    std::string key; ///< an enchantment id ("minecraft:sharpness", "twilightforest:x")
    int         level  = 1;
    int         weight = 1;
};

std::optional<Candidate> weightedPick(JavaRandom& random, std::vector<Candidate> const& list) {
    int total = 0;
    for (auto const& c : list) total += c.weight;
    if (total <= 0) return std::nullopt;
    int r = random.nextInt(total);
    for (auto const& c : list) {
        r -= c.weight;
        if (r < 0) return c;
    }
    return std::nullopt;
}

int javaWeight(Enchant::Frequency f) {
    switch (f) {
    case Enchant::Frequency::Common:
        return 10;
    case Enchant::Frequency::Uncommon:
        return 5;
    case Enchant::Frequency::Rare:
        return 2;
    default:
        return 1;
    }
}

/// Java's getAvailableEnchantmentResults over Bedrock's registry and the discoverable custom specs.
std::vector<Candidate> available(std::string const& itemId, int cost) {
    std::vector<Candidate> out;
    auto                   ref  = game::items::make(itemId);
    Item const*            item = ref ? ref->mItem.get() : nullptr;
    bool                   book = isBook(itemId);
    int                    slot = item ? item->getEnchantSlot() : 0;
    for (auto const& e : Enchant::mEnchants()) {
        if (!e || e->mIsDisabled || e->isTreasureOnly() || !e->isDiscoverable()) continue;
        if (!book && (e->mPrimarySlots & slot) == 0) continue;
        for (int l = e->getMaxLevel(); l >= e->getMinLevel(); --l) {
            if (cost >= e->getMinCost(l) && cost <= e->getMaxCost(l)) {
                out.push_back({"minecraft:" + e->mStringId->getString(), l, javaWeight(e->mFrequency)});
                break;
            }
        }
    }
    auto& st = store();
    std::lock_guard lock(st.mutex);
    for (auto const& [id, entry] : st.specs) {
        auto const& s = entry.spec;
        if (!s.discoverable || s.treasure) continue;
        auto const& primary = s.primaryItems.empty() ? s.supportedItems : s.primaryItems;
        bool        fits    = book || primary.empty()
                     || (primary.starts_with('#') ? tags::has(tags::Registry::Item, primary.substr(1), itemId) : fullId(primary) == itemId);
        if (!fits) continue;
        for (int l = s.maxLevel; l >= 1; --l) {
            int lo = s.minCost.base + s.minCost.perLevelAboveFirst * (l - 1);
            int hi = s.maxCost.base + s.maxCost.perLevelAboveFirst * (l - 1);
            if (cost >= lo && cost <= hi) {
                out.push_back({id, l, s.weight});
                break;
            }
        }
    }
    return out;
}

bool candidatesCompatible(Candidate const& a, Candidate const& b) {
    auto va = vanillaType(a.key), vb = vanillaType(b.key);
    if (va && vb) {
        auto const& all = Enchant::mEnchants();
        auto        ia  = static_cast<std::size_t>(*va);
        if (ia >= all.size() || !all[ia]) return true;
        return all[ia]->isCompatibleWith(*vb);
    }
    return compatible(a.key, b.key);
}

bool anyDiscoverable() {
    auto&           st = store();
    std::lock_guard lock(st.mutex);
    for (auto const& [id, e] : st.specs)
        if (e.spec.discoverable && !e.spec.treasure) return true;
    return false;
}

} // namespace

namespace internal {

std::map<std::string, int> rollTable(std::string const& itemId, int enchantValue, int cost, int seed, int option) {
    std::map<std::string, int> out;
    if (enchantValue <= 0 || cost <= 0) return out;
    JavaRandom random(static_cast<std::int64_t>(seed) + option);
    cost += 1 + random.nextInt(enchantValue / 4 + 1) + random.nextInt(enchantValue / 4 + 1);
    float f = (random.nextFloat() + random.nextFloat() - 1.0f) * 0.15f;
    cost    = std::max(1, static_cast<int>(std::lround(static_cast<float>(cost) + static_cast<float>(cost) * f)));
    auto list = available(itemId, cost);
    std::vector<Candidate> picks;
    if (list.empty()) return out;
    if (auto c = weightedPick(random, list)) picks.push_back(*c);
    while (random.nextInt(50) <= cost) {
        if (!picks.empty()) {
            auto const& last = picks.back();
            std::erase_if(list, [&](Candidate const& c) { return c.key == last.key || !candidatesCompatible(last, c); });
        }
        if (list.empty()) break;
        if (auto c = weightedPick(random, list)) picks.push_back(*c);
        cost /= 2;
    }
    // Java's EnchantmentMenu#getEnchantmentList: a book keeps all but one random pick
    if (isBook(itemId) && picks.size() > 1) picks.erase(picks.begin() + random.nextInt(static_cast<int>(picks.size())));
    for (auto const& p : picks)
        if (!vanillaType(p.key)) out[p.key] = p.level;
    return out;
}

} // namespace internal

namespace {

struct Rolled {
    std::string                             itemId;
    std::vector<std::map<std::string, int>> options; ///< the custom enchantments of each option
    std::vector<std::vector<std::pair<int, int>>> vanilla; ///< (type, level) of each option, sorted
};
std::map<std::int64_t, Rolled>& rolled() {
    static std::map<std::int64_t, Rolled> m;
    return m;
}
thread_local Player* gEnchanting = nullptr;
thread_local bool    gWriting    = false;

std::vector<std::pair<int, int>> vanillaOf(ItemEnchants const& e) {
    std::vector<std::pair<int, int>> out;
    for (auto const& inst : e.getAllEnchants()) out.emplace_back(static_cast<int>(static_cast<Enchant::Type>(inst.mEnchantType)), static_cast<int>(inst.mLevel));
    std::sort(out.begin(), out.end());
    return out;
}

void onOptions(EnchantingContainerManagerModel& model) {
    if (!anyDiscoverable()) return;
    Player& player = model.mPlayer;
    if (player.isClientSide()) return;
    ItemStack input;
    for (auto const& s : model.getItemCopies()) {
        if (game::items::isEmpty(s) || game::items::idOf(s) == "minecraft:lapis_lazuli") continue;
        input = s;
        break;
    }
    auto& r = rolled()[player.getOrCreateUniqueID().rawID];
    r       = {};
    if (game::items::isEmpty(input)) return;
    r.itemId          = game::items::idOf(input);
    Item const* item  = input.mItem.get();
    int         value = item ? item->getEnchantValue() : 0;
    if (isBook(r.itemId) && value <= 0) value = 1;
    auto const& options = model.mEnchantOptions.get();
    for (std::size_t i = 0; i < options.size(); ++i) {
        r.vanilla.push_back(vanillaOf(options[i].mEnchants.get()));
        r.options.push_back(internal::rollTable(r.itemId, value, options[i].mCost, player.mEnchantmentSeed, static_cast<int>(i)));
    }
}

void onEnchanted(ItemStackBase& stack, ItemEnchants const& enchants) {
    if (!gEnchanting || gWriting) return;
    auto it = rolled().find(gEnchanting->getOrCreateUniqueID().rawID);
    if (it == rolled().end()) return;
    auto set = vanillaOf(enchants);
    for (std::size_t i = 0; i < it->second.options.size() && i < it->second.vanilla.size(); ++i) {
        if (it->second.vanilla[i] != set || it->second.options[i].empty()) continue;
        gWriting   = true;
        auto copy  = item::internal::stackOf(stack);
        auto j     = storedOf(copy);
        for (auto const& [id, lvl] : it->second.options[i]) j[id] = std::max(lvl, j.value(id, 0));
        // straight into the engine's stack: the user data of a copy, then the lore
        writeStored(copy, j);
        internal::updateGlint(copy);
        item::internal::refreshLore(copy, gEnchanting);
        if (copy.mUserData) stack.setUserData(copy.mUserData->clone());
        gWriting = false;
        log().info("enchant: {} got table enchantments on {}", gEnchanting->getRealName(), game::items::idOf(copy));
        return;
    }
}

} // namespace

} // namespace gf::enchant

namespace en = gf::enchant;

LL_TYPE_INSTANCE_HOOK(EnchantOptionsHook, ll::memory::HookPriority::Normal, EnchantingContainerManagerModel, &EnchantingContainerManagerModel::recalculateOptions, void) {
    origin();
    try {
        en::onOptions(*this);
    } catch (std::exception const& e) {
        gf::log().error("enchant: table options failed: {}", e.what());
    }
}

LL_TYPE_INSTANCE_HOOK(
    EnchantCraftHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerEnchant,
    &CraftHandlerEnchant::$_handleCraftAction,
    ItemStackNetResult,
    ItemStackRequestActionCraftBase const& action
) {
    struct Restore {
        Player* prev;
        ~Restore() { en::gEnchanting = prev; }
    } restore{en::gEnchanting};
    en::gEnchanting = &static_cast<Player&>(mPlayer);
    return origin(action);
}

LL_TYPE_INSTANCE_HOOK(
    EnchantSaveHook,
    ll::memory::HookPriority::Normal,
    ItemStackBase,
    &ItemStackBase::saveEnchantsToUserData,
    void,
    ItemEnchants const& enchants
) {
    origin(enchants);
    try {
        en::onEnchanted(*this, enchants);
    } catch (std::exception const& e) {
        gf::log().error("enchant: table enchanting failed: {}", e.what());
    }
}

// Java's anvil for the custom enchantments (see the top of the file).
LL_STATIC_HOOK(
    EnchantAnvilHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::combineEnchantedItems,
    int,
    ItemStack const& first,
    ItemStack const& second,
    ItemStack&       out,
    bool             bookEnchant
) {
    int cost = origin(first, second, out, bookEnchant);
    try {
        if (en::internal::custom(first).empty() && en::internal::custom(second).empty()) return cost;
        return cost + en::internal::mergeAnvil(first, second, out, bookEnchant);
    } catch (std::exception const& e) {
        gf::log().error("enchant: anvil merge failed: {}", e.what());
        return cost;
    }
}

// Java's grindstone removes every non-curse enchantment, the custom ones included.
LL_STATIC_HOOK(EnchantGrindHook, ll::memory::HookPriority::Normal, &EnchantUtils::removeEnchants, void, ItemStack& out) {
    origin(out);
    try {
        en::internal::grind(out);
    } catch (std::exception const& e) {
        gf::log().error("enchant: grindstone failed: {}", e.what());
    }
}

LL_STATIC_HOOK(
    EnchantGrindXpHook,
    ll::memory::HookPriority::Normal,
    &CraftHandlerGrindstone::_getExperienceFromItem,
    int,
    ItemStack const& stack
) {
    int xp = origin(stack);
    try {
        return xp + en::internal::grindExperience(stack);
    } catch (...) {
        return xp;
    }
}

namespace gf::enchant {
namespace {
class EnchantModule {
public:
    void install() {
        EnchantOptionsHook::hook();
        EnchantCraftHook::hook();
        EnchantSaveHook::hook();
        EnchantAnvilHook::hook();
        EnchantGrindHook::hook();
        EnchantGrindXpHook::hook();
    }
    void uninstall() {
        EnchantGrindXpHook::unhook();
        EnchantGrindHook::unhook();
        EnchantAnvilHook::unhook();
        EnchantSaveHook::unhook();
        EnchantCraftHook::unhook();
        EnchantOptionsHook::unhook();
        rolled().clear();
    }
};
} // namespace
} // namespace gf::enchant

GF_REGISTER_MODULE_ORDERED(gf::enchant::EnchantModule, gf::ModuleOrder::Contracts);
