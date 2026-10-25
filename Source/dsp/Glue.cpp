#include "Glue.h"

#include <algorithm>
#include <cmath>

namespace glue
{

namespace
{
    constexpr double kDbToNeper = 0.11512925464970229;   // ln(10) / 20

    double toGain (double db) noexcept { return std::exp (db * kDbToNeper); }

    /** A one-pole's step towards its target for a time constant, in seconds. */
    double pole (double seconds, double fs) noexcept { return 1.0 - std::exp (-1.0 / (seconds * fs)); }

    // The auto release: a fast recovery for what passes, and a slower one
    // that charges only while the compression lasts and lets it go gently.
    constexpr double kAutoFast = 0.1, kAutoCharge = 0.45, kAutoHold = 1.6;

    // The output stage's bias: what makes its rounding a little uneven, the
    // second harmonic with the third.
    constexpr double kBias = 0.012;

    // How much second harmonic the VCA adds as it works, per dB of reduction.
    constexpr double kVcaColour = 0.0012;
}

//==============================================================================
double Compressor::curveDb (double levelDb, double thresholdDb, double ratio) noexcept
{
    // The knee softer at the gentle ratio, firmer as it climbs.
    const auto knee = ratio < 3.0 ? 8.0 : ratio < 6.0 ? 6.0 : 4.0;
    const auto over = levelDb - thresholdDb, slope = 1.0 - 1.0 / ratio;

    if (2.0 * over <= -knee)
        return 0.0;

    if (2.0 * over < knee)
        return slope * (over + 0.5 * knee) * (over + 0.5 * knee) / (2.0 * knee);

    return slope * over;
}

void Compressor::prepare (double sampleRate)
{
    fs = sampleRate;
    dcPole = std::exp (-2.0 * M_PI * 5.0 / fs);
    glide = pole (0.02, fs);
    reset();
}

void Compressor::reset() noexcept
{
    threshold = target.threshold;
    makeup = target.makeup;
    headroom = target.headroom;
    mix = target.mix * 0.01;
    inGain = target.in ? 1.0 : 0.0;
    fader = target.fade ? 0.0 : 1.0;

    designedHz = -1.0;
    designFilter (target.scFilter);
    hpS1 = hpS2 = 0.0;
    grFast = grSlow = 0.0;
    peakGr = 0.0f;

    std::fill (std::begin (dcX), std::end (dcX), 0.0);
    std::fill (std::begin (dcY), std::end (dcY), 0.0);
}

void Compressor::designFilter (double hz) noexcept
{
    if (hz < kScFilterOff)
    {
        hpB0 = 1.0;
        hpB1 = hpB2 = hpA1 = hpA2 = 0.0;
        designedHz = 0.0;
        return;
    }

    // Butterworth, second order: the lows the detector no longer hears.
    const auto w = 2.0 * M_PI * hz / fs, c = std::cos (w), alpha = std::sin (w) / std::sqrt (2.0), a0 = 1.0 + alpha;
    hpB0 = 0.5 * (1.0 + c) / a0;
    hpB1 = -(1.0 + c) / a0;
    hpB2 = hpB0;
    hpA1 = -2.0 * c / a0;
    hpA2 = (1.0 - alpha) / a0;
    designedHz = hz;
}

float Compressor::takeGainReduction() noexcept
{
    const auto gr = peakGr;
    peakGr = 0.0f;
    return gr;
}

void Compressor::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, kMaxChannels);

    const auto& s = target;
    const auto hz = s.scFilter < kScFilterOff ? 0.0 : (double) s.scFilter;

    if (std::abs (hz - designedHz) > 0.5)
        designFilter (hz);

    const auto ratio = kRatios[std::clamp (s.ratio, 0, kNumRatios - 1)];
    const auto attack = pole (kAttacksMs[std::clamp (s.attack, 0, kNumAttacks - 1)] * 0.001, fs);
    const auto releaseS = kReleasesS[std::clamp (s.release, 0, kNumReleases - 1)];
    const auto isAuto = releaseS <= 0.0;
    const auto release = pole (isAuto ? kAutoFast : releaseS, fs);
    const auto charge = pole (kAutoCharge, fs), hold = pole (kAutoHold, fs);
    const auto faderStep = 1.0 / (std::max (0.1, (double) s.rate) * fs);
    const auto faderTarget = s.fade ? 0.0 : 1.0;

    auto mostGr = (double) peakGr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Each control glides, and lands exactly: IN off is the signal to the bit.
        auto towards = [this] (double& value, double to)
        {
            value += glide * (to - value);
            value = std::abs (to - value) < 1.0e-7 ? to : value;
        };

        towards (threshold, s.threshold);
        towards (makeup, s.makeup);
        towards (headroom, s.headroom);
        towards (mix, s.mix * 0.01);
        towards (inGain, s.in ? 1.0 : 0.0);

        // The detector hears both sides summed, as the console's does.
        auto side = numChannels == 2 ? 0.5 * ((double) channels[0][i] + (double) channels[1][i]) : (double) channels[0][i];

        const auto filtered = hpB0 * side + hpS1;
        hpS1 = hpB1 * side - hpA1 * filtered + hpS2;
        hpS2 = hpB2 * side - hpA2 * filtered;
        side = filtered;

        const auto levelDb = 20.0 * std::log10 (std::abs (side) + 1.0e-9);
        const auto wanted = curveDb (levelDb, kThresholdAt0 + threshold, ratio);

        grFast += (wanted > grFast ? attack : release) * (wanted - grFast);
        auto gr = grFast;

        if (isAuto)
        {
            grSlow += (grFast > grSlow ? charge : hold) * (grFast - grSlow);
            gr = std::max (grFast, grSlow);
        }

        mostGr = std::max (mostGr, gr * inGain);

        const auto vca = toGain (-gr);
        const auto colour = kVcaColour * std::min (gr, 20.0);
        const auto up = toGain (makeup);

        // Where the output stage starts to round off, and its curve's slope at
        // rest, so that below it the level is untouched.
        const auto ceiling = toGain (headroom + kThresholdAt0);
        const auto rest = std::tanh (kBias), unity = 1.0 - rest * rest;

        if (fader != faderTarget)
            fader = faderTarget > fader ? std::min (faderTarget, fader + faderStep) : std::max (faderTarget, fader - faderStep);

        const auto faderGain = fader * fader * fader;

        for (int c = 0; c < numChannels; ++c)
        {
            const auto dry = (double) channels[c][i];
            auto y = dry * vca;
            y += colour * y * y;
            y *= up;
            y = ceiling * (std::tanh (y / ceiling + kBias) - rest) / unity;

            // The bias and the VCA's colour leave DC: taken out.
            const auto clean = y - dcX[c] + dcPole * dcY[c];
            dcX[c] = y;
            dcY[c] = clean;

            const auto wet = dry + mix * (clean - dry);
            channels[c][i] = (float) ((dry + inGain * (wet - dry)) * faderGain);
        }
    }

    peakGr = (float) mostGr;
}

} // namespace glue
