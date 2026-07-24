#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "KickAnalyzer.h"

/**
    A display-only insert effect: it passes audio through untouched and
    analyses it to report the kick drum's three fundamentals.
*/
class KickFundamentalsProcessor : public juce::AudioProcessor
{
public:
    KickFundamentalsProcessor();
    ~KickFundamentalsProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Drum Fundamentals"; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    KickAnalyzer& getAnalyzer() noexcept { return analyzer; }
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    // Parameter IDs (shared with the editor).
    static constexpr const char* kGateId     = "gate";
    static constexpr const char* kResponseId = "response";
    static constexpr const char* kKeyId      = "key";
    static constexpr const char* kScaleId    = "scale";
    static constexpr const char* kDrumId     = "drum";

    // Per-drum analysis settings.
    struct DrumConfig { float minHz; float maxHz; bool mainIsLoudest; };
    static DrumConfig configForDrum (int drum) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    KickAnalyzer analyzer;

    // Cached raw-parameter pointers for lock-free reads in processBlock.
    std::atomic<float>* gateParam     = nullptr;
    std::atomic<float>* responseParam = nullptr;
    std::atomic<float>* drumParam     = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickFundamentalsProcessor)
};
