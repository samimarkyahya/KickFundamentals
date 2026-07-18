#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class KickFundamentalsEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit KickFundamentalsEditor (KickFundamentalsProcessor&);
    ~KickFundamentalsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Convert a frequency to a musical note name + octave + cents offset.
    static juce::String freqToNoteName (float freqHz, int& centsOut);

    // Cents from a frequency to the nearest occurrence of a pitch class (0=C..11=B).
    static float centsToPitchClass (float freqHz, int pitchClass);

    struct Layout
    {
        juce::Rectangle<int> title, byline, hero, targetLabel, targetBox,
                             bodyToggle, row2, row3, gate, response, hint, footer;
    };
    static Layout computeLayout (juce::Rectangle<int> bounds);

    // Transparent overlay that carries a tooltip for a painted (non-component)
    // area such as the hero panel or a partial row.
    struct TooltipZone : public juce::Component, public juce::SettableTooltipClient
    {
        TooltipZone() { setInterceptsMouseClicks (true, false); }
    };

    void drawHero (juce::Graphics& g, juce::Rectangle<int> area);
    void drawSmallRow (juce::Graphics& g, juce::Rectangle<int> area,
                       const juce::String& tag, int index);
    void drawKnobLabel (juce::Graphics& g, juce::Rectangle<int> area,
                        const juce::String& text);
    void drawKnobEnds (juce::Graphics& g, juce::Rectangle<int> area,
                       const juce::String& leftText, const juce::String& rightText);

    KickFundamentalsProcessor& processor;

    juce::Image logoTitle, logoByline;

    juce::Slider   gateSlider, responseSlider;
    juce::ComboBox targetBox;
    juce::ToggleButton bodyToggle { "Body only" };
    juce::HyperlinkButton siteLink { "www.faderhead.com",
                                     juce::URL ("https://www.faderhead.com") };

    juce::TooltipWindow tooltipWindow { this, 450 };
    TooltipZone heroZone, row2Zone, row3Zone;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> gateAtt, responseAtt;
    std::unique_ptr<ComboAtt>  targetAtt;
    std::unique_ptr<ButtonAtt> bodyAtt;

    std::atomic<float>* targetParam = nullptr;

    static constexpr const char* partialTags[] = { "2ND PARTIAL", "3RD PARTIAL" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickFundamentalsEditor)
};
