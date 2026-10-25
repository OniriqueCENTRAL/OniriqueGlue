// The whole sound of Glue without a host: the curve, the times, the auto
// release, the side-chain filter, the colour, the switches and the fade.
#include "dsp/Glue.h"
#include "TestSignal.h"

#include <cstdio>
#include <cstdlib>

namespace
{
constexpr double kSr = 96000.0;   // the plugin runs the engine at twice the host's rate

int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    failures += ok ? 0 : 1;
}

double db (double v) { return 20.0 * std::log10 (v + 1.0e-12); }

/** Through the compressor, as one channel or as the same on both sides. */
std::vector<float> play (const glue::Settings& s, std::vector<float> x, glue::Compressor* keep = nullptr)
{
    glue::Compressor local;
    auto& c = keep != nullptr ? *keep : local;
    c.setSettings (s);
    c.prepare (kSr);
    float* channels[] = { x.data() };
    c.process (channels, 1, (int) x.size());
    return x;
}

/** The amplitude of one frequency over the second half of a signal. */
double amplitude (const std::vector<float>& x, double hz)
{
    const auto from = x.size() / 2, n = x.size() - from;
    double re = 0.0, im = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        const auto phase = 2.0 * M_PI * hz * (double) (from + i) / kSr;
        re += x[from + i] * std::cos (phase);
        im += x[from + i] * std::sin (phase);
    }

    return 2.0 * std::sqrt (re * re + im * im) / (double) n;
}

float fromDb (double v) { return (float) std::pow (10.0, v / 20.0); }

/** What a tone loses through the compressor, in dB: input over output at its frequency. */
double reduction (const glue::Settings& s, double hz, double levelDb, double seconds = 1.0)
{
    const auto x = mdxtest::makeTone (seconds, kSr, hz, fromDb (levelDb));
    return db (amplitude (x, hz) / amplitude (play (s, x), hz));
}

/** The level of a stretch of signal, in dB RMS. */
double rmsDb (const std::vector<float>& x, double fromS, double toS)
{
    const auto a = (size_t) (fromS * kSr), b = std::min (x.size(), (size_t) (toS * kSr));
    return mdxtest::rmsDb (x.data() + a, b - a);
}
}

int main()
{
    std::printf ("Glue, offline\n");

    glue::Settings base;   // threshold 0 (-14 dBFS), 4:1, 30 ms, auto
    base.makeup = 0.0f;

    // The static curve: a steady tone, well past the attack.
    std::printf ("    reduction of a 1 kHz tone, dB:       -30    -20    -14     -8     -2  dBFS\n");

    for (int r = 0; r < glue::kNumRatios; ++r)
    {
        auto s = base;
        s.ratio = r;
        std::printf ("    %-34s", glue::kRatioNames[r]);

        for (double level : { -30.0, -20.0, -14.0, -8.0, -2.0 })
            std::printf (" %6.2f", reduction (s, 1000.0, level));

        std::printf ("\n");
    }

    {
        check (reduction (base, 1000.0, -30.0) < 0.05, "well under the threshold: untouched");

        // Well over the knee, 6 dB more in: the ratio's share of it taken off. A
        // tone is read by its body, not its crest, so it lands a little short.
        bool slopes = true;

        for (int r = 0; r < glue::kNumRatios; ++r)
        {
            auto s = base;
            s.ratio = r;
            const auto slope = reduction (s, 1000.0, -2.0) - reduction (s, 1000.0, -8.0), wanted = 6.0 * (1.0 - 1.0 / glue::kRatios[r]);
            std::printf ("    %s:1, 6 dB more in, %.2f dB more off (the ratio says %.2f)\n", glue::kRatioNames[r], slope, wanted);
            slopes = slopes && slope > 0.8 * wanted && slope < 1.05 * wanted;
        }

        check (slopes, "each ratio: its share of what comes over");

        const auto at4 = reduction (base, 1000.0, -2.0);
        auto two = base, ten = base;
        two.ratio = 0;
        ten.ratio = 2;
        const auto at2 = reduction (two, 1000.0, -2.0), at10 = reduction (ten, 1000.0, -2.0);
        check (at2 < at4 && at4 < at10, "2:1 less than 4:1, 4:1 less than 10:1");

        auto higher = base;
        higher.threshold = 10.0f;
        check (reduction (higher, 1000.0, -2.0) < at4 - 5.0, "the threshold raised: far less reduction");
    }

    // The times: a step from quiet to loud, how much is taken off in the first millisecond.
    {
        auto step = [] (const glue::Settings& s)
        {
            auto x = mdxtest::makeTone (0.3, kSr, 1000.0, fromDb (-30.0));

            for (size_t i = x.size() / 2; i < x.size(); ++i)
                x[i] *= fromDb (28.0);

            const auto out = play (s, x);
            const auto at = x.size() / 2, ms = (size_t) (0.002 * kSr);
            return rmsDb (std::vector<float> (x.begin() + (long) at, x.begin() + (long) (at + ms)), 0.0, 1.0)
                   - rmsDb (std::vector<float> (out.begin() + (long) at, out.begin() + (long) (at + ms)), 0.0, 1.0);
        };

        auto fast = base, slow = base;
        fast.attack = 0;
        slow.attack = 5;
        const auto f = step (fast), sl = step (slow);
        std::printf ("    first 2 ms of a 28 dB step: %.1f dB off at 0.1 ms, %.1f dB at 30 ms\n", f, sl);
        check (f > 6.0, "0.1 ms attack: the transient is caught at once");
        check (sl < 2.5, "30 ms attack: the transient goes through");
    }

    // The auto release: back quickly after a hit, slowly after a long push.
    {
        auto recovery = [] (const glue::Settings& s, double loudFor)
        {
            const auto total = loudFor + 1.0;
            auto x = mdxtest::makeTone (total, kSr, 1000.0, fromDb (-30.0));

            for (size_t i = 0; i < (size_t) (loudFor * kSr); ++i)
                x[i] *= fromDb (26.0);

            const auto out = play (s, x);
            // 150 ms after the end of the push: what is still taken off.
            const auto a = loudFor + 0.15;
            return rmsDb (x, a, a + 0.02) - rmsDb (out, a, a + 0.02);
        };

        const auto afterHit = recovery (base, 0.03), afterPush = recovery (base, 2.0);
        std::printf ("    auto release, still off 150 ms later: %.2f dB after a 30 ms hit, %.2f dB after 2 s\n", afterHit, afterPush);
        check (afterHit < 1.0, "auto: back quickly after a hit");
        check (afterPush > 2.0, "auto: held after a long push");

        auto fixed = base;
        fixed.release = 0;
        check (recovery (fixed, 2.0) < afterPush - 2.0, "0.1 s release: back quickly whatever came before");
    }

    // The side-chain filter: the bass no longer drives it.
    {
        auto filtered = base;
        filtered.scFilter = 250.0f;
        const auto open = reduction (base, 50.0, -4.0), cut = reduction (filtered, 50.0, -4.0);
        std::printf ("    50 Hz at -4 dBFS: %.2f dB off, %.2f dB with the side-chain filter at 250 Hz\n", open, cut);
        check (cut < open - 4.0, "side-chain filter: the bass pumps it far less");
        check (std::abs (reduction (filtered, 3000.0, -4.0) - reduction (base, 3000.0, -4.0)) < 0.5, "and the mids drive it as before");
    }

    // The colour.
    auto harmonics = [] (const glue::Settings& s, double hz, double levelDb)
    {
        const auto x = mdxtest::makeTone (1.0, kSr, hz, fromDb (levelDb));
        const auto out = play (s, x);
        const auto fundamental = amplitude (out, hz);
        return std::make_pair (db (amplitude (out, 2.0 * hz) / fundamental), db (amplitude (out, 3.0 * hz) / fundamental));
    };

    {
        auto quiet = base;
        quiet.threshold = 15.0f;
        const auto [h2, h3] = harmonics (quiet, 1000.0, -18.0);
        std::printf ("    no reduction, 1 kHz at -18 dBFS: H2 %.1f dB, H3 %.1f dB\n", h2, h3);
        check (h2 < -65.0 && h3 < -65.0, "at a normal level, clean");

        const auto [hot2, hot3] = harmonics (quiet, 1000.0, -4.0);
        std::printf ("    no reduction, 1 kHz at -4 dBFS: H2 %.1f dB, H3 %.1f dB\n", hot2, hot3);
        check (hot2 > -60.0 && hot2 < -35.0 && hot3 > -60.0 && hot3 < -32.0, "pushed hot, it rounds a little: a colour, not a fuzz");

        auto tight = quiet;
        tight.headroom = 4.0f;
        const auto [t2, t3] = harmonics (tight, 1000.0, -4.0);
        std::printf ("    headroom 4 dB, -4 dBFS: H2 %.1f dB, H3 %.1f dB\n", t2, t3);
        check (t3 > hot3 + 10.0, "less headroom: more of it");

        const auto [c2, c3] = harmonics (base, 1000.0, -4.0);
        std::printf ("    compressing ~8 dB, -4 dBFS: H2 %.1f dB, H3 %.1f dB\n", c2, c3);
        check (c2 < -35.0 && c3 < -32.0, "compressing hard, still clean");
    }

    // The make-up.
    {
        auto quiet = base;
        quiet.threshold = 15.0f;
        quiet.makeup = 6.0f;
        check (std::abs (reduction (quiet, 1000.0, -30.0) + 6.0) < 0.1, "make-up 6: 6 dB up");
    }

    // The switches.
    const auto noise = mdxtest::makeNoise (0.3, kSr, 0.7f);
    auto worst = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        auto w = 0.0f;

        for (size_t i = 0; i < a.size(); ++i)
            w = std::max (w, std::abs (a[i] - b[i]));

        return w;
    };

    {
        auto out = base;
        out.in = false;
        out.makeup = 10.0f;
        check (worst (play (out, noise), noise) == 0.0f, "IN off: the signal as it came in, to the bit");

        auto dry = base;
        dry.mix = 0.0f;
        dry.makeup = 10.0f;
        check (worst (play (dry, noise), noise) == 0.0f, "mix at 0: the signal as it came in");
    }
    {
        // Two channels, the same on each: the same out of each.
        auto x = mdxtest::makeNoise (0.2, kSr, 0.5f), y = x;
        glue::Compressor c;
        c.setSettings (base);
        c.prepare (kSr);
        float* channels[] = { x.data(), y.data() };
        c.process (channels, 2, (int) x.size());
        check (x == y, "both sides the same");
        check (c.takeGainReduction() > 3.0f && c.takeGainReduction() == 0.0f, "the meter is told how much, once");
    }
    {
        // Linked: a loud left pulls the right down with it.
        auto l = mdxtest::makeTone (0.5, kSr, 1000.0, fromDb (-2.0)), r = mdxtest::makeTone (0.5, kSr, 1000.0, fromDb (-30.0));
        const auto rIn = r;
        glue::Compressor c;
        c.setSettings (base);
        c.prepare (kSr);
        float* channels[] = { l.data(), r.data() };
        c.process (channels, 2, (int) l.size());
        // The detector hears the sum: a side alone counts half.
        check (db (amplitude (rIn, 1000.0) / amplitude (r, 1000.0)) > 2.5, "one VCA: the quiet side follows the loud one");
    }

    // The fade.
    {
        auto fading = base;
        fading.threshold = 15.0f;
        fading.fade = true;
        fading.rate = 1.0f;

        glue::Compressor c;
        c.setSettings (base);   // prepared with the fader up
        c.prepare (kSr);
        c.setSettings (fading);

        auto x = mdxtest::makeTone (1.5, kSr, 1000.0, 0.5f);
        float* channels[] = { x.data() };
        c.process (channels, 1, (int) x.size());

        const auto half = rmsDb (x, 0.45, 0.55), end = rmsDb (x, 1.1, 1.5);
        std::printf ("    auto fade over 1 s: %.1f dB halfway, %.1f dB after\n", half - rmsDb (x, 0.0, 0.02), end);
        check (half < rmsDb (x, 0.0, 0.02) - 10.0 && half > rmsDb (x, 0.0, 0.02) - 30.0, "halfway through the fade, well down");
        check (end < -150.0, "and after it, silence");
    }
    {
        // The threshold slammed down during a note: no step in the sound.
        auto x = mdxtest::makeTone (0.6, kSr, 80.0, 0.5f);
        glue::Compressor c;
        auto s = base;
        s.threshold = 15.0f;
        s.attack = 0;
        c.setSettings (s);
        c.prepare (kSr);
        float* channels[] = { x.data() };
        const int half = (int) x.size() / 2;
        c.process (channels, 1, half);
        s.threshold = -15.0f;
        s.makeup = 15.0f;
        c.setSettings (s);
        channels[0] = x.data() + half;
        c.process (channels, 1, (int) x.size() - half);

        auto jump = 0.0f;

        for (size_t i = 1; i < x.size(); ++i)
            jump = std::max (jump, std::abs (x[i] - x[i - 1]));

        // An 80 Hz sine at 0.5 moves at most ~0.003 a sample at this rate.
        check (jump < 0.02f, "controls glide: no click");
        check (std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); }), "and nothing blows up");
    }

    std::printf (failures == 0 ? "all good\n" : "%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
