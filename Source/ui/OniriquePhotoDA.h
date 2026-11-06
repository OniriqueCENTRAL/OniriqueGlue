#pragma once

#include "OniriqueDA.h"

//==============================================================================
//  La DA de VOCALBOX pour les façades photographiées (WEIGHT, MASSIF, GLUE) : les potards et la mécanique restent tels
//  que sur les photos ; autour, le fond nuit et le verre de VOCALBOX, la peinture rouge de la façade qui suit la couleur
//  de l'interface, la lueur sur la pièce touchée et la bulle de valeur.
//==============================================================================
namespace onirique::da
{

/** La peinture rouge d'une photo ramenée au cramoisi de la DA ONIRIQUE (celui de VOCALBOX), puis à la couleur de
    l'interface choisie : les pixels rouges prennent la teinte de la DA, plus saturée et un peu plus sombre, en gardant le
    relief de la photo (ombres, reflets, grain) ; le reste (potards, métal, chiffres blancs, lampes) ne bouge pas. */
inline juce::Image tintPhoto (const juce::Image& src, juce::Rectangle<float> keep = {}, float unitsWidth = 0.0f)
{
    if (! src.isValid())
        return src;

    const juce::Colour base (onirique::look::swatches[0].argb);
    auto img = src.convertedToFormat (juce::Image::ARGB).createCopy();
    const float hue = onirique::look::ember.getHue();
    const float k = onirique::look::ember.getSaturation() / juce::jmax (0.01f, base.getSaturation());
    juce::Image::BitmapData data (img, juce::Image::BitmapData::readWrite);

    // « keep » (en points de la façade) : une fenêtre qui garde les couleurs de la photo (le cadran rétroéclairé d'un VU) ;
    // seule la teinte de son éclairage suit la couleur de l'interface, sans rien saturer ni assombrir
    const float px = unitsWidth > 0.0f ? (float) img.getWidth() / unitsWidth : 0.0f;
    const auto window = (keep * px).toNearestInt();

    for (int y = 0; y < data.height; ++y)
        for (int x = 0; x < data.width; ++x)
        {
            const auto c = data.getPixelColour (x, y);
            float h, s, v;
            c.getHSB (h, s, v);
            const float deg = h * 360.0f;

            if (! window.isEmpty() && window.contains (x, y))
            {
                if ((deg <= 25.0f || deg >= 335.0f) && s > 0.18f)
                    data.setPixelColour (x, y, juce::Colour::fromHSV (hue, s, v, c.getFloatAlpha()));
                continue;
            }

            if ((deg <= 25.0f || deg >= 335.0f) && s > 0.18f && v > 0.05f)
            {
                // le cramoisi de la DA : saturé comme la braise, un peu plus sombre que la peinture d'origine. Les reflets à
                // peine rosés (potards noirs, métal) restent tels quels : le passage se fait en douceur entre les deux.
                const float w = juce::jlimit (0.0f, 1.0f, (s - 0.24f) / 0.12f);
                const float sat = juce::jlimit (0.0f, 1.0f, s + ((0.55f + 0.6f * s) * k - s) * w);
                data.setPixelColour (x, y, juce::Colour::fromHSV (w > 0.0f ? hue : h, sat, v * (1.0f - 0.2f * w), c.getFloatAlpha()));
            }
        }

    return img;
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
