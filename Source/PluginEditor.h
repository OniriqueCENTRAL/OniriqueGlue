// Onirique Glue — editor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "ui/Layout.h"
#include "ui/OniriqueBar.h"
#include "ui/Theme.h"

namespace onirique
{

/** The front of a console's bus compressor, shown as photographs: the front
    itself, with its lit meter, and each part at the angle its parameter is
    at (tools/photo.cpp renders them). The controls lie unseen over their
    parts; the meter's needle is drawn over its face. The whole front is
    drawn at one size and scaled as one with the window, the bar over it
    (ui/OniriqueBar.h, on two rows: the front is narrow) with it. */
class GlueEditor final : public AudioProcessorEditor,
                         private Timer
{
public:
    explicit GlueEditor (GlueProcessor&);
    ~GlueEditor() override;

    void resized() override;

    static constexpr int kWidth = layout::kWidth, kHeight = layout::kHeight;
    static constexpr int kBarHeight = 84, kTotalHeight = kBarHeight + kHeight;

private:
    struct Front final : Component
    {
        std::function<void (Graphics&)> onPaint, onPaintOver;
        void paint (Graphics& g) override { onPaint (g); }
        void paintOverChildren (Graphics& g) override { onPaintOver (g); }
    };

    /** A control of the front, the component that takes the pointer for it,
        and the run of photographs of its part. */
    struct Part
    {
        const layout::Control* control;
        Component* component;
        Image photos;
        int frames;
    };

    void timerCallback() override;
    void paintFront (Graphics&);
    void paintNeedle (Graphics&);
    void paintReadout (Graphics&);
    bool isBacklit() const;

    GlueProcessor& processor;
    theme::GlueLookAndFeel lookAndFeel;
    license::theme::Fonts fonts;

    Front front;
    nuit::Bar bar;
    Slider threshold, makeup, attack, release, ratio, scFilter, headroom, mix, rate;
    ToggleButton in, fade;
    std::vector<Part> parts;

    std::vector<std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    /** The front, the meter's lamp off and lit. */
    Image frontOff, frontOn;

    /** What the parts were last drawn at: a change is drawn again. */
    std::vector<float> shownValues;

    /** Where the needle is, in dB, swinging after the reduction with a meter's inertia. */
    float needleDb = 0.0f, shownNeedleDb = -1.0f;

    /** What the control in hand, or under the pointer, is set to; shown beside it. */
    const Part* shown = nullptr;
    String readout;

    TooltipWindow tooltips { this, 2000 };

    /** The licence badge and its activation page. Declared last and added
        last: it lies over everything else. */
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueEditor)
};

} // namespace onirique
