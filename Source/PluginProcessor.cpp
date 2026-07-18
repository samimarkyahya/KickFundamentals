#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout
KickFundamentalsProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { kGateId, 1 }, "Gate",
        NormalisableRange<float> (-80.0f, -25.0f, 0.1f), -55.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { kResponseId, 1 }, "Response",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.80f));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { kTargetId, 1 }, "Target Note",
        StringArray { "Auto", "C", "C#", "D", "D#", "E", "F",
                      "F#", "G", "G#", "A", "A#", "B" }, 0));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { kBodyOnlyId, 1 }, "Body Only", false));

    return layout;
}

KickFundamentalsProcessor::KickFundamentalsProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    gateParam     = apvts.getRawParameterValue (kGateId);
    responseParam = apvts.getRawParameterValue (kResponseId);
    bodyOnlyParam = apvts.getRawParameterValue (kBodyOnlyId);
}

void KickFundamentalsProcessor::prepareToPlay (double sampleRate, int)
{
    analyzer.prepare (sampleRate);
}

bool KickFundamentalsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // As an insert we mirror input to output; accept mono or stereo, matched.
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono()
        && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainIn == mainOut;
}

void KickFundamentalsProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Push the current parameter values to the analyser (lock-free atomics).
    if (gateParam     != nullptr) analyzer.setGateDb   (gateParam->load());
    if (responseParam != nullptr) analyzer.setResponse (responseParam->load());
    if (bodyOnlyParam != nullptr) analyzer.setBodyOnly (bodyOnlyParam->load() > 0.5f);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Feed a mono sum of the input to the analyser. The audio itself is left
    // completely untouched — this is a metering-only insert.
    if (numChannels > 0)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                mono += buffer.getReadPointer (ch)[n];

            analyzer.pushSample (mono / (float) numChannels);
        }
    }
}

void KickFundamentalsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KickFundamentalsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* KickFundamentalsProcessor::createEditor()
{
    return new KickFundamentalsEditor (*this);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickFundamentalsProcessor();
}
