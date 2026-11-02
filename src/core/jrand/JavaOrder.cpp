// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Java-parity orderings: Minecraft 1.21.1's FeatureSorter#buildFeaturesPerStep (with
// Graph#depthFirstSearch), java.util.HashMap's iteration order and Mth's sine table. Written from
// the published algorithms; checked in tests/worldgen.
#include "core/jrand/JavaOrder.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <tuple>
#include <unordered_map>

#pragma clang fp contract(off)

namespace gf::core::jrand {

namespace {

struct FeatureData {
    int         index; ///< first-seen order of the feature over all sources
    int         step;
    std::string id;
    bool        operator<(FeatureData const& o) const { return std::tie(step, index) < std::tie(o.step, o.index); }
};

using Graph = std::map<FeatureData, std::set<FeatureData>>;

/// Graph#depthFirstSearch: true when a cycle was found.
bool dfs(Graph const& g, std::set<FeatureData>& done, std::set<FeatureData>& path, std::vector<FeatureData>& out, FeatureData const& n) {
    if (done.contains(n)) return false;
    if (path.contains(n)) return true;
    path.insert(n);
    if (auto it = g.find(n); it != g.end()) {
        for (auto const& next : it->second)
            if (dfs(g, done, path, out, next)) return true;
    }
    path.erase(n);
    done.insert(n);
    out.push_back(n);
    return false;
}

} // namespace

std::vector<std::vector<std::string>> featureOrder(std::vector<StepFeatures> const& sources, bool* cycle) {
    if (cycle) *cycle = false;
    std::unordered_map<std::string, int> indices;
    Graph                                graph;
    std::size_t                          steps = 0;
    for (auto const& source : sources) {
        std::vector<FeatureData> list;
        steps = std::max(steps, source.size());
        for (std::size_t j = 0; j < source.size(); ++j) {
            for (auto const& id : source[j]) {
                auto [it, fresh] = indices.try_emplace(id, static_cast<int>(indices.size()));
                list.push_back({it->second, static_cast<int>(j), id});
            }
        }
        for (std::size_t k = 0; k < list.size(); ++k) {
            auto& next = graph[list[k]];
            if (k + 1 < list.size()) next.insert(list[k + 1]);
        }
    }
    std::set<FeatureData>    done, path;
    std::vector<FeatureData> order;
    for (auto const& [node, _] : graph) {
        if (!done.contains(node) && dfs(graph, done, path, order, node)) {
            // Java refuses to load (feature order cycle); keep what was sorted and go on
            if (cycle) *cycle = true;
            path.clear();
        }
    }
    std::reverse(order.begin(), order.end());
    std::vector<std::vector<std::string>> out(steps);
    for (auto const& fd : order) out[static_cast<std::size_t>(fd.step)].push_back(fd.id);
    return out;
}

std::vector<std::size_t> hashSetOrder(std::vector<int> const& hashes) {
    std::size_t                           cap = 16;
    std::vector<std::vector<std::size_t>> table(cap);
    auto spread = [](int h) { return static_cast<std::uint32_t>(h) ^ (static_cast<std::uint32_t>(h) >> 16); };
    std::size_t size = 0;
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        table[spread(hashes[i]) & (cap - 1)].push_back(i);
        if (++size > cap * 3 / 4) {
            std::size_t                           newCap = cap * 2;
            std::vector<std::vector<std::size_t>> next(newCap);
            for (std::size_t j = 0; j < cap; ++j)
                for (auto e : table[j]) next[(spread(hashes[e]) & cap) ? j + cap : j].push_back(e);
            table = std::move(next);
            cap   = newCap;
        }
    }
    std::vector<std::size_t> out;
    out.reserve(hashes.size());
    for (auto const& bucket : table) out.insert(out.end(), bucket.begin(), bucket.end());
    return out;
}

namespace {
std::array<float, 65536> const& sinTable() {
    static std::array<float, 65536> const table = [] {
        std::array<float, 65536> t{};
        for (int i = 0; i < 65536; ++i) t[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(static_cast<double>(i) * std::numbers::pi * 2.0 / 65536.0));
        return t;
    }();
    return table;
}
} // namespace

float mthSin(float value) { return sinTable()[static_cast<std::size_t>(static_cast<int>(value * 10430.378F) & 65535)]; }
float mthCos(float value) { return sinTable()[static_cast<std::size_t>(static_cast<int>(value * 10430.378F + 16384.0F) & 65535)]; }

} // namespace gf::core::jrand
