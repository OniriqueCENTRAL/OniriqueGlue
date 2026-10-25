// Onirique Glue — the look: photographs of the front, and the Onirique tooltips
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace onirique::theme
{

using juce::Colour;

inline const Colour ember   { 0xffe23444 };
inline const Colour mist    { 0xfff6eeee };

inline juce::Font displayFont (float height, bool heavy = true)
{
    // Named outright: the "default sans-serif" placeholder does not survive
    // being put in FontOptions, and what comes out the other end is a serif.
   #if JUCE_WINDOWS
    const juce::String family ("Segoe UI");
   #elif JUCE_MAC
    const juce::String family ("Helvetica Neue");
   #else
    const juce::String family ("DejaVu Sans");
   #endif

    return juce::Font (juce::FontOptions (family, height, heavy ? juce::Font::bold : juce::Font::plain));
}

/** The controls draw nothing themselves: they lie, invisible, over the
    photographs of the parts they turn, which the front lays out. */
class GlueLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override {}
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override {}

    // The description of a control, shown once the pointer has rested on it.
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};

} // namespace onirique::theme
