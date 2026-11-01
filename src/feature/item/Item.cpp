// Neutral stub of the "item" contract (F0). F9 replaces this file (rule R18).
#include "feature/item/Item.h"

#include "core/Stub.h"
#include "game/Items.h"

namespace gf::item {

void registerItem(std::string const&, Behaviour) { GF_STUB("item.registerItem"); }
Json getData(ItemStack const& stack, std::string const& key) { return game::items::getData(stack, key); }
void setData(ItemStack& stack, std::string const& key, Json const& value) { game::items::setData(stack, key, value); }
void cooldown(Player&, std::string const&, int) { GF_STUB("item.cooldown"); }
int  cooldownLeft(Player const&, std::string const&) {
    GF_STUB("item.cooldownLeft");
    return 0;
}
void addTooltip(std::string const&, TooltipFn) { GF_STUB("item.addTooltip"); }
void registerDispense(std::string const&, DispenseFn) { GF_STUB("item.registerDispense"); }
void setBar(std::string const&, BarFn) { GF_STUB("item.setBar"); }

} // namespace gf::item
