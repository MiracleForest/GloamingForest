// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/entity/CarminiteBuilderBlockEntity.java
//   src/main/java/gloamingforest/block/entity/CarminiteReactorBlockEntity.java
//   src/main/java/gloamingforest/block/entity/AntibuilderBlockEntity.java
//   src/main/java/gloamingforest/block/SliderBlock.java
//   src/main/java/gloamingforest/entity/boss/UrGhast.java
//   src/main/java/gloamingforest/client/renderer/entity/CarminiteGhastRenderer.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U26 (darktower): the game-independent rules of the dark tower devices and the Ur-Ghast, kept
// pure so they are gtested (tests/darktower). The game adapters in src/feature/darktower call
// these with the world's values.

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/jrand/JRand.h"

namespace gf::core::darktower {

using jrand::RandomSource;

/// Java's Direction in Bedrock FacingID order (0 down, 1 up, 2 north, 3 south, 4 west, 5 east).
enum class Facing : int { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };
/// Unit offset of a facing.
[[nodiscard]] std::array<int, 3> offsetOf(Facing f);

// ---- carminite builder (CarminiteBuilderBlockEntity) ------------------------------------------

inline constexpr int kBuilderRange = 16; ///< RANGE: blocksMade <= 16 places, so 17 blocks a run

/// findNextFacing: `Mth.floor(xRot * 4 / 360 + 1.5) & 3`: 0 up, 2 down, else the horizontal facing.
[[nodiscard]] Facing builderFacing(float xRot, Facing horizontal);
/// Entity#getDirection: the horizontal facing of a yaw (Direction.fromYRot).
[[nodiscard]] Facing horizontalFacing(float yRot);

/// The block entity's state; the adapter supplies the world through the callbacks.
struct BuilderState {
    bool         makingBlocks  = false;
    int          ticksRunning  = 0;
    int          blockedCounter = 0;
    int          ticksStopped  = 0;
    int          blocksMade    = 0;
    bool         hasCursor     = false;
    std::array<int, 3> cursor{};
    bool         tracking      = false; ///< a tracked player exists

    void startBuilding(std::array<int, 3> const& self);
    void resetStats(std::array<int, 3> const& self);
};

/// The outcome of one server tick of an active builder.
struct BuilderTick {
    bool                 placed = false;   ///< a built block was placed at `at`
    std::array<int, 3>   at{};
    bool                 timeout = false;  ///< ticksStopped reached 60: switch to builder_timeout
};
/// CarminiteBuilderBlockEntity#tick (server side). `findPlayer` is called when no player is
/// tracked (true: one found within 16), `facing` gives the tracked player's next facing (nullopt
/// when the player is gone), `isEmpty` asks whether a position is air.
BuilderTick tickBuilder(
    BuilderState&                                         s,
    std::function<bool()> const&                          findPlayer,
    std::function<bool(Facing&)> const&                   facing,
    std::function<bool(std::array<int, 3> const&)> const& isEmpty
);

// ---- slider (SliderBlock) -----------------------------------------------------------------------

/// getTickDelay: `80 - (gameTime - delay * 20) % 80` (Java remainder, truncating).
[[nodiscard]] int sliderTickDelay(std::int64_t gameTime, int delay);

// ---- carminite reactor (CarminiteReactorBlockEntity) ------------------------------------------

struct FakeBlock {
    int  dx, dy, dz;
    bool diamond;
};
/// The 26 fake blocks of the counter-5 transformation, in upstream's order.
[[nodiscard]] std::array<FakeBlock, 26> const& reactorFakeBlocks();

/// One block of drawBlob: the offset from the blob centre and the `fuzz` passed to transformBlock.
struct BlobBlock {
    int dx, dy, dz;
    int fuzz;
};
/// drawBlob(pos, rad, state, fuzz, ...): the positions it transforms, in order.
[[nodiscard]] std::vector<BlobBlock> reactorBlob(int rad, int fuzz);

/// The blob work of one reactor tick (counter % 5 == 0).
struct BlobCall {
    int  centre; ///< 0 primary, 1 secondary, 2 tertiary
    int  rad;
    bool debris; ///< reactor_debris instead of air (primary only)
    int  fuzz;
    bool nether; ///< nether transform
};
/// The drawBlob calls of `counter` (empty unless counter % 5 == 0), in upstream's order.
[[nodiscard]] std::vector<BlobCall> reactorBlobCalls(int counter);
/// The random secondary / tertiary burst offsets of a new block entity (the constructor's six
/// nextBoolean calls and the mirroring).
struct ReactorBursts {
    std::array<int, 3> secondary{};
    std::array<int, 3> tertiary{};
};
[[nodiscard]] ReactorBursts reactorBursts(RandomSource& rand);
inline constexpr int kReactorExplodeAt = 350;

// ---- antibuilder (AntibuilderBlockEntity) --------------------------------------------------------

inline constexpr int kAntibuilderRadius      = 4;
inline constexpr int kAntibuilderDiameter    = 9;
inline constexpr int kAntibuilderRevertChance = 10;
inline constexpr int kAntibuilderPlayerRange = 16;

/// A captured block, by the adapter's own id (the block, not the state, is compared).
struct AntiBlock {
    int  id = 0;               ///< adapter-chosen block identity
    bool air = false;
    bool blocksMotion = false;
    bool unbreakable = false;  ///< destroy speed < 0
    bool ignored = false;      ///< #gloamingforest:antibuilder_ignores
};
/// What a scan asks the world to do at a position.
struct AntiRevert {
    int  index;        ///< x, then y, then z from -4 to 4
    bool toAntibuilt;  ///< the snapshot was not air: an antibuilt block, else air (destroy with drops)
    bool currentAir;   ///< level event 2001 first
};
struct AntibuilderState {
    std::vector<AntiBlock> snapshot; ///< empty: no capture
    int                    tickCount = 0;
    int                    ticksSinceChange = 0;
    bool                   slowScan = false;
};
/// One server tick. `playerInRange`: a living player within 16 of the centre; `areaLoaded`;
/// `read(index)` the block now. Returns the reverts to apply (in order).
std::vector<AntiRevert> tickAntibuilder(
    AntibuilderState&                     s,
    bool                                  playerInRange,
    bool                                  areaLoaded,
    std::function<AntiBlock(int)> const&  read,
    RandomSource&                         rand
);
/// The block offset of a snapshot index.
[[nodiscard]] std::array<int, 3> antibuilderOffset(int index);

// ---- Ur-Ghast phases (UrGhast#hurt) ---------------------------------------------------------

struct UrGhastPhase {
    /// UrGhast's field initializer says 10, but the constructor's setInTantrum(false) (and every load's
    /// readAdditionalSaveData) calls resetDamageUntilNextPhase at once: the first tantrum comes after 18.
    float damageUntilNextPhase = 18.0f;
    bool  tantrum = false;

    /// The damage that reaches super.hurt (divided by 10 in tantrum).
    [[nodiscard]] float incoming(float damage) const { return tantrum ? damage / 10.0f : damage; }
    /// After super.hurt: `lost` health, `freshHurt` (hurtTime == hurtDuration), `dying`,
    /// `trapped` (inTrapCounter > 0). Returns true when the phase switched (setInTantrum(!tantrum)).
    bool afterHurt(float lost, bool freshHurt, bool dying, bool trapped);
    void reset() { damageUntilNextPhase = 18.0f; }
    void setTantrum(bool t) {
        tantrum = t;
        reset();
    }
};

/// The ghast renderers' attack squash (CarminiteGhastRenderer#scale): {xz, y} scale for `scale`
/// (8 ghastguard, 24 Ur-Ghast) and an attack timer (with partial ticks).
struct Squash {
    float xz, y;
};
[[nodiscard]] Squash ghastSquash(float scale, float attackTimer);

} // namespace gf::core::darktower
