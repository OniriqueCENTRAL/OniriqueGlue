// Photographs the front of Glue and its controls all the way round: a path
// tracer over models of the parts (signed distance fields), lit as on a
// photographer's bench by one large softbox above and to the left. A
// development tool: its pictures are committed in Resources/photo, and the
// plugin only shows them.
//
//   glue_photo <folder> [quality] [names]
//
// quality 1 is the final render, 0.1 a quick look; names (comma separated)
// limits it to some of the pictures: front, knob, trim, button; and "preview"
// lays the pictures in the folder out as the plugin will into preview.png.
#include <juce_gui_basics/juce_gui_basics.h>

#include <PhotoData.h>

#include "../Source/dsp/Glue.h"
#include "../Source/ui/Layout.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
using namespace onirique::layout;

constexpr float kPi = 3.14159265358979f;

//==============================================================================
struct V { float x = 0.0f, y = 0.0f, z = 0.0f; };

V operator+ (V a, V b)     { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V operator- (V a, V b)     { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V operator- (V a)          { return { -a.x, -a.y, -a.z }; }
V operator* (V a, float k) { return { a.x * k, a.y * k, a.z * k }; }
V operator* (V a, V b)     { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
float dot (V a, V b)       { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length (V a)         { return std::sqrt (dot (a, a)); }
V norm (V a)               { return a * (1.0f / length (a)); }
V cross (V a, V b)         { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
V grey (float v)           { return { v, v, v }; }
V mix (V a, V b, float t)  { return a * (1.0f - t) + b * t; }
float luminance (V c)      { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
float clamp01 (float v)    { return std::fmin (1.0f, std::fmax (0.0f, v)); }

float smooth (float a, float b, float v)
{
    const auto t = clamp01 ((v - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

float wrap (float a)
{
    return a - 2.0f * kPi * std::floor ((a + kPi) / (2.0f * kPi));
}

/** Two directions square to n and to each other. */
void basis (V n, V& t, V& b)
{
    t = norm (cross (std::fabs (n.x) > 0.5f ? V { 0, 1, 0 } : V { 1, 0, 0 }, n));
    b = cross (n, t);
}

//==============================================================================
/** PCG32: a small, good random generator, one per pixel and sample. */
struct Rng
{
    explicit Rng (uint64_t seed) : state (seed * 6364136223846793005ull + 1442695040888963407ull) { next(); }

    float next()
    {
        const auto old = state;
        state = old * 6364136223846793005ull + 1442695040888963407ull;
        const auto shifted = (uint32_t) (((old >> 18u) ^ old) >> 27u);
        const auto rot = (uint32_t) (old >> 59u);
        const auto r = (shifted >> rot) | (shifted << ((32u - rot) & 31u));
        return (float) (r >> 8) * (1.0f / 16777216.0f);
    }

    uint64_t state;
};

/** Smooth value noise, for the grain of the paint and the wear of the plastic. */
float hash3 (int x, int y, int z)
{
    auto h = (uint32_t) (x * 374761393 + y * 668265263 + z * 1274126177);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float) ((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}

float noise (V p)
{
    const auto fx = std::floor (p.x), fy = std::floor (p.y), fz = std::floor (p.z);
    const int x = (int) fx, y = (int) fy, z = (int) fz;
    auto f = [] (float t) { return t * t * (3.0f - 2.0f * t); };
    const auto u = f (p.x - fx), v = f (p.y - fy), w = f (p.z - fz);
    auto lerp = [] (float a, float b, float t) { return a + (b - a) * t; };

    return lerp (lerp (lerp (hash3 (x, y, z), hash3 (x + 1, y, z), u), lerp (hash3 (x, y + 1, z), hash3 (x + 1, y + 1, z), u), v),
                 lerp (lerp (hash3 (x, y, z + 1), hash3 (x + 1, y, z + 1), u), lerp (hash3 (x, y + 1, z + 1), hash3 (x + 1, y + 1, z + 1), u), v), w);
}

float fbm (V p)
{
    return 0.5f * noise (p) + 0.25f * noise (p * 2.03f) + 0.125f * noise (p * 4.01f) + 0.125f * noise (p * 8.1f);
}

//==============================================================================
// Shapes as distances (after Inigo Quilez).
float smin (float a, float b, float k)
{
    const auto h = std::fmax (k - std::fabs (a - b), 0.0f) / k;
    return std::fmin (a, b) - h * h * k * 0.25f;
}

/** A cylinder standing on its axis (z), centred on the origin, its edges rounded. */
float roundCylinder (V p, float radius, float halfHeight, float round)
{
    const auto dx = std::sqrt (p.x * p.x + p.y * p.y) - radius + round, dy = std::fabs (p.z) - halfHeight + round;
    return std::fmin (std::fmax (dx, dy), 0.0f) + std::sqrt (std::fmax (dx, 0.0f) * std::fmax (dx, 0.0f) + std::fmax (dy, 0.0f) * std::fmax (dy, 0.0f)) - round;
}

float roundBox (V p, V half, float round)
{
    const V q { std::fabs (p.x) - half.x + round, std::fabs (p.y) - half.y + round, std::fabs (p.z) - half.z + round };
    const V out { std::fmax (q.x, 0.0f), std::fmax (q.y, 0.0f), std::fmax (q.z, 0.0f) };
    return length (out) + std::fmin (std::fmax (q.x, std::fmax (q.y, q.z)), 0.0f) - round;
}

float torus (V p, float major, float minor)
{
    const auto q = std::sqrt (p.x * p.x + p.y * p.y) - major;
    return std::sqrt (q * q + p.z * p.z) - minor;
}

/** A trapezium in two dimensions: r1 wide at the foot, r2 at the top, 2 he tall. */
float trapezoid (float px, float py, float r1, float r2, float he)
{
    const float k1x = r2, k1y = he, k2x = r2 - r1, k2y = 2.0f * he;
    px = std::fabs (px);
    const auto cax = px - std::fmin (px, py < 0.0f ? r1 : r2), cay = std::fabs (py) - he;
    const auto t = clamp01 (((k1x - px) * k2x + (k1y - py) * k2y) / (k2x * k2x + k2y * k2y));
    const auto cbx = px - k1x + k2x * t, cby = py - k1y + k2y * t;
    const auto s = (cbx < 0.0f && cay < 0.0f) ? -1.0f : 1.0f;
    return s * std::sqrt (std::fmin (cax * cax + cay * cay, cbx * cbx + cby * cby));
}

/** A cone with rounded ends, r1 at a, r2 at b. */
float roundCone (V p, V a, V b, float r1, float r2)
{
    const auto ba = b - a;
    const auto l2 = dot (ba, ba), rr = r1 - r2, a2 = l2 - rr * rr, il2 = 1.0f / l2;
    const auto pa = p - a;
    const auto y = dot (pa, ba), z = y - l2;
    const auto w = pa * l2 - ba * y;
    const auto x2 = dot (w, w), y2 = y * y * l2, z2 = z * z * l2;
    const auto k = (rr > 0.0f ? 1.0f : -1.0f) * rr * rr * x2;

    if ((z > 0.0f ? 1.0f : -1.0f) * a2 * z2 > k)
        return std::sqrt (x2 + z2) * il2 - r2;

    if ((y > 0.0f ? 1.0f : -1.0f) * a2 * y2 < k)
        return std::sqrt (x2 + y2) * il2 - r1;

    return (std::sqrt (x2 * a2 * il2) + y * rr) * il2 - r1;
}


//==============================================================================
enum class Material { panel, face, rim, knob, skirt, pointer, bezel, lens, lensLit, steel, blackSteel, hollow };

/** A part standing on the front, in its own frame: its centre on the origin,
    the front at z = 0, z towards the camera, y down as on the screen. */
struct Part
{
    virtual ~Part() = default;
    virtual float distance (V p) const = 0;
    virtual Material material (V p) const = 0;

    V at;               // where it stands on the front
    float reach = 0.0f; // the cylinder it fits in
    float top = 0.0f;
};

/** Where a point lies against a part turned by `angle`. */
struct Turned
{
    Turned (V p, float angle)
        : along (p.x * std::sin (angle) - p.y * std::cos (angle)),
          across (p.x * std::cos (angle) + p.y * std::sin (angle)),
          radius (std::sqrt (p.x * p.x + p.y * p.y)),
          theta (wrap (std::atan2 (p.x, -p.y) - angle))
    {}

    float along, across, radius, theta;
};


/** A rectangle's signed distance in two dimensions, its corners rounded by r. */
float roundRect (float x, float y, float halfX, float halfY, float r)
{
    const auto qx = std::fabs (x) - halfX + r, qy = std::fabs (y) - halfY + r;
    return std::sqrt (std::fmax (qx, 0.0f) * std::fmax (qx, 0.0f) + std::fmax (qy, 0.0f) * std::fmax (qy, 0.0f))
           + std::fmin (std::fmax (qx, qy), 0.0f) - r;
}

/** A shape drawn in two dimensions (d2) raised from z = 0 to `height`, its edges rounded by r. */
float raise (float d2, float z, float height, float r)
{
    const auto wx = d2 + r, wy = std::fabs (z - 0.5f * height) - 0.5f * height + r;
    return std::sqrt (std::fmax (wx, 0.0f) * std::fmax (wx, 0.0f) + std::fmax (wy, 0.0f) * std::fmax (wy, 0.0f))
           + std::fmin (std::fmax (wx, wy), 0.0f) - r;
}

/** The console's knob, in two tiers: a dark grey skirt at its foot, its
    edge finely knurled, and standing on it a tall cap of light grey plastic,
    its top rounded off, gently domed, a ring turned in it; a black line from
    the middle out over the cap and across the skirt. */
struct Knob final : Part
{
    Knob (float radius, float a) : R (radius), angle (a) { reach = 1.02f * R + 1.0f; top = capTop + 0.08f * R; }

    float skirt (V p) const
    {
        const Turned t (p, angle);
        const auto knurl = 0.012f * R * std::cos (48.0f * t.theta);
        return roundCylinder (p - V { 0, 0, 0.5f * skirtTop }, R + knurl, 0.5f * skirtTop, 0.06f * R);
    }

    float cap (V p) const
    {
        constexpr float bottom = 0.1f;
        auto d = roundCylinder (p - V { 0, 0, 0.5f * (bottom * R + capTop + 0.03f * R) }, 0.76f * R,
                                0.5f * (capTop + 0.03f * R - bottom * R), 0.17f * R);
        d = std::fmax (d, length (p - V { 0, 0, capTop + 0.03f * R - 3.0f * R }) - 3.0f * R);
        return std::fmax (d, -torus (p - V { 0, 0, capTop + 0.015f * R }, 0.5f * R, 0.022f * R));
    }

    float distance (V p) const override { return smin (skirt (p), cap (p), 0.03f * R); }

    Material material (V p) const override
    {
        const Turned t (p, angle);

        if (std::fabs (t.across) < 0.075f * R && t.along > 0.1f * R && p.z > 0.6f * skirtTop)
            return Material::pointer;

        return cap (p) < skirt (p) ? Material::knob : Material::skirt;
    }

    float R, angle;
    float skirtTop = 0.24f * R, capTop = 0.82f * R;
};

/** A square push button: a thick black bezel with sloping sides, and in it
    a cap of frosted plastic, grey when out, lit from behind when in, sitting
    a little lower then. */
struct Button final : Part
{
    explicit Button (bool on) : lit (on) { reach = 1.42f * kBezelHalf + 1.0f; top = kRise + 1.0f; }

    static constexpr float kRise = 8.0f;

    float bezel (V p) const
    {
        const auto slope = 4.0f * clamp01 (p.z / kRise);
        const auto outer = raise (roundRect (p.x, p.y, kBezelHalf - slope, kBezelHalf - slope, 2.5f) * 0.92f, p.z, kRise, 1.4f);
        const auto hole = std::fmax (roundRect (p.x, p.y, kCapHalf + 1.2f, kCapHalf + 1.2f, 2.0f), 1.0f - p.z);
        return std::fmax (outer, -hole);
    }

    float cap (V p) const
    {
        const auto capTop = lit ? 6.2f : 7.0f;
        return roundBox (p - V { 0, 0, 0.5f * (1.0f + capTop) }, { kCapHalf, kCapHalf, 0.5f * (capTop - 1.0f) }, 1.6f);
    }

    float distance (V p) const override { return std::fmin (bezel (p), cap (p)); }
    Material material (V p) const override { return cap (p) < bezel (p) ? (lit ? Material::lensLit : Material::lens) : Material::bezel; }

    bool lit;
};

/** The headroom trim: a slotted steel screw in a small black collar, set
    almost flush with the front. */
struct Trim final : Part
{
    explicit Trim (float a) : angle (a) { reach = kTrimRadius + 1.0f; top = 2.6f; }

    float head (V p) const
    {
        const Turned t (p, angle);
        const auto dome = roundCylinder (p - V { 0, 0, 0.9f }, 0.68f * kTrimRadius, 0.9f, 0.7f);
        return std::fmax (dome, -roundBox ({ t.across, t.along, p.z - 2.0f }, { 0.75f, 0.8f * kTrimRadius, 1.0f }, 0.2f));
    }

    float collar (V p) const
    {
        return std::fmax (torus (p - V { 0, 0, 0.9f }, 0.84f * kTrimRadius, 0.16f * kTrimRadius), -p.z);
    }

    float distance (V p) const override { return std::fmin (head (p), collar (p)); }
    Material material (V p) const override { return head (p) < collar (p) ? Material::steel : Material::bezel; }

    float angle;
};

/** The meter's black case standing on the front, its sides sloping, its
    window cut down to the dark face the scale is printed on. */
struct MeterCase final : Part
{
    MeterCase()
    {
        at = { 0.5f * (kCaseLeft + kCaseRight), 0.5f * (kCaseTop + kCaseBottom), 0.0f };
        reach = std::sqrt (halfX * halfX + halfY * halfY) + 1.0f;
        top = kCaseHeight + 0.5f;
    }

    static constexpr float halfX = 0.5f * (kCaseRight - kCaseLeft), halfY = 0.5f * (kCaseBottom - kCaseTop), kFloor = 1.5f;

    float window (V p) const
    {
        const auto cx = 0.5f * (kMeterLeft + kMeterRight) - at.x, cy = 0.5f * (kMeterTop + kMeterBottom) - at.y;
        return roundRect (p.x - cx, p.y - cy, 0.5f * (kMeterRight - kMeterLeft), 0.5f * (kMeterBottom - kMeterTop), 3.0f);
    }

    float distance (V p) const override
    {
        const auto slope = 3.0f * clamp01 (p.z / kCaseHeight);
        const auto body = raise (roundRect (p.x, p.y, halfX - slope, halfY - slope, 3.0f) * 0.95f, p.z, kCaseHeight, 1.2f);
        return std::fmax (body, -std::fmax (window (p), kFloor - p.z));
    }

    Material material (V p) const override
    {
        if (p.z < kFloor + 0.05f && window (p) < 0.3f)
            return Material::face;

        // A grey frame round the window, as on the original's case.
        return window (p) < 3.2f && p.z > kCaseHeight - 1.5f ? Material::rim : Material::bezel;
    }
};

/** The meter's zero screw, on its case under the window. */
struct ZeroScrew final : Part
{
    ZeroScrew() { at = { kZeroScrewX, kZeroScrewY, 0.0f }; reach = kZeroScrewRadius + 1.0f; top = kCaseHeight + 1.8f; }

    float distance (V p) const override
    {
        const auto head = roundCylinder (p - V { 0, 0, kCaseHeight + 0.6f }, kZeroScrewRadius, 0.6f, 0.5f);
        const auto along = p.x * 0.8f + p.y * 0.6f, across = -p.x * 0.6f + p.y * 0.8f;
        return std::fmax (head, -roundBox ({ along, across, p.z - kCaseHeight - 1.3f }, { kZeroScrewRadius - 1.0f, 0.9f, 0.6f }, 0.2f));
    }

    Material material (V) const override { return Material::blackSteel; }
};

//==============================================================================
/** What the camera sees at a point: how it reflects, and what it gives off. */
struct Surface
{
    V albedo, f0;
    float rough = 0.5f;
    bool metal = false;
    V emission;
};

struct Scene
{
    std::vector<const Part*> parts;
    bool backlit = false;               // the meter's lamp, under the window, lit
    const std::vector<float>* ink = nullptr;
    int inkWidth = 0, inkHeight = 0;
};

struct Hit
{
    float t = 1.0e30f;
    V p, n;
    Material material = Material::panel;
    const Part* part = nullptr;
};

/** The ray's way into a part's cylinder: from t0 to t1, or nothing. */
bool enter (const Part& part, V o, V d, float& t0, float& t1)
{
    t0 = 0.0f;
    t1 = 1.0e30f;

    if (std::fabs (d.z) < 1.0e-8f)
    {
        if (o.z < 0.0f || o.z > part.top)
            return false;
    }
    else
    {
        auto a = (0.0f - o.z) / d.z, b = (part.top - o.z) / d.z;

        if (a > b)
            std::swap (a, b);

        t0 = std::fmax (t0, a);
        t1 = std::fmin (t1, b);
    }

    const auto ox = o.x - part.at.x, oy = o.y - part.at.y;
    const auto a = d.x * d.x + d.y * d.y, b = 2.0f * (ox * d.x + oy * d.y), c = ox * ox + oy * oy - part.reach * part.reach;

    if (a < 1.0e-10f)
    {
        if (c > 0.0f)
            return false;
    }
    else
    {
        const auto disc = b * b - 4.0f * a * c;

        if (disc < 0.0f)
            return false;

        const auto s = std::sqrt (disc);
        t0 = std::fmax (t0, (-b - s) / (2.0f * a));
        t1 = std::fmin (t1, (-b + s) / (2.0f * a));
    }

    return t0 < t1;
}

/** The nearest thing along a ray, short of `limit`. */
bool intersect (const Scene& scene, V o, V d, Hit& hit, float limit = 1.0e30f)
{
    hit.t = limit;
    bool found = false;

    // The front: a plane.
    if (d.z < -1.0e-6f)
    {
        const auto t = -o.z / d.z;

        if (t > 1.0e-4f && t < hit.t)
        {
            hit = { t, o + d * t, { 0, 0, 1 }, Material::panel, nullptr };
            found = true;
        }
    }

    for (const auto* part : scene.parts)
    {
        float t0, t1;

        if (! enter (*part, o, d, t0, t1) || t0 > hit.t)
            continue;

        auto t = t0;

        for (int i = 0; i < 400 && t < t1 && t < hit.t; ++i)
        {
            const auto distance = part->distance (o + d * t - part->at);

            if (distance < 0.002f)
            {
                hit.t = t;
                hit.part = part;
                found = true;
                break;
            }

            // The sloping sides and the dome bend the distances a little: step short of them.
            t += std::fmax (distance * 0.7f, 0.001f);
        }
    }

    if (found && hit.part != nullptr)
    {
        const auto* part = hit.part;
        const auto local = o + d * hit.t - part->at;
        constexpr float e = 0.004f;
        auto f = [&] (V q) { return part->distance (q); };
        hit.p = o + d * hit.t;
        hit.n = norm (V { 1, -1, -1 } * f (local + V { e, -e, -e }) + V { -1, -1, 1 } * f (local + V { -e, -e, e })
                      + V { -1, 1, -1 } * f (local + V { -e, e, -e }) + V { 1, 1, 1 } * f (local + V { e, e, e }));
        hit.material = part->material (local);
    }

    return found;
}

//==============================================================================
// The light, as for the photographs of the original: a large softbox
// overhead, which glossy faces tilted up mirror as grey; a smaller, brighter
// one above and to the left for the highlights and the shadows; a strip
// light along the top, a white card on the right, and a studio round them.
struct ConeLight { V direction; float cosMax; V radiance; };

const ConeLight kLights[] = {
    { norm ({ -0.12f, -0.55f, 0.83f }), std::cos (24.0f * kPi / 180.0f), V { 4.6f, 4.6f, 4.5f } },
    { norm ({ -0.45f, -0.6f, 0.66f }), std::cos (9.0f * kPi / 180.0f), V { 26.0f, 25.5f, 24.5f } },
    { norm ({ 0.05f, -0.8f, 0.6f }), std::cos (6.0f * kPi / 180.0f), V { 9.0f, 9.0f, 9.2f } },
    { norm ({ 0.62f, -0.25f, 0.74f }), std::cos (24.0f * kPi / 180.0f), V { 1.0f, 1.03f, 1.08f } },
};

V studio (V d)
{
    const auto up = -d.y;
    auto v = 0.05f + 0.14f * smooth (-0.3f, 0.8f, up) * smooth (-0.2f, 0.4f, d.z);

    // White cards to the left and below, catching the softbox: what chrome
    // and gloss show beside their highlights.
    if (d.x < -0.5f && d.z > 0.1f && d.y < 0.5f)
        v += 0.55f;

    if (d.y > 0.55f && d.z > 0.2f)
        v += 0.12f;

    return { v * 1.02f, v, v * 0.97f };
}

const V kPanelColour { 0.125f, 0.0145f, 0.017f };  // the red of the range, where the original is grey
const V kInkColour { 0.78f, 0.77f, 0.75f };

float inkAt (const Scene& scene, float x, float y)
{
    if (scene.ink == nullptr)
        return 0.0f;

    const auto fx = x * (float) kPhotoScale - 0.5f, fy = y * (float) kPhotoScale - 0.5f;
    const auto ix = (int) std::floor (fx), iy = (int) std::floor (fy);
    auto at = [&] (int i, int j)
    {
        return i < 0 || j < 0 || i >= scene.inkWidth || j >= scene.inkHeight ? 0.0f : (*scene.ink)[(size_t) (j * scene.inkWidth + i)];
    };
    const auto u = fx - (float) ix, v = fy - (float) iy;
    return (at (ix, iy) * (1 - u) + at (ix + 1, iy) * u) * (1 - v) + (at (ix, iy + 1) * (1 - u) + at (ix + 1, iy + 1) * u) * v;
}

Surface surfaceAt (const Scene& scene, Hit& hit)
{
    Surface s;
    const auto p = hit.p;

    switch (hit.material)
    {
        case Material::panel:
        {
            // Satin paint, sprayed: its colour drifts from place to place, a
            // fine grain over it, and the faint orange peel of the coat. The
            // edge of the front is folded, and catches more light.
            const auto drift = 1.0f + 0.07f * (fbm (p * 0.02f) - 0.5f) + 0.16f * (noise (p * 1.7f) - 0.5f);
            constexpr float e = 0.15f;
            const auto q = p * 0.9f;
            const auto gx = noise (q + V { e, 0, 0 }) - noise (q - V { e, 0, 0 }), gy = noise (q + V { 0, e, 0 }) - noise (q - V { 0, e, 0 });
            hit.n = norm ({ -gx * 0.05f, -gy * 0.05f, 1.0f });

            const auto edge = std::fmin (std::fmin (p.x, (float) kWidth - p.x), std::fmin (p.y, (float) kHeight - p.y));
            // Darker towards the corners, as the original's photograph is, its very edge a dark line.
            const auto cx = (p.x / (float) kWidth - 0.5f) * 2.0f, cy = (p.y / (float) kHeight - 0.5f) * 2.0f;
            const auto rim = (edge < 1.5f ? 0.45f : edge < 5.0f ? 1.5f : 1.0f) * (1.0f - 0.18f * (cx * cx + cy * cy));

            const auto ink = inkAt (scene, p.x, p.y);
            s.albedo = mix (kPanelColour * (drift * rim), kInkColour, ink);
            s.f0 = grey (0.035f);
            s.rough = 0.48f - 0.1f * ink;
            break;
        }

        case Material::face:
        {
            // The meter's dark card, the scale printed white on it; the glass
            // over it brightens its top a little.
            const auto ink = inkAt (scene, p.x, p.y);
            const auto sheen = 1.0f - smooth (kMeterTop, kMeterTop + 32.0f, p.y);
            s.albedo = mix (grey (0.005f + 0.014f * sheen), kInkColour, ink);
            s.f0 = grey (0.04f);
            s.rough = 0.5f;

            // The lamp hidden under the window's lower edge, in the middle:
            // it lights the card red from below, brightest just over it,
            // fading as it rises; the white scale catches it most.
            if (scene.backlit)
            {
                const auto dx = (p.x - kBacklightX) / 95.0f, dy = (kMeterBottom + 6.0f - p.y) / 62.0f;
                const auto reach = std::exp (-std::sqrt (dx * dx + dy * dy) * 2.2f);
                const auto hot = std::exp (-std::sqrt (dx * dx + dy * dy) * 9.0f);
                s.emission = V { 1.0f, 0.07f, 0.035f } * ((s.albedo.x * 3.4f + 0.2f) * reach) + V { 1.5f, 0.17f, 0.08f } * hot;
            }
            break;
        }

        case Material::rim:
            s.albedo = grey (0.06f);
            s.f0 = grey (0.04f);
            s.rough = 0.3f;
            break;

        case Material::skirt:
            s.albedo = grey (0.03f);
            s.f0 = grey (0.04f);
            s.rough = 0.38f;
            break;

        case Material::knob:
            // Light grey on top, a little darker down its side.
            s.albedo = grey (0.09f + 0.09f * smooth (0.3f * kKnobRadius, 0.7f * kKnobRadius, p.z));
            s.f0 = grey (0.04f);
            s.rough = 0.42f + 0.04f * noise (p * 0.8f);
            break;

        case Material::pointer:
            s.albedo = grey (0.008f);
            s.f0 = grey (0.04f);
            s.rough = 0.4f;
            break;

        case Material::bezel:
            s.albedo = grey (0.011f);
            s.f0 = grey (0.04f);
            s.rough = 0.32f;
            break;

        case Material::lens:
            s.albedo = { 0.21f, 0.21f, 0.205f };
            s.f0 = grey (0.04f);
            s.rough = 0.4f;
            break;

        case Material::lensLit:
        {
            // The lamp behind the frosted cap, blue as the range's lamps: pale
            // in the middle, deeper towards its edges.
            const auto local = hit.part != nullptr ? p - hit.part->at : p;
            const auto edge = clamp01 (std::fmax (std::fabs (local.x), std::fabs (local.y)) / kCapHalf);
            s.albedo = { 0.04f, 0.09f, 0.2f };
            s.f0 = grey (0.04f);
            s.rough = 0.35f;
            s.emission = mix (V { 0.12f, 0.52f, 1.55f }, V { 0.015f, 0.16f, 0.95f }, smooth (0.25f, 0.95f, edge));
            break;
        }

        case Material::steel:
            s.metal = true;
            s.f0 = { 0.5f, 0.5f, 0.52f };
            s.rough = 0.35f;
            break;

        case Material::blackSteel:
            s.metal = true;
            s.f0 = { 0.16f, 0.16f, 0.17f };
            s.rough = 0.3f;
            break;

        case Material::hollow:
            s.albedo = grey (0.006f);
            s.f0 = grey (0.02f);
            s.rough = 0.9f;
            break;
    }

    return s;
}

//==============================================================================
// Reflection: Lambert under GGX microfacets, Schlick's Fresnel.
V schlick (V f0, float c)
{
    const auto m = std::pow (1.0f - clamp01 (c), 5.0f);
    return f0 + (grey (1.0f) - f0) * m;
}

float ggx (float nh, float a)
{
    const auto a2 = a * a, d = nh * nh * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

float smith (float nv, float a)
{
    return 2.0f * nv / (nv + std::sqrt (a * a + (1.0f - a * a) * nv * nv));
}

float specularChance (const Surface& s, float nv)
{
    if (s.metal)
        return 1.0f;

    const auto fresnel = luminance (schlick (s.f0, nv)), diffuse = luminance (s.albedo) * (1.0f - fresnel);
    return std::fmin (0.95f, std::fmax (0.15f, fresnel / (fresnel + diffuse)));
}

V evaluate (const Surface& s, V n, V wo, V wi)
{
    const auto nl = dot (n, wi), nv = dot (n, wo);

    if (nl <= 0.0f || nv <= 0.0f)
        return {};

    const auto h = norm (wo + wi);
    const auto a = std::fmax (0.002f, s.rough * s.rough);
    const auto f = schlick (s.f0, dot (wi, h));
    const auto spec = f * (ggx (dot (n, h), a) * smith (nl, a) * smith (nv, a) / (4.0f * nl * nv));

    if (s.metal)
        return spec;

    return spec + s.albedo * (1.0f - luminance (schlick (s.f0, nv))) * (1.0f / kPi);
}

float density (const Surface& s, V n, V wo, V wi)
{
    const auto nl = dot (n, wi);

    if (nl <= 0.0f)
        return 0.0f;

    const auto h = norm (wo + wi);
    const auto a = std::fmax (0.002f, s.rough * s.rough);
    const auto chance = specularChance (s, dot (n, wo));
    return chance * ggx (dot (n, h), a) * dot (n, h) / (4.0f * std::fmax (1.0e-6f, dot (wo, h))) + (1.0f - chance) * nl / kPi;
}

V sampleDirection (const Surface& s, V n, V wo, Rng& rng)
{
    V t, b;
    basis (n, t, b);
    const auto u1 = rng.next(), u2 = rng.next();

    if (rng.next() < specularChance (s, dot (n, wo)))
    {
        const auto a = std::fmax (0.002f, s.rough * s.rough);
        const auto tan2 = a * a * u1 / (1.0f - u1 + 1.0e-7f);
        const auto cosT = 1.0f / std::sqrt (1.0f + tan2), sinT = std::sqrt (std::fmax (0.0f, 1.0f - cosT * cosT));
        const auto phi = 2.0f * kPi * u2;
        const auto h = t * (sinT * std::cos (phi)) + b * (sinT * std::sin (phi)) + n * cosT;
        return h * (2.0f * dot (wo, h)) - wo;
    }

    const auto r = std::sqrt (u1), phi = 2.0f * kPi * u2;
    return t * (r * std::cos (phi)) + b * (r * std::sin (phi)) + n * std::sqrt (std::fmax (0.0f, 1.0f - u1));
}

/** A direction in a cone round `axis`, uniformly. */
V sampleCone (V axis, float cosMax, Rng& rng)
{
    V t, b;
    basis (axis, t, b);
    const auto c = 1.0f - rng.next() * (1.0f - cosMax), s = std::sqrt (std::fmax (0.0f, 1.0f - c * c)), phi = 2.0f * kPi * rng.next();
    return t * (s * std::cos (phi)) + b * (s * std::sin (phi)) + axis * c;
}

float coneDensity (float cosMax) { return 1.0f / (2.0f * kPi * (1.0f - cosMax)); }

float balance (float a, float b) { return a * a / (a * a + b * b); }

/** The light a ray brings back. */
V trace (const Scene& scene, V o, V d, Rng& rng)
{
    V radiance, through = grey (1.0f);
    float lastDensity = 0.0f;

    for (int depth = 0; depth < 4; ++depth)
    {
        Hit hit;

        if (! intersect (scene, o, d, hit))
        {
            auto sky = studio (d);

            // The lights themselves, as a ray bounced off a surface finds them.
            for (const auto& light : kLights)
                if (dot (d, light.direction) >= light.cosMax)
                    sky = sky + light.radiance * (depth == 0 ? 1.0f : balance (lastDensity, coneDensity (light.cosMax)));

            radiance = radiance + through * sky;
            break;
        }

        const auto s = surfaceAt (scene, hit);
        auto n = hit.n;
        const auto wo = -d;

        if (dot (n, wo) < 0.0f)
            n = -n;

        // What glows is seen straight on, and lights its surroundings only
        // faintly, through one bounce: enough for the meter's lip.
        radiance = radiance + through * s.emission * (depth == 0 ? 1.0f : 0.5f);

        const auto from = hit.p + n * 0.01f;

        for (const auto& light : kLights)
        {
            const auto wi = sampleCone (light.direction, light.cosMax, rng);
            Hit blocker;

            if (dot (n, wi) <= 0.0f || intersect (scene, from, wi, blocker))
                continue;

            const auto lightDensity = coneDensity (light.cosMax);
            radiance = radiance + through * evaluate (s, n, wo, wi) * light.radiance
                                    * (dot (n, wi) / lightDensity * balance (lightDensity, density (s, n, wo, wi)));
        }

        const auto wi = sampleDirection (s, n, wo, rng);
        const auto pdf = density (s, n, wo, wi);

        if (pdf <= 1.0e-6f || dot (n, wi) <= 0.0f)
            break;

        through = through * evaluate (s, n, wo, wi) * (dot (n, wi) / pdf);

        // A rare path that finds the light through a tight gloss would leave a white speck.
        if (luminance (through) > 4.0f)
            through = through * (4.0f / luminance (through));

        lastDensity = pdf;
        o = from;
        d = wi;
    }

    return radiance;
}


//==============================================================================
/** From light to the screen: a filmic curve (ACES, after K. Narkowicz), then sRGB. */
constexpr float kExposure = 1.0f;

float toScreen (float x)
{
    x *= kExposure;
    const auto film = clamp01 ((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
    return film <= 0.0031308f ? 12.92f * film : 1.055f * std::pow (film, 1.0f / 2.4f) - 0.055f;
}

/** Runs rows (from, to) over every core. */
template <typename Rows>
void everyCore (int count, Rows rows)
{
    std::atomic<int> next { 0 };
    std::vector<std::thread> threads;

    for (unsigned c = 0; c < std::max (1u, std::thread::hardware_concurrency()); ++c)
        threads.emplace_back ([&]
        {
            for (int row; (row = next++) < count;)
                rows (row);
        });

    for (auto& t : threads)
        t.join();
}

uint64_t seedOf (int x, int y, int sample, int salt)
{
    return ((uint64_t) (uint32_t) x * 73856093u) ^ ((uint64_t) (uint32_t) y * 19349663u << 20) ^ ((uint64_t) (uint32_t) sample << 40) ^ (uint64_t) salt * 0x9e3779b97f4a7c15ull;
}

//==============================================================================
/** The lettering of the front and of the meter's card, as a mask the white
    paint is printed with. */
std::vector<float> printLettering()
{
    using namespace juce;
    const int w = kWidth * kPhotoScale, h = kHeight * kPhotoScale;
    Image mask (Image::ARGB, w, h, true, SoftwareImageType());

    {
        Graphics g (mask);
        g.addTransform (AffineTransform::scale ((float) kPhotoScale));
        g.setColour (Colours::white);

        // As on the console: a narrow bold grotesque for the front, a bold
        // serif for the maker's name.
        const Font sans (FontOptions ("Arial", 12.0f, Font::bold));
        const Font serif (FontOptions ("Palatino Linotype", 12.0f, Font::bold));

        auto text = [&] (const String& s, float x, float y, float height, const Font& font = Font (FontOptions ("Arial", 12.0f, Font::bold)),
                         float narrow = 0.86f, float tracking = 0.0f)
        {
            g.setFont (font.withHeight (height).withHorizontalScale (narrow).withExtraKerningFactor (tracking));
            g.drawText (s, Rectangle<float> (300.0f, height * 1.6f).withCentre ({ x, y }), Justification::centred, false);
        };

        auto around = [] (float cx, float cy, float degrees, float radius)
        {
            const auto a = degrees * kPi / 180.0f;
            return Point<float> (cx + radius * std::sin (a), cy - radius * std::cos (a));
        };

        // The maker.
        text ("ONIRIQUE", 210.0f, 29.0f, 28.0f, serif, 1.0f, 0.04f);

        // The rules: the frame round the compressor, open at the top, named at
        // its foot; the rule over the fade; the bracket round it.
        const auto stroke = PathStrokeType (1.6f);
        auto ruleGap = [&] (const String& s, float height)
        {
            return 0.5f * GlyphArrangement::getStringWidth (sans.withHeight (height).withHorizontalScale (0.86f), s) + 5.0f;
        };

        {
            const auto gap = ruleGap ("STEREO COMPRESSOR", 17.0f);
            Path frame;
            frame.startNewSubPath (210.0f - gap, kCompressorRuleY);
            frame.lineTo (kRuleLeft + kRuleCorner, kCompressorRuleY);
            frame.quadraticTo (kRuleLeft, kCompressorRuleY, kRuleLeft, kCompressorRuleY - kRuleCorner);
            frame.lineTo (kRuleLeft, kFrameTop);
            frame.startNewSubPath (210.0f + gap, kCompressorRuleY);
            frame.lineTo (kRuleRight - kRuleCorner, kCompressorRuleY);
            frame.quadraticTo (kRuleRight, kCompressorRuleY, kRuleRight, kCompressorRuleY - kRuleCorner);
            frame.lineTo (kRuleRight, kFrameTop);
            g.strokePath (frame, stroke);
            text ("STEREO COMPRESSOR", 210.0f, kCompressorRuleY, 17.0f);
        }

        {
            const auto gap = ruleGap ("ONIRIQUE GLUE", 16.0f);
            g.fillRect (Rectangle<float> (kRuleLeft + 2.0f, kGlueRuleY - 0.8f, 210.0f - gap - kRuleLeft - 2.0f, 1.6f));
            g.fillRect (Rectangle<float> (210.0f + gap, kGlueRuleY - 0.8f, kRuleRight - 2.0f - 210.0f - gap, 1.6f));
            text ("ONIRIQUE GLUE", 210.0f, kGlueRuleY, 16.0f);
        }

        {
            // The bracket, the house's mark in a diamond at its foot.
            constexpr float dx = 25.0f, dy = 17.0f;
            Path bracket;
            bracket.startNewSubPath (210.0f - dx - 3.0f, kBracketY);
            bracket.lineTo (kRuleLeft + kRuleCorner, kBracketY);
            bracket.quadraticTo (kRuleLeft, kBracketY, kRuleLeft, kBracketY - kRuleCorner);
            bracket.lineTo (kRuleLeft, kBracketTop);
            bracket.startNewSubPath (210.0f + dx + 3.0f, kBracketY);
            bracket.lineTo (kRuleRight - kRuleCorner, kBracketY);
            bracket.quadraticTo (kRuleRight, kBracketY, kRuleRight, kBracketY - kRuleCorner);
            bracket.lineTo (kRuleRight, kBracketTop);

            Path diamond;
            diamond.addTriangle (210.0f - dx, kBracketY, 210.0f, kBracketY - dy, 210.0f + dx, kBracketY);
            diamond.addTriangle (210.0f - dx, kBracketY, 210.0f, kBracketY + dy, 210.0f + dx, kBracketY);
            Path outline;
            outline.startNewSubPath (210.0f - dx, kBracketY);
            outline.lineTo (210.0f, kBracketY - dy);
            outline.lineTo (210.0f + dx, kBracketY);
            outline.lineTo (210.0f, kBracketY + dy);
            outline.closeSubPath();
            g.strokePath (bracket, stroke);
            g.strokePath (outline, PathStrokeType (1.4f));

            const auto sigil = ImageCache::getFromMemory (PhotoData::sigil_png, PhotoData::sigil_pngSize);
            const auto sw = 24.0f, sh = sw * (float) sigil.getHeight() / (float) sigil.getWidth();
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (sigil, Rectangle<float> (sw, sh).withCentre ({ 210.0f, kBracketY }));
        }

        // The rate knob's rule over to the fade button.
        g.fillRect (Rectangle<float> (186.0f, 771.2f, 64.0f, 1.6f));

        // The meter's card: its scale on an arc round the pivot, white.
        for (int db = 0; db <= (int) kMeterMaxDb; db += 4)
        {
            const auto degrees = meterDegrees ((float) db);
            g.drawLine (Line<float> (around (kPivotX, kPivotY, degrees, kTickInner), around (kPivotX, kPivotY, degrees, kTickOuter)), 1.7f);

            // The figures lean with the scale.
            const auto at = around (kPivotX, kPivotY, degrees, kNumberRadius);
            const Graphics::ScopedSaveState state (g);
            g.addTransform (AffineTransform::rotation (degrees * kPi / 180.0f, at.x, at.y));
            text (String (db), at.x, at.y, 16.5f, Font (FontOptions ("Arial", 12.0f, Font::plain)), 0.9f);
        }

        text ("dB", kPivotX - 1.0f, 135.0f, 13.5f, Font (FontOptions ("Arial", 12.0f, Font::plain)), 0.9f);
        text ("COMPRESSION", kPivotX, 151.0f, 14.0f, Font (FontOptions ("Arial", 12.0f, Font::plain)), 0.9f);

        // The knobs: a dot for each step round them, figures at the marked
        // ones, their names below.
        auto control = [] (const char* id) -> const Control&
        {
            for (const auto& c : kControls)
                if (juce::String (c.id) == id)
                    return c;

            return kControls[0];
        };

        auto dot = [&] (Point<float> at) { g.fillEllipse (Rectangle<float> (3.4f, 3.4f).withCentre (at)); };

        // Eleven steps, figures in place of the dots at the ends and the middle.
        auto elevenSteps = [&] (const char* id, const char* low, const char* middle, const char* high)
        {
            const auto& c = control (id);

            for (int i = 0; i <= 10; ++i)
            {
                const auto degrees = c.fromDegrees + (float) i * 0.1f * (c.toDegrees - c.fromDegrees);

                if (i == 0 || i == 5 || i == 10)
                {
                    const auto at = around (c.x, c.y, degrees, i == 5 ? 35.0f : 37.0f);
                    text (i == 0 ? low : i == 5 ? middle : high, at.x + (i == 0 ? -3.0f : i == 10 ? 3.0f : 0.0f), at.y + (i == 5 ? 0.0f : 3.0f), 12.5f);
                }
                else
                {
                    dot (around (c.x, c.y, degrees, 33.0f));
                }
            }
        };

        // Figures at each step, a dot beside each.
        auto marked = [&] (const char* id, std::initializer_list<std::pair<float, const char*>> marks)
        {
            const auto& c = control (id);

            for (const auto& [degrees, label] : marks)
            {
                dot (around (c.x, c.y, degrees, 32.0f));
                const auto at = around (c.x, c.y, degrees, String (label).length() > 2 ? 46.0f : 42.0f);
                text (label, at.x, at.y, 12.5f);
            }
        };

        elevenSteps ("threshold", "-15", "0", "+15");
        elevenSteps ("makeup", "0", "7.5", "+15");
        elevenSteps ("scfilter", "OFF", "80", "500");
        elevenSteps ("mix", "0", "50", "100");

        marked ("attack", { { -80.0f, ".1" }, { -48.0f, ".3" }, { -16.0f, "1" }, { 16.0f, "3" }, { 48.0f, "10" }, { 80.0f, "30" } });
        marked ("release", { { -63.0f, ".1" }, { -31.5f, ".3" }, { 0.0f, ".6" }, { 31.5f, "1.2" }, { 63.0f, "Auto" } });
        marked ("ratio", { { -30.0f, "2" }, { 0.0f, "4" }, { 30.0f, "10" } });

        {
            const auto& c = control ("rate");

            for (int i = 0; i < kNumRateMarks; ++i)
            {
                const auto degrees = kRateMarkDegrees[i];
                const auto end = i == 0 || i == kNumRateMarks - 1;
                dot (around (c.x, c.y, degrees, 32.0f));
                const auto at = around (c.x, c.y, degrees, end ? 37.0f : 43.0f);
                text (String (kRateMarks[i], 0), at.x + (i == 0 ? -7.0f : end ? 7.0f : 0.0f), at.y, 12.5f);
            }
        }

        auto name = [&] (const char* id, const String& s, float below)
        {
            const auto& c = control (id);
            text (s, c.x, c.y + below, 15.5f);
        };

        name ("threshold", "THRESHOLD", 43.0f);
        name ("makeup", "MAKE-UP", 43.0f);
        name ("attack", "ATTACK -mS", 43.0f);
        name ("release", "RELEASE -S", 43.0f);
        name ("ratio", "RATIO", 44.0f);
        name ("scfilter", "SC FILTER -Hz", 45.0f);
        name ("headroom", "HR -dB", 29.0f);
        name ("mix", "MIX -%", 45.0f);
        name ("rate", "RATE -S", 49.0f);

        // The ratio's centre mark, under its knob.
        g.fillRect (Rectangle<float> (127.2f, 530.0f, 1.6f, 6.0f));
    }

    std::vector<float> coverage ((size_t) (w * h));
    const Image::BitmapData data (mask, Image::BitmapData::readOnly);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            coverage[(size_t) (y * w + x)] = (float) data.getPixelColour (x, y).getAlpha() / 255.0f;

    return coverage;
}

/** The whole front, with its meter, its lamp off or lit. */
juce::Image photographFront (bool lit, int samples, const std::vector<float>& ink)
{
    Scene scene;
    scene.ink = &ink;
    scene.inkWidth = kWidth * kPhotoScale;
    scene.inkHeight = kHeight * kPhotoScale;

    const MeterCase meterCase;
    const ZeroScrew zeroScrew;
    scene.backlit = lit;
    scene.parts = { &meterCase, &zeroScrew };

    const int w = scene.inkWidth, h = scene.inkHeight;
    juce::Image image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    const auto side = (int) std::ceil (std::sqrt ((float) samples));

    everyCore (h, [&] (int y)
    {
        for (int x = 0; x < w; ++x)
        {
            V sum;

            for (int s = 0; s < side * side; ++s)
            {
                Rng rng (seedOf (x, y, s, lit ? 2 : 1));
                const auto px = ((float) x + ((float) (s % side) + rng.next()) / (float) side) / (float) kPhotoScale;
                const auto py = ((float) y + ((float) (s / side) + rng.next()) / (float) side) / (float) kPhotoScale;
                sum = sum + trace (scene, { px, py, 200.0f }, { 0, 0, -1 }, rng);
            }

            const auto c = sum * (1.0f / (float) (side * side));
            data.setPixelColour (x, y, juce::Colour::fromFloatRGBA (toScreen (c.x), toScreen (c.y), toScreen (c.z), 1.0f));
        }
    });

    return image;
}

std::unique_ptr<Part> makePart (Kind kind, int frame)
{
    const auto angle = (float) frame * kDegreesPerFrame * kPi / 180.0f;

    switch (kind)
    {
        case Kind::knob:   return std::make_unique<Knob> (kKnobRadius, angle);
        case Kind::trim:   return std::make_unique<Trim> (angle);
        case Kind::button: return std::make_unique<Button> (frame == 1);
    }

    return {};
}

/** Every frame of a run, on a plain front: the part itself, and the shadow
    it casts as a darkening of whatever lies under it. Laid out in a grid,
    eight to a row. */
juce::Image photographRun (const Run& run, int samples)
{
    const auto frame = frameOf (run.kind);
    const auto pixels = (int) std::lround (frame.size() * (float) kPhotoScale);
    const auto columns = std::min (run.frames, 8), rows = (run.frames + columns - 1) / columns;

    juce::Image image (juce::Image::ARGB, columns * pixels, rows * pixels, true, juce::SoftwareImageType());
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    const auto side = (int) std::ceil (std::sqrt ((float) samples));
    const Scene bare;

    for (int k = 0; k < run.frames; ++k)
    {
        const auto part = makePart (run.kind, k);
        Scene scene;
        scene.parts.push_back (part.get());

        const auto left = (k % columns) * pixels, top = (k / columns) * pixels;

        everyCore (pixels, [&] (int y)
        {
            for (int x = 0; x < pixels; ++x)
            {
                V onPart, withIt, without;
                int hits = 0, misses = 0;

                for (int s = 0; s < side * side; ++s)
                {
                    Rng rng (seedOf (x, y, s, 100 + k));
                    const auto px = -frame.before + ((float) x + ((float) (s % side) + rng.next()) / (float) side) / (float) kPhotoScale;
                    const auto py = -frame.before + ((float) y + ((float) (s / side) + rng.next()) / (float) side) / (float) kPhotoScale;
                    const V o { px, py, 200.0f }, d { 0, 0, -1 };
                    Hit first;

                    if (intersect (scene, o, d, first) && first.part != nullptr)
                    {
                        onPart = onPart + trace (scene, o, d, rng);
                        ++hits;
                    }
                    else
                    {
                        // The same path with the part and without it: their ratio is the shadow.
                        Rng twin = rng;
                        withIt = withIt + trace (scene, o, d, rng);
                        without = without + trace (bare, o, d, twin);
                        ++misses;
                    }
                }

                const auto cover = (float) hits / (float) (side * side);
                V colour;

                if (hits > 0)
                {
                    const auto c = onPart * (1.0f / (float) hits);
                    colour = { toScreen (c.x), toScreen (c.y), toScreen (c.z) };
                }

                auto shadow = 0.0f;

                if (misses > 0)
                {
                    const auto w = withIt * (1.0f / (float) misses), wo = without * (1.0f / (float) misses);
                    const auto shown = luminance ({ toScreen (w.x), toScreen (w.y), toScreen (w.z) });
                    const auto bright = luminance ({ toScreen (wo.x), toScreen (wo.y), toScreen (wo.z) });
                    shadow = bright > 1.0e-4f ? clamp01 (1.0f - shown / bright) : 0.0f;
                }

                const auto alpha = cover + (1.0f - cover) * shadow;
                auto byte = [] (float v) { return (juce::uint8) std::lround (255.0f * clamp01 (v)); };

                reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (left + x, top + y))
                    ->setARGB (byte (alpha), byte (cover * colour.x), byte (cover * colour.y), byte (cover * colour.z));
            }
        });

        std::printf ("    %s %d/%d\n", run.photo, k + 1, run.frames);
        std::fflush (stdout);
    }

    return image;
}

bool save (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);

    if (file.hasFileExtension ("jpg"))
    {
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality (0.93f);
        return stream.openedOk() && jpeg.writeImageToStream (image, stream);
    }

    return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 2)
    {
        std::printf ("glue_photo <folder> [quality] [names]\n");
        return 1;
    }

    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const auto quality = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    const auto only = argc > 3 ? juce::StringArray::fromTokens (argv[3], ",", {}) : juce::StringArray();
    auto wanted = [&] (const char* name) { return only.isEmpty() || only.contains (name); };
    folder.createDirectory();

    const auto start = juce::Time::getMillisecondCounterHiRes();
    auto elapsed = [&] { return (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0; };

    if (wanted ("front"))
    {
        const auto samples = std::max (1, (int) std::lround (64.0f * quality));
        const auto ink = printLettering();

        for (const auto lit : { false, true })
        {
            const auto name = lit ? "front-on.jpg" : "front-off.jpg";
            save (photographFront (lit, samples, ink), folder.getChildFile (name));
            std::printf ("%s (%.0f s)\n", name, elapsed());
            std::fflush (stdout);
        }
    }

    for (const auto& run : kRuns)
    {
        if (! wanted (run.photo))
            continue;

        const auto samples = std::max (1, (int) std::lround (196.0f * quality));
        save (photographRun (run, samples), folder.getChildFile (juce::String (run.photo) + ".png"));
        std::printf ("%s.png (%.0f s)\n", run.photo, elapsed());
        std::fflush (stdout);
    }

    if (only.contains ("preview"))
    {
        auto front = juce::ImageCache::getFromFile (folder.getChildFile ("front-on.jpg"));
        juce::Image preview (juce::Image::RGB, front.getWidth(), front.getHeight(), false, juce::SoftwareImageType());
        juce::Graphics g (preview);
        g.drawImageAt (front, 0, 0);

        for (const auto& control : kControls)
        {
            const Run* run = nullptr;

            for (const auto& r : kRuns)
                if (r.kind == control.kind)
                    run = &r;

            const auto strip = juce::ImageCache::getFromFile (folder.getChildFile (juce::String (run->photo) + ".png"));
            const auto frame = frameOf (control.kind);
            const auto pixels = (int) std::lround (frame.size() * (float) kPhotoScale);
            const auto degrees = control.fromDegrees + 0.66f * (control.toDegrees - control.fromDegrees);
            const auto k = control.kind == Kind::button ? 1 : ((int) std::lround (degrees / kDegreesPerFrame) + run->frames) % run->frames;
            const auto columns = std::min (run->frames, 8);

            if (strip.isValid())
                g.drawImage (strip, (int) std::lround ((control.x - frame.before) * (float) kPhotoScale),
                             (int) std::lround ((control.y - frame.before) * (float) kPhotoScale), pixels, pixels,
                             (k % columns) * pixels, (k / columns) * pixels, pixels, pixels);
        }

        save (preview, folder.getChildFile ("preview.png"));
    }

    return 0;
}
