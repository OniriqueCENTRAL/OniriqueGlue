#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace onirique
{

namespace
{
    using Attributes = AudioParameterFloatAttributes;

    float parseNumber (const String& text)
    {
        return text.retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getFloatValue();
    }

    /** A knob reads as engraved, with its unit. */
    Attributes reading (const char* unit, int decimals, bool sign = false)
    {
        return Attributes()
            .withStringFromValueFunction ([=] (float v, int)
                                          {
                                              return (sign && v > 0.04f ? "+" : "") + String (v, decimals) + " " + unit;
                                          })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    NormalisableRange<float> skewed (float start, float end, float centre)
    {
        NormalisableRange<float> range { start, end };
        range.setSkewForCentre (centre);
        return range;
    }

    /** The fade's rate, along its knob's uneven engraving. */
    NormalisableRange<float> rateRange()
    {
        using namespace layout;
        return { kRateMarks[0], kRateMarks[kNumRateMarks - 1],
                 [] (float, float, float proportion) { return rateFromProportion (proportion); },
                 [] (float, float, float seconds) { return rateProportion (seconds); },
                 [] (float, float, float seconds) { return jlimit (kRateMarks[0], kRateMarks[kNumRateMarks - 1], seconds); } };
    }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace layout;
        AudioProcessorValueTreeState::ParameterLayout parameters;
        const glue::Settings defaults;

        auto number = [&] (const char* id, const char* name, NormalisableRange<float> range, float value, const Attributes& attributes)
        {
            parameters.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, value, attributes));
        };

        auto choice = [&] (const char* id, const char* name, const StringArray& choices, int value)
        {
            parameters.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, value));
        };

        // The compressor, as on the front.
        number ("threshold", "Threshold", { -15.0f, 15.0f, 0.1f }, defaults.threshold, reading ("dB", 1, true));
        number ("makeup", "Make-Up", { 0.0f, 15.0f, 0.1f }, defaults.makeup, reading ("dB", 1, true));
        choice ("attack", "Attack", { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, defaults.attack);
        choice ("release", "Release", { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" }, defaults.release);
        choice ("ratio", "Ratio", { "2:1", "4:1", "10:1" }, defaults.ratio);
        parameters.add (std::make_unique<AudioParameterBool> (ParameterID { "in", 1 }, "In", defaults.in));

        // The side chain, the output stage, the blend.
        number ("scfilter", "SC Filter", skewed (0.0f, kScFilterMax, kScFilterCentre), defaults.scFilter,
                Attributes().withStringFromValueFunction ([] (float v, int)
                                                          {
                                                              return v < (float) glue::kScFilterOff ? String ("OFF") : String (roundToInt (v)) + " Hz";
                                                          })
                            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); }));
        number ("headroom", "Headroom", { 4.0f, 24.0f, 0.1f }, defaults.headroom, reading ("dB", 1));
        number ("mix", "Mix", { 0.0f, 100.0f, 0.1f }, defaults.mix, reading ("%", 0));

        // The auto fade.
        number ("rate", "Fade Rate", rateRange(), defaults.rate, reading ("s", 1));
        parameters.add (std::make_unique<AudioParameterBool> (ParameterID { "fade", 1 }, "Auto Fade", defaults.fade));

        parameters.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));

        return parameters;
    }

    //==============================================================================
    enum Tags { vocal = 1, drums = 2, bass = 4, instruments = 8, mix = 16, mastering = 32 };

    // The usual ways of setting a bus compressor, as starting points. The stepped controls are given as their
    // position: attack .1/.3/1/3/10/30 ms, release .1/.3/.6/1.2 s/auto, ratio 2/4/10.
    std::vector<PresetBank::Stock> stockPresets()
    {
        return {
            { "Default", 0, {} },

            { "Mix Glue", mix, { { "threshold", -4.0f }, { "makeup", 2.0f }, { "attack", 5.0f }, { "release", 4.0f }, { "ratio", 0.0f } } },
            { "Gentle Bus", mix | mastering, { { "threshold", -2.0f }, { "makeup", 1.0f }, { "attack", 4.0f }, { "release", 2.0f }, { "ratio", 0.0f } } },
            { "Bass-Safe Mix", mix, { { "threshold", -5.0f }, { "makeup", 2.5f }, { "attack", 5.0f }, { "release", 4.0f }, { "ratio", 0.0f },
                                      { "scfilter", 90.0f } } },
            { "Creamy Edge", mix, { { "threshold", -3.0f }, { "makeup", 2.0f }, { "attack", 5.0f }, { "release", 4.0f }, { "ratio", 0.0f },
                                    { "headroom", 8.0f } } },

            { "Master Bus 2:1", mastering, { { "threshold", -3.0f }, { "makeup", 1.5f }, { "attack", 5.0f }, { "release", 4.0f }, { "ratio", 0.0f },
                                             { "scfilter", 60.0f } } },
            { "Loud Master", mastering, { { "threshold", -6.0f }, { "makeup", 4.0f }, { "attack", 5.0f }, { "release", 4.0f }, { "ratio", 1.0f },
                                          { "headroom", 12.0f } } },

            { "Drum Punch", drums, { { "threshold", -6.0f }, { "makeup", 4.0f }, { "attack", 5.0f }, { "release", 0.0f }, { "ratio", 1.0f } } },
            { "Drum Smash", drums, { { "threshold", -10.0f }, { "makeup", 8.0f }, { "attack", 2.0f }, { "release", 0.0f }, { "ratio", 2.0f },
                                     { "mix", 50.0f } } },
            { "Parallel Crush", drums, { { "threshold", -15.0f }, { "makeup", 12.0f }, { "attack", 0.0f }, { "release", 0.0f }, { "ratio", 2.0f },
                                         { "mix", 35.0f } } },
            { "Room Pump", drums, { { "threshold", -12.0f }, { "makeup", 9.0f }, { "attack", 0.0f }, { "release", 1.0f }, { "ratio", 2.0f },
                                    { "mix", 60.0f } } },

            { "Vocal Leveler", vocal, { { "threshold", -5.0f }, { "makeup", 3.0f }, { "attack", 4.0f }, { "release", 4.0f }, { "ratio", 0.0f } } },
            { "Vocal Bus Tight", vocal, { { "threshold", -6.0f }, { "makeup", 4.0f }, { "attack", 3.0f }, { "release", 1.0f }, { "ratio", 1.0f } } },

            { "Bass Control", bass, { { "threshold", -6.0f }, { "makeup", 3.0f }, { "attack", 4.0f }, { "release", 1.0f }, { "ratio", 1.0f } } },

            { "Guitar Bus", instruments, { { "threshold", -5.0f }, { "makeup", 3.0f }, { "attack", 4.0f }, { "release", 4.0f }, { "ratio", 1.0f } } },
            { "Piano & Keys", instruments, { { "threshold", -4.0f }, { "makeup", 2.0f }, { "attack", 5.0f }, { "release", 2.0f }, { "ratio", 0.0f } } },
        };
    }
}

//==============================================================================
GlueProcessor::GlueProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Glue", createLayout()),
      // The auto fade is a hand on the fader, not a setting: no preset holds it down.
      presets (apvts, "Glue", stockPresets(), { "Vocal", "Drums", "Bass", "Instruments", "Mix", "Mastering" }, { "bypass", "fade" })
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    threshold = get ("threshold"); makeup = get ("makeup");     attack = get ("attack");
    release = get ("release");     ratio = get ("ratio");       in = get ("in");
    scFilter = get ("scfilter");   headroom = get ("headroom"); mix = get ("mix");
    rate = get ("rate");           fade = get ("fade");         bypass = get ("bypass");

    bypassParameter = apvts.getParameter ("bypass");
}

bool GlueProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == AudioChannelSet::mono() || out == AudioChannelSet::stereo()) && layouts.getMainInputChannelSet() == out;
}

glue::Settings GlueProcessor::readSettings() const noexcept
{
    glue::Settings s;

    s.threshold = threshold->load();
    s.makeup = makeup->load();
    s.attack = roundToInt (attack->load());
    s.release = roundToInt (release->load());
    s.ratio = roundToInt (ratio->load());
    // Bypassed by the host, the compressor goes out the way the IN button takes it out: gliding.
    s.in = in->load() > 0.5f && bypass->load() < 0.5f;
    s.scFilter = scFilter->load();
    s.headroom = headroom->load();
    s.mix = mix->load();
    s.rate = rate->load();
    s.fade = fade->load() > 0.5f && bypass->load() < 0.5f;

    return s;
}

void GlueProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    license.refresh();

    // Twice the rate, through half-band filters whose delay is a whole number
    // of samples, so the host can make up for it exactly.
    maxBlock = jmax (1, samplesPerBlock);
    oversampling = std::make_unique<dsp::Oversampling<float>> ((size_t) jmax (1, getMainBusNumOutputChannels()), 1,
                                                               dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing ((size_t) maxBlock);
    setLatencySamples (roundToInt (oversampling->getLatencyInSamples()));

    compressor.setSettings (readSettings());
    compressor.prepare (sampleRate * 2.0);
}

void GlueProcessor::reset()
{
    compressor.reset();

    if (oversampling != nullptr)
        oversampling->reset();
}

void GlueProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const auto numChannels = jmin (getMainBusNumOutputChannels(), glue::kMaxChannels, buffer.getNumChannels());
    const auto numSamples = buffer.getNumSamples();

    if (oversampling == nullptr || numChannels < 1)
        return;

    compressor.setSettings (readSettings());

    // Even bypassed, the sound goes up and down again: the delay the host
    // makes up for stays the same, and switching makes no jump.
    dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numChannels, (size_t) numSamples);

    // A host may send more than it announced: in pieces the filters were made for.
    for (int at = 0; at < numSamples; at += maxBlock)
    {
        auto part = block.getSubBlock ((size_t) at, (size_t) jmin (maxBlock, numSamples - at));
        auto up = oversampling->processSamplesUp (part);

        float* channels[glue::kMaxChannels] = {};

        for (int c = 0; c < numChannels; ++c)
            channels[c] = up.getChannelPointer ((size_t) c);

        compressor.process (channels, numChannels, (int) up.getNumSamples());
        oversampling->processSamplesDown (part);
    }

    const auto gr = compressor.takeGainReduction();

    if (gr > gainReduction.load())
        gainReduction.store (gr);

    // Last, and over every output: what an unlicensed plugin lets through.
    license.process (buffer, getSampleRate(), isNonRealtime());
}

AudioProcessorEditor* GlueProcessor::createEditor()
{
    return new GlueEditor (*this);
}

//==============================================================================
void GlueProcessor::getStateInformation (MemoryBlock& destination)
{
    auto state = apvts.copyState();
    presets.writeState (state);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void GlueProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        auto state = ValueTree::fromXml (*xml);
        presets.readState (state);
        apvts.replaceState (state);
    }
}

} // namespace onirique

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new onirique::GlueProcessor();
}
