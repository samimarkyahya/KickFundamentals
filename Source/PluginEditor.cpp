#include "PluginEditor.h"
#include "BinaryData.h"

#include <cmath>

constexpr const char* KickFundamentalsEditor::partialTags[];

// ---- Per-drum identity ------------------------------------------------------
juce::Colour KickFundamentalsEditor::drumAccent (int d)
{
    switch (d)
    {
        case 1:  return juce::Colour (0xff2f9160); // Toms   — dark green
        case 2:  return juce::Colour (0xffdf5b5b); // Snare  — red
        case 3:  return juce::Colour (0xffc6cfda); // Cymbals— light grey
        default: return juce::Colour (0xff4a90d9); // Kick   — blue
    }
}
const char* KickFundamentalsEditor::drumTabName (int d)
{
    switch (d) { case 1: return "TOMS"; case 2: return "SNARE"; case 3: return "CYMBALS"; default: return "KICK"; }
}
const char* KickFundamentalsEditor::heroLabelFor (int d)
{
    switch (d) { case 1: return "TOM NOTE"; case 2: return "SNARE NOTE"; case 3: return "CYMBAL NOTE"; default: return "KICK NOTE"; }
}
juce::String KickFundamentalsEditor::rangeTextFor (int d)
{
    switch (d)
    {
        case 1:  return "70 - 350 Hz";
        case 2:  return "120 - 500 Hz";
        case 3:  return "500 - 8000 Hz";
        default: return "30 - 250 Hz";
    }
}

KickFundamentalsEditor::KickFundamentalsEditor (KickFundamentalsProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    logoByline = juce::ImageCache::getFromMemory (BinaryData::logo_byline_png,
                                                  BinaryData::logo_byline_pngSize);

    auto& apvts = processor.getAPVTS();
    targetParam = apvts.getRawParameterValue (KickFundamentalsProcessor::kTargetId);
    drumParam   = apvts.getRawParameterValue (KickFundamentalsProcessor::kDrumId);

    auto styleKnob = [] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a424e));
        s.setColour (juce::Slider::textBoxTextColourId,         juce::Colour (0xffb7c0cc));
        s.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    };
    styleKnob (gateSlider);
    styleKnob (responseSlider);
    addAndMakeVisible (gateSlider);
    addAndMakeVisible (responseSlider);

    // --- Target-note selector ------------------------------------------------
    static const juce::StringArray items { "Auto", "C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < items.size(); ++i)
        targetBox.addItem (items[i], i + 1);
    targetBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff20242c));
    targetBox.setColour (juce::ComboBox::textColourId,       juce::Colours::white);
    targetBox.setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff3a424e));
    targetBox.setColour (juce::ComboBox::arrowColourId,      juce::Colour (0xff9aa4b0));
    addAndMakeVisible (targetBox);

    // --- Body-only toggle ----------------------------------------------------
    bodyToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffb7c0cc));
    bodyToggle.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff3a424e));
    addAndMakeVisible (bodyToggle);

    // --- Site link -----------------------------------------------------------
    siteLink.setColour (juce::HyperlinkButton::textColourId, juce::Colour (0xff7f8b9c));
    siteLink.setFont (juce::Font (13.0f, juce::Font::plain), false, juce::Justification::centred);
    siteLink.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (siteLink);

    // --- Tooltips (drum-agnostic) --------------------------------------------
    heroZone.setTooltip ("The main note to tune to your track. "
                         "Dot turns green when it's in tune.");
    row2Zone.setTooltip ("The 2nd-strongest tone. Usually a different note, "
                         "which is normal for drums.");
    row3Zone.setTooltip ("The 3rd tone. Extra colour on top of the main note.");
    addAndMakeVisible (heroZone);
    addAndMakeVisible (row2Zone);
    addAndMakeVisible (row3Zone);

    targetBox.setTooltip ("\"Auto\" = nearest note. Or pick your song's key "
                          "to see how far off the drum is.");
    bodyToggle.setTooltip ("Ignores the attack, reads just the sustained body. "
                           "Good for drums that \"pitch down.\"");
    gateSlider.setTooltip ("How loud a hit must be to read. Raise if it flickers "
                           "on noise; lower for quiet hits.");
    responseSlider.setTooltip ("Left = snappy. Right = rock-steady. "
                               "Turn right if the note jitters.");

    // --- Parameter attachments ----------------------------------------------
    gateAtt     = std::make_unique<SliderAtt> (apvts, KickFundamentalsProcessor::kGateId,     gateSlider);
    responseAtt = std::make_unique<SliderAtt> (apvts, KickFundamentalsProcessor::kResponseId, responseSlider);
    targetAtt   = std::make_unique<ComboAtt>  (apvts, KickFundamentalsProcessor::kTargetId,   targetBox);
    bodyAtt     = std::make_unique<ButtonAtt> (apvts, KickFundamentalsProcessor::kBodyOnlyId, bodyToggle);

    applyDrumTheme (drumParam != nullptr ? (int) (drumParam->load() + 0.5f) : 0);

    setSize (470, 610);
    startTimerHz (30);
}

KickFundamentalsEditor::~KickFundamentalsEditor()
{
    stopTimer();
}

void KickFundamentalsEditor::applyDrumTheme (int d)
{
    currentDrum = d;
    accent = drumAccent (d);
    const auto pointer = accent.brighter (0.4f);
    gateSlider.setColour     (juce::Slider::rotarySliderFillColourId, accent);
    gateSlider.setColour     (juce::Slider::thumbColourId,            pointer);
    responseSlider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    responseSlider.setColour (juce::Slider::thumbColourId,            pointer);
    bodyToggle.setColour     (juce::ToggleButton::tickColourId,       accent);
    repaint();
}

void KickFundamentalsEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto L = computeLayout (getLocalBounds());
    if (! L.tabs.contains (e.getPosition()))
        return;

    int idx = (e.x - L.tabs.getX()) * numDrums / juce::jmax (1, L.tabs.getWidth());
    idx = juce::jlimit (0, numDrums - 1, idx);

    if (auto* param = processor.getAPVTS().getParameter (KickFundamentalsProcessor::kDrumId))
        param->setValueNotifyingHost (param->convertTo0to1 ((float) idx));
}

void KickFundamentalsEditor::timerCallback()
{
    processor.getAnalyzer().processIfReady();

    // Pick up drum changes (clicks, automation, preset recall) and re-theme.
    if (drumParam != nullptr)
    {
        const int d = (int) (drumParam->load() + 0.5f);
        if (d != currentDrum)
            applyDrumTheme (d);
    }

    repaint();
}

juce::String KickFundamentalsEditor::freqToNoteName (float freqHz, int& centsOut)
{
    centsOut = 0;
    if (freqHz <= 0.0f)
        return "--";

    const float midi = 69.0f + 12.0f * std::log2 (freqHz / 440.0f);
    const int   nearest = juce::roundToInt (midi);
    centsOut = juce::roundToInt ((midi - (float) nearest) * 100.0f);

    static const char* names[] =
        { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const int note   = ((nearest % 12) + 12) % 12;
    const int octave = nearest / 12 - 1;

    return juce::String (names[note]) + juce::String (octave);
}

float KickFundamentalsEditor::centsToPitchClass (float freqHz, int pitchClass)
{
    if (freqHz <= 0.0f)
        return 0.0f;

    const float midi = 69.0f + 12.0f * std::log2 (freqHz / 440.0f);
    const float base = std::round ((midi - (float) pitchClass) / 12.0f) * 12.0f + (float) pitchClass;
    return (midi - base) * 100.0f; // signed cents (can exceed +/-50 up to +/-600)
}

KickFundamentalsEditor::Layout KickFundamentalsEditor::computeLayout (juce::Rectangle<int> bounds)
{
    Layout L;
    auto b = bounds.reduced (16, 0);

    b.removeFromTop (12);
    L.title  = b.removeFromTop (30);
    b.removeFromTop (2);
    L.byline = b.removeFromTop (15);
    b.removeFromTop (12);

    L.tabs = b.removeFromTop (34);
    b.removeFromTop (2);
    L.range = b.removeFromTop (18);
    b.removeFromTop (10);

    L.hero = b.removeFromTop (150);
    b.removeFromTop (10);

    auto modeRow = b.removeFromTop (30);
    L.targetLabel = modeRow.removeFromLeft (66);
    L.targetBox   = modeRow.removeFromLeft (96).reduced (0, 2);
    L.bodyToggle  = modeRow.removeFromRight (120);
    b.removeFromTop (10);

    L.row2 = b.removeFromTop (46);
    b.removeFromTop (4);
    L.row3 = b.removeFromTop (46);
    b.removeFromTop (14);

    auto knobRow = b.removeFromTop (110);
    L.gate     = knobRow.removeFromLeft (knobRow.getWidth() / 2);
    L.response = knobRow;

    L.footer = b.removeFromBottom (24);
    L.hint   = b.removeFromBottom (16);
    return L;
}

void KickFundamentalsEditor::resized()
{
    const auto L = computeLayout (getLocalBounds());

    targetBox.setBounds (L.targetBox);
    bodyToggle.setBounds (L.bodyToggle);
    siteLink.setBounds (L.footer);

    gateSlider.setBounds     (L.gate.withTrimmedTop (16));
    responseSlider.setBounds (L.response.withTrimmedTop (16));

    heroZone.setBounds (L.hero);
    row2Zone.setBounds (L.row2);
    row3Zone.setBounds (L.row3);
}

void KickFundamentalsEditor::drawTabs (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int tw = area.getWidth() / numDrums;
    for (int i = 0; i < numDrums; ++i)
    {
        auto r = area.withX (area.getX() + i * tw)
                     .withWidth (i == numDrums - 1 ? area.getRight() - (area.getX() + i * tw) : tw);
        const bool active = (i == currentDrum);

        g.setColour (active ? accent : juce::Colour (0xff6c7788));
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText (drumTabName (i), r.withTrimmedBottom (6), juce::Justification::centred);

        if (active)
        {
            g.setColour (accent);
            g.fillRoundedRectangle ((float) (r.getX() + 10), (float) (r.getBottom() - 3),
                                    (float) (r.getWidth() - 20), 2.5f, 1.5f);
        }
    }
    g.setColour (juce::Colour (0xff2b323c));
    g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);
}

void KickFundamentalsEditor::drawHero (juce::Graphics& g, juce::Rectangle<int> area)
{
    const float freq = processor.getAnalyzer().getFrequency (0);

    int centsNearest = 0;
    const juce::String note = freqToNoteName (freq, centsNearest);
    const bool hasSignal = freq > 0.0f;

    const int targetIdx = targetParam != nullptr ? (int) (targetParam->load() + 0.5f) : 0;
    const bool useTarget = targetIdx > 0;

    float cents = (float) centsNearest;
    juce::String targetName;
    if (useTarget)
    {
        static const char* names[] =
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        targetName = names[targetIdx - 1];
        cents = centsToPitchClass (freq, targetIdx - 1);
    }

    const bool inTune = hasSignal && std::abs (cents) <= 5.0f;

    auto panel = area;
    g.setColour (juce::Colour (0xff262c36));
    g.fillRoundedRectangle (panel.toFloat(), 10.0f);
    // Accent bar along the top of the hero panel.
    g.setColour (accent.withAlpha (0.85f));
    g.fillRoundedRectangle ((float) panel.getX(), (float) panel.getY(),
                            (float) panel.getWidth(), 3.0f, 1.5f);

    auto inner = panel.reduced (18, 12);

    g.setColour (juce::Colour (0xff7f8b9c));
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    const juce::String midDot (juce::CharPointer_UTF8 ("\xc2\xb7"));
    const juce::String baseLabel (heroLabelFor (currentDrum));
    const juce::String heroLabel = useTarget
        ? baseLabel + "  " + midDot + "  vs " + targetName
        : baseLabel;
    g.drawText (heroLabel, inner.removeFromTop (18), juce::Justification::topLeft);

    if (hasSignal)
    {
        const juce::String hz = juce::String (freq, 1) + " Hz";
        juce::String off;
        if (useTarget && std::abs (cents) >= 100.0f)
            off = (cents >= 0 ? "+" : "") + juce::String (cents / 100.0f, 2) + " st";
        else
            off = (cents >= 0 ? "+" : "") + juce::String (juce::roundToInt (cents)) + " cents";

        g.setColour (juce::Colour (0xffb7c0cc));
        g.setFont (juce::Font (14.0f));
        g.drawText (hz + "   " + off,
                    panel.reduced (18, 12).removeFromTop (18),
                    juce::Justification::topRight);
    }

    auto noteArea = inner.removeFromTop (78);
    g.setColour (hasSignal ? juce::Colours::white : juce::Colour (0xff3a4049));
    g.setFont (juce::Font (72.0f, juce::Font::bold));
    g.drawText (hasSignal ? note : juce::String ("--"),
                noteArea, juce::Justification::centredLeft);

    auto scale = inner.removeFromTop (22).reduced (2, 6);
    if (hasSignal)
    {
        const float cx = scale.getCentreX();

        g.setColour (juce::Colour (0xff3a424e));
        g.fillRoundedRectangle ((float) scale.getX(), scale.getCentreY() - 1.5f,
                                (float) scale.getWidth(), 3.0f, 1.5f);
        g.setColour (juce::Colour (0xff59c36a));
        g.fillRect (cx - 0.5f, (float) scale.getY(), 1.0f, (float) scale.getHeight());

        const float t = juce::jlimit (-1.0f, 1.0f, cents / 50.0f);
        const float mx = cx + t * (scale.getWidth() * 0.5f);
        const bool pegged = std::abs (cents) > 50.0f;
        g.setColour (inTune ? juce::Colour (0xff59c36a)
                            : (pegged ? juce::Colour (0xffd05b5b) : juce::Colour (0xffd7a24b)));
        g.fillEllipse (mx - 6.0f, scale.getCentreY() - 6.0f, 12.0f, 12.0f);
    }
    else
    {
        g.setColour (juce::Colour (0xff3a4049));
        g.setFont (juce::Font (14.0f));
        g.drawText ("no signal", scale, juce::Justification::centredLeft);
    }
}

void KickFundamentalsEditor::drawSmallRow (juce::Graphics& g, juce::Rectangle<int> area,
                                           const juce::String& tag, int index)
{
    const float freq = processor.getAnalyzer().getFrequency (index);

    int cents = 0;
    const juce::String note = freqToNoteName (freq, cents);
    const bool hasSignal = freq > 0.0f;

    auto row = area;
    g.setColour (juce::Colour (0xff1b1f27));
    g.fillRoundedRectangle (row.toFloat(), 8.0f);

    row.reduce (16, 0);

    g.setColour (juce::Colour (0xff6c7788));
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText (tag, row.removeFromLeft (94), juce::Justification::centredLeft);

    g.setColour (hasSignal ? juce::Colour (0xffdfe4ea) : juce::Colour (0xff3a4049));
    g.setFont (juce::Font (26.0f, juce::Font::bold));
    g.drawText (hasSignal ? note : juce::String ("--"),
                row.removeFromLeft (86), juce::Justification::centredLeft);

    g.setFont (juce::Font (13.0f));
    if (hasSignal)
    {
        const juce::String hz = juce::String (freq, 1) + " Hz";
        const juce::String ct = (cents >= 0 ? "+" : "") + juce::String (cents) + " cents";
        g.setColour (juce::Colour (0xff9aa4b0));
        g.drawText (hz + "   " + ct, row, juce::Justification::centredRight);
    }
}

void KickFundamentalsEditor::drawKnobLabel (juce::Graphics& g, juce::Rectangle<int> area,
                                            const juce::String& text)
{
    g.setColour (juce::Colour (0xff7f8b9c));
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText (text, area.removeFromTop (16), juce::Justification::centred);
}

void KickFundamentalsEditor::drawKnobEnds (juce::Graphics& g, juce::Rectangle<int> area,
                                           const juce::String& leftText,
                                           const juce::String& rightText)
{
    auto band = area.withTrimmedTop (16).withTrimmedBottom (18);
    g.setColour (juce::Colour (0xff5b6472));
    g.setFont (juce::Font (10.5f));
    g.drawText (leftText,  band.removeFromLeft (58),  juce::Justification::centred);
    g.drawText (rightText, band.removeFromRight (58), juce::Justification::centred);
}

void KickFundamentalsEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14171d));

    const auto L = computeLayout (getLocalBounds());

    // Title wordmark (rendered as text for now) + byline logo.
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (25.0f, juce::Font::bold));
    g.drawText ("DRUM FUNDAMENTALS", L.title, juce::Justification::centred);
    if (logoByline.isValid())
        g.drawImageWithin (logoByline, L.byline.getX(), L.byline.getY(),
                           L.byline.getWidth(), L.byline.getHeight(),
                           juce::RectanglePlacement::centred);

    drawTabs (g, L.tabs);

    // Frequency range for the active drum.
    g.setColour (juce::Colour (0xff7f8b9c));
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText ("FUNDAMENTAL RANGE", L.range, juce::Justification::centredLeft);
    g.setColour (accent);
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    g.drawText (rangeTextFor (currentDrum), L.range, juce::Justification::centredRight);

    drawHero (g, L.hero);

    g.setColour (juce::Colour (0xff7f8b9c));
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText ("TUNE TO", L.targetLabel, juce::Justification::centredLeft);

    drawSmallRow (g, L.row2, partialTags[0], 1);
    drawSmallRow (g, L.row3, partialTags[1], 2);

    drawKnobLabel (g, L.gate,     "GATE");
    drawKnobLabel (g, L.response, "RESPONSE");
    drawKnobEnds  (g, L.gate,     "Sensitive", "Strict");
    drawKnobEnds  (g, L.response, "Fast",      "Steady");

    g.setColour (juce::Colour (0xff5b6472));
    g.setFont (juce::Font (11.0f));
    g.drawText ("hover over anything for tips", L.hint, juce::Justification::centred);
}
