// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U8 (weapons): registers the unit's items, projectiles and handlers (Weapons.h).

#include "feature/weapons/Weapons.h"

#include "feature/ext/item/Item.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::weapons {

namespace {

class WeaponsModule {
public:
    // The item, projectile and combat registries have no removal: registered once per process.
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        registerChain();
        registerCube();
        registerBows();
        registerSwords();
        registerShield();
        registerHorn();
        registerLamp();
        registerMoonworm();
        registerMagnet();
        registerFan();
        registerPowder();
        registerIceBomb();
        using gf::item::ext::Rarity;
        gf::item::ext::setRarity(kCube, Rarity::Uncommon);
        gf::item::ext::setRarity(kCubeTalisman, Rarity::Uncommon);
        log().info("weapons: registered");
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::feature::weapons

GF_REGISTER_MODULE(gf::feature::weapons::WeaponsModule);
