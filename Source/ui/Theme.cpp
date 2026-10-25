#include "Theme.h"

namespace onirique::theme
{

using namespace juce;

//==============================================================================
namespace
{
    constexpr int kTooltipWidth = 250;

    TextLayout layoutTooltip (const String& text)
    {
        AttributedString s;
        s.setJustification (Justification::centredLeft);
        s.append (text, displayFont (12.5f, false), mist);

        TextLayout layout;
        layout.createLayout (s, (float) kTooltipWidth);
        return layout;
    }
}

Rectangle<int> GlueLookAndFeel::getTooltipBounds (const String& text, Point<int> screenPos, Rectangle<int> parentArea)
{
    const auto layout = layoutTooltip (text);
    const auto width = roundToInt (layout.getWidth()) + 22;
    const auto height = roundToInt (layout.getHeight()) + 14;

    // Below and to the right of the pointer, or on the other side when that
    // would run off the edge.
    return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - width - 12 : screenPos.x + 18,
                           screenPos.y > parentArea.getCentreY() ? screenPos.y - height - 10 : screenPos.y + 18,
                           width, height)
               .constrainedWithin (parentArea);
}

void GlueLookAndFeel::drawTooltip (Graphics& g, const String& text, int width, int height)
{
    const auto bounds = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setGradientFill (ColourGradient (Colour (0xff3d0911), 0.0f, 0.0f, Colour (0xff1c0508), 0.0f, (float) height, false));
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (ember.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);

    layoutTooltip (text).draw (g, bounds.reduced (11.0f, 7.0f));
}

} // namespace onirique::theme
