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
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    // Convert a frequency to a musical note name + octave + cents offset.
    static juce::String freqToNoteName (float freqHz, int& centsOut);

    // Per-drum identity (accent colour, tab name, hero label, search range).
    static constexpr int numDrums = 4;
    static juce::Colour drumAccent  (int d);
    static const char*  drumTabName (int d);
    static const char*  heroLabelFor(int d);
    static juce::String rangeTextFor(int d);
    void applyDrumTheme (int d);

    struct Layout
    {
        juce::Rectangle<int> title, byline, tabs, range, hero, keyLabel, keyBox,
                             scaleLabel, scaleBox, row2, row3, gate, response, hint, footer;
    };
    static Layout computeLayout (juce::Rectangle<int> bounds);

    // Transparent overlay that carries a tooltip for a painted (non-component)
    // area such as the hero panel or a partial row.
    struct TooltipZone : public juce::Component, public juce::SettableTooltipClient
    {
        TooltipZone() { setInterceptsMouseClicks (true, false); }
    };

    void drawTabs (juce::Graphics& g, juce::Rectangle<int> area);
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
    juce::ComboBox keyBox, scaleBox;
    juce::HyperlinkButton siteLink { "www.faderhead.com",
                                     juce::URL ("https://www.faderhead.com") };

    juce::TooltipWindow tooltipWindow { this, 450 };
    TooltipZone heroZone, row2Zone, row3Zone;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAtt> gateAtt, responseAtt;
    std::unique_ptr<ComboAtt>  keyAtt, scaleAtt;

    std::atomic<float>* keyParam   = nullptr;
    std::atomic<float>* scaleParam = nullptr;
    std::atomic<float>* drumParam  = nullptr;

    juce::Colour accent { 0xff4a90d9 };
    int currentDrum = -1; // -1 forces the first applyDrumTheme()

    static constexpr const char* partialTags[] = { "2ND PARTIAL", "3RD PARTIAL" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickFundamentalsEditor)
};
