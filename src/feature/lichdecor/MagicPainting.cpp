// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/MagicPainting.java
//   src/main/java/twilightforest/entity/MagicPaintingVariant.java
//   src/main/java/twilightforest/item/MagicPaintingItem.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): the magic painting, a hanging entity of five variants (registry
// twilight/magic_paintings). Bedrock has no hanging entities for add-ons: the entity
// twilightforest:magic_painting stands at the bottom centre of its picture, 1/32 off the wall,
// turned to face away from it; its variant is the entity property twilightforest:variant (0..4 in
// kVariants order, drives the RP) and the actor data "variant" (TF's id), its wall block and facing
// the actor data "block" / "direction". U21's lich tower gallery spawns it with "variant" and
// "direction" at the marker block; the first tick hangs it there.
//
// - Placement (MagicPaintingItem#useOn): on a horizontal face; the item's data
//   "magic_painting_variant" picks the variant, else MagicPainting#create: a random one among the
//   largest that survive there. HangingEntity#survives: every block behind the picture is solid and
//   every cell of the picture is free.
// - Every 100 ticks it checks that it still survives; a broken one drops its item (not for a
//   creative breaker; doEntityDrops) with minecraft:entity.painting.break.

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/item/Item.h"
#include "feature/lichdecor/Lichdecor.h"
#include "feature/lichdecor/TestAccess.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::lichdecor {

namespace {

namespace ia    = gf::interactions;
namespace ge    = gf::entity;
namespace items = gf::game::items;

constexpr char kEntity[] = "twilightforest:magic_painting";
constexpr char kItem[]   = "twilightforest:magic_painting";

struct Variant {
    char const* id;
    int         width;  ///< pixels
    int         height; ///< pixels
};
/// The registry's five entries (data/twilightforest/twilight/magic_paintings), sorted by id.
constexpr std::array<Variant, 5> kVariants{{
    {"twilightforest:castaway_paradise", 32, 64},
    {"twilightforest:darkness", 64, 32},
    {"twilightforest:lucid_lands", 48, 48},
    {"twilightforest:music_in_the_mire", 64, 48},
    {"twilightforest:the_hostile_paradise", 48, 32},
}};

int variantIndex(std::string const& id) {
    for (int i = 0; i < static_cast<int>(kVariants.size()); ++i)
        if (id == kVariants[i].id) return i;
    return -1;
}

int ccwOf(int face) {
    switch (face) {
    case 2: return 4; // north -> west
    case 4: return 3; // west -> south
    case 3: return 5; // south -> east
    case 5: return 2; // east -> north
    default: return 2;
    }
}

float yawOf(int face) {
    switch (face) {
    case 3: return 0.0F;    // south
    case 4: return 90.0F;   // west
    case 2: return 180.0F;  // north
    case 5: return -90.0F;  // east
    default: return 0.0F;
    }
}

double offsetFor(int px) { return px % 32 == 0 ? 0.5 : 0.0; }

/// The cells of the picture hung at `pos` facing `face` (calculateBoundingBox).
std::vector<BlockPos> cellsOf(BlockPos const& pos, int face, Variant const& v) {
    int    wb = v.width / 16, hb = v.height / 16;
    int    ccw = ccwOf(face);
    auto   s   = faceStep(ccw);
    double wo = offsetFor(v.width), ho = offsetFor(v.height);
    std::vector<BlockPos> out;
    for (int i = 0; i < wb; ++i) {
        // even sizes are offset half a block (offsetForPaintingSize): every cell lands on a block
        int k = static_cast<int>(std::lround(i - (wb - 1) / 2.0 + wo));
        for (int j = 0; j < hb; ++j) {
            int m = static_cast<int>(std::lround(j - (hb - 1) / 2.0 + ho));
            out.push_back(BlockPos{pos.x + s[0] * k, pos.y + m, pos.z + s[2] * k});
        }
    }
    return out;
}

/// The entity position: the bottom centre of the picture, 1/32 off the wall.
Vec3 positionOf(BlockPos const& pos, int face, Variant const& v) {
    auto   d  = faceStep(face);
    auto   c  = faceStep(ccwOf(face));
    double wo = offsetFor(v.width), ho = offsetFor(v.height);
    double x  = pos.x + 0.5 - d[0] * 0.46875 + c[0] * wo;
    double z  = pos.z + 0.5 - d[2] * 0.46875 + c[2] * wo;
    double cy = pos.y + 0.5 + ho;
    return Vec3{static_cast<float>(x), static_cast<float>(cy - v.height / 32.0), static_cast<float>(z)};
}

/// HangingEntity#survives at `pos` facing `face`.
bool survives(BlockSource& region, BlockPos const& pos, int face, Variant const& v) {
    int back = oppositeFace(face);
    for (auto const& cell : cellsOf(pos, face, v)) {
        if (!game::replaceable(region, cell)) return false;
        BlockPos behind = relative(cell, back);
        auto     id     = game::blockIdAt(region, behind);
        if (id.empty() || game::replaceable(region, behind)) return false;
    }
    return true;
}

Level* level() { return ll::service::getLevel().as_ptr(); }

/// Hangs `actor` as `variant` at `pos` facing `face`.
void hang(Actor& actor, BlockPos const& pos, int face, int variant) {
    auto const& v = kVariants[variant];
    ge::data(actor, "variant")   = v.id;
    ge::data(actor, "direction") = faceName(face);
    ge::data(actor, "block")     = Json::array({pos.x, pos.y, pos.z});
    ge::setProperty(actor, "twilightforest:variant", variant);
    actor.moveTo(positionOf(pos, face, v), Vec2{0.0F, yawOf(face)});
}

Actor* spawn(BlockSource& region, BlockPos const& pos, int face, int variant) {
    auto* lvl = level();
    if (!lvl) return nullptr;
    auto owned = lvl->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(kEntity), nullptr, positionOf(pos, face, kVariants[variant]),
                                                           Vec2{0.0F, yawOf(face)});
    Actor* a = owned ? lvl->addEntity(region, std::move(owned)) : nullptr;
    if (a) hang(*a, pos, face, variant);
    return a;
}

/// MagicPainting#dropItem: the item (its variant kept) unless a creative player broke it.
void breakPainting(Actor& self, Player* breaker) {
    // HangingEntity#hurt: !isRemoved() (two hits in one tick break it once)
    if (self.mRemoved || ge::data(self, "broken").is_boolean()) return;
    ge::data(self, "broken") = true;
    auto& region = self.getDimensionBlockSource();
    playSound(region, self.getPosition(), "minecraft:entity.painting.break", 1.0F, 1.0F);
    if (!(breaker && breaker->isCreative())) {
        if (auto stack = items::make(kItem, 1)) {
            auto v = ge::data(self, "variant");
            if (v.is_string()) items::setData(*stack, "magic_painting_variant", v);
            region.getLevel().getSpawner().spawnItem(region, *stack, nullptr, self.getPosition(), 10);
        }
    }
    self.remove();
}

class MagicPaintingModule {
public:
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        ge::registerType(kEntity, ge::Behaviour{
            .tick = [](Actor& self) {
                auto& hung = ge::data(self, "block");
                auto& ticks = ge::data(self, "check");
                if (!hung.is_array()) {
                    // spawned by U21's gallery: hang it at its marker block
                    auto v     = ge::data(self, "variant");
                    int  index = v.is_string() ? variantIndex(v.get<std::string>()) : -1;
                    if (index < 0) index = 1; // MagicPaintingVariants.DEFAULT (darkness)
                    auto dir   = ge::data(self, "direction");
                    int  face  = faceOf(dir.is_string() ? dir.get<std::string>() : std::string("south"));
                    if (face < 2) face = 3;
                    auto const& p = self.getPosition();
                    hang(self, BlockPos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))}, face, index);
                    return;
                }
                // HangingEntity#tick: survival every 100 ticks
                int t = ticks.is_number_integer() ? ticks.get<int>() : 0;
                ticks = t + 1;
                if ((t + 1) % 100 != 0) return;
                int  index = variantIndex(ge::data(self, "variant").get<std::string>());
                int  face  = faceOf(ge::data(self, "direction").get<std::string>());
                BlockPos pos{hung[0].get<int>(), hung[1].get<int>(), hung[2].get<int>()};
                if (index < 0 || face < 2) return;
                // its own cells hold the entity, not blocks: only the wall behind is checked again
                auto& region = self.getDimensionBlockSource();
                for (auto const& cell : cellsOf(pos, face, kVariants[index])) {
                    BlockPos behind = relative(cell, oppositeFace(face));
                    auto     id     = game::blockIdAt(region, behind);
                    if (id.empty()) return; // not loaded
                    if (game::replaceable(region, behind) || !game::replaceable(region, cell)) {
                        breakPainting(self, nullptr);
                        return;
                    }
                }
            },
            .hurt = [](Actor& self, float&, std::string const&, Actor* attacker) {
                // any damage breaks it (HangingEntity#hurt)
                Player* breaker = attacker && attacker->isPlayer() ? static_cast<Player*>(attacker) : nullptr;
                breakPainting(self, breaker);
                return false;
            },
        });
        ia::onAttack({.block = kEntity}, [](ia::EntityContext const& c) {
            breakPainting(c.entity, &c.player);
            return ia::Result::Consume;
        });
        // MagicPaintingItem#useOn (Item#useOn: only after the clicked block's own use passed)
        ia::onUnusedUseOn({.item = kItem}, [](ia::UseOnContext const& c) {
            if (c.face < 2) return ia::Result::Pass; // mayPlace: not on a vertical face (FAIL)
            if (!c.firstEvent) return ia::Result::Consume;
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return ia::Result::Pass;
            BlockPos at = relative(game::blockPos(c.pos), c.face);
            int      chosen = -1;
            auto     wanted = items::getData(c.item, "magic_painting_variant");
            if (wanted.is_string()) {
                chosen = variantIndex(wanted.get<std::string>());
                if (chosen >= 0 && !survives(*region, at, c.face, kVariants[chosen])) return ia::Result::Consume;
            } else {
                // MagicPainting#create: the largest variants that survive, one at random
                std::vector<int> fit;
                int              best = 0;
                for (int i = 0; i < static_cast<int>(kVariants.size()); ++i) {
                    if (!survives(*region, at, c.face, kVariants[i])) continue;
                    int area = kVariants[i].width * kVariants[i].height;
                    if (area > best) fit.clear(), best = area;
                    if (area == best) fit.push_back(i);
                }
                if (fit.empty()) return ia::Result::Consume;
                chosen = fit[randomInt(static_cast<int>(fit.size()))];
            }
            if (chosen < 0) return ia::Result::Consume;
            if (auto* painting = spawn(*region, at, c.face, chosen)) {
                playSound(*region, painting->getPosition(), "minecraft:entity.painting.place", 1.0F, 1.0F);
                consumeOne(c.player, c.item);
            }
            return ia::Result::Consume;
        }, ia::priority::High);
        gf::item::addTooltip(kItem, [](ItemStack const& stack, std::vector<std::string>& lines, Player const* viewer) {
            auto v = items::getData(stack, "magic_painting_variant");
            if (!v.is_string()) return;
            auto id    = v.get<std::string>();
            auto colon = id.find(':');
            auto key   = "magic_painting." + id.substr(0, colon) + "." + id.substr(colon + 1);
            auto title = viewer ? trFor(*viewer, key + ".title", id) : tr(key + ".title", id);
            auto by    = viewer ? trFor(*viewer, key + ".author", "") : tr(key + ".author", "");
            lines.push_back(std::string("\xC2\xA7") + "e" + title);
            lines.push_back(std::string("\xC2\xA7") + "7" + by);
        });
    }
    void uninstall() {}
};

} // namespace

/// Selftest access: hangs a magic painting of `variantId` at `pos` facing `face` (nullptr: refused).
Actor* spawnMagicPaintingForTest(BlockSource& region, BlockPos const& pos, int face, std::string const& variantId) {
    int index = variantIndex(variantId);
    if (index < 0 || !survives(region, pos, face, kVariants[index])) return nullptr;
    return spawn(region, pos, face, index);
}

} // namespace gf::feature::lichdecor

GF_REGISTER_MODULE(gf::feature::lichdecor::MagicPaintingModule);
