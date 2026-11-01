// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): the forward MDCT of the Vorbis encoder.
#include "core/synth/Mdct.h"

#include <stdexcept>
#include <utility>

#include "core/synth/SynthMath.h"

namespace gf::core::synth {

Mdct::Mdct(int n) : mN(n) {
    if (n < 16 || (n & (n - 1)) != 0) throw std::invalid_argument("MDCT size must be a power of two >= 16");
    int const M = n / 2; // coefficients
    int const H = M / 2; // complex FFT points
    mPreCos.resize(H);
    mPreSin.resize(H);
    mPostCos.resize(H);
    mPostSin.resize(H);
    for (int m = 0; m < H; ++m) {
        double a   = -math::kPi * double(4 * m + 1) / double(4 * M);
        mPreCos[m] = math::cos(a);
        mPreSin[m] = math::sin(a);
        double b   = -math::kPi * double(m) / double(M);
        mPostCos[m] = math::cos(b);
        mPostSin[m] = math::sin(b);
    }
    mTwCos.resize(H / 2 > 0 ? H / 2 : 1);
    mTwSin.resize(mTwCos.size());
    for (int k = 0; k < H / 2; ++k) {
        double a  = -math::kTwoPi * double(k) / double(H);
        mTwCos[k] = math::cos(a);
        mTwSin[k] = math::sin(a);
    }
    mBitRev.resize(H);
    int bits = 0;
    while ((1 << bits) < H) ++bits;
    for (int i = 0; i < H; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) {
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        }
        mBitRev[i] = r;
    }
}

void Mdct::fft(std::vector<double>& re, std::vector<double>& im) const {
    int const H = int(re.size());
    for (int i = 0; i < H; ++i) {
        int j = mBitRev[i];
        if (j > i) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= H; len <<= 1) {
        int half = len / 2;
        int step = H / len;
        for (int start = 0; start < H; start += len) {
            for (int k = 0; k < half; ++k) {
                double wr = mTwCos[k * step];
                double wi = mTwSin[k * step];
                int    a  = start + k;
                int    b  = a + half;
                double tr = re[b] * wr - im[b] * wi;
                double ti = re[b] * wi + im[b] * wr;
                re[b]     = re[a] - tr;
                im[b]     = im[a] - ti;
                re[a]     = re[a] + tr;
                im[a]     = im[a] + ti;
            }
        }
    }
}

void Mdct::forward(double const* x, double* out) const {
    int const M  = mN / 2;
    int const H  = M / 2;
    int const h3 = 3 * (M / 2); // 3n/4
    // fold the 4 quarters (a, b, c, d) into v = (-c_r - d, a - b_r), a DCT-IV input of length M
    std::vector<double> v(M);
    for (int i = 0; i < H; ++i) v[i] = -x[h3 - 1 - i] - x[h3 + i];
    for (int i = 0; i < H; ++i) v[H + i] = x[i] - x[M - 1 - i];
    // DCT-IV through an M/2-point complex FFT
    std::vector<double> re(H), im(H);
    for (int m = 0; m < H; ++m) {
        double a = v[2 * m];
        double b = v[M - 1 - 2 * m];
        re[m]    = a * mPreCos[m] - b * mPreSin[m];
        im[m]    = a * mPreSin[m] + b * mPreCos[m];
    }
    fft(re, im);
    for (int k = 0; k < H; ++k) {
        double wr      = re[k] * mPostCos[k] - im[k] * mPostSin[k];
        double wi      = re[k] * mPostSin[k] + im[k] * mPostCos[k];
        out[2 * k]     = wr;
        out[M - 1 - 2 * k] = -wi;
    }
}

void Mdct::forwardDirect(int n, double const* x, double* out) {
    int const M = n / 2;
    for (int k = 0; k < M; ++k) {
        double acc = 0;
        for (int i = 0; i < n; ++i) {
            acc += x[i] * math::cos(math::kTwoPi / double(n) * (double(i) + 0.5 + double(n) / 4.0) * (double(k) + 0.5));
        }
        out[k] = acc;
    }
}

} // namespace gf::core::synth
