// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// U14, client mod: the advancement screen with TF's tab ("progression.advancements"), drawn as
// Java's AdvancementsScreen lays it out (window 252 x 140, the tree inside at (9, 18), 234 x 113,
// the tab above the window with the root's icon, "Advancements" at (8, 6)); the tab's background
// is the root's (TF's mazestone_large_brick), tiled 16 x 16 and scrolled with the tree; nodes sit
// at (floor(x * 28), floor(y * 27)) of the server's TreeNodePosition layout, connected by Java's
// black-outlined white lines. Hovering a node (AdvancementWidget#isMouseOver: its 27 x 27 cell, a
// hidden node only once earned) dims the tree (fade up to 0.3) and draws AdvancementWidget's
// hover: the title bar (gold for the earned part, blue for the rest, progress "met/total" with
// more than one requirement), the description in the frame's colour below the node, or above it
// when it would not fit under the tab, and the box opening to the left when it would leave the
// screen. The frame and window art is redrawn (Java's sprites are Mojang's): task frames
// square, goals with cut corners, challenges with spikes; earned frames gold, the others grey.
// Panning: dragging with the left button (AdvancementsScreen#mouseDragged) and the mouse wheel
// (AdvancementsScreen#mouseScrolled, 16 pixels a notch; Shift: horizontally), clamped as
// AdvancementTab#scroll. Everything inside the tree is clipped to it (Java's scissor).

#ifdef GF_CLIENT

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/MouseInputEvent.h"

#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/input/RectangleArea.h"

#include "client/Client.h"
#include "client/progression/Client.h"
#include "client/screen/base/Screen.h"
#include "client/ui/Ui.h"
#include "core/progression/Rules.h"
#include "feature/ext/ui/Client.h"
#include "mod/Log.h"

namespace gf::client::progression {

namespace {

using screen::Canvas;
using screen::Json;
using screen::Rect;

constexpr float kWinW = 252, kWinH = 140;
constexpr float kInX = 9, kInY = 18, kInW = 234, kInH = 113;

/// The left button went up since the drag began (the framework only routes presses).
std::atomic<bool> gLeftReleased{false};

struct Node {
    std::string id, parent, title, desc, frame;
    bool        done = false, hidden = false;
    int         met = 0, groups = 0;
    float       x = 0, y = 0; ///< tab pixels (Java's widget x / y)
    std::shared_ptr<ItemStack> iconStack;
    // AdvancementWidget's hover layout, computed on the first hover (needs the font)
    bool                     measured = false;
    float                    width    = 0;
    std::vector<std::string> lines;
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

/// A 26 x 26 frame at (x, y); `clip`: inner coordinates clipped to the tree, else panel coordinates.
void drawFrame(Canvas& c, float x, float y, std::string const& frame, bool done, bool clip = true) {
    auto p   = paletteOf(done);
    auto box = [&](Rect r, unsigned col) {
        if (clip) fillIn(c, r, col);
        else c.fill(r, col);
    };
    if (frame == "challenge") {
        // spikes: the corners and the edge middles stand out of a 22 x 22 body
        for (auto [sx, sy] : {std::pair{0.f, 0.f}, {22.f, 0.f}, {0.f, 22.f}, {22.f, 22.f}, {11.f, 0.f}, {11.f, 22.f}, {0.f, 11.f}, {22.f, 11.f}})
            box({x + sx, y + sy, 4, 4}, p.border);
        box({x + 2, y + 2, 22, 22}, p.border);
        box({x + 3, y + 3, 20, 20}, p.fill);
        box({x + 3, y + 3, 20, 1}, p.light);
        box({x + 3, y + 3, 1, 20}, p.light);
        box({x + 3, y + 22, 20, 1}, p.dark);
        box({x + 22, y + 3, 1, 20}, p.dark);
        box({x + 1, y + 1, 2, 2}, p.fill);
        box({x + 23, y + 1, 2, 2}, p.fill);
        box({x + 1, y + 23, 2, 2}, p.fill);
        box({x + 23, y + 23, 2, 2}, p.fill);
        return;
    }
    if (frame == "goal") {
        // cut corners
        box({x + 3, y, 20, 26}, p.border);
        box({x, y + 3, 26, 20}, p.border);
        box({x + 1, y + 1, 24, 24}, p.border);
        box({x + 3, y + 1, 20, 24}, p.fill);
        box({x + 1, y + 3, 24, 20}, p.fill);
        box({x + 2, y + 2, 22, 22}, p.fill);
        box({x + 3, y + 1, 20, 1}, p.light);
        box({x + 1, y + 3, 1, 20}, p.light);
        box({x + 3, y + 24, 20, 1}, p.dark);
        box({x + 24, y + 3, 1, 20}, p.dark);
        return;
    }
    box({x, y, 26, 26}, p.border);
    box({x + 1, y + 1, 24, 24}, p.fill);
    box({x + 1, y + 1, 24, 1}, p.light);
    box({x + 1, y + 1, 1, 24}, p.light);
    box({x + 1, y + 24, 24, 1}, p.dark);
    box({x + 24, y + 1, 1, 24}, p.dark);
}

/// StringSplitter#splitLines: words onto lines of at most `width` GUI pixels.
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

/// AdvancementWidget#findOptimalLines: the split (at the target width + 0, -10, +10, -25, +25)
/// whose widest line is within 10 of the target, else the closest one.
std::vector<std::string> optimalLines(Canvas& c, std::string const& text, int target) {
    std::vector<std::string> best;
    float                    bestDiff = 3.4e38f;
    for (int offset : {0, 10, -10, 25, -25}) {
        auto  lines = wrap(c, text, static_cast<float>(target - offset));
        float widest = 0;
        for (auto const& l : lines) widest = std::max(widest, c.textWidth(l));
        float diff = std::abs(widest - static_cast<float>(target));
        if (diff <= 10.0f) return lines;
        if (diff < bestDiff) {
            bestDiff = diff;
            best     = std::move(lines);
        }
    }
    return best;
}

std::string progressText(Node const& n) {
    // AdvancementProgress#getProgressText: "advancements.progress" (%s/%s) with more than one requirement
    if (n.groups <= 1) return {};
    return ctr("advancements.progress", "%s/%s", n.met, n.groups);
}

/// AdvancementWidget's constructor: the hover's width and description lines.
void measure(Canvas& c, Node& n) {
    if (n.measured) return;
    n.measured = true;
    int digits = static_cast<int>(std::to_string(n.groups).size());
    float k    = n.groups > 1 ? c.textWidth("  ") + c.textWidth("0") * static_cast<float>(digits * 2) + c.textWidth("/") : 0;
    float l    = 29 + c.textWidth(n.title) + k;
    n.lines    = n.desc.empty() ? std::vector<std::string>{} : optimalLines(c, n.desc, static_cast<int>(l));
    for (auto const& line : n.lines) l = std::max(l, c.textWidth(line));
    n.width = l + 3 + 5;
}

class TreeView : public screen::Widget {
public:
    explicit TreeView(class AdvancementScreen& s) : Widget({kInX, kInY, kInW, kInH}), mScreen(s) {}
    void render(Canvas&, float, float) override {}
    bool scroll(float mx, float my, int delta, bool shift) override;
    bool click(float mx, float my, screen::Button b, bool shift) override;

private:
    class AdvancementScreen& mScreen;
};

class AdvancementScreen : public screen::Screen {
public:
    explicit AdvancementScreen(Json const& data) : Screen("", kWinW, kWinH) { load(data, true); }

    void init() override { add<TreeView>(*this); }

    void onData(Json const& data) override { load(data, false); }

    /// AdvancementTab#scroll: an axis moves only when the tree is larger than the view.
    void pan(float dx, float dy) {
        if (mMaxX - mMinX > kInW) mScrollX = std::clamp(mScrollX + dx, -(mMaxX - kInW), 0.0f);
        if (mMaxY - mMinY > kInH) mScrollY = std::clamp(mScrollY + dy, -(mMaxY - kInH), 0.0f);
    }

    void startDrag(float mx, float my) {
        gLeftReleased = false;
        mDragging     = true;
        mDragX        = mx;
        mDragY        = my;
        mDragMoved    = 0;
    }

    void renderExtra(Canvas& c, float mx, float my) override {
        followDrag(mx, my);
        // the tab above the window (AdvancementTabType.ABOVE, the selected one)
        c.bevel({0, -28, 28, 32}, screen::color::kPanel, screen::color::kPanelLight, screen::color::kPanelDark, false);
        c.fill({1, 1, 26, 3}, screen::color::kPanel);
        if (mTabIcon) c.item(*mTabIcon, 6, -28 + 9);
        // inside: black, then the tab's contents
        c.fill({kInX, kInY, kInW, kInH}, 0xFF000000);
        Node* hovered = nullptr;
        if (mNodes.empty()) {
            c.text(kInX + kInW / 2, kInY + kInH / 2 - 9, ctr("advancements.empty", "There doesn't seem to be anything here..."), 0xFFFFFFFF, screen::Align::Centre);
            c.text(kInX + kInW / 2, kInY + kInH - 9, ":(", 0xFFFFFFFF, screen::Align::Centre);
        } else {
            drawContents(c);
            hovered = nodeUnder(mx, my);
        }
        // the window's inner frame and the title
        c.frame({kInX - 1, kInY - 1, kInW + 2, kInH + 2}, screen::color::kPanelDark);
        c.text(8, 6, ctr("gui.advancements", "Advancements"), screen::color::kTextDark, screen::Align::Left, false);
        if (!mNodes.empty()) c.text(8 + c.textWidth(ctr("gui.advancements", "Advancements")) + 6, 6, "- " + mTabTitle, screen::color::kTextDark, screen::Align::Left, false);
        // AdvancementTab#drawTooltips: the tree dims while a node is hovered
        mFade = hovered ? std::min(mFade + 0.02f, 0.3f) : std::max(mFade - 0.04f, 0.0f);
        if (mFade > 0) c.fill({kInX, kInY, kInW, kInH}, static_cast<unsigned>(std::floor(mFade * 255.0f)) << 24);
        if (hovered) drawHover(c, *hovered);
        logCells(c);
        if (hovered && hovered->id != mLastHover) logHover(*hovered);
        mLastHover = hovered ? hovered->id : std::string{};
        // the tab's title when its tab is hovered
        if (mx >= 0 && mx < 28 && my >= -28 && my < 0) c.tooltip(mx, my, {mTabTitle});
    }

private:
    std::vector<Node>          mNodes;
    std::string                mTabTitle, mBackground;
    std::shared_ptr<ItemStack> mTabIcon;
    float                      mScrollX = 0, mScrollY = 0;
    float                      mMinX = 0, mMaxX = 0, mMinY = 0, mMaxY = 0;
    float                      mFade = 0;
    std::string                mLastHover;
    bool                       mDragging = false;
    float                      mDragX = 0, mDragY = 0, mDragMoved = 0;

    /// AdvancementsScreen#mouseDragged: the tree follows the pointer while the left button is down.
    void followDrag(float mx, float my) {
        if (!mDragging) return;
        float dx = mx - mDragX, dy = my - mDragY;
        mDragX = mx, mDragY = my;
        if (dx != 0 || dy != 0) {
            pan(dx, dy);
            mDragMoved += std::abs(dx) + std::abs(dy);
        }
        if (gLeftReleased.exchange(false)) {
            mDragging = false;
            if (mDragMoved > 0)
                log().info("client screen: progression.advancements dragged to ({}, {})", static_cast<int>(std::floor(mScrollX)), static_cast<int>(std::floor(mScrollY)));
        }
    }

    static std::shared_ptr<ItemStack> iconFrom(Json const& item, std::string const& id) {
        if (item.is_object())
            if (auto s = screen::iconOf(item)) return s;
        return screen::iconOf(Json{{"id", id}, {"count", 1}, {"aux", 0}});
    }

    void load(Json const& data, bool first) {
        auto tab    = data.value("tab", Json::object());
        mTabTitle   = clientText(tab.value("title", Json("")));
        mBackground = tab.value("background", std::string{});
        mTabIcon    = iconFrom(tab.value("iconItem", Json()), tab.value("icon", std::string{}));
        mNodes.clear();
        for (auto const& j : data.value("nodes", Json::array())) {
            Node n;
            n.id        = j.value("id", std::string{});
            n.parent    = j.value("parent", std::string{});
            n.title     = clientText(j.value("title", Json("")));
            n.desc      = clientText(j.value("desc", Json("")));
            n.frame     = j.value("frame", std::string{"task"});
            n.done      = j.value("done", false);
            n.hidden    = j.value("hidden", false);
            n.met       = j.value("met", 0);
            n.groups    = j.value("groups", 0);
            n.x         = std::floor(j.value("x", 0.0f) * 28);
            n.y         = std::floor(j.value("y", 0.0f) * 27);
            n.iconStack = iconFrom(j.value("iconItem", Json()), j.value("icon", std::string{}));
            mNodes.push_back(std::move(n));
        }
        if (mNodes.empty()) {
            if (first) log().info("client screen: progression.advancements (empty)");
            return;
        }
        // AdvancementTab#addWidget: the tree's extent
        mMinX = mMinY = 2147483647.0f;
        mMaxX = mMaxY = -2147483648.0f;
        for (auto const& n : mNodes) {
            mMinX = std::min(mMinX, n.x);
            mMaxX = std::max(mMaxX, n.x + 28);
            mMinY = std::min(mMinY, n.y);
            mMaxY = std::max(mMaxY, n.y + 27);
        }
        if (first) {
            // AdvancementTab#drawContents: centred on the tree the first time
            mScrollX = static_cast<float>(117 - static_cast<int>(mMaxX + mMinX) / 2); // Java's int division
            mScrollY = static_cast<float>(56 - static_cast<int>(mMaxY + mMinY) / 2);
            log().info("client screen: progression.advancements ({} nodes, {} earned)", mNodes.size(),
                       std::count_if(mNodes.begin(), mNodes.end(), [](Node const& n) { return n.done; }));
        }
    }

    Node const* nodeOf(std::string const& id) const {
        for (auto const& n : mNodes)
            if (n.id == id) return &n;
        return nullptr;
    }

    /// AdvancementWidget#isMouseOver (inclusive 26 x 26 from the widget's x, not the frame's).
    Node* nodeUnder(float mx, float my) {
        if (mx < kInX || mx >= kInX + kInW || my < kInY || my >= kInY + kInH) return nullptr;
        int ix = static_cast<int>(std::floor(mx - kInX)), iy = static_cast<int>(std::floor(my - kInY));
        int sx = static_cast<int>(std::floor(mScrollX)), sy = static_cast<int>(std::floor(mScrollY));
        for (auto& n : mNodes) {
            if (n.hidden && !n.done) continue;
            int x0 = sx + static_cast<int>(n.x), y0 = sy + static_cast<int>(n.y);
            if (ix >= x0 && ix <= x0 + 26 && iy >= y0 && iy <= y0 + 26) return &n;
        }
        return nullptr;
    }

    /// Java's scissor over the tree: the item icons (the fills clip themselves) of nodes partly
    /// outside the tree are cut at its edge.
    struct Clip {
        MinecraftUIRenderContext& ctx;
        Clip(Canvas& c) : ctx(c.context()) {
            ctx.saveCurrentClippingRectangle();
            RectangleArea r;
            r._x0 = c.originX() + kInX;
            r._x1 = c.originX() + kInX + kInW;
            r._y0 = c.originY() + kInY;
            r._y1 = c.originY() + kInY + kInH;
            ctx.setClippingRectangle(r);
        }
        ~Clip() { ctx.restoreSavedClippingRectangle(); }
    };

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
        // the nodes: frame at (x + 3, y), icon at (x + 8, y + 5), cut at the tree's edge
        Clip clip(c);
        for (auto const& n : mNodes) {
            float fx = sx + n.x + 3, fy = sy + n.y;
            if (fx >= kInW || fy >= kInH || fx + 26 <= 0 || fy + 26 <= 0) continue;
            drawFrame(c, fx, fy, n.frame, n.done);
            float ix = sx + n.x + 8, iy = sy + n.y + 5;
            if (n.iconStack && ix < kInW && iy < kInH && ix + 16 > 0 && iy + 16 > 0) c.item(*n.iconStack, kInX + ix, kInY + iy);
        }
    }

    void drawHover(Canvas& c, Node& n) {
        // AdvancementWidget#drawHover, in panel coordinates (the tab's origin is (9, 18))
        measure(c, n);
        int   sx = static_cast<int>(std::floor(mScrollX)), sy = static_cast<int>(std::floor(mScrollY));
        float wx = kInX + static_cast<float>(sx) + n.x; // widget x
        float wy = kInY + static_cast<float>(sy) + n.y; // widget y
        auto  progress = progressText(n);
        float pw       = progress.empty() ? 0 : c.textWidth(progress);
        // the box opens to the left when it would leave the screen; the description goes above
        // the node when it would not fit under the tab
        float screenW = screen::guiSize().first;
        bool  left    = c.originX() + static_cast<float>(sx) + n.x + n.width + 26 >= screenW;
        bool  above   = 113 - static_cast<float>(sy) - n.y - 26 <= 6 + 9.0f * static_cast<float>(n.lines.size());
        float frac    = n.done ? 1.0f : n.groups > 0 ? static_cast<float>(n.met) / static_cast<float>(n.groups) : 0.0f;
        int   j       = static_cast<int>(std::floor(frac * n.width));
        bool  leftGold, rightGold;
        if (frac >= 1.0f) {
            j        = static_cast<int>(n.width) / 2;
            leftGold = rightGold = true;
        } else if (j < 2) {
            j        = static_cast<int>(n.width) / 2;
            leftGold = rightGold = false;
        } else if (static_cast<float>(j) > n.width - 2) {
            j        = static_cast<int>(n.width) / 2;
            leftGold = rightGold = true;
        } else {
            leftGold  = true;
            rightGold = false;
        }
        float x0 = left ? wx - n.width + 26 + 6 : wx;
        float h  = 32 + 9.0f * static_cast<float>(n.lines.size());
        if (!n.lines.empty()) {
            float by = above ? wy + 26 - h : wy;
            c.fill({x0, by, n.width, h}, 0xFF555555);
            c.fill({x0 + 1, by + 1, n.width - 2, h - 2}, 0xFF212121);
        }
        auto bar = [&](float bx, float bw, bool gold) {
            if (bw <= 0) return;
            c.fill({bx, wy + 3, bw, 20}, 0xFF000000);
            c.fill({bx, wy + 4, bw, 18}, gold ? 0xFFC8971E : 0xFF1F5FA6);
            c.fill({bx, wy + 4, bw, 1}, gold ? 0xFFF2CE5A : 0xFF4A86CF);
        };
        bar(x0, static_cast<float>(j), leftGold);
        bar(x0 + static_cast<float>(j), n.width - static_cast<float>(j), rightGold);
        // the node again on top of its bar
        drawFrame(c, wx + 3, wy, n.frame, n.done, false);
        if (left) {
            c.text(x0 + 5, wy + 9, n.title, 0xFFFFFFFF);
            if (!progress.empty()) c.text(wx - pw, wy + 9, progress, 0xFFFFFFFF);
        } else {
            c.text(wx + 32, wy + 9, n.title, 0xFFFFFFFF);
            if (!progress.empty()) c.text(wx + n.width - pw - 5, wy + 9, progress, 0xFFFFFFFF);
        }
        // the description in the frame's colour (AdvancementType#getChatColor), no shadow
        unsigned descCol = n.frame == "challenge" ? 0xFFAA00AA : 0xFF55FF55;
        for (std::size_t i = 0; i < n.lines.size(); ++i) {
            float ly = above ? wy + 26 - h + 7 + 9.0f * static_cast<float>(i) : wy + 9 + 17 + 9.0f * static_cast<float>(i);
            c.text(x0 + 5, ly, n.lines[i], descCol, screen::Align::Left, false);
        }
        if (n.iconStack) c.item(*n.iconStack, wx + 8, wy + 5);
        mHoverLeft  = left;
        mHoverAbove = above;
    }

    bool mHoverLeft = false, mHoverAbove = false;
    bool mCellsLogged = false;

    /// e2e: once per opening, where each node in view sits on the screen (fractions of the GUI,
    /// the harness's relative pointer coordinates).
    void logCells(Canvas& c) {
        if (mCellsLogged || mNodes.empty()) return;
        auto [gw, gh] = screen::guiSize();
        if (gw <= 0 || gh <= 0) return;
        mCellsLogged = true;
        int sx = static_cast<int>(std::floor(mScrollX)), sy = static_cast<int>(std::floor(mScrollY));
        for (auto const& n : mNodes) {
            float cx = static_cast<float>(sx) + n.x + 16, cy = static_cast<float>(sy) + n.y + 13; // the frame's centre
            if (cx < 0 || cy < 0 || cx >= kInW || cy >= kInH) continue;
            log().info("client screen: progression.advancements cell {} at {:.4f} {:.4f}", n.id, (c.originX() + kInX + cx) / gw, (c.originY() + kInY + cy) / gh);
        }
    }

    void logHover(Node const& n) const {
        std::string desc;
        for (auto const& l : n.lines) desc += (desc.empty() ? "" : " ") + l;
        log().info("client screen: progression.advancements hover {} ({}, {}): {} | {}", n.id, mHoverAbove ? "above" : "below", mHoverLeft ? "left" : "right", n.title,
                   desc);
    }
};

bool TreeView::scroll(float, float, int delta, bool shift) {
    // AdvancementsScreen#mouseScrolled: 16 pixels a notch
    float step = static_cast<float>(delta) * 16;
    if (shift) mScreen.pan(step, 0);
    else mScreen.pan(0, step);
    return true;
}

bool TreeView::click(float mx, float my, screen::Button b, bool) {
    if (b != screen::Button::Left || !rect.contains(mx, my)) return false;
    mScreen.startDrag(mx, my);
    return true;
}

} // namespace

std::string clientText(nlohmann::json const& component) {
    return core::progression::renderComponent(component, [](std::string const& key, std::string const& fallback, std::vector<std::string> const& args) {
        return core::formatMessage(translateClient(key, fallback), args);
    });
}

void registerAdvancementScreen() {
    screen::registerScreen("progression.advancements", [](Json const& data, screen::SendFn) -> std::unique_ptr<screen::Screen> {
        return std::make_unique<AdvancementScreen>(data);
    });
}

void watchMouseRelease() {
    // the screen framework routes presses only; a drag ends when the left button goes up
    static auto listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::MouseInputEvent>([](ll::event::MouseInputEvent& ev) {
        if (ev.actionButtonId() == 1 && ev.buttonData() == 0) gLeftReleased = true;
    });
    (void)listener;
}

} // namespace gf::client::progression

#endif
