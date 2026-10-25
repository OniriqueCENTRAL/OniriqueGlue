// Onirique Glue — where everything sits on the front, shared by the editor
// and by the renderer that photographs the parts (tools/photo.cpp)
#pragma once

#include <cmath>

namespace onirique::layout
{

// The front, in points; its photographs have this many pixels to the point.
constexpr int kWidth = 420, kHeight = 1040;
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

inline constexpr Control kControls[] = {
    { "threshold", "THRESHOLD", Kind::knob,   110.0f, 355.0f, 76.0f, -135.0f, 135.0f },
    { "makeup",    "MAKE-UP",   Kind::knob,   310.0f, 355.0f, 76.0f, -135.0f, 135.0f },
    { "attack",    "ATTACK",    Kind::knob,   110.0f, 480.0f, 76.0f, -135.0f, 135.0f },
    { "release",   "RELEASE",   Kind::knob,   310.0f, 480.0f, 76.0f, -120.0f, 120.0f },
    { "ratio",     "RATIO",     Kind::knob,   110.0f, 605.0f, 76.0f, -60.0f,  60.0f },
    { "in",        "IN",        Kind::button, 310.0f, 605.0f, 70.0f, 0.0f,    0.0f },
    { "scfilter",  "SC FILTER", Kind::knob,   90.0f,  765.0f, 76.0f, -135.0f, 135.0f },
    { "headroom",  "HEADROOM",  Kind::trim,   210.0f, 765.0f, 40.0f, -135.0f, 135.0f },
    { "mix",       "MIX",       Kind::knob,   330.0f, 765.0f, 76.0f, -135.0f, 135.0f },
    { "rate",      "FADE RATE", Kind::knob,   110.0f, 930.0f, 76.0f, -135.0f, 135.0f },
    { "fade",      "AUTO FADE", Kind::button, 310.0f, 930.0f, 70.0f, 0.0f,    0.0f },
};

constexpr float kKnobRadius = 24.0f, kTrimRadius = 11.0f, kButtonHalf = 30.0f;

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
        case Kind::knob:   return { 29.0f, 56.0f };
        case Kind::trim:   return { 14.0f, 27.0f };
        case Kind::button: return { 37.0f, 52.0f };
    }

    return { 0.0f, 0.0f };
}

// The meter: a lit window, its needle turning on a pivot hidden under the
// front, 0 to 20 dB of compression from left to right.
constexpr float kMeterLeft = 60.0f, kMeterTop = 92.0f, kMeterRight = 360.0f, kMeterBottom = 262.0f;
constexpr float kPivotX = 210.0f, kPivotY = 300.0f, kNeedleLength = 172.0f, kScaleRadius = 160.0f;
constexpr float kMeterMaxDb = 20.0f, kMeterHalfDegrees = 42.0f;

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

// The ranges with a skew, shared with the parameters.
constexpr float kScFilterMax = 500.0f, kScFilterCentre = 80.0f;
constexpr float kRateMin = 1.0f, kRateMax = 60.0f, kRateCentre = 10.0f;

// What is part of the front itself, photographed with it.
struct Screw { float x, y, slot; };
inline constexpr Screw kScrews[] = { { 30.0f, 30.0f, 0.5f }, { 390.0f, 30.0f, -0.3f },
                                     { 30.0f, 1010.0f, 0.9f }, { 390.0f, 1010.0f, 0.2f } };
constexpr float kScrewRadius = 9.0f;

} // namespace onirique::layout
