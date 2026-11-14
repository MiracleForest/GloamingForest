// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): the forward MDCT of the Vorbis encoder.
#pragma once

#include <vector>

namespace gf::core::synth {

/// Forward MDCT of block length n (a power of two >= 16), computed as a DCT-IV of the folded
/// block through an n/4-point complex FFT:
///     X[k] = sum_{i<n} x[i] cos(2 pi / n (i + 1/2 + n/4)(k + 1/2)),   k < n/2
/// (the kernel of the Vorbis inverse MDCT, unscaled).
class Mdct {
public:
    explicit Mdct(int n);

    [[nodiscard]] int size() const { return mN; }
    /// `in` holds n samples, `out` receives n/2 coefficients.
    void forward(double const* in, double* out) const;
    /// The O(n^2) definition (tests).
    static void forwardDirect(int n, double const* in, double* out);

private:
    void fft(std::vector<double>& re, std::vector<double>& im) const;

    int                 mN;
    std::vector<double> mPreCos, mPreSin;   // exp(-i pi (4m+1) / (4M)), m < M/2
    std::vector<double> mPostCos, mPostSin; // exp(-i pi k / M), k < M/2
    std::vector<double> mTwCos, mTwSin;     // FFT twiddles
    std::vector<int>    mBitRev;
};

} // namespace gf::core::synth
