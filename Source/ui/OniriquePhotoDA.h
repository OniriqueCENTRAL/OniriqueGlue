#pragma once

#include "OniriqueDA.h"

//==============================================================================
//  La DA de VOCALBOX pour les façades photographiées (WEIGHT, MASSIF, GLUE) : les potards et la mécanique restent tels
//  que sur les photos ; autour, le fond nuit et le verre de VOCALBOX, la peinture rouge de la façade qui suit la couleur
//  de l'interface, la lueur sur la pièce touchée et la bulle de valeur.
//==============================================================================
namespace onirique::da
{

/** La peinture rouge d'une photo ramenée au cramoisi de la DA ONIRIQUE (celui de VOCALBOX), dans la couleur de l'interface.
    prepare() repère une fois les pixels rouges (leur place, leur saturation, leur valeur) ; render() les recolore ensuite
    en quelques millisecondes à chaque changement de couleur. Toute la peinture prend la teinte choisie (jusqu'aux reflets) ;
    la saturation et l'assombrissement de la DA viennent en fondu, comme pour le rouge par défaut, quelle que soit la
    couleur. « keep » (en points de la façade) : une fenêtre (le cadran d'un VU) dont seule la teinte change. */
class PhotoTint
{
public:
    /** dial : la fenêtre est un cadran (tout son fond sombre prend la couleur) ; sinon c'est un halo (seule sa teinte change). */
    void prepare (const juce::Image& src, juce::Rectangle<float> keep = {}, float unitsWidth = 0.0f, bool dial = true)
    {
        original = src.isValid() ? src.convertedToFormat (juce::Image::ARGB) : juce::Image();
        pixels.clear();

        if (! original.isValid())
            return;

        const float px = unitsWidth > 0.0f ? (float) original.getWidth() / unitsWidth : 0.0f;
        const auto window = (keep * px).toNearestInt();
        const juce::Image::BitmapData data (original, juce::Image::BitmapData::readOnly);
        pixels.reserve ((size_t) (data.width * data.height / 2));

        for (int y = 0; y < data.height; ++y)
            for (int x = 0; x < data.width; ++x)
            {
                auto c = *reinterpret_cast<const juce::PixelARGB*> (data.getPixelPointer (x, y));
                c.unpremultiply();
                const int r = c.getRed(), g = c.getGreen(), b = c.getBlue();
                const int mx = juce::jmax (r, g, b), mn = juce::jmin (r, g, b);
                if (mx == 0) continue;
                const float v = (float) mx / 255.0f, s = (float) (mx - mn) / (float) mx;
                const bool inWindow = window.contains (x, y);

                // dans la fenêtre (le cadran d'un VU) : tout le fond sombre, gris compris, prend la couleur ; les traits
                // et chiffres clairs, l'aiguille blanche restent tels quels
                if (inWindow && dial)
                {
                    if (v > 0.03f && v < 0.62f && s < 0.9f)
                        pixels.push_back ({ (juce::uint16) x, (juce::uint16) y, (juce::uint8) juce::roundToInt (s * 255.0f),
                                            (juce::uint8) juce::roundToInt (v * 255.0f), 1 });
                    else if (v >= 0.62f && s > 0.2f && r == mx)   // l'éclairage du cadran : sa teinte seulement
                        pixels.push_back ({ (juce::uint16) x, (juce::uint16) y, (juce::uint8) juce::roundToInt (s * 255.0f),
                                            (juce::uint8) juce::roundToInt (v * 255.0f), 2 });
                    continue;
                }

                if (r != mx) continue;   // le rouge domine (la teinte est entre 335 et 25 degrés)
                const float deg = mx == mn ? 0.0f : (g >= b ? 60.0f * (float) (g - b) / (float) (mx - mn)
                                                            : 360.0f - 60.0f * (float) (b - g) / (float) (mx - mn));

                if ((deg <= 25.0f || deg >= 335.0f) && s > 0.12f && v > 0.04f)
                    pixels.push_back ({ (juce::uint16) x, (juce::uint16) y, (juce::uint8) juce::roundToInt (s * 255.0f),
                                        (juce::uint8) juce::roundToInt (v * 255.0f), (juce::uint8) (inWindow ? 2 : 0) });
            }
    }

    juce::Image render() const
    {
        if (! original.isValid())
            return original;

        auto img = original.createCopy();
        const juce::Colour base (onirique::look::swatches[0].argb);
        const auto ember = onirique::look::ember;
        const float hue = ember.getHue();
        // le fondu du rouge par défaut pour chaque couleur ; seul l'argent (presque sans couleur) reste gris
        const float k = ember.getSaturation() < 0.2f ? ember.getSaturation() / juce::jmax (0.01f, base.getSaturation()) : 1.0f;
        juce::Image::BitmapData data (img, juce::Image::BitmapData::readWrite);

        for (const auto& p : pixels)
        {
            const float s = (float) p.s / 255.0f, v = (float) p.v / 255.0f;
            float sat = s, val = v;

            if (p.window == 2)
            {
                // un halo : sa teinte seulement, il garde son éclat
            }
            else if (p.window == 1)
            {
                // le cadran : une teinte égale, assez saturée pour que le haut (gris sur la photo) ne paraisse plus terne
                sat = juce::jlimit (0.0f, 1.0f, juce::jmax (s, 0.55f) * k);
            }
            else
            {
                const float w = juce::jlimit (0.0f, 1.0f, (s - 0.24f) / 0.12f);
                sat = juce::jlimit (0.0f, 1.0f, s + ((0.55f + 0.6f * s) * k - s) * w);
                val = v * (1.0f - 0.2f * w);
            }

            auto* d = reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (p.x, p.y));
            const auto a = d->getAlpha();
            juce::uint8 r, g, b;
            hsvToRgb (hue, sat, val, r, g, b);
            d->setARGB (a, r, g, b);
            d->premultiply();
        }

        return img;
    }

private:
    static void hsvToRgb (float h, float s, float v, juce::uint8& r, juce::uint8& g, juce::uint8& b) noexcept
    {
        const float hh = (h - std::floor (h)) * 6.0f;
        const int i = (int) hh;
        const float f = hh - (float) i, p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
        float rr = v, gg = t, bb = p;
        switch (i) { case 1: rr = q; gg = v; bb = p; break; case 2: rr = p; gg = v; bb = t; break;
                     case 3: rr = p; gg = q; bb = v; break; case 4: rr = t; gg = p; bb = v; break;
                     case 5: rr = v; gg = p; bb = q; break; default: break; }
        r = (juce::uint8) juce::roundToInt (rr * 255.0f); g = (juce::uint8) juce::roundToInt (gg * 255.0f); b = (juce::uint8) juce::roundToInt (bb * 255.0f);
    }

    struct Pixel { juce::uint16 x, y; juce::uint8 s, v, window; };
    juce::Image original;
    std::vector<Pixel> pixels;
};

/** Pour un usage ponctuel : préparer et recolorer en une fois. */
inline juce::Image tintPhoto (const juce::Image& src, juce::Rectangle<float> keep = {}, float unitsWidth = 0.0f)
{
    PhotoTint t;
    t.prepare (src, keep, unitsWidth);
    return t.render();
}

/** La façade posée sur le fond nuit comme un panneau de verre : son ombre douce et son liseré. */
inline void photoCard (juce::Graphics& g, juce::Rectangle<float> r, float corner, float u)
{
    glass (g, r.expanded (5.0f * u), corner + 5.0f * u, u, true);
}

/** La lueur de VOCALBOX sur la pièce que la main touche (amount 0..1, qui s'allume et s'éteint en douceur). */
inline void partGlow (juce::Graphics& g, juce::Point<float> c, float radius, float amount)
{
    if (amount <= 0.01f)
        return;

    juce::Path ring;
    ring.addEllipse (juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (c));
    softGlow (g, ring, onirique::look::ember.withAlpha (0.38f * amount), radius * 0.35f);   // un halo doux, sans contour
}

/** La bulle de valeur de VOCALBOX : verre sombre, liseré rosé, texte en mono. */
inline void valueBubble (juce::Graphics& g, juce::Rectangle<float> pill, const juce::String& text, const juce::Font& font, juce::Colour ink)
{
    const float r = pill.getHeight() * 0.5f;
    juce::Path p;
    p.addRoundedRectangle (pill, r);
    softGlow (g, p, juce::Colours::black.withAlpha (0.55f), 10.0f, 2.0f);
    g.setColour (juce::Colour (0xee0d0a0c));
    g.fillPath (p);
    glass (g, pill, r, 1.0f, false);
    g.setColour (onirique::look::emberPink.withAlpha (0.22f));
    g.strokePath (p, juce::PathStrokeType (1.0f));
    g.setColour (ink);
    g.setFont (font);
    g.drawText (text, pill, juce::Justification::centred, false);
}

} // namespace onirique::da
