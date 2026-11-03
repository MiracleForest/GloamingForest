// The F12 fixture, client side (selftests "ui.*" and the client e2e scenario ui.ps1). Client build
// only, development builds only (gf::ui::fixture::enabled: the gf_test packs are deployed). The server
// turns it on with "ui.fixture" {show, hud, toast, world, fog, map} (feature/ui/Fixture.cpp):
//
// - HUD layer "ui.fixture": a panel with an item, the graphics mode and the three blit methods;
// - two toasts (Java's toast layout on the game's toast frame): LockedBiomeToast's colours (yellow
//   title) with a flat icon (a map) and MissingAdvancementToast's (white title) with a block icon
//   (grass) and a literal body;
// - stage renderers (the frame step, "stage": its origin): a block renderer on the fixture block left
//   of the frame (the fixture geometry, lit by the block above it) and a tint of the oak leaves right
//   of it built on the game's foliage colour (red and blue swapped on the stage only);
// - world: a line box around the block below the player, a line up to the sky and the fixture
//   geometry (geometry.gf_ui.fixture) drawn with drawGeo in front of the player;
// - fog: a provider for the player's dimension (red fog 4..24 blocks, clouds at y 160), and a sky
//   provider that reports the sky hook;
// - item renderers: gf_test:fixture_item in item frames (Fixed) and gf_test:fixture_block on heads
//   (Head): a box, the item space's axes (x red, y green, z blue) and the fixture geometry;
// - map: TF-style decorations (gf_test:fixture_icon*) on the map the player holds, one of them
//   conquered (TF's red X) and the vanilla player icon drawn on top (MagicMapPlayerIconRenderer);
// - screen "ui.fixture": a ContainerScreen with the fixture GUI skin (an original texture with
//   the layout of TF's container GUIs), nine container slots (flat and block items) and the player
//   inventory;
// - key "ui.fixture" (J, registered at mod load) and "ui.fixture_late" (K, registered at install,
//   after the game built its input handler): forwarded to the server, which logs the presses;
// - native particle "gf_test:fixture_particle": the client scenario runs /particle with it and the
//   server's ui.fixture_fx plays it with gf::fx::particle (F10's fx.particle, F12's native sink);
// - map decorations on a framed map: the vanilla player icon is left out (Java's showOnItemFrame).
//
// The fixture's textures and geometry are built into the mod (FixtureAssets.h): the textures are
// written next to the test data and drawn as file textures, so the resource pack carries nothing of
// the fixture. One log line per first draw of each part (the e2e scenario waits for them).

#ifdef GF_CLIENT

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/options/GraphicsMode.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "client/render/Geo.h"
#include "client/render/Render.h"
#include "client/ui/FixtureAssets.h"
#include "client/ui/Ui.h"
#include "client/world/Sky.h"
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "feature/ui/Fixture.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::client::ui::fixture {

namespace {

using Json = nlohmann::json;
constexpr char kGeo[]         = "geometry.gf_ui.fixture";
constexpr char kFixtureItem[] = "gf_test:fixture_item";
constexpr char kFixtureHead[] = "gf_test:fixture_block";
constexpr char kIconA[]       = "gf_test:fixture_icon";
constexpr char kIconB[]       = "gf_test:fixture_icon_skin";
constexpr char kParticle[]    = "gf_test:fixture_particle";
constexpr int  kFixtureKey    = 'J';
constexpr int  kLateKey       = 'K';

/// The fixture's file textures (absolute paths, set at install).
struct Textures {
    std::string skin, geo;
};
Textures& tex() {
    static Textures t;
    return t;
}

/// Writes an embedded texture to `dir` (when missing or different) and returns its absolute path.
std::string writeTexture(std::filesystem::path const& dir, char const* name, unsigned char const* data, std::size_t size) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto path = std::filesystem::absolute(dir / name, ec);
    if (!std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) != size) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<char const*>(data), static_cast<std::streamsize>(size));
    }
    return path.generic_string();
}

void installAssets() {
    auto dir   = gf::Mod::getInstance().getSelf().getModDir() / "data" / "gf_test" / "ui";
    tex().skin = writeTexture(dir, "fixture_container.png", assets::kContainerPng, sizeof(assets::kContainerPng));
    tex().geo  = writeTexture(dir, "fixture_geo.png", assets::kGeoPng, sizeof(assets::kGeoPng));
    std::string error;
    auto        models = render::geo::parse(Json::parse(assets::kGeoJson), &error);
    if (models.empty()) log().error("ui fixture: geometry: {}", error);
    render::geo::add(std::move(models));
}

struct State {
    std::atomic<bool> hud{false}, world{false}, mapPending{false};
    std::atomic<int>  fogDim{-1};
    std::atomic<bool> loggedHud{false}, loggedWorld{false}, loggedFixed{false}, loggedHead{false};
    std::shared_ptr<ItemStack> hudItem; // client thread
    std::int64_t               mapId = 0; // client thread
};
State& st() {
    static State s;
    return s;
}

std::string graphicsModeName() {
    auto* c = currentClient();
    if (!c) return "?";
    switch (c->getOptions().getGraphicsMode()) {
    case GraphicsMode::Simple: return "simple";
    case GraphicsMode::Fancy: return "fancy (Classic)";
    case GraphicsMode::Advanced: return "advanced (Vibrant Visuals)";
    case GraphicsMode::RayTraced: return "ray traced";
    }
    return "?";
}

void drawHud(screen::Canvas& c, float) {
    if (!st().hud) return;
    if (!st().hudItem) {
        if (auto s = game::items::make("minecraft:map")) st().hudItem = std::make_shared<ItemStack>(std::move(*s));
    }
    screen::Rect panel{6, 40, 170, 44};
    c.bevel(panel, screen::color::kPanel, screen::color::kPanelLight, screen::color::kPanelDark, false);
    c.text(panel.x + 6, panel.y + 6, ctr("gf.ui.fixture.title", "GloamingForest UI fixture"), screen::color::kTextDark, screen::Align::Left, false);
    c.text(panel.x + 26, panel.y + 20, ctr("gf.ui.fixture.hud_line", "HUD layer"), 0xFF3B6E3B, screen::Align::Left, false);
    c.text(panel.x + 26, panel.y + 30, graphicsModeName(), screen::color::kTextDark, screen::Align::Left, false);
    if (st().hudItem) c.item(*st().hudItem, panel.x + 6, panel.y + 20);
    // the three ways ui::blit can draw a texture, side by side (spike S5): the top-left 88 x 83
    // of the fixture skin at half size, labelled with the method
    int keep = ui::blitMethod();
    for (int m = 0; m < 3; ++m) {
        screen::Rect r{panel.x + static_cast<float>(m) * 50, panel.y + panel.h + 4, 44, 41};
        c.fill(r, 0xFF202020);
        ui::setBlitMethod(m);
        bool ok = ui::blit(c, tex().skin, r, 0, 0, 88, 83);
        c.text(r.x + 2, r.y + r.h + 2, std::to_string(m) + (ok ? "" : " -"), 0xFFFFFFFF);
    }
    ui::setBlitMethod(keep);
    if (!st().loggedHud.exchange(true)) log().info("ui fixture: hud layer drawn ({})", graphicsModeName());
}

void drawWorld(render::RenderContext& ctx, float) {
    if (!st().world) return;
    auto* p = currentPlayer();
    if (!p) return;
    auto const& pos = p->getPosition();
    float       bx = std::floor(pos.x), by = std::floor(pos.y - 1.62f), bz = std::floor(pos.z);
    {
        render::Lines lines(ctx);
        // the block the player stands on, a 3x3 frame around it and a beam to the sky
        lines.box({bx, by - 1, bz}, {bx + 1, by, bz + 1}, 0xFFFF3030);
        lines.box({bx - 1, by - 1, bz - 1}, {bx + 2, by + 2, bz + 2}, 0xFF30FF60);
        lines.line({bx + .5f, by, bz + .5f}, {bx + .5f, by + 40, bz + .5f}, 0xFF40A0FF);
    }
    // the fixture geometry three blocks to the south, turning slowly
    float spin = static_cast<float>(std::fmod(static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 25.0, 360.0));
    render::drawGeo(ctx, kGeo, tex().geo, {.translation = {bx + .5f, by, bz + 3.5f}, .rotationDeg = {0, spin, 0}, .scale = {1, 1, 1}});
    if (!st().loggedWorld.exchange(true)) log().info("ui fixture: world lines drawn");
}

/// An item space as the fixture draws it: a box of half size `half`, the axes and the geometry.
void drawItemSpace(render::RenderContext& ctx, float half) {
    {
        render::Lines lines(ctx);
        lines.box({-half, -half, -half}, {half, half, half}, 0xFFFFD040);
        lines.line({0, 0, 0}, {half * 1.6f, 0, 0}, 0xFFFF3030);
        lines.line({0, 0, 0}, {0, half * 1.6f, 0}, 0xFF30FF30);
        lines.line({0, 0, 0}, {0, 0, half * 1.6f}, 0xFF3060FF);
    }
    render::drawGeo(ctx, kGeo, tex().geo, {.translation = {0, -half, 0}, .rotationDeg = {0, 0, 0}, .scale = {half, half, half}});
}

void drawFixed(render::RenderContext& ctx, ItemStack const&, render::ItemContext) {
    drawItemSpace(ctx, 0.5f);
    if (!st().loggedFixed.exchange(true)) log().info("ui fixture: item frame item drawn");
}

void drawHead(render::RenderContext& ctx, ItemStack const&, render::ItemContext) {
    drawItemSpace(ctx, 0.45f);
    if (!st().loggedHead.exchange(true)) log().info("ui fixture: head item drawn");
}

// ---- the stage's renderers (the frame step): a block renderer and a tint on the game's foliage ----

constexpr char kStageLeaves[] = "minecraft:oak_leaves";

/// The stage's origin (its feet block) while the stage renderers are on (chunk builders read it).
struct StageAt {
    std::atomic<bool> on{false};
    std::atomic<int>  x{0}, y{0}, z{0};
    std::atomic<bool> loggedBlock{false}, loggedTint{false};
    int               ticks = 0; // client thread: ticks since the stage renderers came on
};
StageAt& stageAt() {
    static StageAt s;
    return s;
}

/// Whether `p` lies in the stage box (fixture::prepareStage: x -1..1, y -1..3, z 0..4 of the origin).
bool onStage(BlockPos const& p) {
    auto& s = stageAt();
    if (!s.on) return false;
    int x = s.x, y = s.y, z = s.z;
    return p.x >= x - 1 && p.x <= x + 1 && p.y >= y - 1 && p.y <= y + 3 && p.z >= z && p.z <= z + 4;
}

/// The fixture block's renderer: the fixture geometry on top, lit by the light of the block above
/// (a renderer moving its light: the fixture block is a full cube, dark inside).
void drawStageBlock(render::RenderContext& ctx, BlockPos const& pos, Block const&, float) {
    if (!onStage(pos)) return;
    float x = static_cast<float>(pos.x), y = static_cast<float>(pos.y), z = static_cast<float>(pos.z);
    ctx.lightAt = Vec3{x + .5f, y + 1.5f, z + .5f};
    render::drawGeo(ctx, kGeo, tex().geo, {.translation = {x + .5f, y + 1.f, z + .5f}, .rotationDeg = {0, 45, 0}, .scale = {.5f, .5f, .5f}});
    if (!stageAt().loggedBlock.exchange(true))
        log().info("ui fixture: stage block renderer drawn at {} {} {} (light set {} time(s) so far)", pos.x, pos.y, pos.z, render::lightSetups());
}

/// Oak leaves: the game's own foliage colour, with red and blue swapped on the stage (the screenshot
/// shows the game's colour went through the tint function); everywhere else as the game tints them.
unsigned tintStageLeaves(BlockPos const& p, tint::GameColors const& game) {
    unsigned f = game.foliage().value_or(0x48B518u); // FoliageColor.getDefaultColor without a world
    if (!onStage(p)) return f;
    unsigned swapped = (f & 0x00FF00u) | ((f >> 16) & 0xFFu) | ((f & 0xFFu) << 16);
    if (!stageAt().loggedTint.exchange(true)) log().info("ui fixture: stage leaves tinted from the game's foliage #{:06X} (drawn #{:06X})", f, swapped);
    return swapped;
}

void setStage(std::optional<BlockPos> origin) {
    auto& s = stageAt();
    if (!origin) {
        if (!s.on.exchange(false)) return;
        render::unregisterBlockRenderer(kFixtureHead);
        tint::unregisterBlock(kStageLeaves);
        return;
    }
    s.x = origin->x, s.y = origin->y, s.z = origin->z;
    s.loggedBlock = false;
    s.loggedTint  = false;
    s.ticks       = 0;
    s.on          = true;
    render::registerBlockRenderer(kFixtureHead, drawStageBlock);
    tint::registerBlock(kStageLeaves, tint::GameTintFn{tintStageLeaves});
    log().info("ui fixture: stage renderers on at {} {} {}", origin->x, origin->y, origin->z);
}

/// What the client sees on the stage 3 s after the renderers came on, when none of them ran yet.
void tickStage() {
    auto& s = stageAt();
    if (!s.on || ++s.ticks != 60 || (s.loggedBlock && s.loggedTint)) return;
    auto* p = currentPlayer();
    if (!p) return;
    auto& region = p->getDimensionBlockSource();
    int   x = s.x, y = s.y, z = s.z;
    log().warn(
        "ui fixture: stage renderers not run yet (block {}, tint {}); the client sees {} and {} beside the frame", s.loggedBlock.load(), s.loggedTint.load(),
        game::blockId(region.getBlock(BlockPos{x - 1, y, z + 2})), game::blockId(region.getBlock(BlockPos{x + 1, y, z + 2}))
    );
}

void setFog(bool on) {
    auto* player = currentPlayer();
    int   dim    = player ? static_cast<int>(player->getDimensionId()) : 0;
    if (!on) {
        int old = st().fogDim.exchange(-1);
        if (old >= 0) sky::clearProvider(old);
        return;
    }
    st().fogDim = dim;
    sky::setProvider(
        dim,
        {
            .sky    = [](render::RenderContext&, float) { return false; }, // the vanilla sky stays; the hook reports itself
            .fog    = [](std::string const&) -> std::optional<sky::FogSpec> { return sky::FogSpec{{0.75f, 0.2f, 0.2f}, 4.0f, 24.0f}; },
            .clouds = [] { return std::optional<float>(160.0f); },
        }
    );
}

/// The id of the map the local player holds (0: none yet; a new map gets its id from the server).
std::int64_t heldMapId() {
    auto* p = currentPlayer();
    if (!p) return 0;
    auto const& s = p->getSelectedItem();
    if (s.isNull() || s.getTypeName() != "minecraft:filled_map" || !s.mUserData || !s.mUserData->contains("map_uuid")) return 0;
    try {
        return static_cast<std::int64_t>((*s.mUserData)["map_uuid"]);
    } catch (...) {
        return 0;
    }
}

/// Client tick: the decorations go on the held map once it has its id.
void tickMap() {
    if (!st().mapPending) return;
    auto id = heldMapId();
    if (id == 0) return;
    st().mapPending = false;
    st().mapId      = id;
    maps::setIcon(kIconA, tex().geo);
    maps::setIcon(kIconB, tex().skin);
    // corners, centre and a turned icon: the map space and the rotation show in the screenshot; the
    // north-west icon is conquered (TF's red X over it) and the vanilla player icon, listed before the
    // centre icon it overlaps, is drawn on top of it (MagicMapPlayerIconRenderer)
    maps::setDecorations(
        id,
        {
            {.type = kIconA, .x = -96, .z = -96, .rotation = 0, .name = "NW"},
            {.type = "minecraft:player", .x = 6, .z = 6, .rotation = 0, .name = ""},
            {.type = kIconB, .x = 0, .z = 0, .rotation = 0, .name = ""},
            {.type = kIconA, .x = 96, .z = -96, .rotation = 4, .name = ""},
            {.type = kIconA, .x = 96, .z = 96, .rotation = 8, .name = "SE"},
        },
        {{.conquered = true}, {.topmost = true}, {}, {}, {}}
    );
    log().info("ui fixture: map decorations set on map {}", id);
}

void clearMap() {
    st().mapPending = false;
    if (st().mapId != 0) maps::setDecorations(st().mapId, {});
    st().mapId = 0;
    maps::removeIcon(kIconA);
    maps::removeIcon(kIconB);
}

void onMessage(Json const& msg) {
    bool show = msg.value("show", true);
    if (!show) {
        st().hud   = false;
        st().world = false;
        setFog(false);
        clearMap();
        setStage(std::nullopt);
        log().info("ui fixture: hidden");
        return;
    }
    if (auto const& at = msg.value("stage", Json()); at.is_array() && at.size() == 3) setStage(BlockPos{at[0].get<int>(), at[1].get<int>(), at[2].get<int>()});
    if (msg.value("hud", false)) {
        st().hud       = true;
        st().loggedHud = false;
    }
    if (msg.value("world", false)) {
        st().world       = true;
        st().loggedWorld = false;
    }
    if (msg.value("toast", false)) {
        // LockedBiomeToast's colours (the contract's default) with a flat icon, then
        // MissingAdvancementToast's with a block icon (TF's advancement icons are often blocks:
        // its FALLBACK is the portal miniature) and a literal body (an advancement title as text)
        toast::show({.iconItem = "minecraft:map", .titleKey = "gf.ui.fixture.toast_title", .bodyKey = "gf.ui.fixture.toast_body", .ms = 30000});
        toast::show(
            toast::LiteralToastSpec{{.iconItem = "minecraft:grass_block", .titleKey = "gf.ui.fixture.toast_advancement", .ms = 30000}, {}, "Literal: The Twilight Forest"},
            toast::styleOf(toast::Kind::MissingAdvancement)
        );
    }
    if (msg.contains("fog")) setFog(msg.value("fog", false));
    if (msg.value("map", false)) st().mapPending = true;
    st().loggedFixed = false;
    st().loggedHead  = false;
    log().info("ui fixture: shown ({})", graphicsModeName());
}

/// The fixture screen: TF's container layout (176 x 166, title at (8, 6), nine slots in a row at
/// y 18, the player inventory at (8, 84)).
class FixtureScreen : public screen::ContainerScreen {
public:
    FixtureScreen(Json const& data, screen::SendFn send)
    : ContainerScreen(ctr("gf.ui.fixture.title", "GloamingForest UI fixture"), tex().skin, 176, 166),
      mSend(std::move(send)) {
        for (int i = 0; i < 9; ++i) slots.push_back({i, 8 + static_cast<float>(i) * 18, 18});
        onData(data);
    }
    void onData(Json const& data) override {
        ui::setContainerItems(*this, data.value("items", Json::object()));
        mClicks = data.value("clicks", 0);
    }
    void renderExtra(screen::Canvas& c, float mx, float my) override {
        ContainerScreen::renderExtra(c, mx, my);
        c.text(8, 44, ctr("gf.ui.fixture.clicks", "Clicks: {}", mClicks), 0xFF404040, screen::Align::Left, false);
    }
    bool clickBackground(float mx, float my, screen::Button b, bool) override {
        if (b != screen::Button::Left) return false;
        int slot = ui::containerSlotAt(*this, mx, my);
        if (slot < 0) return false;
        if (mSend) mSend({{"click", slot}});
        return true;
    }

private:
    screen::SendFn mSend;
    int            mClicks = 0;
};

class FixtureModule {
public:
    // keys at mod load: LeviLamina binds them when the game builds its input handler
    void load() { registerFixtureKey(); }
    void install() {
        if (!gf::ui::fixture::enabled() || mInstalled) return; // production build: no fixture
        mInstalled = true;
        installAssets();
        hud::addLayer("ui.fixture", 1000, drawHud);
        render::onWorld(drawWorld);
        render::registerItemRenderer(kFixtureItem, drawFixed, render::Fixed);
        render::registerItemRenderer(kFixtureHead, drawHead, render::Head);
        screen::registerScreen("ui.fixture", [](Json const& data, screen::SendFn send) { return std::make_unique<FixtureScreen>(data, std::move(send)); });
        net::client::on("ui.fixture", onMessage);
        registerFixtureKey();
        // a key registered after the game built its input handler: delivered from raw key events
        keys::registerKey("ui.fixture_late", kLateKey, "key.categories.gloamingforest.fixture", [] { log().info("ui fixture: late key pressed (client)"); });
        // a native particle the client scenario spawns with /particle (SpawnParticleEffectPacket) and
        // the server with gf::fx::particle (fx.particle)
        particles::registerNative(kParticle, [](Vec3 const& pos, Vec3 const&, Json const&) {
            static std::atomic<bool> logged{false};
            if (!logged.exchange(true)) log().info("ui fixture: particle factory ran at {:.1f} {:.1f} {:.1f}", pos.x, pos.y, pos.z);
        });
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            st().hud   = false;
            st().world = false;
            setFog(false);
            clearMap();
            setStage(std::nullopt);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) {
            tickMap();
            tickStage();
        }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    void registerFixtureKey() {
        if (mKey || !gf::ui::fixture::enabled()) return;
        mKey = true;
        keys::registerKey("ui.fixture", kFixtureKey, "key.categories.gloamingforest.fixture", [] { log().info("ui fixture: key pressed (client)"); });
    }

    bool                                mKey       = false;
    bool                                mInstalled = false;
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

} // namespace gf::client::ui::fixture

GF_REGISTER_MODULE(gf::client::ui::fixture::FixtureModule);

#endif // GF_CLIENT
