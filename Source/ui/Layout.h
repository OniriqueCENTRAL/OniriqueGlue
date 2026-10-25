// Onirique Glue — where everything sits on the front, shared by the editor
// and by the renderer that photographs the parts (tools/photo.cpp)
#pragma once

#include <cmath>

namespace onirique::layout
{

// The front, in points; its photographs have this many pixels to the point.
constexpr int kWidth = 420, kHeight = 866;
constexpr int kPhotoScale = 2;

enum class Kind { knob, trim, button };

/** A run of photographs: a knob all the way round, five degrees apart, so
    that every control turns to any angle with the nearest one; or a button,
    out then lit. */
struct Run
{
    const char* photo;
    Kind kind;
    int frames;
};

inline constexpr Run kRuns[] = {
    { "knob",   Kind::knob,   72 },
    { "trim",   Kind::trim,   72 },
    { "button", Kind::button, 2 },
};

constexpr float kDegreesPerFrame = 5.0f;

/** A control: its parameter, its readout's name, its run, where it sits. */
struct Control
{
    const char* id;
    const char* name;
    Kind kind;
    float x, y;              // its centre
    float size;              // the square that takes the pointer
    float fromDegrees, toDegrees;   // the pointer at the parameter's lowest and highest, clockwise from twelve o'clock
};

// As on the console's front: the sweeps of the knobs are those engraved round them.
inline constexpr Control kControls[] = {
    { "threshold", "THRESHOLD", Kind::knob,   128.0f, 285.0f, 64.0f, -143.0f, 143.0f },
    { "makeup",    "MAKE-UP",   Kind::knob,   293.0f, 285.0f, 64.0f, -143.0f, 143.0f },
    { "attack",    "ATTACK",    Kind::knob,   125.0f, 395.0f, 64.0f, -80.0f,  80.0f },
    { "release",   "RELEASE",   Kind::knob,   295.0f, 395.0f, 64.0f, -63.0f,  63.0f },
    { "ratio",     "RATIO",     Kind::knob,   128.0f, 505.0f, 64.0f, -30.0f,  30.0f },
    { "in",        "IN",        Kind::button, 295.0f, 508.0f, 90.0f, 0.0f,    0.0f },
    { "scfilter",  "SC FILTER", Kind::knob,   125.0f, 635.0f, 64.0f, -141.0f, 141.0f },
    { "headroom",  "HEADROOM",  Kind::trim,   210.0f, 636.0f, 30.0f, -135.0f, 135.0f },
    { "mix",       "MIX",       Kind::knob,   294.0f, 635.0f, 64.0f, -141.0f, 141.0f },
    { "rate",      "FADE RATE", Kind::knob,   128.0f, 770.0f, 64.0f, -147.0f, 147.0f },
    { "fade",      "AUTO FADE", Kind::button, 295.0f, 772.0f, 90.0f, 0.0f,    0.0f },
};

constexpr float kKnobRadius = 24.0f, kTrimRadius = 7.5f;
constexpr float kCapHalf = 31.0f, kBezelHalf = 45.0f;

/** Where a part's photograph lies around its centre: the part, and room for
    the shadow it casts down and to the right, away from the light. */
struct Frame
{
    float before, after;     // from the centre to the left and top edges, and to the right and bottom ones

    constexpr float size() const { return before + after; }
};

constexpr Frame frameOf (Kind kind)
{
    switch (kind)
    {
        case Kind::knob:   return { 28.0f, 52.0f };
        case Kind::trim:   return { 10.0f, 14.0f };
        case Kind::button: return { 49.0f, 66.0f };
    }

    return { 0.0f, 0.0f };
}

// The meter: a black case standing on the front, its window cut in it, the
// needle turning on a pivot behind the zero screw, 0 to 20 dB of compression
// from left to right.
constexpr float kCaseLeft = 104.0f, kCaseTop = 54.0f, kCaseRight = 320.0f, kCaseBottom = 232.0f, kCaseHeight = 7.0f;
constexpr float kMeterLeft = 120.0f, kMeterTop = 69.0f, kMeterRight = 302.0f, kMeterBottom = 168.0f;
constexpr float kZeroScrewX = 210.0f, kZeroScrewY = 202.0f, kZeroScrewRadius = 9.0f;
constexpr float kPivotX = 211.0f, kPivotY = 194.0f, kNeedleLength = 98.0f;
constexpr float kTickInner = 82.0f, kTickOuter = 89.0f, kNumberRadius = 105.0f;
constexpr float kMeterMaxDb = 20.0f, kMeterHalfDegrees = 44.0f;

inline float meterDegrees (float db)
{
    const auto t = db < 0.0f ? 0.0f : db > kMeterMaxDb ? 1.0f : db / kMeterMaxDb;
    return -kMeterHalfDegrees + 2.0f * kMeterHalfDegrees * t;
}

/** Where a value lies along a range whose middle sits at `centre`, from 0 to
    1: the knob's travel, as the parameter's own skew lays it out. */
inline float skewedProportion (float value, float start, float end, float centre)
{
    const auto skew = std::log (0.5f) / std::log ((centre - start) / (end - start));
    return std::pow ((value - start) / (end - start), skew);
}

constexpr float kScFilterMax = 500.0f, kScFilterCentre = 80.0f;

// The fade's rate is engraved unevenly, 10 at the top: its knob follows the
// engraving, evenly in between from one mark to the next.
constexpr int kNumRateMarks = 7;
constexpr float kRateMarks[kNumRateMarks] = { 1.0f, 3.0f, 5.0f, 10.0f, 20.0f, 40.0f, 60.0f };
constexpr float kRateMarkDegrees[kNumRateMarks] = { -147.0f, -90.0f, -47.0f, 0.0f, 48.0f, 90.0f, 147.0f };

inline float rateProportion (float seconds)
{
    for (int i = 1; i < kNumRateMarks; ++i)
    {
        if (seconds <= kRateMarks[i] || i == kNumRateMarks - 1)
        {
            const auto t = std::log (std::fmax (seconds, kRateMarks[0]) / kRateMarks[i - 1]) / std::log (kRateMarks[i] / kRateMarks[i - 1]);
            const auto degrees = kRateMarkDegrees[i - 1] + std::fmin (1.0f, t) * (kRateMarkDegrees[i] - kRateMarkDegrees[i - 1]);
            return (degrees - kRateMarkDegrees[0]) / (kRateMarkDegrees[kNumRateMarks - 1] - kRateMarkDegrees[0]);
        }
    }

    return 1.0f;
}

inline float rateFromProportion (float proportion)
{
    const auto degrees = kRateMarkDegrees[0] + proportion * (kRateMarkDegrees[kNumRateMarks - 1] - kRateMarkDegrees[0]);

    for (int i = 1; i < kNumRateMarks; ++i)
    {
        if (degrees <= kRateMarkDegrees[i] || i == kNumRateMarks - 1)
        {
            const auto t = std::fmax (0.0f, (degrees - kRateMarkDegrees[i - 1]) / (kRateMarkDegrees[i] - kRateMarkDegrees[i - 1]));
            return kRateMarks[i - 1] * std::pow (kRateMarks[i] / kRateMarks[i - 1], std::fmin (1.0f, t));
        }
    }

    return kRateMarks[kNumRateMarks - 1];
}

// The white rules of the front: the frame round the compressor, open at the
// top, the rule of the side-chain section, the bracket round the fade.
constexpr float kRuleLeft = 52.0f, kRuleRight = 368.0f, kFrameTop = 28.0f, kCompressorRuleY = 576.0f;
constexpr float kGlueRuleY = 711.0f, kBracketTop = 782.0f, kBracketY = 834.0f, kRuleCorner = 7.0f;

} // namespace onirique::layout
