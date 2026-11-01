#pragma once

// Contract "combat" (header owned by F0, implemented by F10; spike S3).
//
// The single hurt hook of the mod (rule R8: no unit hooks Mob::hurt): incoming damage passes the
// `onIncoming` handlers by priority (higher first) and may be changed or cancelled; `onDealt`
// handlers hear about damage that was applied. TF's 40 damage types and their 70 death messages
// are pre-registered from the imported data (DamageSources ASM equivalent).
//
//     gf::combat::onIncoming(0, [](gf::combat::DamageInfo& d) {
//         if (d.type == "minecraft:fall" && wearsWings(d.victim)) return gf::combat::Verdict::Cancel;
//         return gf::combat::Verdict::Pass;
//     });
//     gf::combat::hurt(target, 4.0f, "twilightforest:squished", attacker);

#include <functional>
#include <string>

class Actor;

namespace gf::combat {

struct DamageInfo {
    Actor&      victim;
    std::string type;               ///< damage type id ("twilightforest:hydra_bite", "minecraft:fall")
    Actor*      attacker = nullptr; ///< the responsible entity (causing entity)
    Actor*      direct   = nullptr; ///< the direct entity (projectile, ...)
    float       amount   = 0;
};

enum class Verdict { Pass, Cancel };

namespace priority {
inline constexpr int Highest  = 1000;
inline constexpr int High     = 100;
inline constexpr int Normal   = 0;
inline constexpr int Low      = -100;
inline constexpr int Lowest   = -1000;
} // namespace priority

using IncomingFn = std::function<Verdict(DamageInfo& damage)>;
using DealtFn    = std::function<void(DamageInfo const& damage)>;

/// Before damage applies (armour not yet applied): change `amount` or cancel.
void onIncoming(int priority, IncomingFn fn);
/// After damage applied (`amount` is what was dealt).
void onDealt(int priority, DealtFn fn);
/// Deals `amount` of damage type `typeId` to `victim` through the normal pipeline; true when
/// it was applied.
bool hurt(Actor& victim, float amount, std::string const& typeId, Actor* attacker = nullptr, Actor* direct = nullptr);

} // namespace gf::combat
