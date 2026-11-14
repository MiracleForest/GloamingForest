// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/Restriction.java
//   src/main/java/gloamingforest/util/Enforcement.java
//   src/main/java/gloamingforest/init/custom/Enforcements.java
//   src/main/java/gloamingforest/events/ProgressionEvents.java
//   src/main/java/gloamingforest/client/event/LockedBiomeToastHandler.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).

#include "core/progression/Rules.h"

// the tree layout must round exactly as Java's float arithmetic: no fused multiply-adds
#pragma STDC FP_CONTRACT OFF

#include <algorithm>
#include <deque>
#include <functional>
#include <map>

namespace gf::core::progression {

namespace {
std::string ns(std::string id) {
    if (!id.empty() && id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}
} // namespace

Enforcement enforcementOf(std::string const& id) {
    auto n = ns(id);
    if (n == "gloamingforest:darkness") return Enforcement::Darkness;
    if (n == "gloamingforest:hunger") return Enforcement::Hunger;
    if (n == "gloamingforest:fire") return Enforcement::Fire;
    if (n == "gloamingforest:frost") return Enforcement::Frost;
    if (n == "gloamingforest:acid_rain") return Enforcement::AcidRain;
    return Enforcement::Unknown;
}

char const* enforcementName(Enforcement e) {
    switch (e) {
    case Enforcement::Darkness: return "darkness";
    case Enforcement::Hunger: return "hunger";
    case Enforcement::Fire: return "fire";
    case Enforcement::Frost: return "frost";
    case Enforcement::AcidRain: return "acid_rain";
    default: return "unknown";
    }
}

std::optional<Restriction> Restriction::fromJson(std::string const& biome, nlohmann::json const& j) {
    if (!j.is_object()) return std::nullopt;
    Restriction r;
    r.biome         = ns(biome);
    r.enforcementId = ns(j.value("enforcement", std::string{}));
    r.enforcement   = enforcementOf(r.enforcementId);
    if (r.enforcementId.empty()) return std::nullopt;
    if (auto m = j.find("multiplier"); m != j.end() && m->is_number()) r.multiplier = m->get<float>();
    else return std::nullopt; // fieldOf("multiplier"): required
    if (auto s = j.find("structure_key"); s != j.end() && s->is_string()) r.structureKey = ns(s->get<std::string>());
    if (auto t = j.find("locked_biome_toast"); t != j.end()) {
        if (t->is_string()) r.toastIcon = ns(t->get<std::string>());
        else if (t->is_object()) r.toastIcon = ns(t->value("id", t->value("item", std::string{})));
    }
    for (auto const& a : j.value("advancements", nlohmann::json::array()))
        if (a.is_string()) r.advancements.push_back(ns(a.get<std::string>()));
    if (r.advancements.empty()) return std::nullopt; // ExtraCodecs.nonEmptyList
    return r;
}

bool Restriction::liftedFor(std::function<bool(std::string const&)> const& has) const {
    return std::all_of(advancements.begin(), advancements.end(), [&](std::string const& a) { return has(a); });
}

int innerPeriod(Enforcement e) { return e == Enforcement::AcidRain ? 5 : 60; }

bool innerGate(Enforcement e, int tickCount) { return tickCount % innerPeriod(e) == 0; }

bool enforcementDue(Enforcement e, int tickCount) {
    return e != Enforcement::Unknown && outerGate(tickCount) && innerGate(e, tickCount);
}

int hungerAmplifier(int currentAmplifier, float multiplier) {
    int m = static_cast<int>(multiplier);
    return currentAmplifier >= 0 ? currentAmplifier + m : m;
}

bool LockedToastCountdown::step(bool lockedWithIcon) {
    if (lockedWithIcon) {
        timeUntilToast--;
        if (!shownToast && timeUntilToast <= 0) {
            shownToast = true;
            return true;
        }
        return false;
    }
    if (shownToast) {
        timeUntilToast = 60;
        shownToast     = false;
    }
    return false;
}

// ---- the tree layout ------------------------------------------------------------------------------
// The same arithmetic as net.minecraft.advancements.TreeNodePosition (single-precision floats in
// the same order of operations; tests/progression/TreeLayoutGolden.inc holds Java's own output).


namespace {

struct Pos {
    std::size_t       node        = 0;
    Pos*              parent      = nullptr;
    Pos*              previous    = nullptr; ///< the previous sibling
    int               childIndex  = 1;       ///< 1-based
    std::vector<Pos*> children;
    Pos*              ancestor    = nullptr;
    Pos*              thread      = nullptr;
    int               x           = 0;
    float             y           = -1;
    float             mod         = 0;
    float             change      = 0;
    float             shift       = 0;

    Pos* previousOrThread() const { return thread ? thread : children.empty() ? nullptr : children.front(); }
    Pos* nextOrThread() const { return thread ? thread : children.empty() ? nullptr : children.back(); }
};

class Walker {
public:
    Walker(std::vector<TreeNode>& nodes, std::map<std::string, std::vector<std::size_t>> const& children) : mNodes(nodes), mChildren(children) {}

    void run(std::size_t root) {
        Pos* r = make(root, nullptr, nullptr, 1, 0);
        firstWalk(*r);
        float top = secondWalk(*r, 0.0f, 0, r->y);
        if (top < 0.0f) thirdWalk(*r, -top);
        for (auto& p : mPool) {
            mNodes[p.node].x      = static_cast<float>(p.x);
            mNodes[p.node].y      = p.y;
            mNodes[p.node].placed = true;
        }
    }

private:
    std::vector<TreeNode>&                                 mNodes;
    std::map<std::string, std::vector<std::size_t>> const& mChildren;
    std::deque<Pos>                                        mPool; ///< stable addresses

    std::vector<std::size_t> const& childrenOf(std::size_t i) const {
        static std::vector<std::size_t> const none;
        auto it = mChildren.find(mNodes[i].id);
        return it == mChildren.end() ? none : it->second;
    }

    Pos* make(std::size_t node, Pos* parent, Pos* previous, int childIndex, int x) {
        Pos& p       = mPool.emplace_back();
        p.node       = node;
        p.parent     = parent;
        p.previous   = previous;
        p.childIndex = childIndex;
        p.ancestor   = &p;
        p.x          = x;
        Pos* last    = nullptr;
        for (auto c : childrenOf(node)) last = addChild(p, c, last);
        return &p;
    }

    /// A displayed child becomes a position; a hidden one hands its children to `self`.
    Pos* addChild(Pos& self, std::size_t child, Pos* previous) {
        if (mNodes[child].display) {
            Pos* c = make(child, &self, previous, static_cast<int>(self.children.size()) + 1, self.x + 1);
            self.children.push_back(c);
            return c;
        }
        for (auto g : childrenOf(child)) previous = addChild(self, g, previous);
        return previous;
    }

    void firstWalk(Pos& v) {
        if (v.children.empty()) {
            v.y = v.previous ? v.previous->y + 1.0f : 0.0f;
            return;
        }
        Pos* defaultAncestor = nullptr;
        for (Pos* c : v.children) {
            firstWalk(*c);
            defaultAncestor = apportion(*c, defaultAncestor ? defaultAncestor : c);
        }
        executeShifts(v);
        float mid = (v.children.front()->y + v.children.back()->y) / 2.0f;
        if (v.previous) {
            v.y   = v.previous->y + 1.0f;
            v.mod = v.y - mid;
        } else {
            v.y = mid;
        }
    }

    float secondWalk(Pos& v, float offset, int column, float top) {
        v.y += offset;
        v.x = column;
        if (v.y < top) top = v.y;
        for (Pos* c : v.children) top = secondWalk(*c, offset + v.mod, column + 1, top);
        return top;
    }

    void thirdWalk(Pos& v, float by) {
        v.y += by;
        for (Pos* c : v.children) thirdWalk(*c, by);
    }

    static void executeShifts(Pos& v) {
        float shift = 0.0f, change = 0.0f;
        for (auto i = static_cast<std::ptrdiff_t>(v.children.size()) - 1; i >= 0; --i) {
            Pos& c = *v.children[static_cast<std::size_t>(i)];
            c.y += shift;
            c.mod += shift;
            change += c.change;
            shift += c.shift + change;
        }
    }

    static void moveSubtree(Pos& from, Pos& to, float shift) {
        float subtrees = static_cast<float>(to.childIndex - from.childIndex);
        if (subtrees != 0.0f) {
            to.change -= shift / subtrees;
            from.change += shift / subtrees;
        }
        to.shift += shift;
        to.y += shift;
        to.mod += shift;
    }

    static Pos* ancestorOf(Pos const& insideLeft, Pos const& self, Pos* fallback) {
        auto const& siblings = self.parent->children;
        if (insideLeft.ancestor && std::find(siblings.begin(), siblings.end(), insideLeft.ancestor) != siblings.end()) return insideLeft.ancestor;
        return fallback;
    }

    /// Buchheim's APPORTION: `self` against its left siblings' contours.
    Pos* apportion(Pos& self, Pos* defaultAncestor) {
        if (!self.previous) return defaultAncestor;
        Pos*  insideRight  = &self;          // vi+
        Pos*  outsideRight = &self;          // vo+
        Pos*  insideLeft   = self.previous;  // vi-
        Pos*  outsideLeft  = self.parent->children.front(); // vo-
        float sInsideRight = self.mod, sOutsideRight = self.mod;
        float sInsideLeft = insideLeft->mod, sOutsideLeft = outsideLeft->mod;
        while (insideLeft->nextOrThread() && insideRight->previousOrThread()) {
            insideLeft  = insideLeft->nextOrThread();
            insideRight = insideRight->previousOrThread();
            outsideLeft = outsideLeft ? outsideLeft->previousOrThread() : nullptr;
            outsideRight = outsideRight->nextOrThread();
            outsideRight->ancestor = &self;
            float shift = insideLeft->y + sInsideLeft - (insideRight->y + sInsideRight) + 1.0f;
            if (shift > 0.0f) {
                moveSubtree(*ancestorOf(*insideLeft, self, defaultAncestor), self, shift);
                sInsideRight += shift;
                sOutsideRight += shift;
            }
            sInsideLeft += insideLeft->mod;
            sInsideRight += insideRight->mod;
            if (outsideLeft) sOutsideLeft += outsideLeft->mod;
            sOutsideRight += outsideRight->mod;
        }
        if (insideLeft->nextOrThread() && !outsideRight->nextOrThread()) {
            outsideRight->thread = insideLeft->nextOrThread();
            outsideRight->mod += sInsideLeft - sOutsideRight;
        } else {
            if (insideRight->previousOrThread() && outsideLeft && !outsideLeft->previousOrThread()) {
                outsideLeft->thread = insideRight->previousOrThread();
                outsideLeft->mod += sInsideRight - sOutsideLeft;
            }
            defaultAncestor = &self;
        }
        return defaultAncestor;
    }
};

} // namespace

void layoutTree(std::vector<TreeNode>& nodes) {
    std::map<std::string, std::size_t>              index;
    std::map<std::string, std::vector<std::size_t>> children;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        index[nodes[i].id] = i;
        nodes[i].placed    = false;
    }
    std::vector<std::size_t> roots;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        auto const& p = nodes[i].parent;
        if (p.empty() || !index.contains(p)) roots.push_back(i);
        else children[p].push_back(i);
    }
    // ServerAdvancementManager: only trees whose root has a display are positioned
    for (auto r : roots)
        if (nodes[r].display) Walker(nodes, children).run(r);
}

// ---- text components --------------------------------------------------------------------------------

std::string renderComponent(nlohmann::json const& c, TranslateFn const& translate) {
    if (c.is_string()) return c.get<std::string>();
    if (c.is_number() || c.is_boolean()) return c.dump();
    std::string out;
    if (c.is_array()) {
        for (auto const& part : c) out += renderComponent(part, translate);
        return out;
    }
    if (!c.is_object()) return out;
    if (auto t = c.find("text"); t != c.end() && t->is_string()) out = t->get<std::string>();
    else if (auto k = c.find("translate"); k != c.end() && k->is_string()) {
        std::vector<std::string> args;
        if (auto w = c.find("with"); w != c.end() && w->is_array())
            for (auto const& a : *w) args.push_back(renderComponent(a, translate));
        auto key      = k->get<std::string>();
        auto fallback = c.value("fallback", key);
        out           = translate(key, fallback, args);
    }
    if (auto e = c.find("extra"); e != c.end() && e->is_array())
        for (auto const& part : *e) out += renderComponent(part, translate);
    return out;
}
} // namespace gf::core::progression
