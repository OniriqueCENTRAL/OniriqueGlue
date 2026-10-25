// Renders the editor to a PNG without a host, so a change to the front can be
// looked at straight from the build. A development tool, not part of the product.
//
//   glue_snapshot out.png [scale]
//   glue_snapshot --selftest       the plugin as a host drives it: blocks, twice the rate, bypass
#include "../Source/PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
/** A tone through the whole plugin, in blocks larger than it was prepared for;
    what comes out at that frequency, in dB against what went in. */
double through (juce::AudioProcessor& processor, double hz, float level, bool& finite)
{
    constexpr double sr = 48000.0;
    constexpr int block = 700;
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    double re = 0.0, im = 0.0;
    int n = 0;

    for (int b = 0, t = 0; b < 100; ++b)
    {
        for (int i = 0; i < block; ++i)
            for (int c = 0; c < 2; ++c)
                buffer.setSample (c, i, level * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (t + i) / sr));

        processor.processBlock (buffer, midi);

        for (int i = 0; i < block; ++i)
        {
            const auto v = buffer.getSample (0, i);
            finite = finite && std::isfinite (v);

            if (b >= 60)
            {
                const auto phase = 2.0 * juce::MathConstants<double>::pi * hz * (t + i) / sr;
                re += v * std::cos (phase);
                im += v * std::sin (phase);
                ++n;
            }
        }

        t += block;
    }

    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / n / level);
}

int selfTest (onirique::GlueProcessor& processor, juce::AudioProcessorValueTreeState& state)
{
    auto set = [&] (const char* id, float value)
    {
        auto* parameter = state.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    int failures = 0;
    auto check = [&] (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what);
        failures += ok ? 0 : 1;
    };

    bool finite = true;
    std::printf ("  latency: %d samples\n", processor.getLatencySamples());
    check (processor.getLatencySamples() > 0, "the delay of the doubled rate is told to the host");

    const auto quiet = through (processor, 1000.0, 0.05f, finite);
    std::printf ("  1 kHz at -26 dBFS: %+.2f dB\n", quiet);
    check (std::abs (quiet) < 0.1, "under the threshold, it comes out as it went in");

    const auto top = through (processor, 18000.0, 0.05f, finite);
    std::printf ("  18 kHz at -26 dBFS: %+.2f dB\n", top);
    check (std::abs (top) < 0.5, "the top end comes back down from the doubled rate intact");

    set ("threshold", -15.0f);
    const auto pressed = through (processor, 1000.0, 0.5f, finite);
    std::printf ("  threshold -15, 1 kHz at -6 dBFS: %+.2f dB\n", pressed);
    check (pressed < -6.0, "over it, the compressor is heard through the plugin");
    check (processor.takeGainReduction() > 6.0f, "and the meter is told");

    set ("bypass", 1.0f);
    const auto bypassed = through (processor, 1000.0, 0.5f, finite);
    std::printf ("  bypassed: %+.2f dB\n", bypassed);
    check (std::abs (bypassed) < 0.1, "bypassed: as it came in");
    check (finite, "nothing blows up");

    return failures == 0 ? 0 : 1;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshot.png");
    const auto scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    processor->setPlayConfigDetails (2, 2, 48000.0, 512);
    processor->prepareToPlay (48000.0, 512);

    auto& glue = static_cast<onirique::GlueProcessor&> (*processor);
    auto& state = glue.apvts;

    if (juce::String (argc > 1 ? argv[1] : "") == "--selftest")
        return selfTest (glue, state);

    // The classic bus setting: 4:1, 30 ms, auto, a few dB off.
    for (auto [id, value] : { std::pair ("threshold", -4.0f), std::pair ("makeup", 3.0f), std::pair ("scfilter", 80.0f),
                              std::pair ("mix", 100.0f) })
    {
        auto* parameter = state.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (glue.createEditor());
        editor->setSize (onirique::GlueEditor::kWidth, onirique::GlueEditor::kHeight);

        // This machine may hold no licence for the plugin: the activation page
        // is not what is being looked at.
        for (auto* child : editor->getChildren())
            if (dynamic_cast<onirique::license::Overlay*> (child) != nullptr)
                child->setVisible (false);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);

        file.deleteFile();
        juce::FileOutputStream stream (file);

        if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
            return 1;
    }

    std::printf ("%s\n", file.getFullPathName().toRawUTF8());
    return 0;
}
