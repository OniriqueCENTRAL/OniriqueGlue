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
enum class Material { panel, face, plastic, cap, paint, aluminium, bezel, lens, lensLit, nickel, hollow };

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


/** A console knob: a thin brushed aluminium skirt at its foot, a tall
    knurled body of dark grey plastic, a dished cap on top, and the white
    pointer line across the cap and down onto the skirt. */
struct Knob final : Part
{
    Knob (float radius, float a) : R (radius), angle (a) { reach = R + 1.0f; top = 0.84f * R; }

    float distance (V p) const override
    {
        const Turned t (p, angle);
        const auto skirt = roundCylinder (p - V { 0, 0, 0.07f * R }, R, 0.07f * R, 0.035f * R);
        const auto rib = 0.012f * R * std::cos (32.0f * t.theta);
        auto body = roundCylinder (p - V { 0, 0, 0.4f * R }, 0.72f * R + rib, 0.4f * R, 0.1f * R);

        // The cap's dish: a shallow hollow, as a large sphere just touching it.
        body = std::fmax (body, -(length (p - V { 0, 0, 3.77f * R }) - 3.0f * R));
        return smin (skirt, body, 0.04f * R);
    }

    Material material (V p) const override
    {
        const Turned t (p, angle);
        const auto onLine = std::fabs (t.across) < 0.05f * R && t.along > 0.0f;

        if (p.z > 0.74f * R && t.radius < 0.66f * R)
            return onLine && t.along > 0.14f * R ? Material::paint : Material::cap;

        if (p.z < 0.16f * R && t.radius > 0.76f * R)
            return onLine ? Material::paint : Material::aluminium;

        return Material::plastic;
    }

    float R, angle;
};

/** A square push button: its cap of cream plastic, lit from behind when it is
    in, sitting a little lower then, in a black bezel. */
struct Button final : Part
{
    explicit Button (bool on) : lit (on) { reach = 1.5f * H + 2.0f; top = 12.0f; }

    static constexpr float H = kButtonHalf;

    float bezel (V p) const
    {
        const auto outer = roundBox (p - V { 0, 0, 1.6f }, { H + 4.0f, H + 4.0f, 1.6f }, 1.2f);
        const auto hole = roundBox (p, { H + 0.9f, H + 0.9f, 6.0f }, 1.8f);
        return std::fmax (outer, -hole);
    }

    float cap (V p) const
    {
        return roundBox (p - V { 0, 0, (lit ? 4.0f : 5.5f) }, { H, H, 5.0f }, 2.4f);
    }

    float distance (V p) const override { return std::fmin (bezel (p), cap (p)); }
    Material material (V p) const override { return cap (p) < bezel (p) ? (lit ? Material::lensLit : Material::lens) : Material::bezel; }

    bool lit;
};

/** A slotted, domed nickel screw. */
struct ScrewPart final : Part
{
    explicit ScrewPart (const Screw& s) : slot (s.slot) { at = { s.x, s.y, 0.0f }; reach = kScrewRadius + 1.0f; top = 4.0f; }

    float distance (V p) const override
    {
        auto head = std::fmax (roundCylinder (p - V { 0, 0, 1.5f }, kScrewRadius, 1.5f, 1.3f), length (p - V { 0, 0, -16.0f }) - 19.0f);
        const auto along = p.x * std::cos (slot) + p.y * std::sin (slot), across = -p.x * std::sin (slot) + p.y * std::cos (slot);
        return std::fmax (head, -roundBox ({ along, across, p.z - 3.3f }, { 10.2f, 0.9f, 2.2f }, 0.3f));
    }

    Material material (V) const override { return Material::nickel; }

    float slot;
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
    bool meter = false;                 // the meter's window cut through the front, its face behind
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

constexpr float kFaceDepth = 7.0f;

bool inMeter (float x, float y)
{
    // The window's corners rounded a little.
    constexpr float r = 6.0f;
    const auto dx = std::fmax (0.0f, std::fmax (kMeterLeft + r - x, x - (kMeterRight - r)));
    const auto dy = std::fmax (0.0f, std::fmax (kMeterTop + r - y, y - (kMeterBottom - r)));
    return x > kMeterLeft && x < kMeterRight && y > kMeterTop && y < kMeterBottom && dx * dx + dy * dy < r * r;
}

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

    // The front: a plane, cut through at the meter, its lit face behind.
    if (d.z < -1.0e-6f)
    {
        const auto t = -o.z / d.z;

        if (t > 1.0e-4f && t < hit.t)
        {
            const auto p = o + d * t;

            if (scene.meter && inMeter (p.x, p.y))
            {
                const auto behind = (-kFaceDepth - o.z) / d.z;

                if (behind < hit.t)
                {
                    hit = { behind, o + d * behind, { 0, 0, 1 }, Material::face, nullptr };
                    found = true;
                }
            }
            else
            {
                hit = { t, p, { 0, 0, 1 }, Material::panel, nullptr };
                found = true;
            }
        }
    }

    // From the face, up through the front: only the window lets it out.
    if (scene.meter && o.z < -1.0e-3f && d.z > 1.0e-6f)
    {
        const auto t = -o.z / d.z;
        const auto p = o + d * t;

        if (t < hit.t && ! inMeter (p.x, p.y))
        {
            hit = { t, p, { 0, 0, -1 }, Material::hollow, nullptr };
            found = true;
        }
    }

    for (const auto* part : scene.parts)
    {
        float t0, t1;

        if (! enter (*part, o, d, t0, t1) || t0 > hit.t)
            continue;

        auto t = t0;

        for (int i = 0; i < 300 && t < t1 && t < hit.t; ++i)
        {
            const auto distance = part->distance (o + d * t - part->at);

            if (distance < 0.002f)
            {
                hit.t = t;
                hit.part = part;
                found = true;
                break;
            }

            // The knurls and the dish bend the distances a little: step short of them.
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

const V kPanelColour { 0.058f, 0.058f, 0.062f };   // the console's dark grey
const V kInkColour { 0.74f, 0.73f, 0.7f };
const V kFaceColour { 0.78f, 0.65f, 0.4f };         // the meter's cream card
const V kFaceInk { 0.025f, 0.022f, 0.02f };

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
            // Satin paint: its colour drifts a little from place to place, and
            // its surface has the faint orange peel of a sprayed coat.
            const auto drift = 1.0f + 0.07f * (fbm (p * 0.02f) - 0.5f);
            constexpr float e = 0.15f;
            const auto q = p * 0.9f;
            const auto gx = noise (q + V { e, 0, 0 }) - noise (q - V { e, 0, 0 }), gy = noise (q + V { 0, e, 0 }) - noise (q - V { 0, e, 0 });
            hit.n = norm ({ -gx * 0.05f, -gy * 0.05f, 1.0f });

            const auto ink = inkAt (scene, p.x, p.y);
            s.albedo = mix (kPanelColour * drift, kInkColour, ink);
            s.f0 = grey (0.035f);
            s.rough = 0.62f - 0.1f * ink;
            break;
        }

        case Material::face:
        {
            // The card behind the window, printed black, lit from behind
            // through the paper: warm, brightest in the middle.
            const auto ink = inkAt (scene, p.x, p.y);
            const auto dx = (p.x - 0.5f * (kMeterLeft + kMeterRight)) / (kMeterRight - kMeterLeft);
            const auto dy = (p.y - 0.5f * (kMeterTop + kMeterBottom)) / (kMeterBottom - kMeterTop);
            const auto glow = 1.0f - 0.9f * (dx * dx + dy * dy);
            s.albedo = mix (kFaceColour * (1.0f + 0.04f * (fbm (p * 0.08f) - 0.5f)), kFaceInk, ink);
            s.f0 = grey (0.03f);
            s.rough = 0.85f;
            s.emission = V { 0.56f, 0.38f, 0.15f } * (glow * (1.0f - 0.97f * ink));
            break;
        }

        case Material::plastic:
            s.albedo = grey (0.028f);
            s.f0 = grey (0.04f);
            s.rough = 0.36f + 0.05f * noise (p * 0.6f);
            break;

        case Material::cap:
            s.albedo = grey (0.04f);
            s.f0 = grey (0.04f);
            s.rough = 0.28f;
            break;

        case Material::paint:
            s.albedo = { 0.82f, 0.81f, 0.78f };
            s.f0 = grey (0.04f);
            s.rough = 0.4f;
            break;

        case Material::aluminium:
        {
            // Brushed round its centre: the grain shows as a faint ring pattern.
            const auto r = std::sqrt (p.x * p.x + p.y * p.y);
            s.metal = true;
            s.f0 = V { 0.78f, 0.78f, 0.8f } * (0.92f + 0.08f * noise ({ r * 6.0f, 0.0f, 0.0f }));
            s.rough = 0.3f;
            break;
        }

        case Material::bezel:
            s.albedo = grey (0.012f);
            s.f0 = grey (0.04f);
            s.rough = 0.3f;
            break;

        case Material::lens:
        case Material::lensLit:
        {
            // Frosted cream plastic: when lit, the lamp behind it glows
            // through, strongest in the middle.
            const auto lit = hit.material == Material::lensLit;
            const auto local = hit.part != nullptr ? p - hit.part->at : p;
            const auto edge = std::fmax (std::fabs (local.x), std::fabs (local.y)) / kButtonHalf;
            s.albedo = lit ? V { 0.85f, 0.64f, 0.28f } : V { 0.42f, 0.38f, 0.3f };
            s.f0 = grey (0.04f);
            s.rough = 0.24f;
            s.emission = lit ? V { 1.6f, 0.98f, 0.28f } * (1.15f - 0.45f * edge * edge) : V { 0.006f, 0.005f, 0.003f };
            break;
        }

        case Material::nickel:
            s.metal = true;
            s.f0 = { 0.6f, 0.58f, 0.55f };
            s.rough = 0.26f + 0.08f * noise (p * 1.5f);
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
/** The lettering of the front and of the meter's card, as a mask the paint
    is printed with: light on the front, black on the card. */
std::vector<float> printLettering()
{
    using namespace juce;
    const int w = kWidth * kPhotoScale, h = kHeight * kPhotoScale;
    Image mask (Image::ARGB, w, h, true, SoftwareImageType());

    {
        Graphics g (mask);
        g.addTransform (AffineTransform::scale ((float) kPhotoScale));
        g.setColour (Colours::white);

        const auto face = Typeface::createSystemTypefaceFor (PhotoData::HankenGrotesk600_ttf, PhotoData::HankenGrotesk600_ttfSize);

        auto text = [&] (const String& s, float x, float y, float height, float tracking = 0.04f,
                         Justification justification = Justification::centred)
        {
            g.setFont (Font (FontOptions (face).withHeight (height)).withExtraKerningFactor (tracking));
            auto box = Rectangle<float> (240.0f, height * 1.6f).withCentre ({ x, y });

            if (justification == Justification::centredLeft)
                box.setX (x);

            g.drawText (s, box, justification, false);
        };

        auto around = [] (float cx, float cy, float degrees, float radius)
        {
            const auto a = degrees * kPi / 180.0f;
            return Point<float> (cx + radius * std::sin (a), cy - radius * std::cos (a));
        };

        // The maker, and what it is.
        text ("ONIRIQUE", 52.0f, 58.0f, 18.0f, 0.16f, Justification::centredLeft);
        text ("BUS COMPRESSOR", 210.0f, 287.0f, 10.0f, 0.22f);

        // The meter: its window's outline on the front, its scale on the card.
        g.drawRoundedRectangle (Rectangle<float> (kMeterLeft, kMeterTop, kMeterRight - kMeterLeft, kMeterBottom - kMeterTop).expanded (5.0f), 9.0f, 1.2f);

        Path arc;
        arc.addCentredArc (kPivotX, kPivotY, kScaleRadius, kScaleRadius, 0.0f,
                           meterDegrees (0.0f) * kPi / 180.0f, meterDegrees (kMeterMaxDb) * kPi / 180.0f, true);
        g.strokePath (arc, PathStrokeType (1.3f));

        for (int db = 0; db <= (int) kMeterMaxDb; ++db)
        {
            const auto major = db % 4 == 0;
            const auto degrees = meterDegrees ((float) db);
            g.drawLine (Line<float> (around (kPivotX, kPivotY, degrees, kScaleRadius), around (kPivotX, kPivotY, degrees, kScaleRadius + (major ? 10.0f : 5.5f))),
                        major ? 1.5f : 1.0f);

            if (major)
            {
                const auto at = around (kPivotX, kPivotY, degrees, kScaleRadius + 21.0f);
                text (String (db), at.x, at.y, 13.0f, 0.0f);
            }
        }

        text ("dB", kPivotX, 192.0f, 12.0f, 0.04f);
        text ("COMPRESSION", kPivotX, 212.0f, 10.5f, 0.18f);
        text ("ONIRIQUE", kPivotX, 244.0f, 7.5f, 0.3f);

        // The knobs: their marks and values round them, their names below.
        auto marks = [&] (const Control& c, std::initializer_list<std::pair<float, const char*>> values, float radius = 42.0f, float height = 10.0f)
        {
            for (const auto& [proportion, label] : values)
            {
                const auto degrees = c.fromDegrees + proportion * (c.toDegrees - c.fromDegrees);
                const auto dot = around (c.x, c.y, degrees, radius - 11.0f);
                g.fillEllipse (Rectangle<float> (2.2f, 2.2f).withCentre (dot));

                const auto at = around (c.x, c.y, degrees, radius);
                text (label, at.x, at.y, height, 0.0f);
            }
        };

        auto control = [] (const char* id) -> const Control&
        {
            for (const auto& c : kControls)
                if (juce::String (c.id) == id)
                    return c;

            return kControls[0];
        };

        auto name = [&] (const char* id, const String& s, float below = 51.0f)
        {
            const auto& c = control (id);
            text (s, c.x, c.y + below, 10.5f, 0.12f);
        };

        marks (control ("threshold"), { { 0.0f, "-15" }, { 1.0f / 6, "-10" }, { 2.0f / 6, "-5" }, { 0.5f, "0" },
                                         { 4.0f / 6, "+5" }, { 5.0f / 6, "+10" }, { 1.0f, "+15" } });
        marks (control ("makeup"), { { 0.0f, "-5" }, { 0.25f, "0" }, { 0.5f, "+5" }, { 0.75f, "+10" }, { 1.0f, "+15" } });
        marks (control ("attack"), { { 0.0f, glue::kAttackNames[0] }, { 0.2f, glue::kAttackNames[1] }, { 0.4f, glue::kAttackNames[2] },
                                      { 0.6f, glue::kAttackNames[3] }, { 0.8f, glue::kAttackNames[4] }, { 1.0f, glue::kAttackNames[5] } });
        marks (control ("release"), { { 0.0f, glue::kReleaseNames[0] }, { 0.25f, glue::kReleaseNames[1] }, { 0.5f, glue::kReleaseNames[2] },
                                       { 0.75f, glue::kReleaseNames[3] }, { 1.0f, glue::kReleaseNames[4] } });
        marks (control ("ratio"), { { 0.0f, glue::kRatioNames[0] }, { 0.5f, glue::kRatioNames[1] }, { 1.0f, glue::kRatioNames[2] } });

        auto sc = [] (float hz) { return skewedProportion (hz, 0.0f, kScFilterMax, kScFilterCentre); };
        marks (control ("scfilter"), { { 0.0f, "OFF" }, { sc (30.0f), "30" }, { 0.5f, "80" }, { sc (200.0f), "200" }, { 1.0f, "500" } });
        marks (control ("headroom"), { { 0.0f, "4" }, { 1.0f, "24" } }, 21.0f, 8.5f);
        marks (control ("mix"), { { 0.0f, "0" }, { 0.25f, "25" }, { 0.5f, "50" }, { 0.75f, "75" }, { 1.0f, "100" } });

        auto rate = [] (float s) { return skewedProportion (s, kRateMin, kRateMax, kRateCentre); };
        marks (control ("rate"), { { 0.0f, "1" }, { rate (3.0f), "3" }, { 0.5f, "10" }, { rate (30.0f), "30" }, { 1.0f, "60" } });

        name ("threshold", "THRESHOLD");
        name ("makeup", "MAKE-UP");
        name ("attack", "ATTACK - mS");
        name ("release", "RELEASE - S");
        name ("ratio", "RATIO");
        name ("scfilter", "SC FILTER - Hz");
        name ("headroom", "HR - dB", 30.0f);
        name ("mix", "MIX - %");
        name ("rate", "RATE - S");

        // The two sections, each named on a rule across the front.
        auto rule = [&] (const String& s, float y, float height, float tracking)
        {
            g.setFont (Font (FontOptions (face).withHeight (height)).withExtraKerningFactor (tracking));
            const auto half = 0.5f * GlyphArrangement::getStringWidth (g.getCurrentFont(), s) + 12.0f;
            g.fillRect (Rectangle<float> (26.0f, y - 0.6f, 210.0f - half - 26.0f, 1.2f));
            g.fillRect (Rectangle<float> (210.0f + half, y - 0.6f, 394.0f - 210.0f - half, 1.2f));
            text (s, 210.0f, y, height, tracking);
        };

        rule ("STEREO COMPRESSOR", 684.0f, 11.5f, 0.2f);
        rule ("GLUE", 852.0f, 15.0f, 0.4f);

        const auto sigil = ImageCache::getFromMemory (PhotoData::sigil_png, PhotoData::sigil_pngSize);
        const auto sw = 30.0f, sh = sw * (float) sigil.getHeight() / (float) sigil.getWidth();
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (sigil, Rectangle<float> (sw, sh).withCentre ({ 210.0f, 1008.0f }));
    }

    std::vector<float> coverage ((size_t) (w * h));
    const Image::BitmapData data (mask, Image::BitmapData::readOnly);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            coverage[(size_t) (y * w + x)] = (float) data.getPixelColour (x, y).getAlpha() / 255.0f;

    return coverage;
}

/** The whole front, with its screws and its lit meter. */
juce::Image photographFront (int samples, const std::vector<float>& ink)
{
    std::vector<std::unique_ptr<Part>> owned;
    Scene scene;
    scene.meter = true;
    scene.ink = &ink;
    scene.inkWidth = kWidth * kPhotoScale;
    scene.inkHeight = kHeight * kPhotoScale;

    for (const auto& screw : kScrews)
        owned.push_back (std::make_unique<ScrewPart> (screw));

    for (const auto& p : owned)
        scene.parts.push_back (p.get());

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
                Rng rng (seedOf (x, y, s, 1));
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
        case Kind::trim:   return std::make_unique<Knob> (kTrimRadius, angle);
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
        save (photographFront (samples, printLettering()), folder.getChildFile ("front.jpg"));
        std::printf ("front.jpg (%.0f s)\n", elapsed());
        std::fflush (stdout);
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
        auto front = juce::ImageCache::getFromFile (folder.getChildFile ("front.jpg"));
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
