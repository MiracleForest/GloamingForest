// Neutral stub of the "boss" contract (F0). F8 replaces this file (rule R18).
#include "feature/boss/Boss.h"

#include "core/Stub.h"

namespace gf::boss {

void    registerBoss(BossSpec) { GF_STUB("boss.registerBoss"); }
void    registerSpawner(SpawnerSpec) { GF_STUB("boss.registerSpawner"); }
void    BossBar::setProgress(float) const { GF_STUB("boss.bar"); }
void    BossBar::setName(std::string const&) const { GF_STUB("boss.bar"); }
void    BossBar::setVisible(bool) const { GF_STUB("boss.bar"); }
BossBar bar(Mob&) {
    GF_STUB("boss.bar");
    return {};
}
void setMinion(Actor&, bool) { GF_STUB("boss.setMinion"); }
bool isMinion(Actor const&) {
    GF_STUB("boss.isMinion");
    return false;
}
std::vector<Player*> participants(Mob const&) {
    GF_STUB("boss.participants");
    return {};
}
std::optional<TrophyVariant> trophyVariant(std::string const&) {
    GF_STUB("boss.trophyVariant");
    return std::nullopt;
}

} // namespace gf::boss
