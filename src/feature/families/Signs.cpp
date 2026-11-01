// Contract "families+blockevents", sign text (F11; spike S3 backend: block data). TF's 32 signs
// (standing, wall, hanging and wall hanging signs of the eight woods) are custom blocks, which have
// no block entities: their text lives in block data (gf::blockdata key "sign"):
//
//     {"front": {"lines": ["", "", "", ""], "color": "black", "glowing": false},
//      "back":  {...}, "waxed": false}
//
// Java SignBlock / SignBlockEntity behaviour: placing a sign opens its editor for the placer (the
// standing / ceiling item becomes the wall variant on a side, params.wall); using a sign edits the
// side facing the player, dyes / glow ink / ink sac colour or light that side, honeycomb waxes the
// sign (no more edits). The editor is a server form (vanilla clients and, until the client
// screen framework lands, companions too); companions get every text change on "signs.text" and
// may edit through "signs.edit" (a native editor screen).

#include "feature/families/Signs.h"

#include <cmath>
#include <numbers>

#include "ll/api/form/CustomForm.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/families/Families.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;
namespace bd = blockdata;

namespace {

constexpr char kKey[] = "sign";
constexpr int  kLines = 4;
constexpr int  kMaxLineLength = 90; // Java's sign text is limited by width; 90 chars is plenty

bool isSign(Entry const* e) {
    return e && (e->family == Family::Sign || e->family == Family::WallSign || e->family == Family::HangingSign || e->family == Family::WallHangingSign);
}

Json emptySide() { return {{"lines", Json::array({"", "", "", ""})}, {"color", "black"}, {"glowing", false}}; }

Json docAt(GlobalPos const& pos) {
    auto* region = regionOf(pos.dim);
    if (!region) return Json();
    auto const* j = bd::get(*region, game::blockPos(pos), kKey);
    Json        d = j && j->is_object() ? *j : Json::object();
    if (!d.contains("front") || !d["front"].is_object()) d["front"] = emptySide();
    if (!d.contains("back") || !d["back"].is_object()) d["back"] = emptySide();
    if (!d.contains("waxed")) d["waxed"] = false;
    return d;
}

void broadcast(GlobalPos const& pos, Json const& doc) {
    auto level = ll::service::getLevel();
    if (!level) return;
    Json payload{{"dim", pos.dim}, {"x", pos.x}, {"y", pos.y}, {"z", pos.z}, {"sign", doc}};
    level->forEachPlayer([&](Player& p) {
        if (net::isCompanion(p)) net::send(p, "signs.text", payload);
        return true;
    });
}

void store(GlobalPos const& pos, Json doc) {
    auto* region = regionOf(pos.dim);
    if (!region) return;
    bd::set(*region, game::blockPos(pos), kKey, doc);
    broadcast(pos, doc);
}

/// Java SignBlock.getYRotationDegrees: the direction the front text faces.
float signYaw(Block const& b, Entry const& e) {
    if (e.family == Family::Sign || e.family == Family::HangingSign) return intState(b, state::Rotation).value_or(0) * 22.5f;
    static constexpr float toYRot[6]{0, 0, 180, 0, 90, 270}; // Direction.toYRot: S 0, W 90, N 180, E 270
    return toYRot[dirOf(strState(b, state::Cardinal)).value_or(North)];
}

/// Java SignBlock.isFacingFrontText.
bool facingFront(Block const& b, Entry const& e, GlobalPos const& pos, Player const& player) {
    auto   p     = player.getPosition();
    double dx    = p.x - (pos.x + 0.5), dz = p.z - (pos.z + 0.5);
    float  yaw   = signYaw(b, e);
    float  angle = static_cast<float>(std::atan2(dz, dx) * 180.0 / std::numbers::pi) - 90.0f;
    float  diff  = std::fmod(std::fabs(yaw - angle), 360.0f);
    if (diff > 180.0f) diff = 360.0f - diff;
    return diff <= 90.0f;
}

void openEditor(Player& player, GlobalPos const& pos, bool front) {
    auto doc   = docAt(pos);
    auto lines = doc[front ? "front" : "back"]["lines"];
    ll::form::CustomForm form(trFor(player, "sign.edit", "Edit Sign Message"));
    for (int i = 0; i < kLines; ++i) {
        std::string current = i < static_cast<int>(lines.size()) && lines[i].is_string() ? lines[i].get<std::string>() : "";
        form.appendInput("line" + std::to_string(i), "", "", current);
    }
    auto uid = player.getOrCreateUniqueID().rawID;
    form.sendTo(player, [pos, front, uid](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result || p.getOrCreateUniqueID().rawID != uid) return;
        std::vector<std::string> out;
        for (int i = 0; i < kLines; ++i) {
            auto it = result->find("line" + std::to_string(i));
            out.push_back(it != result->end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second) : "");
        }
        // Java SignBlockEntity.updateSignText: only the player who may edit, within reach
        auto const* b = blockAt(pos);
        if (!b || !isSign(entryOf(*b)) || docAt(pos).value("waxed", false)) return;
        signs::setText(pos, out, front);
        log().info("families: {} edited the sign at {} {} {}", p.getRealName(), pos.x, pos.y, pos.z);
    });
}

/// Java's sign applicators: dyes, glow ink sac, ink sac, honeycomb. True when applied.
bool applyItem(Player& player, GlobalPos const& pos, std::string const& itemId, ItemStack const& item, bool front) {
    auto doc = docAt(pos);
    if (doc.value("waxed", false)) return false; // Java: a waxed sign takes nothing
    auto& side    = doc[front ? "front" : "back"];
    bool  hasText = false;
    for (auto const& l : side["lines"])
        if (l.is_string() && !l.get<std::string>().empty()) hasText = true;
    auto* region = regionOf(pos.dim);
    if (!region) return false;
    BlockPos bp = game::blockPos(pos);
    if (itemId == "minecraft:honeycomb") {
        doc["waxed"] = true;
        bedrockSound(*region, bp, "sign.wax.use");
    } else if (itemId == "minecraft:glow_ink_sac") {
        if (!hasText || side.value("glowing", false)) return false;
        side["glowing"] = true;
        bedrockSound(*region, bp, "sign.ink_sac.use");
    } else if (itemId == "minecraft:ink_sac") {
        if (!hasText || !side.value("glowing", false)) return false;
        side["glowing"] = false;
        bedrockSound(*region, bp, "sign.ink_sac.use");
    } else if (itemId.ends_with("_dye")) {
        auto color = itemId.substr(10, itemId.size() - 14); // minecraft:<color>_dye
        if (!hasText || side.value("color", "black") == color) return false;
        side["color"] = color;
        bedrockSound(*region, bp, "sign.dye.use");
    } else {
        return false;
    }
    store(pos, doc);
    consumeHeld(player, item);
    return true;
}

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

} // namespace

void installSigns() {
    for (auto family : {Family::Sign, Family::WallSign, Family::HangingSign, Family::WallHangingSign}) {
        for (auto const* e : entriesOf(family)) {
            ia::Match m;
            m.block = e->id;
            // placement: the wall variant on a side, the rotation, then the editor (Java setPlacedBy)
            handlerIds().push_back(ia::onPlace(m, [](ia::PlaceContext const& c) {
                auto const* e = entryOf(c.blockId);
                if (!e) return;
                GlobalPos pos = c.pos;
                auto      wall = e->str("wall");
                if (!wall.empty() && horizontal(c.face)) {
                    if (auto const* w = game::blockWith(wall)) {
                        if (auto const* oriented = withStates(*w, {{state::Cardinal, dirName(c.face)}})) set(pos, *oriented);
                    }
                } else if (e->family == Family::Sign) {
                    setStates(pos, {{state::Rotation, signRotation(c.player)}});
                } else if (e->family == Family::HangingSign) {
                    // Java CeilingHangingSignBlock: attached (V chains) when sneaking or the block above
                    // has no full bottom face; then the free rotation, else the 4 directions
                    auto* region = regionOf(pos.dim);
                    bool  attached = c.sneaking || !(region && sturdy(*region, offset(game::blockPos(pos), Up), Down));
                    int   rot      = attached ? signRotation(c.player) : ((signRotation(c.player) + 2) / 4 * 4) & 15;
                    setStates(pos, {{state::Rotation, rot}, {state::Attached, attached ? 1 : 0}});
                } else if (e->family == Family::WallSign || e->family == Family::WallHangingSign) {
                    setStates(pos, {{state::Cardinal, dirName(horizontal(c.face) ? c.face : opposite(lookDir(c.player)))}});
                }
                if (ia::isServerPlayer(c.player)) openEditor(c.player, pos, true);
            }));
            handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
                if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass;
                if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
                auto const* e = entryOf(c.block);
                if (!e) return ia::Result::Pass;
                bool front = facingFront(c.block, *e, c.pos, c.player);
                if (!c.itemId.empty() && applyItem(c.player, c.pos, c.itemId, c.item, front)) return ia::Result::Consume;
                if (docAt(c.pos).value("waxed", false)) {
                    bedrockSound(c.player.getDimensionBlockSource(), game::blockPos(c.pos), "sign.waxed_interact_fail");
                    return ia::Result::Consume;
                }
                openEditor(c.player, c.pos, front);
                return ia::Result::Consume;
            }));
        }
    }
    static bool registered = false;
    if (registered) return;
    registered = true;
    // a companion's editor screen
    net::on("signs.edit", [](Player& player, Json const& p) {
        GlobalPos pos{static_cast<int>(player.getDimensionId()), p.value("x", 0), p.value("y", 0), p.value("z", 0)};
        auto      at = player.getPosition();
        double    dx = at.x - (pos.x + .5), dy = at.y - (pos.y + .5), dz = at.z - (pos.z + .5);
        if (dx * dx + dy * dy + dz * dz > 64.0) return; // Java: within 8 blocks
        auto const* b = blockAt(pos);
        if (!b || !isSign(entryOf(*b)) || docAt(pos).value("waxed", false)) return;
        std::vector<std::string> lines;
        if (p.contains("lines") && p["lines"].is_array())
            for (auto const& l : p["lines"]) lines.push_back(l.is_string() ? l.get<std::string>() : "");
        signs::setText(pos, lines, p.value("front", true));
    });
    // companions get the signs around them when they join (the chunks within 8 of theirs; later
    // changes reach them through store())
    net::onCompanionJoined([](Player& player) {
        int  dim = static_cast<int>(player.getDimensionId());
        auto at  = player.getPosition();
        int  pcx = static_cast<int>(std::floor(at.x)) >> 4, pcz = static_cast<int>(std::floor(at.z)) >> 4;
        for (int cx = pcx - 8; cx <= pcx + 8; ++cx) {
            for (int cz = pcz - 8; cz <= pcz + 8; ++cz) {
                bd::forEachInChunk(dim, cx, cz, [&player](GlobalPos const& pos, Json const& doc) {
                    if (doc.contains(kKey)) net::send(player, "signs.text", {{"dim", pos.dim}, {"x", pos.x}, {"y", pos.y}, {"z", pos.z}, {"sign", doc[kKey]}});
                });
            }
        }
    });
}

void uninstallSigns() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::families

namespace gf::signs {

namespace fam = feature::families;

std::vector<std::string> text(core::GlobalPos const& pos, bool front) {
    auto const* b = fam::blockAt(pos);
    if (!b || !fam::isSign(fam::entryOf(*b))) return {};
    auto doc   = fam::docAt(pos);
    auto lines = doc[front ? "front" : "back"]["lines"];
    std::vector<std::string> out;
    for (auto const& l : lines) out.push_back(l.is_string() ? l.get<std::string>() : "");
    out.resize(fam::kLines);
    return out;
}

bool setText(core::GlobalPos const& pos, std::vector<std::string> const& lines, bool front) {
    auto const* b = fam::blockAt(pos);
    if (!b || !fam::isSign(fam::entryOf(*b))) return false;
    auto doc  = fam::docAt(pos);
    auto side = nlohmann::json::array();
    for (int i = 0; i < fam::kLines; ++i) {
        std::string l = i < static_cast<int>(lines.size()) ? lines[i] : std::string();
        if (l.size() > static_cast<std::size_t>(fam::kMaxLineLength)) l.resize(fam::kMaxLineLength);
        side.push_back(l);
    }
    doc[front ? "front" : "back"]["lines"] = side;
    fam::store(pos, doc);
    return true;
}

} // namespace gf::signs
