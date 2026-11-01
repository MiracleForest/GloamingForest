// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Java block states -> Bedrock blocks for the structure kit: TF's structure code and its
// templates name vanilla blocks by their Java ids and Java properties (facing, half, type,
// axis, ...). This maps ids that differ on Bedrock, turns the properties into the Bedrock states
// of the block (whichever of the known encodings the block has), and rotates / mirrors Bedrock
// blocks the way BlockState#rotate / #mirror turn Java ones. TF's own blocks keep their ids; their
// properties are matched by name against the frozen Bedrock states (docs/spec/blockstates.md):
// "<ns>:<property>", "<property>", and the vanilla traits for facing / axis / slab halves.
#include "feature/structure/Internal.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/material/Material.h"

#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::structure {

// ---- orientation maths ------------------------------------------------------------------------------

Orientation rotateDir(Orientation dir, Rotation rotation) {
    int steps = rotationIndex(rotation);
    return static_cast<Orientation>((static_cast<int>(dir) + steps) & 3);
}
Orientation opposite(Orientation dir) { return static_cast<Orientation>((static_cast<int>(dir) + 2) & 3); }
Orientation clockwise(Orientation dir) { return static_cast<Orientation>((static_cast<int>(dir) + 1) & 3); }
int         stepX(Orientation dir) { return dir == Orientation::East ? 1 : dir == Orientation::West ? -1 : 0; }
int         stepZ(Orientation dir) { return dir == Orientation::South ? 1 : dir == Orientation::North ? -1 : 0; }
int         data2D(Orientation dir) {
    switch (dir) {
    case Orientation::South:
        return 0;
    case Orientation::West:
        return 1;
    case Orientation::North:
        return 2;
    default:
        return 3;
    }
}
Orientation from2D(int value) {
    switch (value & 3) {
    case 0:
        return Orientation::South;
    case 1:
        return Orientation::West;
    case 2:
        return Orientation::North;
    default:
        return Orientation::East;
    }
}
int rotationIndex(Rotation rotation) {
    switch (rotation) {
    case Rotation::Clockwise90:
        return 1;
    case Rotation::Clockwise180:
        return 2;
    case Rotation::Counterclockwise90:
        return 3;
    default:
        return 0;
    }
}
Rotation rotationFromIndex(int index) {
    switch (index & 3) {
    case 1:
        return Rotation::Clockwise90;
    case 2:
        return Rotation::Clockwise180;
    case 3:
        return Rotation::Counterclockwise90;
    default:
        return Rotation::None;
    }
}
Rotation addRotation(Rotation a, Rotation b) { return rotationFromIndex(rotationIndex(a) + rotationIndex(b)); }
Rotation relativeRotation(Orientation from, Orientation to) {
    return rotationFromIndex((static_cast<int>(to) - static_cast<int>(from) + 4) & 3);
}

BlockPos transformPos(BlockPos const& target, Mirror mirror, Rotation rotation, BlockPos const& offset) {
    int  i    = target.x;
    int  j    = target.y;
    int  k    = target.z;
    bool flag = true;
    switch (mirror) {
    case Mirror::LeftRight:
        k = -k;
        break;
    case Mirror::FrontBack:
        i = -i;
        break;
    default:
        flag = false;
    }
    int l  = offset.x;
    int i1 = offset.z;
    switch (rotation) {
    case Rotation::Counterclockwise90:
        return BlockPos{l - i1 + k, j, l + i1 - i};
    case Rotation::Clockwise90:
        return BlockPos{l + i1 - k, j, i1 - l + i};
    case Rotation::Clockwise180:
        return BlockPos{l + l - i, j, i1 + i1 - k};
    default:
        return flag ? BlockPos{i, j, k} : target;
    }
}

BlockBox templateBox(BlockPos const& start, Rotation rotation, BlockPos const& pivot, Mirror mirror, int sx, int sy, int sz) {
    BlockPos a = transformPos(BlockPos{0, 0, 0}, mirror, rotation, pivot);
    BlockPos b = transformPos(BlockPos{sx - 1, sy - 1, sz - 1}, mirror, rotation, pivot);
    return moved(BlockBox::of(a.x, a.y, a.z, b.x, b.y, b.z), start.x, start.y, start.z);
}

BlockPos boxCenter(BlockBox const& box) {
    return BlockPos{box.minX + (box.maxX - box.minX + 1) / 2, box.minY + (box.maxY - box.minY + 1) / 2, box.minZ + (box.maxZ - box.minZ + 1) / 2};
}
BlockBox moved(BlockBox box, int dx, int dy, int dz) {
    box.minX += dx;
    box.maxX += dx;
    box.minY += dy;
    box.maxY += dy;
    box.minZ += dz;
    box.maxZ += dz;
    return box;
}
std::optional<BlockBox> intersection(BlockBox const& a, BlockBox const& b) {
    if (!a.intersects(b)) return std::nullopt;
    return BlockBox{std::max(a.minX, b.minX), std::max(a.minY, b.minY), std::max(a.minZ, b.minZ),
                    std::min(a.maxX, b.maxX), std::min(a.maxY, b.maxY), std::min(a.maxZ, b.maxZ)};
}
Json boxToJson(BlockBox const& b) { return Json::array({b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ}); }
BlockBox boxFromJson(Json const& j) {
    if (j.is_array() && j.size() == 6) return BlockBox{j[0].get<int>(), j[1].get<int>(), j[2].get<int>(), j[3].get<int>(), j[4].get<int>(), j[5].get<int>()};
    if (j.is_object())
        return BlockBox{j.value("minX", 0), j.value("minY", 0), j.value("minZ", 0), j.value("maxX", 0), j.value("maxY", 0), j.value("maxZ", 0)};
    return {};
}

void warnOnce(std::string const& key, std::string const& message) {
    static std::mutex                      mutex;
    static std::unordered_set<std::string> seen;
    {
        std::lock_guard lock(mutex);
        if (!seen.insert(key).second) return;
    }
    log().warn("{}", message);
}

namespace blocks {

namespace {

// Java id -> Bedrock candidates (first that exists wins). Only ids that differ (or that exist on
// Bedrock with another meaning) are listed; every other Java id is looked up as it is.
std::unordered_map<std::string, std::vector<std::string>> const& renames() {
    static std::unordered_map<std::string, std::vector<std::string>> const table{
        {"minecraft:cave_air", {"minecraft:air"}},
        {"minecraft:void_air", {"minecraft:air"}},
        {"minecraft:spawner", {"minecraft:mob_spawner"}},
        {"minecraft:cobweb", {"minecraft:web"}},
        {"minecraft:grass_block", {"minecraft:grass_block", "minecraft:grass"}},
        {"minecraft:short_grass", {"minecraft:short_grass", "minecraft:tallgrass"}},
        {"minecraft:dirt_path", {"minecraft:grass_path"}},
        {"minecraft:snow", {"minecraft:snow_layer"}},
        {"minecraft:snow_block", {"minecraft:snow"}},
        {"minecraft:jack_o_lantern", {"minecraft:lit_pumpkin"}},
        {"minecraft:terracotta", {"minecraft:hardened_clay"}},
        {"minecraft:magma_block", {"minecraft:magma"}},
        {"minecraft:nether_bricks", {"minecraft:nether_brick"}},
        {"minecraft:red_nether_bricks", {"minecraft:red_nether_brick"}},
        {"minecraft:end_stone_bricks", {"minecraft:end_bricks"}},
        {"minecraft:melon", {"minecraft:melon_block"}},
        {"minecraft:sugar_cane", {"minecraft:reeds"}},
        {"minecraft:lily_pad", {"minecraft:waterlily"}},
        {"minecraft:note_block", {"minecraft:noteblock"}},
        {"minecraft:slime_block", {"minecraft:slime", "minecraft:slime_block"}},
        {"minecraft:powered_rail", {"minecraft:golden_rail"}},
        {"minecraft:bricks", {"minecraft:brick_block"}},
        {"minecraft:dead_bush", {"minecraft:deadbush"}},
        {"minecraft:nether_portal", {"minecraft:portal"}},
        {"minecraft:tripwire", {"minecraft:trip_wire"}},
        {"minecraft:tripwire_hook", {"minecraft:tripwire_hook", "minecraft:trip_wire_hook"}},
        {"minecraft:chain", {"minecraft:iron_chain", "minecraft:chain"}},
        {"minecraft:stone_stairs", {"minecraft:normal_stone_stairs"}},
        {"minecraft:cobblestone_stairs", {"minecraft:stone_stairs"}},
        {"minecraft:stone_slab", {"minecraft:normal_stone_slab"}},
        {"minecraft:stone_brick_wall", {"minecraft:stone_brick_wall"}},
        {"minecraft:wall_torch", {"minecraft:torch"}},
        {"minecraft:soul_wall_torch", {"minecraft:soul_torch"}},
        {"minecraft:redstone_wall_torch", {"minecraft:redstone_torch"}},
        {"minecraft:oak_door", {"minecraft:wooden_door"}},
        {"minecraft:oak_trapdoor", {"minecraft:trapdoor"}},
        {"minecraft:oak_fence_gate", {"minecraft:fence_gate"}},
        {"minecraft:oak_button", {"minecraft:wooden_button"}},
        {"minecraft:oak_pressure_plate", {"minecraft:wooden_pressure_plate"}},
        {"minecraft:oak_sign", {"minecraft:standing_sign"}},
        {"minecraft:oak_wall_sign", {"minecraft:wall_sign"}},
        {"minecraft:spruce_sign", {"minecraft:spruce_standing_sign"}},
        {"minecraft:birch_sign", {"minecraft:birch_standing_sign"}},
        {"minecraft:jungle_sign", {"minecraft:jungle_standing_sign"}},
        {"minecraft:acacia_sign", {"minecraft:acacia_standing_sign"}},
        {"minecraft:dark_oak_sign", {"minecraft:darkoak_standing_sign"}},
        {"minecraft:dark_oak_wall_sign", {"minecraft:darkoak_wall_sign"}},
        {"minecraft:mangrove_sign", {"minecraft:mangrove_standing_sign"}},
        {"minecraft:cherry_sign", {"minecraft:cherry_standing_sign"}},
        {"minecraft:bamboo_sign", {"minecraft:bamboo_standing_sign"}},
        {"minecraft:crimson_sign", {"minecraft:crimson_standing_sign"}},
        {"minecraft:warped_sign", {"minecraft:warped_standing_sign"}},
        {"minecraft:white_bed", {"minecraft:bed"}},
        {"minecraft:red_bed", {"minecraft:bed"}},
        {"minecraft:skeleton_wall_skull", {"minecraft:skeleton_skull"}},
        {"minecraft:wither_skeleton_wall_skull", {"minecraft:wither_skeleton_skull"}},
        {"minecraft:zombie_wall_head", {"minecraft:zombie_head"}},
        {"minecraft:creeper_wall_head", {"minecraft:creeper_head"}},
        {"minecraft:player_wall_head", {"minecraft:player_head"}},
        {"minecraft:light", {"minecraft:light_block"}},
        {"minecraft:moving_piston", {"minecraft:moving_block"}},
        {"minecraft:piston_head", {"minecraft:piston_arm_collision"}},
        {"minecraft:sticky_piston_head", {"minecraft:sticky_piston_arm_collision"}},
        {"minecraft:bubble_column", {"minecraft:bubble_column"}},
        {"minecraft:rose_bush", {"minecraft:rose_bush"}},
        {"minecraft:potted_dead_bush", {"minecraft:flower_pot"}},
        {"minecraft:flower_pot", {"minecraft:flower_pot"}},
        {"minecraft:attached_melon_stem", {"minecraft:melon_stem"}},
        {"minecraft:attached_pumpkin_stem", {"minecraft:pumpkin_stem"}},
        {"minecraft:cocoa", {"minecraft:cocoa"}},
        {"minecraft:beetroots", {"minecraft:beetroot"}},
        {"minecraft:sweet_berry_bush", {"minecraft:sweet_berry_bush"}},
        {"minecraft:repeater", {"minecraft:unpowered_repeater"}},
        {"minecraft:comparator", {"minecraft:unpowered_comparator"}},
        {"minecraft:water_cauldron", {"minecraft:cauldron"}},
        {"minecraft:lava_cauldron", {"minecraft:cauldron"}},
        {"minecraft:powder_snow_cauldron", {"minecraft:cauldron"}},
        {"minecraft:cartography_table", {"minecraft:cartography_table"}},
        {"minecraft:stonecutter", {"minecraft:stonecutter_block"}},
        {"minecraft:shulker_box", {"minecraft:undyed_shulker_box"}},
        {"minecraft:sign", {"minecraft:standing_sign"}},
        {"minecraft:quartz_pillar", {"minecraft:quartz_pillar"}},
        {"minecraft:infested_stone", {"minecraft:infested_stone"}},
        {"minecraft:smooth_stone", {"minecraft:smooth_stone"}},
    };
    return table;
}

/// Java ids whose Bedrock form depends on a property value.
struct Variant {
    std::string javaId;
    std::string property;
    std::string value;
    std::string bedrockId;
};
std::vector<Variant> const& variants() {
    static std::vector<Variant> const table{
        {"minecraft:furnace", "lit", "true", "minecraft:lit_furnace"},
        {"minecraft:blast_furnace", "lit", "true", "minecraft:lit_blast_furnace"},
        {"minecraft:smoker", "lit", "true", "minecraft:lit_smoker"},
        {"minecraft:redstone_lamp", "lit", "true", "minecraft:lit_redstone_lamp"},
        {"minecraft:redstone_ore", "lit", "true", "minecraft:lit_redstone_ore"},
        {"minecraft:deepslate_redstone_ore", "lit", "true", "minecraft:lit_deepslate_redstone_ore"},
        {"minecraft:redstone_torch", "lit", "false", "minecraft:unlit_redstone_torch"},
        {"minecraft:redstone_wall_torch", "lit", "false", "minecraft:unlit_redstone_torch"},
        {"minecraft:repeater", "powered", "true", "minecraft:powered_repeater"},
        {"minecraft:comparator", "powered", "true", "minecraft:powered_comparator"},
        {"minecraft:daylight_detector", "inverted", "true", "minecraft:daylight_detector_inverted"},
    };
    return table;
}

std::shared_mutex                                         gCacheMutex;
std::unordered_map<std::string, Block const*>             gJavaCache;
std::unordered_map<std::string, std::string>              gIdCache;
std::unordered_map<std::uint64_t, Block const*>           gTransformCache; ///< (block, op) -> block

Block const* lookup(std::string const& id) { return game::blockWith(id); }

/// A state value read from a block's serialization id.
struct StateValue {
    enum class Kind { Bool, Int, String } kind = Kind::Int;
    int         i = 0;
    std::string s;
};

std::map<std::string, StateValue> readStates(Block const& block) {
    std::map<std::string, StateValue> out;
    auto const&                       nbt = *block.mSerializationId;
    if (!nbt.contains("states")) return out;
    auto const& states = nbt["states"];
    if (!states.is_object()) return out;
    for (auto const& [name, value] : states.items()) {
        StateValue v;
        if (value.is_string()) {
            v.kind = StateValue::Kind::String;
            v.s    = std::string(value.get<StringTag>());
        } else if (value.hold(Tag::Type::Byte)) {
            v.kind = StateValue::Kind::Bool;
            v.i    = static_cast<int>(value.get<ByteTag>().data) != 0 ? 1 : 0;
        } else if (value.hold(Tag::Type::Int)) {
            v.kind = StateValue::Kind::Int;
            v.i    = value.get<IntTag>().data;
        } else {
            continue;
        }
        out.emplace(name, std::move(v));
    }
    return out;
}

/// `block` with some states replaced (values typed as the block declares them); states the block
/// does not have are ignored. nullptr when the permutation does not exist.
Block const* withStates(Block const& block, std::map<std::string, StateValue> const& changes) {
    if (changes.empty()) return &block;
    auto nbt = block.mSerializationId->clone();
    if (!nbt->contains("states") || !(*nbt)["states"].is_object()) return &block;
    auto& current = (*nbt)["states"].get<CompoundTag>();
    bool  changed = false;
    for (auto const& [name, value] : changes) {
        if (!current.contains(name)) continue;
        auto& slot = current[name];
        if (slot.hold(Tag::Type::Byte)) {
            bool on = value.kind == StateValue::Kind::String ? value.s == "true" : value.i != 0;
            slot    = ByteTag(on ? 1 : 0);
        } else if (slot.hold(Tag::Type::Int)) {
            if (value.kind == StateValue::Kind::String) {
                try {
                    slot = IntTag(std::stoi(value.s));
                } catch (...) {
                    continue;
                }
            } else {
                slot = IntTag(value.i);
            }
        } else if (slot.is_string()) {
            if (value.kind == StateValue::Kind::String) slot = StringTag(value.s);
            else if (value.kind == StateValue::Kind::Bool) slot = StringTag(std::string(value.i ? "true" : "false"));
            else slot = StringTag(std::to_string(value.i));
        } else {
            continue;
        }
        changed = true;
    }
    if (!changed) return &block;
    auto found = Block::tryGetFromRegistry(*nbt);
    return found ? found.as_ptr() : nullptr;
}

StateValue sv(std::string s) {
    StateValue v;
    v.kind = StateValue::Kind::String;
    v.s    = std::move(s);
    return v;
}
StateValue iv(int i) {
    StateValue v;
    v.kind = StateValue::Kind::Int;
    v.i    = i;
    return v;
}
StateValue bv(bool b) {
    StateValue v;
    v.kind = StateValue::Kind::Bool;
    v.i    = b ? 1 : 0;
    return v;
}

// ---- direction encodings --------------------------------------------------------------------------
// Java Direction 3D order: down, up, north, south, west, east.
constexpr std::array<char const*, 6> kDir3D = {"down", "up", "north", "south", "west", "east"};

int dir3DIndex(std::string_view name) {
    for (int i = 0; i < 6; i++)
        if (name == kDir3D[i]) return i;
    return -1;
}
std::optional<Orientation> horizontal(std::string_view name) {
    if (name == "north") return Orientation::North;
    if (name == "east") return Orientation::East;
    if (name == "south") return Orientation::South;
    if (name == "west") return Orientation::West;
    return std::nullopt;
}
char const* horizontalName(Orientation o) {
    switch (o) {
    case Orientation::North:
        return "north";
    case Orientation::East:
        return "east";
    case Orientation::South:
        return "south";
    default:
        return "west";
    }
}
// Bedrock stairs / trapdoor "weirdo" order: east, west, south, north.
int weirdo(Orientation o) {
    switch (o) {
    case Orientation::East:
        return 0;
    case Orientation::West:
        return 1;
    case Orientation::South:
        return 2;
    default:
        return 3;
    }
}
Orientation fromWeirdo(int v) {
    switch (v & 3) {
    case 0:
        return Orientation::East;
    case 1:
        return Orientation::West;
    case 2:
        return Orientation::South;
    default:
        return Orientation::North;
    }
}

enum class DirectionCodec { Legacy2D, Door, Weirdo };
DirectionCodec directionCodec(Block const& block) {
    auto const& props = block.getBlockType().mProperties;
    auto        p     = static_cast<std::uint64_t>(static_cast<BlockProperty>(props));
    if (p & static_cast<std::uint64_t>(BlockProperty::Door)) return DirectionCodec::Door;
    if (p & static_cast<std::uint64_t>(BlockProperty::Trapdoor)) return DirectionCodec::Weirdo;
    return DirectionCodec::Legacy2D;
}
int encodeDirection(DirectionCodec c, Orientation o) {
    switch (c) {
    case DirectionCodec::Door:
        return (data2D(o) + 3) & 3;
    case DirectionCodec::Weirdo:
        return weirdo(o);
    default:
        return data2D(o);
    }
}
Orientation decodeDirection(DirectionCodec c, int v) {
    switch (c) {
    case DirectionCodec::Door:
        return from2D((v + 1) & 3);
    case DirectionCodec::Weirdo:
        return fromWeirdo(v);
    default:
        return from2D(v);
    }
}

bool hasFlag(Block const& block, BlockProperty flag) {
    auto p = static_cast<std::uint64_t>(static_cast<BlockProperty>(block.getBlockType().mProperties));
    return (p & static_cast<std::uint64_t>(flag)) != 0;
}

std::string nsOf(std::string_view id) {
    auto c = id.find(':');
    return c == std::string_view::npos ? std::string("minecraft") : std::string(id.substr(0, c));
}

std::string propText(Json const& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    return v.dump();
}

/// Java properties -> Bedrock state changes for `block`.
std::map<std::string, StateValue> mapProperties(Block const& block, Json const& props) {
    std::map<std::string, StateValue> out;
    if (!props.is_object() || props.empty()) return out;
    auto states = readStates(block);
    auto has    = [&](std::string const& n) { return states.contains(n); };
    auto ns     = nsOf(block.getTypeName());

    // "face" (buttons, levers, grindstones) combines with "facing"
    std::string face = props.contains("face") ? propText(props["face"]) : "";

    for (auto const& [name, value] : props.items()) {
        std::string v = propText(value);
        if (name == "waterlogged" || name == "shape" || name == "distance" || name == "snowy" || name == "face") continue;
        if (name == "facing") {
            auto h = horizontal(v);
            int  d = dir3DIndex(v);
            if (!face.empty() && face != "wall") d = face == "floor" ? 1 : 0; // buttons on floor face up
            if (has("weirdo_direction") && h) out["weirdo_direction"] = iv(weirdo(*h));
            else if (has("minecraft:cardinal_direction") && h) out["minecraft:cardinal_direction"] = sv(horizontalName(*h));
            else if (has("minecraft:facing_direction") && d >= 0) out["minecraft:facing_direction"] = sv(kDir3D[d]);
            else if (has("torch_facing_direction")) out["torch_facing_direction"] = sv(h ? horizontalName(*h) : "top");
            else if (has("facing_direction") && d >= 0) out["facing_direction"] = iv(d);
            else if (has("direction") && h) out["direction"] = iv(encodeDirection(directionCodec(block), *h));
            else if (has(ns + ":facing")) out[ns + ":facing"] = states[ns + ":facing"].kind == StateValue::Kind::Int ? iv(std::max(d, 0)) : sv(v);
            else if (has("facing")) out["facing"] = sv(v);
            continue;
        }
        if (name == "half") {
            bool upper = v == "top" || v == "upper";
            if (has("upside_down_bit")) out["upside_down_bit"] = bv(upper);
            else if (has("upper_block_bit")) out["upper_block_bit"] = bv(upper);
            else if (has("minecraft:vertical_half")) out["minecraft:vertical_half"] = sv(upper ? "top" : "bottom");
            else if (has(ns + ":half")) out[ns + ":half"] = sv(v);
            continue;
        }
        if (name == "type" && (v == "top" || v == "bottom")) {
            if (has("minecraft:vertical_half")) out["minecraft:vertical_half"] = sv(v);
            else if (has("top_slot_bit")) out["top_slot_bit"] = bv(v == "top");
            else if (has(ns + ":type")) out[ns + ":type"] = sv(v);
            continue;
        }
        if (name == "axis") {
            if (has("pillar_axis")) out["pillar_axis"] = sv(v);
            else if (has("portal_axis")) out["portal_axis"] = sv(v);
            else if (has(ns + ":axis")) out[ns + ":axis"] = sv(v);
            continue;
        }
        if (name == "open") {
            if (has("open_bit")) out["open_bit"] = bv(v == "true");
            else if (has(ns + ":open")) out[ns + ":open"] = bv(v == "true");
            continue;
        }
        if (name == "hinge") {
            if (has("door_hinge_bit")) out["door_hinge_bit"] = bv(v == "right");
            continue;
        }
        if (name == "powered") {
            if (has("button_pressed_bit")) out["button_pressed_bit"] = bv(v == "true");
            else if (has("powered_bit")) out["powered_bit"] = bv(v == "true");
            else if (has(ns + ":powered")) out[ns + ":powered"] = bv(v == "true");
            continue;
        }
        if (name == "attached" && has("attached_bit")) {
            out["attached_bit"] = bv(v == "true");
            continue;
        }
        if (name == "disarmed" && has("disarmed_bit")) {
            out["disarmed_bit"] = bv(v == "true");
            continue;
        }
        if (name == "persistent" && has("persistent_bit")) {
            out["persistent_bit"] = bv(v == "true");
            continue;
        }
        if (name == "occupied" && has("occupied_bit")) {
            out["occupied_bit"] = bv(v == "true");
            continue;
        }
        if (name == "part" && has("head_piece_bit")) {
            out["head_piece_bit"] = bv(v == "head");
            continue;
        }
        if (name == "eye" && has("end_portal_eye_bit")) {
            out["end_portal_eye_bit"] = bv(v == "true");
            continue;
        }
        if (name == "hanging" && has("hanging")) {
            out["hanging"] = bv(v == "true");
            continue;
        }
        if (name == "layers" && has("height")) {
            out["height"] = iv(std::max(0, std::atoi(v.c_str()) - 1));
            continue;
        }
        if (name == "candles" && has("candles")) {
            out["candles"] = iv(std::max(0, std::atoi(v.c_str()) - 1));
            continue;
        }
        if (name == "delay" && has("repeater_delay")) {
            out["repeater_delay"] = iv(std::max(0, std::atoi(v.c_str()) - 1));
            continue;
        }
        if (name == "bites" && has("bite_counter")) {
            out["bite_counter"] = iv(std::atoi(v.c_str()));
            continue;
        }
        if (name == "power" && has("redstone_signal")) {
            out["redstone_signal"] = iv(std::atoi(v.c_str()));
            continue;
        }
        if (name == "moisture" && has("moisturized_amount")) {
            out["moisturized_amount"] = iv(std::atoi(v.c_str()));
            continue;
        }
        if (name == "rotation" && has("ground_sign_direction")) {
            out["ground_sign_direction"] = iv(std::atoi(v.c_str()));
            continue;
        }
        if (name == "lit" && has("lit")) {
            out["lit"] = bv(v == "true");
            continue;
        }
        if ((name == "north" || name == "east" || name == "south" || name == "west") && has("vine_direction_bits")) {
            int  bit  = name == "south" ? 1 : name == "west" ? 2 : name == "north" ? 4 : 8;
            auto cur  = out.contains("vine_direction_bits") ? out["vine_direction_bits"].i : 0;
            out["vine_direction_bits"] = iv(v == "true" ? (cur | bit) : (cur & ~bit));
            continue;
        }
        if (name == "north" || name == "east" || name == "south" || name == "west" || name == "up" || name == "down") {
            if (has(ns + ":" + name)) out[ns + ":" + name] = bv(v == "true");
            continue; // fences, walls, panes connect by themselves on Bedrock
        }
        // generic: "<ns>:<name>" then "<name>"
        for (auto const& candidate : {ns + ":" + name, std::string(name)}) {
            if (!has(candidate)) continue;
            auto& st = states[candidate];
            if (st.kind == StateValue::Kind::Bool) out[candidate] = bv(v == "true");
            else if (st.kind == StateValue::Kind::Int) {
                int d = dir3DIndex(v);
                if (d >= 0) out[candidate] = iv(d);
                else if (v == "true" || v == "false") out[candidate] = iv(v == "true" ? 1 : 0);
                else out[candidate] = iv(std::atoi(v.c_str()));
            } else out[candidate] = sv(v);
            break;
        }
    }
    return out;
}

std::string cacheKey(std::string_view id, Json const& props) {
    std::string key(id);
    if (props.is_object() && !props.empty()) {
        // sorted (nlohmann::json objects are ordered by key)
        key += props.dump();
    }
    return key;
}

std::string resolveId(std::string_view javaId, Json const& props) {
    std::string id(javaId);
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    for (auto const& v : variants()) {
        if (v.javaId == id && props.is_object() && props.contains(v.property) && propText(props[v.property]) == v.value) {
            if (lookup(v.bedrockId)) return v.bedrockId;
        }
    }
    // slabs of type double: "<x>_slab" -> "<x>_double_slab"
    if (props.is_object() && props.contains("type") && propText(props["type"]) == "double" && id.size() > 5 && id.ends_with("_slab")) {
        std::string base = id.substr(0, id.size() - 5);
        auto        it   = renames().find(id);
        if (it != renames().end() && !it->second.empty() && it->second[0].ends_with("_slab"))
            base = it->second[0].substr(0, it->second[0].size() - 5);
        if (lookup(base + "_double_slab")) return base + "_double_slab";
    }
    {
        std::shared_lock lock(gCacheMutex);
        if (auto it = gIdCache.find(id); it != gIdCache.end()) return it->second;
    }
    std::string result;
    // Java's potted_* blocks are one flower pot block on Bedrock (the plant is the pot's content)
    if (id.starts_with("minecraft:potted_")) {
        if (lookup("minecraft:flower_pot")) result = "minecraft:flower_pot";
    } else if (auto it = renames().find(id); it != renames().end()) {
        for (auto const& c : it->second)
            if (lookup(c)) {
                result = c;
                break;
            }
    } else if (lookup(id)) {
        result = id;
    }
    std::unique_lock lock(gCacheMutex);
    gIdCache[id] = result;
    return result;
}

} // namespace

Block const& air() {
    // looked up each time: block types are registered per world on the client
    static Block const* const fallback = lookup("minecraft:air");
    auto const*               a        = lookup("minecraft:air");
    return a ? *a : *fallback;
}

void clearCaches() {
    {
        std::unique_lock lock(gCacheMutex);
        gJavaCache.clear();
        gIdCache.clear();
        gTransformCache.clear();
    }
    liquids::clearCaches();
}

std::string const& id(Block const& block) { return block.getTypeName(); }

std::string bedrockId(std::string_view javaId) { return resolveId(javaId, Json::object()); }

Block const* fromJava(std::string_view javaId, Json const& props) {
    std::string key = cacheKey(javaId, props);
    {
        std::shared_lock lock(gCacheMutex);
        if (auto it = gJavaCache.find(key); it != gJavaCache.end()) return it->second;
    }
    Block const* result = nullptr;
    std::string  bid    = resolveId(javaId, props);
    if (!bid.empty()) {
        Block const* base = lookup(bid);
        if (base) {
            Json p = props;
            // standing torches face "top" (Bedrock torches default to "unknown")
            if ((javaId == "minecraft:torch" || javaId == "minecraft:soul_torch" || javaId == "minecraft:redstone_torch") && !p.contains("facing"))
                p["facing"] = "up";
            auto changes = mapProperties(*base, p);
            result       = withStates(*base, changes);
            if (!result) result = base;
        }
    }
    if (!result) warnOnce("block:" + std::string(javaId), "structure: unknown block " + std::string(javaId) + " (skipped)");
    std::unique_lock lock(gCacheMutex);
    gJavaCache[key] = result;
    return result;
}

Block const& java(std::string_view javaId, Json const& props) {
    auto const* b = fromJava(javaId, props);
    return b ? *b : air();
}

std::string pottedPlant(std::string_view javaId) {
    constexpr std::string_view prefix = "minecraft:potted_";
    if (!javaId.starts_with(prefix)) return {};
    std::string plant(javaId.substr(prefix.size()));
    // FlowerPotBlock's content: the pot's id minus "potted_" (azalea bushes hold the azalea)
    if (plant == "azalea_bush") plant = "azalea";
    else if (plant == "flowering_azalea_bush") plant = "flowering_azalea";
    auto id = bedrockId("minecraft:" + plant);
    if (id.empty()) warnOnce("potted:" + plant, "structure: no Bedrock block for the potted plant minecraft:" + plant + " (empty pot)");
    return id;
}

bool isRegisteredBlock(std::string_view id) {
    if (id.empty()) return false;
    return !resolveId(id, Json::object()).empty();
}

bool blocksMotion(Block const& block) {
    if (block.isAir()) return false;
    return block.getBlockType().mMaterial.mBlocksMotion;
}

Block const* fromJavaState(Json const& state) {
    if (state.is_string()) return fromJava(state.get<std::string>());
    if (!state.is_object()) return nullptr;
    auto name = state.value("Name", std::string{});
    if (name.empty()) return nullptr;
    return fromJava(name, state.value("Properties", Json::object()));
}

bool is(Block const& block, std::string_view javaId) {
    auto bid = bedrockId(javaId);
    return !bid.empty() && block.getTypeName() == bid;
}

Block const& withJava(Block const& block, Json const& props) {
    if (!props.is_object() || props.empty()) return block;
    auto const* b = withStates(block, mapProperties(block, props));
    return b ? *b : block;
}

std::optional<std::string> javaProperty(Block const& block, std::string_view name) {
    auto states = readStates(block);
    auto ns     = nsOf(block.getTypeName());
    if (name == "facing") {
        if (auto it = states.find("weirdo_direction"); it != states.end()) return horizontalName(fromWeirdo(it->second.i));
        if (auto it = states.find("minecraft:cardinal_direction"); it != states.end()) return it->second.s;
        if (auto it = states.find("minecraft:facing_direction"); it != states.end()) return it->second.s;
        if (auto it = states.find("torch_facing_direction"); it != states.end()) return it->second.s == "top" ? "up" : it->second.s;
        if (auto it = states.find("facing_direction"); it != states.end())
            return it->second.i >= 0 && it->second.i < 6 ? std::optional<std::string>(kDir3D[it->second.i]) : std::nullopt;
        if (auto it = states.find("direction"); it != states.end()) return horizontalName(decodeDirection(directionCodec(block), it->second.i));
        if (auto it = states.find(ns + ":facing"); it != states.end())
            return it->second.kind == StateValue::Kind::String ? it->second.s : (it->second.i >= 0 && it->second.i < 6 ? kDir3D[it->second.i] : "north");
        return std::nullopt;
    }
    if (name == "half") {
        if (auto it = states.find("upside_down_bit"); it != states.end()) return it->second.i ? "top" : "bottom";
        if (auto it = states.find("upper_block_bit"); it != states.end()) return it->second.i ? "upper" : "lower";
        if (auto it = states.find("minecraft:vertical_half"); it != states.end()) return it->second.s;
        return std::nullopt;
    }
    if (name == "type") {
        if (block.getTypeName().ends_with("_double_slab")) return "double";
        if (auto it = states.find("minecraft:vertical_half"); it != states.end()) return it->second.s;
        return std::nullopt;
    }
    if (name == "axis") {
        if (auto it = states.find("pillar_axis"); it != states.end()) return it->second.s;
        if (auto it = states.find(ns + ":axis"); it != states.end()) return it->second.s;
        return std::nullopt;
    }
    for (auto const& candidate : {ns + ":" + std::string(name), std::string(name)}) {
        if (auto it = states.find(candidate); it != states.end()) {
            if (it->second.kind == StateValue::Kind::String) return it->second.s;
            if (it->second.kind == StateValue::Kind::Bool) return it->second.i ? "true" : "false";
            return std::to_string(it->second.i);
        }
    }
    return std::nullopt;
}

Block const& transferStates(Block const& from, Block const& target) {
    auto src = readStates(from);
    if (src.empty()) return target;
    auto const* b = withStates(target, src);
    // the double slab id of a slab
    if (from.getTypeName().ends_with("_double_slab") && !target.getTypeName().ends_with("_double_slab") && target.getTypeName().ends_with("_slab")) {
        std::string dbl = target.getTypeName().substr(0, target.getTypeName().size() - 5) + "_double_slab";
        if (auto const* d = lookup(dbl)) return *withStates(*d, src);
    }
    return b ? *b : target;
}

namespace {

Block const& transform(Block const& block, Rotation rotation, Mirror mirrorMode) {
    if (rotation == Rotation::None && mirrorMode == Mirror::None) return block;
    std::uint64_t key = reinterpret_cast<std::uintptr_t>(&block) * 16 + static_cast<std::uint64_t>(rotationIndex(rotation)) * 4 +
                        static_cast<std::uint64_t>(mirrorMode);
    {
        std::shared_lock lock(gCacheMutex);
        if (auto it = gTransformCache.find(key); it != gTransformCache.end()) return *it->second;
    }
    auto states = readStates(block);
    std::map<std::string, StateValue> changes;
    auto turn = [&](Orientation o) {
        if (mirrorMode == Mirror::LeftRight && (o == Orientation::North || o == Orientation::South)) o = opposite(o);
        if (mirrorMode == Mirror::FrontBack && (o == Orientation::East || o == Orientation::West)) o = opposite(o);
        return rotateDir(o, rotation);
    };
    for (auto const& [name, value] : states) {
        if (name == "weirdo_direction") changes[name] = iv(weirdo(turn(fromWeirdo(value.i))));
        else if (name == "facing_direction") {
            if (value.i >= 2 && value.i < 6) {
                auto o = horizontal(kDir3D[value.i]);
                if (o) changes[name] = iv(dir3DIndex(horizontalName(turn(*o))));
            }
        } else if (name == "direction") {
            auto c         = directionCodec(block);
            changes[name] = iv(encodeDirection(c, turn(decodeDirection(c, value.i))));
        } else if (name == "ground_sign_direction") {
            int r = value.i;
            if (mirrorMode != Mirror::None) {
                int i = 8, j = r > i ? r - 16 : r;
                r = mirrorMode == Mirror::LeftRight ? (i - j + 16) % 16 : (16 - j) % 16;
            }
            changes[name] = iv((r + rotationIndex(rotation) * 4) & 15);
        } else if (name == "vine_direction_bits") {
            int bits = 0;
            // bit order: 1 south, 2 west, 4 north, 8 east
            std::array<Orientation, 4> order{Orientation::South, Orientation::West, Orientation::North, Orientation::East};
            for (int b = 0; b < 4; b++) {
                if (!(value.i & (1 << b))) continue;
                auto o = turn(order[b]);
                for (int k = 0; k < 4; k++)
                    if (order[k] == o) bits |= 1 << k;
            }
            changes[name] = iv(bits);
        } else if (name == "pillar_axis" || name.ends_with(":axis")) {
            if (value.kind == StateValue::Kind::String && (rotation == Rotation::Clockwise90 || rotation == Rotation::Counterclockwise90)) {
                if (value.s == "x") changes[name] = sv("z");
                else if (value.s == "z") changes[name] = sv("x");
            }
        } else if (name == "door_hinge_bit" && mirrorMode != Mirror::None) {
            changes[name] = bv(value.i == 0);
        } else if (value.kind == StateValue::Kind::String) {
            if (auto o = horizontal(value.s)) changes[name] = sv(horizontalName(turn(*o)));
        }
    }
    Block const* result = withStates(block, changes);
    if (!result) result = &block;
    std::unique_lock lock(gCacheMutex);
    gTransformCache[key] = result;
    return *result;
}

} // namespace

Block const& rotate(Block const& block, Rotation rotation) { return transform(block, rotation, Mirror::None); }
Block const& mirror(Block const& block, Mirror m) { return transform(block, Rotation::None, m); }

bool isAir(Block const& block) { return block.isAir(); }

bool isLiquid(Block const& block) {
    auto const& id = block.getTypeName();
    return id == "minecraft:water" || id == "minecraft:flowing_water" || id == "minecraft:lava" || id == "minecraft:flowing_lava";
}
bool isWater(Block const& block) {
    auto const& id = block.getTypeName();
    return id == "minecraft:water" || id == "minecraft:flowing_water";
}

namespace {

std::unordered_set<std::string> idSet(std::initializer_list<char const*> javaIds) {
    std::unordered_set<std::string> out;
    for (auto const* j : javaIds) {
        auto b = bedrockId(j);
        out.insert(b.empty() ? std::string(j) : b);
    }
    return out;
}

// Vanilla 1.21.1 tag membership the kit needs when gf::tags does not know a tag (Bedrock has no
// Java tags); TF's own additions to these tags come through gf::tags.
std::unordered_map<std::string, std::unordered_set<std::string>> const& builtinTags() {
    static std::unordered_map<std::string, std::unordered_set<std::string>> const table = [] {
        std::unordered_map<std::string, std::unordered_set<std::string>> t;
        t["minecraft:dirt"] = idSet({"minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium",
                                     "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots"});
        t["minecraft:base_stone_overworld"] =
            idSet({"minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate"});
        t["minecraft:features_cannot_replace"] = idSet({"minecraft:bedrock", "minecraft:spawner", "minecraft:chest", "minecraft:end_portal_frame",
                                                        "minecraft:reinforced_deepslate", "minecraft:trial_spawner", "minecraft:vault"});
        auto lush = t["minecraft:dirt"];
        for (auto const& s : t["minecraft:base_stone_overworld"]) lush.insert(s);
        for (auto const& s : idSet({"minecraft:cave_vines", "minecraft:cave_vines_plant", "minecraft:clay", "minecraft:gravel", "minecraft:sand"}))
            lush.insert(s);
        t["minecraft:lush_ground_replaceable"] = lush;
        t["minecraft:replaceable_by_trees"] = idSet({"minecraft:short_grass", "minecraft:fern", "minecraft:dead_bush", "minecraft:vine",
                                                     "minecraft:glow_lichen", "minecraft:sunflower", "minecraft:lilac", "minecraft:rose_bush",
                                                     "minecraft:peony", "minecraft:tall_grass", "minecraft:large_fern", "minecraft:hanging_roots",
                                                     "minecraft:pitcher_plant", "minecraft:water", "minecraft:seagrass", "minecraft:tall_seagrass",
                                                     "minecraft:warped_roots", "minecraft:nether_sprouts", "minecraft:crimson_roots"});
        t["minecraft:replaceable"] =
            idSet({"minecraft:air", "minecraft:water", "minecraft:lava", "minecraft:short_grass", "minecraft:fern", "minecraft:dead_bush",
                   "minecraft:seagrass", "minecraft:tall_seagrass", "minecraft:fire", "minecraft:soul_fire", "minecraft:snow", "minecraft:vine",
                   "minecraft:glow_lichen", "minecraft:light", "minecraft:tall_grass", "minecraft:large_fern", "minecraft:structure_void",
                   "minecraft:void_air", "minecraft:cave_air", "minecraft:bubble_column", "minecraft:warped_roots", "minecraft:nether_sprouts",
                   "minecraft:crimson_roots", "minecraft:hanging_roots"});
        t["twilightforest:supports_stalagmites"] =
            idSet({"twilightforest:deadrock", "twilightforest:cracked_deadrock", "twilightforest:weathered_deadrock", "minecraft:packed_ice"});
        t["twilightforest:worldgen_replaceables"] = t["minecraft:lush_ground_replaceable"];
        for (auto const& s : t["minecraft:replaceable_by_trees"]) t["twilightforest:worldgen_replaceables"].insert(s);
        return t;
    }();
    return table;
}

} // namespace

bool inTag(Block const& block, std::string_view tag) {
    auto const& bid = block.getTypeName();
    std::string t(tag);
    if (tags::has(tags::Registry::Block, t, bid)) return true;
    if (t == "minecraft:leaves" || t == "minecraft:replaceable_by_trees" || t == "twilightforest:worldgen_replaceables") {
        if (hasFlag(block, BlockProperty::Leaves) || bid.ends_with("leaves")) return true;
    }
    auto const& table = builtinTags();
    auto        it    = table.find(t);
    return it != table.end() && it->second.contains(bid);
}

bool canBeReplaced(Block const& block) {
    if (block.isAir() || isLiquid(block)) return true;
    auto const& bid = block.getTypeName();
    if (bid == "minecraft:snow_layer") {
        auto st = readStates(block);
        return !st.contains("height") || st["height"].i == 0;
    }
    return inTag(block, "minecraft:replaceable");
}

bool worldGenReplaceable(Block const& block) {
    return (canBeReplaced(block) || inTag(block, "twilightforest:worldgen_replaceables")) && !inTag(block, "minecraft:features_cannot_replace");
}

bool replaceableByStructures(Block const& block) {
    if (block.isAir() || isLiquid(block)) return true;
    auto const& bid = block.getTypeName();
    return bid == "minecraft:glow_lichen" || bid == "minecraft:seagrass" || bid == "minecraft:tall_seagrass";
}

bool isPartial(Block const& block) {
    auto const& bid = block.getTypeName();
    if (hasFlag(block, BlockProperty::Stair) || hasFlag(block, BlockProperty::HalfSlab)) return true;
    return bid.ends_with("_fence") || bid.ends_with("_wall") || bid.ends_with("_slab") || bid.ends_with("_stairs") || bid == "minecraft:fence" ||
           bid.ends_with("_double_slab");
}

std::optional<Orientation> parseDirection(std::string_view name) { return horizontal(name); }
char const*                directionName(Orientation o) { return horizontalName(o); }

std::vector<std::string> unresolvedRenames() {
    std::vector<std::string> out;
    for (auto const& [java, candidates] : renames()) {
        bool any = false;
        for (auto const& c : candidates) any |= lookup(c) != nullptr;
        if (!any) out.push_back(java);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace blocks
} // namespace gf::structure
