// Onirique Glue — plugin entry point
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "License/LicenseGuard.h"
#include "dsp/Glue.h"

namespace onirique
{

using namespace juce;

/** The plugin instance: the front panel's knobs and buttons in, and the block
    through the compressor at twice the host's rate, so that the output stage
    rounds the peaks without folding anything back. */
class GlueProcessor final : public AudioProcessor
{
public:
    GlueProcessor();
    ~GlueProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override     { return false; }
    bool producesMidi() const override    { return false; }
    bool isMidiEffect() const override    { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    int getNumPrograms() override            { return 1; }
    int getCurrentProgram() override         { return 0; }
    void setCurrentProgram (int) override    {}
    const String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** The most gain reduction since the meter last asked, in dB. */
    float takeGainReduction() noexcept { return gainReduction.exchange (0.0f); }

    AudioProcessorValueTreeState apvts;

    /** The licence ONIRIQUE CENTRAL wrote on this machine for this plugin, under
        the identifier the plugin has in its catalogue. */
    onirique::license::Guard license { "onirique-glue" };

private:
    glue::Settings readSettings() const noexcept;

    glue::Compressor compressor;
    std::unique_ptr<dsp::Oversampling<float>> oversampling;
    int maxBlock = 0;
    std::atomic<float> gainReduction { 0.0f };

    std::atomic<float> *threshold, *makeup, *attack, *release, *ratio, *in, *scFilter, *headroom, *mix, *rate, *fade, *bypass;
    AudioProcessorParameter* bypassParameter = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueProcessor)
};

} // namespace onirique
