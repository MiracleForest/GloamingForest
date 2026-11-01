// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): an Ogg Vorbis I encoder for the synthesized sounds.
//
// Written from the Vorbis I specification (Xiph.Org Foundation): the floor-1 curve synthesis,
// codeword assignment and the inverse dB table follow the specification text so that the
// encoder predicts exactly what every decoder reconstructs.
#include "core/synth/Vorbis.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <queue>
#include <stdexcept>

#include "core/synth/Mdct.h"
#include "core/synth/SynthMath.h"

namespace gf::core::synth {

namespace {

// ---- constants ------------------------------------------------------------------------------------

constexpr int kBlockExpShort = 8;  // 256 (declared, never used: every block is long)
constexpr int kBlockExpLong  = 11; // 2048
constexpr int kN             = 1 << kBlockExpLong;
constexpr int kHalf          = kN / 2;
constexpr int kMultiplier    = 2;
constexpr int kRange         = 128; // floor-1 Y range for multiplier 2
constexpr int kYBits         = 7;   // ilog(kRange - 1)
constexpr int kRangeBits     = 10;  // X range 0..1024
constexpr int kPartitionSize = 16;
constexpr int kPartitions    = kHalf / kPartitionSize;
constexpr int kClassDim      = 3; // floor-1 partition dimension

// interior floor-1 posts (sorted); 10 partitions of 3
constexpr std::array<int, 30> kPostsSorted = {1,  2,  3,  4,   6,   8,   10,  13,  16,  20,  25,  31,  38,  46,  56,
                                              68, 82, 99, 119, 143, 172, 207, 249, 299, 359, 431, 518, 622, 747, 896};
constexpr int                 kPosts       = int(kPostsSorted.size()) + 2;

// floor1_inverse_dB_table (Vorbis I specification, section 10.1)
constexpr float kInverseDb[256] = {1.0649863e-07f, 1.1341951e-07f, 1.2079015e-07f, 1.2863978e-07f,
                                   1.3699951e-07f, 1.4590251e-07f, 1.5538408e-07f, 1.6548181e-07f,
                                   1.7623575e-07f, 1.8768855e-07f, 1.9988561e-07f, 2.1287530e-07f,
                                   2.2670913e-07f, 2.4144197e-07f, 2.5713223e-07f, 2.7384213e-07f,
                                   2.9163793e-07f, 3.1059021e-07f, 3.3077411e-07f, 3.5226968e-07f,
                                   3.7516214e-07f, 3.9954229e-07f, 4.2550680e-07f, 4.5315863e-07f,
                                   4.8260743e-07f, 5.1396998e-07f, 5.4737065e-07f, 5.8294187e-07f,
                                   6.2082472e-07f, 6.6116941e-07f, 7.0413592e-07f, 7.4989464e-07f,
                                   7.9862701e-07f, 8.5052630e-07f, 9.0579828e-07f, 9.6466216e-07f,
                                   1.0273513e-06f, 1.0941144e-06f, 1.1652161e-06f, 1.2409384e-06f,
                                   1.3215816e-06f, 1.4074654e-06f, 1.4989305e-06f, 1.5963394e-06f,
                                   1.7000785e-06f, 1.8105592e-06f, 1.9282195e-06f, 2.0535261e-06f,
                                   2.1869758e-06f, 2.3290978e-06f, 2.4804557e-06f, 2.6416497e-06f,
                                   2.8133190e-06f, 2.9961443e-06f, 3.1908506e-06f, 3.3982101e-06f,
                                   3.6190449e-06f, 3.8542308e-06f, 4.1047004e-06f, 4.3714470e-06f,
                                   4.6555282e-06f, 4.9580707e-06f, 5.2802740e-06f, 5.6234160e-06f,
                                   5.9888572e-06f, 6.3780469e-06f, 6.7925283e-06f, 7.2339451e-06f,
                                   7.7040476e-06f, 8.2047000e-06f, 8.7378876e-06f, 9.3057248e-06f,
                                   9.9104632e-06f, 1.0554501e-05f, 1.1240392e-05f, 1.1970856e-05f,
                                   1.2748789e-05f, 1.3577278e-05f, 1.4459606e-05f, 1.5399272e-05f,
                                   1.6400004e-05f, 1.7465768e-05f, 1.8600792e-05f, 1.9809576e-05f,
                                   2.1096914e-05f, 2.2467911e-05f, 2.3928002e-05f, 2.5482978e-05f,
                                   2.7139006e-05f, 2.8902651e-05f, 3.0780908e-05f, 3.2781225e-05f,
                                   3.4911534e-05f, 3.7180282e-05f, 3.9596466e-05f, 4.2169667e-05f,
                                   4.4910090e-05f, 4.7828601e-05f, 5.0936773e-05f, 5.4246931e-05f,
                                   5.7772202e-05f, 6.1526565e-05f, 6.5524908e-05f, 6.9783085e-05f,
                                   7.4317983e-05f, 7.9147585e-05f, 8.4291040e-05f, 8.9768747e-05f,
                                   9.5602426e-05f, 0.00010181521f, 0.00010843174f, 0.00011547824f,
                                   0.00012298267f, 0.00013097477f, 0.00013948625f, 0.00014855085f,
                                   0.00015820453f, 0.00016848555f, 0.00017943469f, 0.00019109536f,
                                   0.00020351382f, 0.00021673929f, 0.00023082423f, 0.00024582449f,
                                   0.00026179955f, 0.00027881276f, 0.00029693158f, 0.00031622787f,
                                   0.00033677814f, 0.00035866388f, 0.00038197188f, 0.00040679456f,
                                   0.00043323036f, 0.00046138411f, 0.00049136745f, 0.00052329927f,
                                   0.00055730621f, 0.00059352311f, 0.00063209358f, 0.00067317058f,
                                   0.00071691700f, 0.00076350630f, 0.00081312324f, 0.00086596457f,
                                   0.00092223983f, 0.00098217216f, 0.0010459992f,  0.0011139742f,
                                   0.0011863665f,  0.0012634633f,  0.0013455702f,  0.0014330129f,
                                   0.0015261382f,  0.0016253153f,  0.0017309374f,  0.0018434235f,
                                   0.0019632195f,  0.0020908006f,  0.0022266726f,  0.0023713743f,
                                   0.0025254795f,  0.0026895994f,  0.0028643847f,  0.0030505286f,
                                   0.0032487691f,  0.0034598925f,  0.0036847358f,  0.0039241906f,
                                   0.0041792066f,  0.0044507950f,  0.0047400328f,  0.0050480668f,
                                   0.0053761186f,  0.0057254891f,  0.0060975636f,  0.0064938176f,
                                   0.0069158225f,  0.0073652516f,  0.0078438871f,  0.0083536271f,
                                   0.0088964928f,  0.009474637f,   0.010090352f,   0.010746080f,
                                   0.011444421f,   0.012188144f,   0.012980198f,   0.013823725f,
                                   0.014722068f,   0.015678791f,   0.016697687f,   0.017782797f,
                                   0.018938423f,   0.020169149f,   0.021479854f,   0.022875735f,
                                   0.024362330f,   0.025945531f,   0.027631618f,   0.029427276f,
                                   0.031339626f,   0.033376252f,   0.035545228f,   0.037855157f,
                                   0.040315199f,   0.042935108f,   0.045725273f,   0.048696758f,
                                   0.051861348f,   0.055231591f,   0.058820850f,   0.062643361f,
                                   0.066714279f,   0.071049749f,   0.075666962f,   0.080584227f,
                                   0.085821044f,   0.091398179f,   0.097337747f,   0.10366330f,
                                   0.11039993f,    0.11757434f,    0.12521498f,    0.13335215f,
                                   0.14201813f,    0.15124727f,    0.16107617f,    0.17154380f,
                                   0.18269168f,    0.19456402f,    0.20720788f,    0.22067342f,
                                   0.23501402f,    0.25028656f,    0.26655159f,    0.28387361f,
                                   0.30232132f,    0.32196786f,    0.34289114f,    0.36517414f,
                                   0.38890521f,    0.41417847f,    0.44109412f,    0.46975890f,
                                   0.50028648f,    0.53279791f,    0.56742212f,    0.60429640f,
                                   0.64356699f,    0.68538959f,    0.72993007f,    0.77736504f,
                                   0.82788260f,    0.88168307f,    0.9389798f,     1.0f};

// ---- bit packing (LSB first, Vorbis I section 2) -----------------------------------------------------

class BitWriter {
public:
    void write(std::uint64_t value, int bits) {
        while (bits > 0) {
            if (mBit == 0) mBytes.push_back(0);
            int take        = std::min(bits, 8 - mBit);
            mBytes.back()  |= std::uint8_t((value & ((1ull << take) - 1)) << mBit);
            value         >>= take;
            bits           -= take;
            mBit            = (mBit + take) & 7;
        }
    }
    void bytes(char const* s, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) write(std::uint8_t(s[i]), 8);
    }
    std::vector<std::uint8_t> take() {
        mBit = 0;
        return std::move(mBytes);
    }

private:
    std::vector<std::uint8_t> mBytes;
    int                       mBit = 0;
};

// ---- codebooks --------------------------------------------------------------------------------------

std::uint32_t packFloat(int v) {
    std::uint32_t sign = v < 0 ? 0x80000000u : 0u;
    std::uint32_t mant = std::uint32_t(v < 0 ? -v : v);
    return sign | (788u << 21) | (mant & 0x1FFFFF);
}

int ilog(std::uint32_t v) {
    int r = 0;
    while (v) {
        ++r;
        v >>= 1;
    }
    return r;
}

int lookup1Values(int entries, int dim) {
    int r = 1;
    while (true) {
        long long p = 1;
        for (int d = 0; d < dim; ++d) p *= (r + 1);
        if (p > entries) return r;
        ++r;
    }
}

/// Huffman code lengths for `counts` (0 = unused entry); max length `maxLen`.
std::vector<int> huffmanLengths(std::vector<std::uint64_t> counts, int maxLen) {
    int const        n = int(counts.size());
    std::vector<int> lengths(n, 0);
    while (true) {
        struct Node {
            std::uint64_t weight;
            int           id;
        };
        auto cmp = [](Node const& a, Node const& b) {
            return a.weight != b.weight ? a.weight > b.weight : a.id > b.id;
        };
        std::priority_queue<Node, std::vector<Node>, decltype(cmp)> heap(cmp);
        std::vector<int>                                            parent;
        int                                                         used = 0;
        for (int i = 0; i < n; ++i) {
            parent.push_back(-1);
            if (counts[i] > 0) {
                heap.push({counts[i], i});
                ++used;
            }
        }
        if (used == 0) return lengths;
        if (used == 1) {
            for (int i = 0; i < n; ++i) lengths[i] = counts[i] > 0 ? 1 : 0;
            return lengths;
        }
        while (heap.size() > 1) {
            Node a = heap.top();
            heap.pop();
            Node b = heap.top();
            heap.pop();
            int id = int(parent.size());
            parent.push_back(-1);
            parent[a.id] = id;
            parent[b.id] = id;
            heap.push({a.weight + b.weight, id});
        }
        int maxSeen = 0;
        for (int i = 0; i < n; ++i) {
            if (counts[i] == 0) {
                lengths[i] = 0;
                continue;
            }
            int d = 0;
            for (int p = parent[i]; p != -1; p = parent[p]) ++d;
            lengths[i] = d;
            maxSeen    = std::max(maxSeen, d);
        }
        if (maxSeen <= maxLen) return lengths;
        for (auto& c : counts) {
            if (c > 0) c = c / 2 + 1; // flatten and retry
        }
    }
}

/// Codeword assignment of the specification (section 3.2.1): entries in order, each taking the
/// lowest free codeword of its length. Returns false for an over-populated length set.
bool assignCodewords(std::vector<int> const& lengths, std::vector<std::uint32_t>& codes) {
    std::uint32_t marker[33] = {};
    codes.assign(lengths.size(), 0);
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        int length = lengths[i];
        if (length <= 0) continue;
        std::uint32_t entry = marker[length];
        if (length < 32 && (entry >> length)) return false;
        codes[i] = entry;
        for (int j = length; j > 0; --j) {
            if (marker[j] & 1) {
                if (j == 1) marker[1]++;
                else marker[j] = marker[j - 1] << 1;
                break;
            }
            marker[j]++;
        }
        for (int j = length + 1; j < 33; ++j) {
            if ((marker[j] >> 1) == entry) {
                entry     = marker[j];
                marker[j] = marker[j - 1] << 1;
            } else {
                break;
            }
        }
    }
    return true;
}

struct Codebook {
    int                        dim     = 1;
    int                        entries = 0;
    int                        lookup  = 0; // 0 scalar, 1 lattice
    int                        minimum = 0;
    int                        delta   = 1;
    int                        values  = 0; // lattice values per dimension
    std::vector<std::uint64_t> counts;
    std::vector<int>           lengths;
    std::vector<std::uint32_t> reversed; // codewords bit-reversed for LSB-first writing

    static Codebook scalar(int entries) {
        Codebook b;
        b.entries = entries;
        b.counts.assign(entries, 0);
        return b;
    }
    static Codebook lattice(int dim, int lo, int hi, int step = 1) {
        Codebook b;
        b.dim     = dim;
        b.lookup  = 1;
        b.minimum = lo;
        b.delta   = step;
        b.values  = (hi - lo) / step + 1;
        b.entries = 1;
        for (int d = 0; d < dim; ++d) b.entries *= b.values;
        b.counts.assign(b.entries, 0);
        return b;
    }

    /// The entry of a lattice vector (values already in range and on the grid).
    [[nodiscard]] int entryOf(int const* v) const {
        int e   = 0;
        int mul = 1;
        for (int d = 0; d < dim; ++d) {
            e   += ((v[d] - minimum) / delta) * mul;
            mul *= values;
        }
        return e;
    }

    void build() {
        // make sure at least two entries are in use (a single-entry tree is under-populated)
        int used = 0;
        for (auto c : counts) used += c > 0;
        for (int i = 0; used < 2 && i < entries; ++i) {
            if (counts[i] == 0) {
                counts[i] = 1;
                ++used;
            }
        }
        lengths = huffmanLengths(counts, 24);
        std::vector<std::uint32_t> codes;
        if (!assignCodewords(lengths, codes)) throw std::logic_error("vorbis: bad Huffman lengths");
        reversed.assign(entries, 0);
        for (int i = 0; i < entries; ++i) {
            std::uint32_t c = codes[i];
            std::uint32_t r = 0;
            for (int b = 0; b < lengths[i]; ++b) {
                r   = (r << 1) | (c & 1);
                c >>= 1;
            }
            reversed[i] = r;
        }
    }

    void writeHeader(BitWriter& w) const {
        w.write(0x564342, 24);
        w.write(std::uint32_t(dim), 16);
        w.write(std::uint32_t(entries), 24);
        w.write(0, 1); // not ordered
        // a dense list needs a length for every entry; unused entries make the book sparse
        bool sparse = std::any_of(lengths.begin(), lengths.end(), [](int l) { return l == 0; });
        w.write(sparse ? 1 : 0, 1);
        for (int l : lengths) {
            if (sparse) {
                w.write(l > 0 ? 1 : 0, 1);
                if (l > 0) w.write(std::uint32_t(l - 1), 5);
            } else {
                w.write(std::uint32_t(l - 1), 5);
            }
        }
        w.write(std::uint32_t(lookup), 4);
        if (lookup == 1) {
            w.write(packFloat(minimum), 32);
            w.write(packFloat(delta), 32);
            int bits = ilog(std::uint32_t(values - 1));
            if (bits < 1) bits = 1;
            w.write(std::uint32_t(bits - 1), 4);
            w.write(0, 1); // sequence_p
            int n = lookup1Values(entries, dim);
            for (int i = 0; i < n; ++i) w.write(std::uint32_t(i), bits);
        }
    }

    void encode(BitWriter& w, int entry) const { w.write(reversed[entry], lengths[entry]); }
};

// ---- floor 1 ----------------------------------------------------------------------------------------

struct FloorLayout {
    std::array<int, kPosts> x{};       // coded order: 0, 1024, then coarse to fine
    std::array<int, kPosts> low{};     // low_neighbor offsets
    std::array<int, kPosts> high{};    // high_neighbor offsets
    std::array<int, kPosts> sorted{};  // post indices by X
    std::array<int, kPosts> lowEdge{}; // envelope interval per post (bins)
    std::array<int, kPosts> highEdge{};

    FloorLayout() {
        x[0] = 0;
        x[1] = kHalf;
        // coarse-to-fine order: breadth-first bisection of the sorted interior posts
        std::vector<std::pair<int, int>> queue{
            {0, int(kPostsSorted.size()) - 1}
        };
        int k = 2;
        for (std::size_t qi = 0; qi < queue.size(); ++qi) {
            auto [lo, hi] = queue[qi];
            if (lo > hi) continue;
            int mid = (lo + hi) / 2;
            x[k++]  = kPostsSorted[mid];
            queue.emplace_back(lo, mid - 1);
            queue.emplace_back(mid + 1, hi);
        }
        for (int i = 2; i < kPosts; ++i) {
            int lo = -1, hi = -1;
            for (int j = 0; j < i; ++j) {
                if (x[j] < x[i] && (lo < 0 || x[j] > x[lo])) lo = j;
                if (x[j] > x[i] && (hi < 0 || x[j] < x[hi])) hi = j;
            }
            low[i]  = lo;
            high[i] = hi;
        }
        for (int i = 0; i < kPosts; ++i) sorted[i] = i;
        std::sort(sorted.begin(), sorted.end(), [this](int a, int b) { return x[a] < x[b]; });
        for (int s = 0; s < kPosts; ++s) {
            int i       = sorted[s];
            lowEdge[i]  = s > 0 ? x[sorted[s - 1]] : 0;
            highEdge[i] = s + 1 < kPosts ? x[sorted[s + 1]] : kHalf;
            if (highEdge[i] > kHalf - 1) highEdge[i] = kHalf - 1;
        }
    }
};

FloorLayout const& layout() {
    static FloorLayout const l;
    return l;
}

int renderPoint(int x0, int y0, int x1, int y1, int x) {
    int dy  = y1 - y0;
    int adx = x1 - x0;
    int ady = dy < 0 ? -dy : dy;
    int err = ady * (x - x0);
    int off = err / adx;
    return dy < 0 ? y0 - off : y0 + off;
}

void renderLine(int x0, int y0, int x1, int y1, int* v, int n) {
    int dy    = y1 - y0;
    int adx   = x1 - x0;
    int ady   = dy < 0 ? -dy : dy;
    int base  = dy / adx;
    int y     = y0;
    int err   = 0;
    int sy    = dy < 0 ? base - 1 : base + 1;
    ady      -= (base < 0 ? -base : base) * adx;
    if (x0 < n) v[x0] = y;
    for (int x = x0 + 1; x < x1; ++x) {
        err += ady;
        if (err >= adx) {
            err -= adx;
            y   += sy;
        } else {
            y += base;
        }
        if (x < n) v[x] = y;
    }
}

/// The decoder's floor-1 curve for coded values `vals` (vals[0], vals[1] are Y0, Y1).
void floorCurve(std::array<int, kPosts> const& vals, float* curve) {
    auto const&              L = layout();
    std::array<int, kPosts>  fin{};
    std::array<bool, kPosts> step2{};
    fin[0]   = vals[0];
    fin[1]   = vals[1];
    step2[0] = step2[1] = true;
    for (int i = 2; i < kPosts; ++i) {
        int lo        = L.low[i];
        int hi        = L.high[i];
        int predicted = renderPoint(L.x[lo], fin[lo], L.x[hi], fin[hi], L.x[i]);
        int val       = vals[i];
        int highroom  = kRange - predicted;
        int lowroom   = predicted;
        int room      = highroom < lowroom ? highroom * 2 : lowroom * 2;
        if (val != 0) {
            step2[lo] = step2[hi] = step2[i] = true;
            if (val >= room) {
                fin[i] = highroom > lowroom ? val - lowroom + predicted : predicted - val + highroom - 1;
            } else {
                fin[i] = (val & 1) ? predicted - (val + 1) / 2 : predicted + val / 2;
            }
        } else {
            step2[i] = false;
            fin[i]   = predicted;
        }
    }
    std::array<int, kHalf> v{};
    int                    lx = 0;
    int                    ly = fin[L.sorted[0]] * kMultiplier;
    int                    hx = 0;
    int                    hy = 0;
    for (int s = 1; s < kPosts; ++s) {
        int i = L.sorted[s];
        if (!step2[i]) continue;
        hy = fin[i] * kMultiplier;
        hx = L.x[i];
        renderLine(lx, ly, hx, hy, v.data(), kHalf);
        lx = hx;
        ly = hy;
    }
    if (hx < kHalf) renderLine(hx, hy, kHalf, hy, v.data(), kHalf);
    for (int k = 0; k < kHalf; ++k) curve[k] = kInverseDb[v[k] & 255];
}

/// The value that makes the decoder reconstruct `want` from `predicted` (and the result).
int floorVal(int want, int predicted, int& result) {
    int highroom = kRange - predicted;
    int lowroom  = predicted;
    int room     = highroom < lowroom ? highroom * 2 : lowroom * 2;
    if (want == predicted) {
        result = predicted;
        return 0;
    }
    int val;
    if (want > predicted) {
        int d = want - predicted;
        val   = 2 * d < room ? 2 * d : d + lowroom;
    } else {
        int d = predicted - want;
        val   = 2 * d - 1 < room ? 2 * d - 1 : d + highroom - 1;
    }
    if (val >= room) {
        result = highroom > lowroom ? val - lowroom + predicted : predicted - val + highroom - 1;
    } else {
        result = (val & 1) ? predicted - (val + 1) / 2 : predicted + val / 2;
    }
    return val;
}

int quantizeY(double amp) {
    for (int y = 0; y < kRange; ++y) {
        if (double(kInverseDb[y * kMultiplier]) >= amp) return y;
    }
    return kRange - 1;
}

// ---- residue classes --------------------------------------------------------------------------------

constexpr int kClasses      = 7;
constexpr int kClassLimit[] = {0, 1, 2, 4, 8, 24, 1 << 30};
constexpr int kEscapeStep   = 49;
constexpr int kEscapeMax    = 7;
constexpr int kResidueMax   = kEscapeStep * kEscapeMax + 24;

enum Book { kFloorBook, kClassBook, kR1, kR2, kR4, kR8, kR24, kEsc, kBookCount };

// books per class and pass (-1 = none)
constexpr int kClassBooks[kClasses][2] = {
    {-1,   -1  },
    {kR1,  -1  },
    {kR2,  -1  },
    {kR4,  -1  },
    {kR8,  -1  },
    {kR24, -1  },
    {kEsc, kR24},
};

struct Block {
    bool                                  nonzero = false;
    std::array<int, kPosts>               vals{};
    std::array<std::int16_t, kHalf>       q{};
    std::array<std::uint8_t, kPartitions> cls{};
};

/// Entries written for one partition and pass: calls fn(book, entry).
template <class F>
void partitionEntries(Block const& b, int part, int pass, F&& fn, std::vector<Codebook> const& books) {
    int c    = b.cls[part];
    int book = kClassBooks[c][pass];
    if (book < 0) return;
    Codebook const& cb = books[book];
    int const*      q0 = nullptr;
    int             vals[kPartitionSize];
    for (int i = 0; i < kPartitionSize; ++i) vals[i] = b.q[part * kPartitionSize + i];
    if (c == kClasses - 1) {
        // escape class: pass 0 codes multiples of kEscapeStep, pass 1 the remainder
        for (int i = 0; i < kPartitionSize; ++i) {
            int a = int(math::round(double(vals[i]) / kEscapeStep));
            a     = std::clamp(a, -kEscapeMax, kEscapeMax);
            if (pass == 0) vals[i] = a * kEscapeStep;
            else vals[i] = std::clamp(vals[i] - a * kEscapeStep, -24, 24);
        }
    }
    q0 = vals;
    for (int i = 0; i < kPartitionSize; i += cb.dim) fn(book, cb.entryOf(q0 + i));
}

std::vector<Codebook> makeBooks() {
    std::vector<Codebook> books(kBookCount);
    books[kFloorBook] = Codebook::scalar(kRange);
    books[kClassBook] = Codebook::scalar(kClasses);
    books[kR1]        = Codebook::lattice(4, -1, 1);
    books[kR2]        = Codebook::lattice(2, -2, 2);
    books[kR4]        = Codebook::lattice(2, -4, 4);
    books[kR8]        = Codebook::lattice(2, -8, 8);
    books[kR24]       = Codebook::lattice(1, -24, 24);
    books[kEsc]       = Codebook::lattice(1, -kEscapeStep * kEscapeMax, kEscapeStep * kEscapeMax, kEscapeStep);
    return books;
}

// ---- analysis -----------------------------------------------------------------------------------------

// The floor is the quantisation step: the local RMS of the spectrum scaled so that the uniform
// quantisation noise (step^2 / 12) sits a signal-to-mask ratio below the local energy. The ratio
// grows with the quality and shrinks above 5 kHz; content below an absolute threshold (about the
// 16-bit noise floor) or far below the frame's peak is dropped.
struct Analyzer {
    double                     absThreshold;
    double                     relThreshold;
    std::array<double, kPosts> stepScale{};

    Analyzer(double quality, int sampleRate) {
        quality       = math::clamp(quality, 0.0, 1.0);
        double smr    = 12.0 + 20.0 * quality;
        absThreshold  = math::dbToGain(-(84.0 + 16.0 * quality));
        relThreshold  = math::dbToGain(-(50.0 + 30.0 * quality));
        auto const& L = layout();
        for (int i = 0; i < kPosts; ++i) {
            double hz      = double(L.x[i]) * double(sampleRate) / double(kN);
            double penalty = hz > 5000.0 ? std::min(10.0, 10.0 * math::log(hz / 5000.0) / math::log(3.2)) : 0.0;
            stepScale[i]   = math::sqrt(12.0) * math::dbToGain(-(smr - penalty));
        }
    }

    void analyze(double const* coef, Block& b) const {
        auto const& L        = layout();
        double      frameMax = 0;
        for (int k = 0; k < kHalf; ++k) frameMax = std::max(frameMax, math::abs(coef[k]));
        double thr = std::max(absThreshold, frameMax * relThreshold);
        b.nonzero  = false;
        b.q.fill(0);
        b.cls.fill(0);
        if (frameMax < thr) return;
        std::array<int, kPosts> want{};
        for (int i = 0; i < kPosts; ++i) {
            double peak = 0, energy = 0;
            for (int k = L.lowEdge[i]; k <= L.highEdge[i]; ++k) {
                peak    = std::max(peak, math::abs(coef[k]));
                energy += coef[k] * coef[k];
            }
            double rms  = math::sqrt(energy / double(L.highEdge[i] - L.lowEdge[i] + 1));
            double step = peak < thr ? thr * 2.0 : std::max(rms * stepScale[i], thr);
            want[i]     = quantizeY(step);
        }
        // values: Y0, Y1 raw, the others relative to their prediction (within 1 step -> 0)
        std::array<int, kPosts> fin{};
        b.vals[0] = fin[0] = want[0];
        b.vals[1] = fin[1] = want[1];
        for (int i = 2; i < kPosts; ++i) {
            int lo        = L.low[i];
            int hi        = L.high[i];
            int predicted = renderPoint(L.x[lo], fin[lo], L.x[hi], fin[hi], L.x[i]);
            int w         = want[i];
            if (w - predicted <= 1 && predicted - w <= 1) w = predicted;
            int result = 0;
            b.vals[i]  = floorVal(w, predicted, result);
            fin[i]     = result;
        }
        std::array<float, kHalf> curve{};
        floorCurve(b.vals, curve.data());
        bool any = false;
        for (int k = 0; k < kHalf; ++k) {
            double r  = coef[k] / double(curve[k]);
            int    q  = int(math::round(math::clamp(r, -double(kResidueMax), double(kResidueMax))));
            b.q[k]    = std::int16_t(q);
            any      |= q != 0;
        }
        if (!any) return; // all residue zero: an unused floor is cheaper
        b.nonzero = true;
        for (int p = 0; p < kPartitions; ++p) {
            int m = 0;
            for (int i = 0; i < kPartitionSize; ++i) {
                int a = b.q[p * kPartitionSize + i];
                m     = std::max(m, a < 0 ? -a : a);
            }
            int c = 0;
            while (m > kClassLimit[c]) ++c;
            b.cls[p] = std::uint8_t(c);
        }
    }
};

// ---- ogg ------------------------------------------------------------------------------------------------

std::array<std::uint32_t, 256> const& crcTable() {
    static std::array<std::uint32_t, 256> const table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k) r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : (r << 1);
            t[i] = r;
        }
        return t;
    }();
    return table;
}

class OggWriter {
public:
    explicit OggWriter(std::uint32_t serial) : mSerial(serial) {}

    void packet(std::vector<std::uint8_t> const& data, std::int64_t granule) {
        std::size_t pos = 0;
        while (true) {
            if (mLacing.size() == 255) flush(false);
            std::size_t chunk = std::min<std::size_t>(255, data.size() - pos);
            mLacing.push_back(std::uint8_t(chunk));
            mBody.insert(mBody.end(), data.begin() + std::ptrdiff_t(pos), data.begin() + std::ptrdiff_t(pos + chunk));
            pos += chunk;
            if (chunk < 255) {
                mGranule = granule;
                break;
            }
        }
    }
    /// Starts a new page at the next packet when the current one is large enough.
    void maybeFlush() {
        if (mBody.size() >= 4096) flush(false);
    }
    void flush(bool eos) {
        if (mLacing.empty() && !eos) return;
        std::vector<std::uint8_t> page;
        page.reserve(27 + mLacing.size() + mBody.size());
        page.insert(page.end(), {'O', 'g', 'g', 'S', 0});
        std::uint8_t flags = 0;
        if (mContinued) flags |= 0x01;
        if (mSequence == 0) flags |= 0x02;
        if (eos) flags |= 0x04;
        page.push_back(flags);
        auto put = [&page](std::uint64_t v, int n) {
            for (int i = 0; i < n; ++i) page.push_back(std::uint8_t(v >> (8 * i)));
        };
        put(std::uint64_t(mGranule), 8);
        put(mSerial, 4);
        put(mSequence, 4);
        put(0, 4); // CRC placeholder
        page.push_back(std::uint8_t(mLacing.size()));
        page.insert(page.end(), mLacing.begin(), mLacing.end());
        page.insert(page.end(), mBody.begin(), mBody.end());
        std::uint32_t crc = oggCrc(page.data(), page.size());
        for (int i = 0; i < 4; ++i) page[22 + i] = std::uint8_t(crc >> (8 * i));
        mOut.insert(mOut.end(), page.begin(), page.end());
        mContinued = !mLacing.empty() && mLacing.back() == 255;
        ++mSequence;
        mLacing.clear();
        mBody.clear();
        mGranule = -1;
    }
    std::vector<std::uint8_t> take() { return std::move(mOut); }

private:
    std::uint32_t             mSerial;
    std::uint32_t             mSequence  = 0;
    bool                      mContinued = false;
    std::int64_t              mGranule   = -1;
    std::vector<std::uint8_t> mLacing;
    std::vector<std::uint8_t> mBody;
    std::vector<std::uint8_t> mOut;
};

// ---- headers ------------------------------------------------------------------------------------------

std::vector<std::uint8_t> identificationHeader(int sampleRate) {
    BitWriter w;
    w.write(1, 8);
    w.bytes("vorbis", 6);
    w.write(0, 32); // version
    w.write(1, 8);  // channels
    w.write(std::uint32_t(sampleRate), 32);
    w.write(0, 32); // bitrate maximum (unset)
    w.write(0, 32); // nominal
    w.write(0, 32); // minimum
    w.write(kBlockExpShort, 4);
    w.write(kBlockExpLong, 4);
    w.write(1, 1); // framing
    return w.take();
}

std::vector<std::uint8_t> commentHeader(VorbisOptions const& o) {
    BitWriter w;
    w.write(3, 8);
    w.bytes("vorbis", 6);
    w.write(std::uint32_t(o.vendor.size()), 32);
    w.bytes(o.vendor.data(), o.vendor.size());
    w.write(std::uint32_t(o.comments.size()), 32);
    for (auto const& c : o.comments) {
        w.write(std::uint32_t(c.size()), 32);
        w.bytes(c.data(), c.size());
    }
    w.write(1, 1);
    return w.take();
}

std::vector<std::uint8_t> setupHeader(std::vector<Codebook> const& books) {
    BitWriter w;
    w.write(5, 8);
    w.bytes("vorbis", 6);
    // codebooks
    w.write(std::uint32_t(books.size() - 1), 8);
    for (auto const& b : books) b.writeHeader(w);
    // time domain transforms: one placeholder
    w.write(0, 6);
    w.write(0, 16);
    // floors: one floor 1
    w.write(0, 6);
    w.write(1, 16);
    int const partitions = (kPosts - 2) / kClassDim;
    w.write(std::uint32_t(partitions), 5);
    for (int i = 0; i < partitions; ++i) w.write(0, 4); // every partition uses class 0
    w.write(kClassDim - 1, 3);                          // class 0: dimensions
    w.write(0, 2);                                      // no subclasses
    w.write(kFloorBook + 1, 8);                         // subclass book 0
    w.write(kMultiplier - 1, 2);
    w.write(kRangeBits, 4);
    auto const& L = layout();
    for (int i = 2; i < kPosts; ++i) w.write(std::uint32_t(L.x[i]), kRangeBits);
    // residues: one residue 1
    w.write(0, 6);
    w.write(1, 16);
    w.write(0, 24);     // begin
    w.write(kHalf, 24); // end
    w.write(kPartitionSize - 1, 24);
    w.write(kClasses - 1, 6);
    w.write(kClassBook, 8);
    for (int c = 0; c < kClasses; ++c) {
        int cascade = 0;
        for (int p = 0; p < 2; ++p) {
            if (kClassBooks[c][p] >= 0) cascade |= 1 << p;
        }
        w.write(std::uint32_t(cascade & 7), 3);
        w.write(0, 1); // no high bits
    }
    for (int c = 0; c < kClasses; ++c) {
        for (int p = 0; p < 2; ++p) {
            if (kClassBooks[c][p] >= 0) w.write(std::uint32_t(kClassBooks[c][p]), 8);
        }
    }
    // mappings: one mapping 0, one submap, no coupling
    w.write(0, 6);
    w.write(0, 16);
    w.write(0, 1); // submaps flag
    w.write(0, 1); // coupling flag
    w.write(0, 2); // reserved
    w.write(0, 8); // time placeholder
    w.write(0, 8); // floor 0
    w.write(0, 8); // residue 0
    // modes: one long-block mode
    w.write(0, 6);
    w.write(1, 1);  // blockflag
    w.write(0, 16); // window type
    w.write(0, 16); // transform type
    w.write(0, 8);  // mapping 0
    w.write(1, 1);  // framing
    return w.take();
}

std::vector<std::uint8_t> audioPacket(Block const& b, std::vector<Codebook> const& books) {
    BitWriter w;
    w.write(0, 1); // audio packet
    // one mode: no mode bits
    w.write(1, 1); // previous window long
    w.write(1, 1); // next window long
    w.write(b.nonzero ? 1 : 0, 1);
    if (!b.nonzero) return w.take();
    w.write(std::uint32_t(b.vals[0]), kYBits);
    w.write(std::uint32_t(b.vals[1]), kYBits);
    for (int i = 2; i < kPosts; ++i) books[kFloorBook].encode(w, b.vals[i]);
    for (int pass = 0; pass < 2; ++pass) {
        for (int p = 0; p < kPartitions; ++p) {
            if (pass == 0) books[kClassBook].encode(w, b.cls[p]);
            partitionEntries(b, p, pass, [&](int book, int entry) { books[book].encode(w, entry); }, books);
        }
    }
    return w.take();
}

} // namespace

std::uint32_t oggCrc(std::uint8_t const* data, std::size_t size) {
    auto const&   t   = crcTable();
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < size; ++i) crc = (crc << 8) ^ t[((crc >> 24) & 0xFF) ^ data[i]];
    return crc;
}

std::vector<std::uint8_t> encodeVorbis(std::span<float const> pcm, VorbisOptions const& options) {
    if (options.sampleRate < 8000 || options.sampleRate > 192000)
        throw std::invalid_argument("vorbis: bad sample rate");
    std::int64_t const length = std::max<std::int64_t>(1, std::int64_t(pcm.size()));
    int const          blocks = int((length + kHalf - 1) / kHalf) + 1;

    std::array<double, kN> window{};
    for (int i = 0; i < kN; ++i) {
        double s  = math::sin((double(i) + 0.5) / double(kN) * math::kPi);
        window[i] = math::sin(math::kPi / 2.0 * s * s);
    }
    Mdct const     mdct(kN);
    Analyzer const analyzer(options.quality, options.sampleRate);

    std::vector<Block>        coded(static_cast<std::size_t>(blocks));
    std::array<double, kN>    buf{};
    std::array<double, kHalf> coef{};
    // libvorbis' forward scale: with the decoders' unscaled inverse MDCT and the power-complementary
    // window this reconstructs the input amplitude (checked against ffmpeg and stb_vorbis)
    double const scale = 4.0 / double(kN);
    for (int b = 0; b < blocks; ++b) {
        std::int64_t start = std::int64_t(b - 1) * kHalf;
        for (int i = 0; i < kN; ++i) {
            std::int64_t s = start + i;
            double       x = (s >= 0 && s < std::int64_t(pcm.size())) ? double(pcm[std::size_t(s)]) : 0.0;
            buf[i]         = x * window[i];
        }
        mdct.forward(buf.data(), coef.data());
        for (auto& c : coef) c *= scale;
        analyzer.analyze(coef.data(), coded[std::size_t(b)]);
    }

    // statistics -> per-file Huffman codebooks
    auto books = makeBooks();
    for (auto const& b : coded) {
        if (!b.nonzero) continue;
        for (int i = 2; i < kPosts; ++i) books[kFloorBook].counts[std::size_t(b.vals[i])]++;
        for (int pass = 0; pass < 2; ++pass) {
            for (int p = 0; p < kPartitions; ++p) {
                if (pass == 0) books[kClassBook].counts[b.cls[p]]++;
                partitionEntries(
                    b,
                    p,
                    pass,
                    [&](int book, int entry) { books[book].counts[std::size_t(entry)]++; },
                    books
                );
            }
        }
    }
    for (auto& b : books) b.build();

    OggWriter ogg(options.serial);
    ogg.packet(identificationHeader(options.sampleRate), 0);
    ogg.flush(false);
    ogg.packet(commentHeader(options), 0);
    ogg.packet(setupHeader(books), 0);
    ogg.flush(false);
    for (int b = 0; b < blocks; ++b) {
        std::int64_t granule = b == 0 ? 0 : std::min<std::int64_t>(std::int64_t(b) * kHalf, length);
        ogg.maybeFlush();
        ogg.packet(audioPacket(coded[std::size_t(b)], books), granule);
    }
    ogg.flush(true);
    return ogg.take();
}

OggVorbisInfo probeOggVorbis(std::span<std::uint8_t const> f) {
    OggVorbisInfo info;
    std::size_t   pos   = 0;
    int           pages = 0;
    while (pos < f.size()) {
        if (f.size() - pos < 27 || std::memcmp(f.data() + pos, "OggS", 4) != 0) {
            info.error = "bad page at byte " + std::to_string(pos);
            return info;
        }
        int         nseg = f[pos + 26];
        std::size_t hdr  = 27 + std::size_t(nseg);
        if (f.size() - pos < hdr) {
            info.error = "truncated page";
            return info;
        }
        std::size_t body = 0;
        for (int i = 0; i < nseg; ++i) body += f[pos + 27 + std::size_t(i)];
        if (f.size() - pos < hdr + body) {
            info.error = "truncated page";
            return info;
        }
        std::vector<std::uint8_t> page(f.begin() + std::ptrdiff_t(pos), f.begin() + std::ptrdiff_t(pos + hdr + body));
        std::uint32_t             stored = 0;
        for (int i = 0; i < 4; ++i) stored |= std::uint32_t(page[22 + std::size_t(i)]) << (8 * i);
        std::memset(page.data() + 22, 0, 4);
        if (oggCrc(page.data(), page.size()) != stored) {
            info.error = "CRC mismatch on page " + std::to_string(pages);
            return info;
        }
        if (pages == 0) {
            if (!(page[5] & 0x02) || body < 30 || page[hdr] != 1
                || std::memcmp(page.data() + hdr + 1, "vorbis", 6) != 0) {
                info.error = "first page is not a Vorbis identification header";
                return info;
            }
            info.channels = page[hdr + 11];
            info.sampleRate =
                int(std::uint32_t(page[hdr + 12]) | std::uint32_t(page[hdr + 13]) << 8
                    | std::uint32_t(page[hdr + 14]) << 16 | std::uint32_t(page[hdr + 15]) << 24);
        }
        std::uint64_t granule = 0;
        for (int i = 0; i < 8; ++i) granule |= std::uint64_t(page[6 + std::size_t(i)]) << (8 * i);
        if (granule != ~std::uint64_t(0)) info.samples = std::int64_t(granule);
        ++pages;
        pos += hdr + body;
        if (page[5] & 0x04) break;
    }
    info.valid = pages >= 3 && info.channels > 0 && info.sampleRate > 0;
    if (!info.valid && info.error.empty()) info.error = "too few pages";
    return info;
}

} // namespace gf::core::synth
