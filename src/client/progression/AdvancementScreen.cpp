// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// U14, client mod: the advancement screen with TF's tab ("progression.advancements"), drawn as
// Java's AdvancementsScreen lays it out (window 252 x 140, the tree inside at (9, 18), 234 x 113,
// the tab above the window with the root's icon, "Advancements" at (8, 6)); the tab's background
// is the root's (TF's mazestone_large_brick), tiled 16 x 16 and scrolled with the tree; nodes sit
// at (x * 28, y * 27) of the layout, connected by Java's black-outlined white lines; a hovered
// node shows its title bar (progress "met/total" while unfinished) and its description in the
// frame's colour. The frame and window art is redrawn (Java's sprites are Mojang's): task frames
// square, goals with cut corners, challenges with spikes; earned frames gold, the others grey.
// Panning: the mouse wheel scrolls vertically, Shift + wheel horizontally (the screen framework
// has no drag events; Java pans by dragging).

#ifdef GF_CLIENT

#include <algorithm>
#include <cmath>
#include <memory>

#include "client/Client.h"
#include "client/screen/base/Screen.h"
#include "client/ui/Ui.h"
#include "feature/ext/ui/Client.h"
#include "mod/Log.h"

namespace gf::client::progression {

namespace {

using screen::Canvas;
using screen::Json;
using screen::Rect;

constexpr float kWinW = 252, kWinH = 140;
constexpr float kInX = 9, kInY = 18, kInW = 234, kInH = 113;

struct Node {
    std::string id, parent, title, desc, icon, frame;
    bool        done = false;
    int         met = 0, groups = 0;
    float       x = 0, y = 0; ///< tab pixels (Java's widget x / y)
    std::shared_ptr<ItemStack> iconStack;
};

/// Fills `r` (inner coordinates) clipped to the inner area.
void fillIn(Canvas& c, Rect r, unsigned argb) {
    float x0 = std::max(r.x, 0.0f), y0 = std::max(r.y, 0.0f);
    float x1 = std::min(r.x + r.w, kInW), y1 = std::min(r.y + r.h, kInH);
    if (x1 <= x0 || y1 <= y0) return;
    c.fill({kInX + x0, kInY + y0, x1 - x0, y1 - y0}, argb);
}

struct FramePalette {
    unsigned border, fill, light, dark;
};
FramePalette paletteOf(bool done) {
    return done ? FramePalette{0xFF4A3300, 0xFFE3B53A, 0xFFFFF0A0, 0xFFA87A12} : FramePalette{0xFF2B2B2B, 0xFFB5B5B5, 0xFFEFEFEF, 0xFF7A7A7A};
}

/// A 26 x 26 frame at inner (x, y).
void drawFrame(Canvas& c, float x, float y, std::string const& frame, bool done) {
    auto p = paletteOf(done);
    if (frame == "challenge") {
        // spikes: the corners and the edge middles stand out of a 22 x 22 body
        for (auto [sx, sy] : {std::pair{0.f, 0.f}, {22.f, 0.f}, {0.f, 22.f}, {22.f, 22.f}, {11.f, 0.f}, {11.f, 22.f}, {0.f, 11.f}, {22.f, 11.f}})
            fillIn(c, {x + sx, y + sy, 4, 4}, p.border);
        fillIn(c, {x + 2, y + 2, 22, 22}, p.border);
        fillIn(c, {x + 3, y + 3, 20, 20}, p.fill);
        fillIn(c, {x + 3, y + 3, 20, 1}, p.light);
        fillIn(c, {x + 3, y + 3, 1, 20}, p.light);
        fillIn(c, {x + 3, y + 22, 20, 1}, p.dark);
        fillIn(c, {x + 22, y + 3, 1, 20}, p.dark);
        fillIn(c, {x + 1, y + 1, 2, 2}, p.fill);
        fillIn(c, {x + 23, y + 1, 2, 2}, p.fill);
        fillIn(c, {x + 1, y + 23, 2, 2}, p.fill);
        fillIn(c, {x + 23, y + 23, 2, 2}, p.fill);
        return;
    }
    if (frame == "goal") {
        // cut corners
        fillIn(c, {x + 3, y, 20, 26}, p.border);
        fillIn(c, {x, y + 3, 26, 20}, p.border);
        fillIn(c, {x + 1, y + 1, 24, 24}, p.border);
        fillIn(c, {x + 3, y + 1, 20, 24}, p.fill);
        fillIn(c, {x + 1, y + 3, 24, 20}, p.fill);
        fillIn(c, {x + 2, y + 2, 22, 22}, p.fill);
        fillIn(c, {x + 3, y + 1, 20, 1}, p.light);
        fillIn(c, {x + 1, y + 3, 1, 20}, p.light);
        fillIn(c, {x + 3, y + 24, 20, 1}, p.dark);
        fillIn(c, {x + 24, y + 3, 1, 20}, p.dark);
        return;
    }
    fillIn(c, {x, y, 26, 26}, p.border);
    fillIn(c, {x + 1, y + 1, 24, 24}, p.fill);
    fillIn(c, {x + 1, y + 1, 24, 1}, p.light);
    fillIn(c, {x + 1, y + 1, 1, 24}, p.light);
    fillIn(c, {x + 1, y + 24, 24, 1}, p.dark);
    fillIn(c, {x + 24, y + 1, 1, 24}, p.dark);
}

/// Splits `text` into lines of at most `width` GUI pixels.
std::vector<std::string> wrap(Canvas& c, std::string const& text, float width) {
    std::vector<std::string> lines;
    std::string              line, word;
    auto flushWord = [&] {
        if (word.empty()) return;
        auto candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && c.textWidth(candidate) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char ch : text) {
        if (ch == ' ') flushWord();
        else if (ch == '\n') {
            flushWord();
            lines.push_back(line);
            line.clear();
        } else word += ch;
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

class TreeView : public screen::Widget {
public:
    explicit TreeView(class AdvancementScreen& s) : Widget({kInX, kInY, kInW, kInH}), mScreen(s) {}
    void render(Canvas&, float, float) override {}
    bool scroll(float mx, float my, int delta, bool shift) override;

private:
    class AdvancementScreen& mScreen;
};

class AdvancementScreen : public screen::Screen {
public:
    explicit AdvancementScreen(Json const& data) : Screen("", kWinW, kWinH) { load(data, true); }

    void init() override { add<TreeView>(*this); }

    void onData(Json const& data) override { load(data, false); }

    void pan(float dx, float dy) {
        mScrollX = clampAxis(mScrollX + dx, mMinX, mMaxX, kInW);
        mScrollY = clampAxis(mScrollY + dy, mMinY, mMaxY, kInH);
    }

    void renderExtra(Canvas& c, float mx, float my) override {
        // the tab above the window (AdvancementTabType.ABOVE, the selected one)
        c.bevel({0, -28, 28, 32}, screen::color::kPanel, screen::color::kPanelLight, screen::color::kPanelDark, false);
        c.fill({1, 1, 26, 3}, screen::color::kPanel);
        if (mTabIcon) c.item(*mTabIcon, 6, -28 + 9);
        // inside: black, then the tab's contents
        c.fill({kInX, kInY, kInW, kInH}, 0xFF000000);
        if (mNodes.empty()) {
            c.text(kInX + kInW / 2, kInY + kInH / 2 - 9, ctr("advancements.empty", "There doesn't seem to be anything here..."), 0xFFFFFFFF, screen::Align::Centre);
            c.text(kInX + kInW / 2, kInY + kInH - 9, ":(", 0xFFFFFFFF, screen::Align::Centre);
        } else {
            drawContents(c);
        }
        // the window's inner frame and the title
        c.frame({kInX - 1, kInY - 1, kInW + 2, kInH + 2}, screen::color::kPanelDark);
        c.text(8, 6, ctr("gui.advancements", "Advancements"), screen::color::kTextDark, screen::Align::Left, false);
        if (!mNodes.empty()) {
            auto tab = ctr(mTabTitle, mTabTitle);
            c.text(8 + c.textWidth(ctr("gui.advancements", "Advancements")) + 6, 6, "- " + tab, screen::color::kTextDark, screen::Align::Left, false);
        }
        // hover: the node under the mouse
        if (mx >= kInX && mx < kInX + kInW && my >= kInY && my < kInY + kInH) {
            float ix = mx - kInX, iy = my - kInY;
            for (auto const& n : mNodes) {
                float nx = std::floor(mScrollX) + n.x + 3, ny = std::floor(mScrollY) + n.y;
                if (ix >= nx && ix < nx + 26 && iy >= ny && iy < ny + 26) {
                    drawHover(c, n, nx, ny);
                    break;
                }
            }
        }
        // the tab's title when its tab is hovered
        if (mx >= 0 && mx < 28 && my >= -28 && my < 0) c.tooltip(mx, my, {ctr(mTabTitle, mTabTitle)});
    }

private:
    std::vector<Node>          mNodes;
    std::string                mTabTitle, mBackground;
    std::shared_ptr<ItemStack> mTabIcon;
    float                      mScrollX = 0, mScrollY = 0;
    float                      mMinX = 0, mMaxX = 0, mMinY = 0, mMaxY = 0;

    static float clampAxis(float v, float lo, float hi, float view) {
        if (hi - lo <= view) return view / 2 - (hi + lo) / 2; // centred, as Java
        return std::clamp(v, -(hi - view), -lo);
    }

    void load(Json const& data, bool first) {
        auto tab    = data.value("tab", Json::object());
        mTabTitle   = tab.value("title", std::string{});
        mBackground = tab.value("background", std::string{});
        mTabIcon    = screen::iconOf(Json{{"id", tab.value("icon", std::string{})}, {"count", 1}, {"aux", 0}});
        mNodes.clear();
        for (auto const& j : data.value("nodes", Json::array())) {
            Node n;
            n.id     = j.value("id", std::string{});
            n.parent = j.value("parent", std::string{});
            n.title  = j.value("title", std::string{});
            n.desc   = j.value("desc", std::string{});
            n.icon   = j.value("icon", std::string{});
            n.frame  = j.value("frame", std::string{"task"});
            n.done   = j.value("done", false);
            n.met    = j.value("met", 0);
            n.groups = j.value("groups", 0);
            n.x      = std::floor(j.value("x", 0.0f) * 28);
            n.y      = std::floor(j.value("y", 0.0f) * 27);
            n.iconStack = screen::iconOf(Json{{"id", n.icon}, {"count", 1}, {"aux", 0}});
            mNodes.push_back(std::move(n));
        }
        if (mNodes.empty()) {
            if (first) log().info("client screen: progression.advancements (empty)");
            return;
        }
        mMinX = mMinY = 1e9f;
        mMaxX = mMaxY = -1e9f;
        for (auto const& n : mNodes) {
            mMinX = std::min(mMinX, n.x);
            mMaxX = std::max(mMaxX, n.x + 28);
            mMinY = std::min(mMinY, n.y);
            mMaxY = std::max(mMaxY, n.y + 27);
        }
        if (first) {
            // AdvancementTab: centred on the tree
            mScrollX = 117 - (mMaxX + mMinX) / 2;
            mScrollY = 56 - (mMaxY + mMinY) / 2;
            log().info("client screen: progression.advancements ({} nodes, {} earned)", mNodes.size(),
                       std::count_if(mNodes.begin(), mNodes.end(), [](Node const& n) { return n.done; }));
        }
        pan(0, 0);
    }

    Node const* nodeOf(std::string const& id) const {
        for (auto const& n : mNodes)
            if (n.id == id) return &n;
        return nullptr;
    }

    void drawContents(Canvas& c) {
        int sx = static_cast<int>(std::floor(mScrollX)), sy = static_cast<int>(std::floor(mScrollY));
        // the background: 16 x 16 tiles scrolled with the tree
        if (!mBackground.empty()) {
            int k = sx % 16, l = sy % 16;
            for (int i = -1; i <= 15; ++i)
                for (int j = -1; j <= 8; ++j) {
                    float tx = static_cast<float>(k + 16 * i), ty = static_cast<float>(l + 16 * j);
                    float x0 = std::max(tx, 0.0f), y0 = std::max(ty, 0.0f);
                    float x1 = std::min(tx + 16, kInW), y1 = std::min(ty + 16, kInH);
                    if (x1 <= x0 || y1 <= y0) continue;
                    ui::blit(c, mBackground, {kInX + x0, kInY + y0, x1 - x0, y1 - y0}, x0 - tx, y0 - ty, x1 - x0, y1 - y0, 16, 16);
                }
        }
        // connections: the black outline first, then the white line (AdvancementWidget#drawConnectivity)
        for (bool shadow : {true, false}) {
            unsigned col = shadow ? 0xFF000000 : 0xFFFFFFFF;
            for (auto const& n : mNodes) {
                auto const* p = nodeOf(n.parent);
                if (!p) continue;
                float i = sx + p->x + 13, j = sx + p->x + 26 + 4, k = sy + p->y + 13;
                float l = sx + n.x + 13, i1 = sy + n.y + 13;
                auto hLine = [&](float a, float b, float y) { fillIn(c, {std::min(a, b), y, std::abs(b - a) + 1, 1}, col); };
                auto vLine = [&](float x, float a, float b) { fillIn(c, {x, std::min(a, b), 1, std::abs(b - a) + 1}, col); };
                if (shadow) {
                    hLine(j, i, k - 1);
                    hLine(j + 1, i, k);
                    hLine(j, i, k + 1);
                    hLine(l, j - 1, i1 - 1);
                    hLine(l, j - 1, i1);
                    hLine(l, j - 1, i1 + 1);
                    vLine(j - 1, i1, k);
                    vLine(j + 1, i1, k);
                } else {
                    hLine(j, i, k);
                    hLine(l, j, i1);
                    vLine(j, i1, k);
                }
            }
        }
        // the nodes: frame at (x + 3, y), icon at (x + 8, y + 5)
        for (auto const& n : mNodes) {
            float fx = sx + n.x + 3, fy = sy + n.y;
            drawFrame(c, fx, fy, n.frame, n.done);
            float ix = sx + n.x + 8, iy = sy + n.y + 5;
            if (n.iconStack && ix >= 0 && iy >= 0 && ix + 16 <= kInW && iy + 16 <= kInH) c.item(*n.iconStack, kInX + ix, kInY + iy);
        }
    }

    void drawHover(Canvas& c, Node const& n, float nx, float ny) {
        // AdvancementWidget#drawHover: the title bar from the node to the right, the description
        // below it in the frame's colour, progress on the bar while unfinished
        auto        title = ctr(n.title, n.title);
        std::string progress = !n.done && n.groups > 1 ? std::to_string(n.met) + "/" + std::to_string(n.groups) : std::string{};
        float       width = std::max(29 + c.textWidth(title) + (progress.empty() ? 0 : c.textWidth(progress) + 8) + 8, 120.0f);
        auto        desc  = wrap(c, ctr(n.desc, n.desc), width - 10);
        float       bx = kInX + nx, by = kInY + ny;
        // keep the box inside the window: open to the left when it would leave it on the right
        bool left = bx + width > kWinW;
        float x0  = left ? bx + 26 - width : bx;
        unsigned descCol = n.frame == "challenge" ? 0xFFAA00AA : 0xFF55FF55;
        if (!desc.empty()) {
            float h = 6 + 9.0f * static_cast<float>(desc.size());
            c.fill({x0, by + 16, width, h + 10}, 0xFF555555);
            c.fill({x0 + 1, by + 17, width - 2, h + 8}, 0xFF212121);
            for (std::size_t i = 0; i < desc.size(); ++i) c.text(x0 + 5, by + 28 + 9.0f * static_cast<float>(i), desc[i], descCol);
        }
        unsigned bar = n.done ? 0xFFC8971E : 0xFF1F5FA6;
        c.fill({x0, by + 3, width, 20}, 0xFF000000);
        c.fill({x0 + 1, by + 4, width - 2, 18}, bar);
        c.fill({x0 + 1, by + 4, width - 2, 1}, n.done ? 0xFFF2CE5A : 0xFF4A86CF);
        // the node again on top of its bar
        drawFrame(c, nx, ny, n.frame, n.done);
        if (n.iconStack) c.item(*n.iconStack, kInX + nx + 5, kInY + ny + 5);
        float tx = left ? x0 + 5 : bx + 32;
        c.text(tx, by + 9, title, 0xFFFFFFFF);
        if (!progress.empty()) c.text(left ? bx + 26 - 30 - c.textWidth(progress) : x0 + width - 5, by + 9, progress, 0xFFFFFFFF, left ? screen::Align::Left : screen::Align::Right);
    }
};

bool TreeView::scroll(float, float, int delta, bool shift) {
    float step = static_cast<float>(delta) * 16;
    if (shift) mScreen.pan(step, 0);
    else mScreen.pan(0, step);
    return true;
}

} // namespace

void registerAdvancementScreen() {
    screen::registerScreen("progression.advancements", [](Json const& data, screen::SendFn) -> std::unique_ptr<screen::Screen> {
        return std::make_unique<AdvancementScreen>(data);
    });
}

} // namespace gf::client::progression

#endif
