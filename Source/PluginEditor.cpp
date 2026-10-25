#include "PluginEditor.h"
#include "ui/OniriqueWindowSize.h"

#include <BinaryData.h>

namespace onirique
{

using namespace theme;

namespace
{
    Image photo (const String& file)
    {
        int size = 0;
        const auto* data = BinaryData::getNamedResource (file.removeCharacters ("-").replaceCharacter ('.', '_').toRawUTF8(), size);
        return data != nullptr ? ImageCache::getFromMemory (data, size) : Image();
    }

    const layout::Run& runOf (layout::Kind kind)
    {
        for (const auto& run : layout::kRuns)
            if (run.kind == kind)
                return run;

        return layout::kRuns[0];
    }

    Rectangle<float> meterArea()
    {
        return { layout::kMeterLeft, layout::kMeterTop, layout::kMeterRight - layout::kMeterLeft, layout::kMeterBottom - layout::kMeterTop };
    }
}

GlueEditor::GlueEditor (GlueProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      frontPhoto (photo ("front.jpg"))
{
    setLookAndFeel (&lookAndFeel);

    front.onPaint = [this] (Graphics& g) { paintFront (g); };
    front.onPaintOver = [this] (Graphics& g) { paintReadout (g); };
    front.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (front);

    auto& state = processor.apvts;
    Component* components[] = { &threshold, &makeup, &attack, &release, &ratio, &in, &scFilter, &headroom, &mix, &rate, &fade };
    static_assert (std::size (layout::kControls) == 11);

    for (size_t i = 0; i < std::size (layout::kControls); ++i)
    {
        const auto& control = layout::kControls[i];
        const auto& run = runOf (control.kind);
        auto* component = components[i];
        parts.push_back ({ &control, component, photo (String (run.photo) + ".png"), run.frames });

        if (auto* slider = dynamic_cast<Slider*> (component))
        {
            slider->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            slider->setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
            slider->setRotaryParameters (MathConstants<float>::twoPi + degreesToRadians (control.fromDegrees),
                                         MathConstants<float>::twoPi + degreesToRadians (control.toDegrees), true);
            sliderAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, control.id, *slider));

            auto* parameter = state.getParameter (control.id);
            slider->setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        }
        else if (auto* button = dynamic_cast<ToggleButton*> (component))
        {
            buttonAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, control.id, *button));
        }

        component->setBounds (Rectangle<float> (control.size, control.size).withCentre ({ control.x, control.y }).toNearestInt());
        front.addAndMakeVisible (component);
    }

    // What each control does, for a pointer that rests on it. In French, accents and all.
    auto describe = [] (SettableTooltipClient& c, const char* text) { c.setTooltip (String::fromUTF8 (text)); };

    describe (threshold, "Threshold : le seuil, de -15 à +15 dB. Plus il descend, plus le compresseur travaille ; l'aiguille montre combien il retire. Sur un bus, 2 à 4 dB suffisent pour que tout se tienne.");
    describe (makeup, "Make-Up : rend le niveau que la compression a pris, de -5 à +15 dB.");
    describe (attack, "Attack : le temps de réaction, de 0,1 à 30 ms. Lent (10, 30), les attaques passent avant qu'il ne serre : du punch. Rapide (0,1, 0,3), il rattrape tout : plus serré, plus dense.");
    describe (release, "Release : le temps de relâche, de 0,1 à 1,2 s. AUTO s'adapte à la musique : il relâche vite après un coup isolé, et lentement quand la compression dure. C'est le réglage qui colle un mix.");
    describe (ratio, "Ratio : 2, 4 ou 10 pour 1. 2 pour coller en douceur, 4 pour tenir un bus, 10 pour écraser.");
    describe (in, "IN : allumé, le compresseur est en service ; éteint, le signal ressort tel qu'il est entré.");
    describe (scFilter, "SC Filter : retire le bas de ce que le compresseur entend, de OFF à 500 Hz. La basse et le kick ne le font plus pomper ; le grave du mix reste plein.");
    describe (headroom, "Headroom : la marge de l'étage de sortie, de 4 à 24 dB. Haut, le son reste propre ; plus on descend, plus les crêtes s'arrondissent en douceur, une saturation crémeuse.");
    describe (mix, "Mix : le son compressé mêlé à celui qui entre, de 0 à 100 %. Vers 50 %, la compression parallèle : de la densité sans perdre les attaques.");
    describe (rate, "Rate : la durée du fondu de l'Auto Fade, de 1 à 60 secondes.");
    describe (fade, "Auto Fade : appuyé, le son descend jusqu'au silence en Rate secondes, comme un fader de console ; relâché, il remonte de même.");

    licenseOverlay = std::make_unique<onirique::license::Overlay> ("GLUE", processor.license);
    addAndMakeVisible (*licenseOverlay);

    // The front keeps its proportions; the window grows by zooming it.
    constexpr int minWidth = 340, maxWidth = 840;
    setResizable (true, true);
    setResizeLimits (minWidth, minWidth * kHeight / kWidth, maxWidth, maxWidth * kHeight / kWidth);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);

    // taille d'ouverture : la règle commune des plugins ONIRIQUE (ui/OniriqueWindowSize.h)
    const auto opening = onirique::window::openingSize ({}, "ONIRIQUE GLUE", { kWidth * 6 / 5, kHeight * 6 / 5 },
                                                        { minWidth, minWidth * kHeight / kWidth }, { maxWidth, maxWidth * kHeight / kWidth });
    const auto width = jmin (opening.x, opening.y * kWidth / kHeight);
    setSize (width, width * kHeight / kWidth);

    timerCallback();
    startTimerHz (60);
}

GlueEditor::~GlueEditor()
{
    onirique::window::rememberOnThisComputer ("ONIRIQUE GLUE", getWidth(), getHeight());
    setLookAndFeel (nullptr);
}

void GlueEditor::resized()
{
    const auto scale = (float) getWidth() / (float) kWidth;

    front.setBounds (0, 0, kWidth, kHeight);
    front.setTransform (AffineTransform::scale (scale));

    // The licence badge sits at the top on the right, beside the maker's name.
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), scale, { 170, 44, 190, 28 }, Justification::centredRight);
}

void GlueEditor::timerCallback()
{
    // The needle: up fast, back with the weight of a meter's movement.
    const auto gr = processor.takeGainReduction();
    needleDb += (gr > needleDb ? 0.45f : 0.12f) * (gr - needleDb);

    if (std::abs (needleDb - shownNeedleDb) > 0.02f)
    {
        shownNeedleDb = needleDb;
        front.repaint (meterArea().expanded (2.0f).toNearestInt());
    }

    // The part in hand first, else the one under the pointer.
    const Part* now = nullptr;

    for (const auto& part : parts)
        if (part.component->isMouseButtonDown())
            now = &part;

    if (now == nullptr)
        for (const auto& part : parts)
            if (part.component->isMouseOver())
                now = &part;

    String text;

    if (now != nullptr)
    {
        auto* parameter = processor.apvts.getParameter (now->control->id);
        text << now->control->name << "   ";

        if (now->control->kind == layout::Kind::button)
            text << (parameter->getValue() > 0.5f ? "ON" : "OFF");
        else
            text << parameter->getCurrentValueAsText();
    }

    std::vector<float> values;

    for (const auto& part : parts)
        values.push_back (processor.apvts.getParameter (part.control->id)->getValue());

    if (now != shown || text != readout || values != shownValues)
    {
        shown = now;
        readout = text;
        shownValues = values;
        front.repaint();
    }
}

//==============================================================================
void GlueEditor::paintFront (Graphics& g)
{
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (frontPhoto, Rectangle<float> ((float) kWidth, (float) kHeight));

    paintNeedle (g);

    for (const auto& part : parts)
    {
        const auto& c = *part.control;

        if (! part.photos.isValid())
            continue;

        const auto value = processor.apvts.getParameter (c.id)->getValue();
        const auto frame = layout::frameOf (c.kind);
        const auto pixels = roundToInt (frame.size() * (float) layout::kPhotoScale);
        const auto columns = jmin (part.frames, 8);

        // A button: out or lit. A knob: the photograph nearest its angle,
        // turned the rest of the way, so it moves smoothly.
        auto k = value > 0.5f ? 1 : 0;
        auto rest = 0.0f;

        if (c.kind != layout::Kind::button)
        {
            const auto degrees = c.fromDegrees + value * (c.toDegrees - c.fromDegrees);
            const auto step = roundToInt (degrees / layout::kDegreesPerFrame);
            rest = degrees - (float) step * layout::kDegreesPerFrame;
            k = ((step % part.frames) + part.frames) % part.frames;
        }

        const auto shot = part.photos.getClippedImage ({ (k % columns) * pixels, (k / columns) * pixels, pixels, pixels });

        g.drawImageTransformed (shot, AffineTransform::scale (frame.size() / (float) pixels)
                                          .translated (c.x - frame.before, c.y - frame.before)
                                          .rotated (degreesToRadians (rest), c.x, c.y));

        // The button's name, printed on its cap.
        if (c.kind == layout::Kind::button)
        {
            const auto lit = k == 1;
            const auto name = String (c.name);
            g.setColour (Colour (lit ? 0xd0402a10 : 0xb02a2622));
            g.setFont (displayFont (name.length() > 3 ? 10.5f : 15.0f));
            g.drawFittedText (name.replace (" ", "\n"), Rectangle<float> (2.0f * layout::kButtonHalf - 10.0f, 2.0f * layout::kButtonHalf - 10.0f)
                                                           .withCentre ({ c.x, c.y + (lit ? 1.0f : 0.0f) }).toNearestInt(),
                              Justification::centred, 2, 1.0f);
        }
    }
}

void GlueEditor::paintNeedle (Graphics& g)
{
    using namespace layout;
    const Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (meterArea().toNearestInt());

    const auto angle = degreesToRadians (meterDegrees (needleDb));
    const Point<float> pivot { kPivotX, kPivotY };
    const auto tip = pivot.getPointOnCircumference (kNeedleLength, angle);
    const auto tail = pivot.getPointOnCircumference (kNeedleLength * 0.35f, angle);

    // Its shadow on the lit face, then the needle: thin, black, a touch thicker at the root.
    Path needle;
    needle.addLineSegment ({ tail, tip }, 1.0f);
    needle.addLineSegment ({ tail, tail.getPointOnCircumference (kNeedleLength * 0.3f, angle) }, 1.4f);

    g.setColour (Colours::black.withAlpha (0.16f));
    g.fillPath (needle, AffineTransform::translation (2.5f, 3.5f));
    g.setColour (Colour (0xff17130f));
    g.fillPath (needle);
}

void GlueEditor::paintReadout (Graphics& g)
{
    if (shown == nullptr || readout.isEmpty())
        return;

    // Beside the control: under it, or over it when there is no room below.
    const auto font = fonts->monoBoldFont (12.0f, 0.04f);
    const auto width = GlyphArrangement::getStringWidth (font, readout) + 26.0f;
    const auto bounds = shown->component->getBounds().toFloat();
    auto pill = Rectangle<float> (width, 24.0f).withCentre ({ bounds.getCentreX(), bounds.getBottom() + 14.0f });

    if (pill.getBottom() > (float) kHeight - 2.0f)
        pill.setY (bounds.getY() - 26.0f);

    pill = pill.constrainedWithin (Rectangle<float> ((float) kWidth, (float) kHeight).reduced (4.0f));

    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (pill.translated (0.0f, 2.0f), pill.getHeight() * 0.5f);
    g.setColour (Colour (0xe6141414));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 1.0f);

    g.setColour (mist);
    g.setFont (font);
    g.drawText (readout, pill, Justification::centred, false);
}

} // namespace onirique
