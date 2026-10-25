// Onirique Glue — the sound: a stereo bus compressor in the spirit of the
// console's centre section (the G-series bus compressor): one VCA for both
// sides, a fed-forward detector on their sum, stepped times, the auto release
// that holds a mix together, and an output stage that rounds the peaks
#pragma once

namespace glue
{

constexpr int kMaxChannels = 2;

// The stepped controls, as they are engraved on the front.
constexpr int kNumAttacks = 6, kNumReleases = 5, kNumRatios = 3;
constexpr double kAttacksMs[kNumAttacks] = { 0.1, 0.3, 1.0, 3.0, 10.0, 30.0 };
constexpr double kReleasesS[kNumReleases] = { 0.1, 0.3, 0.6, 1.2, 0.0 };   // the last: auto
constexpr double kRatios[kNumRatios] = { 2.0, 4.0, 10.0 };
constexpr const char* kAttackNames[kNumAttacks] = { ".1", ".3", "1", "3", "10", "30" };
constexpr const char* kReleaseNames[kNumReleases] = { ".1", ".3", ".6", "1.2", "AUTO" };
constexpr const char* kRatioNames[kNumRatios] = { "2", "4", "10" };

// Where the threshold knob at 0 sits, in dBFS: the console's 0 dBu with the
// usual 14 dB above it to full scale.
constexpr double kThresholdAt0 = -14.0;

// Below this the side-chain filter is off.
constexpr double kScFilterOff = 20.0;

/** The front panel. */
struct Settings
{
    float threshold = 0.0f;     // -15 to +15 dB
    float makeup = 0.0f;        // -5 to +15 dB
    int attack = 5;             // 30 ms
    int release = 4;            // auto
    int ratio = 1;              // 4:1
    bool in = true;             // out: the signal as it came in
    float scFilter = 0.0f;      // the detector's high-pass, Hz; under 20, off
    float headroom = 20.0f;     // 4 to 24 dB: how far above the console's 0 the output stage starts to round off
    float mix = 100.0f;         // %: the compressed sound against the one that came in
    bool fade = false;          // the auto fade: pressed, the fader goes down; released, it comes back
    float rate = 5.0f;          // how long the fade takes, s
};

/** One channel or two, through the same VCA. */
class Compressor
{
public:
    /** Settings set before this are taken at once, without gliding to them. */
    void prepare (double sampleRate);
    void reset() noexcept;

    void setSettings (const Settings& s) noexcept { target = s; }
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** The gain reduction as the meter shows it, in dB (positive): the most since the last call. */
    float takeGainReduction() noexcept;

    /** What the knee does to a steady level, in dB of gain reduction: the static curve. */
    static double curveDb (double levelDb, double thresholdDb, double ratio) noexcept;

private:
    void designFilter (double hz) noexcept;

    double fs = 96000.0;
    Settings target;

    // The controls that glide: threshold, make-up, headroom, mix, in.
    double threshold = 0.0, makeup = 0.0, headroom = 18.0, mix = 1.0, inGain = 1.0, glide = 0.0;

    // The detector: its high-pass (a second-order section) and the two time
    // constants of the release, the fast one and the one that holds.
    double hpB0 = 1.0, hpB1 = 0.0, hpB2 = 0.0, hpA1 = 0.0, hpA2 = 0.0, hpS1 = 0.0, hpS2 = 0.0;
    double designedHz = -1.0;
    double grFast = 0.0, grSlow = 0.0;

    // The output stage leaves a little DC behind its bias: taken out.
    double dcPole = 0.0, dcX[kMaxChannels] = {}, dcY[kMaxChannels] = {};

    double fader = 1.0;
    float peakGr = 0.0f;
};

} // namespace glue
