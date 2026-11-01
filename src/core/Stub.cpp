#include "core/Stub.h"

#include <cstdio>
#include <mutex>
#include <set>

namespace gf::core {

namespace {
struct StubState {
    std::mutex                                 mutex;
    std::set<std::string, std::less<>>         seen;
    std::vector<std::string>                   order;
    std::function<void(std::string const&)>    sink;
};
StubState& state() {
    static StubState s;
    return s;
}
} // namespace

void notImplemented(std::string_view name) {
    auto&                                   s = state();
    std::function<void(std::string const&)> sink;
    std::string                             line;
    {
        std::lock_guard lock(s.mutex);
        if (s.seen.contains(name)) return;
        s.seen.emplace(name);
        s.order.emplace_back(name);
        sink = s.sink;
        line = std::string(name) + ": not implemented (contract stub)";
    }
    if (sink) sink(line);
    else std::fprintf(stderr, "%s\n", line.c_str());
}

void setStubSink(std::function<void(std::string const& line)> sink) {
    std::lock_guard lock(state().mutex);
    state().sink = std::move(sink);
}

std::vector<std::string> stubCalls() {
    std::lock_guard lock(state().mutex);
    return state().order;
}

} // namespace gf::core
