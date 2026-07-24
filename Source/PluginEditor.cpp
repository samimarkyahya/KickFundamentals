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
    switch (d) { case 1: return "TOMS / PERC"; case 2: return "SNARE"; case 3: return "CYMBALS"; default: return "KICK"; }
}
const char* KickFundamentalsEditor::heroLabelFor (int d)
{
    switch (d) { case 1: return "PERC NOTE"; case 2: return "SNARE NOTE"; case 3: return "CYMBAL NOTE"; default: return "KICK NOTE"; }
}
juce::String KickFundamentalsEditor::rangeTextFor (int d)
{
    switch (d)
    {
        case 1:  return "60 - 500 Hz";
        case 2:  return "120 - 500 Hz";
        case 3:  return "1000 - 15000 Hz";
        default: return "30 - 250 Hz";
    }
}

KickFundamentalsEditor::KickFundamentalsEditor (KickFundamentalsProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    logoTitle  = juce::ImageCache::getFromMemory (BinaryData::logo_title_png,
                                                  BinaryData::logo_title_pngSize);
    logoByline = juce::ImageCache::getFromMemory (BinaryData::logo_byline_png,
                                                  BinaryData::logo_byline_pngSize);

    auto& apvts = processor.getAPVTS();
    keyParam   = apvts.getRawParameterValue (KickFundamentalsProcessor::kKeyId);
    scaleParam = apvts.getRawParameterValue (KickFundamentalsProcessor::kScaleId);
    drumParam  = apvts.getRawParameterValue (KickFundamentalsProcessor::kDrumId);

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

    // --- Key + Scale selectors -----------------------------------------------
    auto styleCombo = [] (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff20242c));
        c.setColour (juce::ComboBox::textColourId,       juce::Colours::white);
        c.setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff3a424e));
        c.setColour (juce::ComboBox::arrowColourId,      juce::Colour (0xff9aa4b0));
    };

    static const juce::StringArray keyItems { "Auto", "C", "C#", "D", "D#", "E", "F",
                                              "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < keyItems.size(); ++i)
        keyBox.addItem (keyItems[i], i + 1);
    styleCombo (keyBox);
    addAndMakeVisible (keyBox);

    scaleBox.addItem ("Major", 1);
    scaleBox.addItem ("Minor", 2);
    styleCombo (scaleBox);
    addAndMakeVisible (scaleBox);

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

    keyBox.setTooltip ("Pick your song's key. The plugin then shows the smallest "
                       "tuning nudge that lands the drum on an in-key note. Leave "
                       "on Auto to just read the nearest note.");
    scaleBox.setTooltip ("Major or minor - sets which notes count as in-key. "
                         "The plugin always aims for the closest one, so the drum "
                         "only moves as little as possible.");
    gateSlider.setTooltip ("How loud a hit must be to read. Raise if it flickers "
                           "on noise; lower for quiet hits.");
    responseSlider.setTooltip ("Left = snappy. Right = rock-steady. "
                               "Turn right if the note jitters.");

    // --- Parameter attachments ----------------------------------------------
    gateAtt     = std::make_unique<SliderAtt> (apvts, KickFundamentalsProcessor::kGateId,     gateSlider);
    responseAtt = std::make_unique<SliderAtt> (apvts, KickFundamentalsProcessor::kResponseId, responseSlider);
    keyAtt      = std::make_unique<ComboAtt>  (apvts, KickFundamentalsProcessor::kKeyId,      keyBox);
    scaleAtt    = std::make_unique<ComboAtt>  (apvts, KickFundamentalsProcessor::kScaleId,    scaleBox);

    applyDrumTheme (drumParam != nullptr ? (int) (drumParam->load() + 0.5f) : 0);

    setSize (470, 630);
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

    // The Scale selector only makes sense once a Key is chosen.
    const int keyIdx = keyParam != nullptr ? (int) (keyParam->load() + 0.5f) : 0;
    scaleBox.setVisible (keyIdx > 0);

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

namespace
{
    const char* pitchClassName (int pc)
    {
        static const char* names[] =
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((pc % 12) + 12) % 12];
    }

    // The MIDI note (any octave) of pitch-class pc nearest to `midi`.
    float nearestMidiOfPitchClass (float midi, int pc)
    {
        return std::round ((midi - (float) pc) / 12.0f) * 12.0f + (float) pc;
    }

    // Where the drum should be tuned, given the Key (0=Auto,1=C..12=B) and
    // Scale (0=Major,1=Minor). We snap to the NEAREST in-key note so the drum
    // moves as little as possible while still fitting the track.
    struct Target
    {
        float midi = 0.0f;      // MIDI note to aim for (nearest octave)
        int   pc   = 0;         // its pitch class (0=C..11=B)
    };

    Target resolveTarget (float midi, int keyIdx, int scaleIdx)
    {
        Target t;

        // Auto key: aim for the nearest chromatic note.
        if (keyIdx <= 0)
        {
            const int nearest = juce::roundToInt (midi);
            t.midi = (float) nearest;
            t.pc   = ((nearest % 12) + 12) % 12;
            return t;
        }

        const int keyPc = keyIdx - 1;

        // The seven scale degrees (semitones from the root) for major / minor.
        static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minor[7] = { 0, 2, 3, 5, 7, 8, 10 }; // natural minor
        const int* scale = (scaleIdx == 1) ? minor : major;

        // Not all in-key notes are equally good landing spots. Strong, stable
        // degrees (root, then 5th, then 3rd) get a "pull" in cents so a near-tie
        // resolves to the musically solid note instead of a passing tone. This
        // only tips close calls — a genuinely closer note still wins, and a far
        // root never drags the drum across the octave.
        //
        // The root's pull is a full semitone (120c) so that a drum within a
        // semitone of the root always tunes UP to the root rather than parking
        // on the major 7th (the leading tone) sitting right below it — that note
        // is in the scale but is the least stable place to rest. Only the major
        // 7th is close enough to be affected; the 2nd above and the minor 7th
        // below are a whole step away and stay put.
        static const float bonus[7] = { 120.0f, 0.0f, 30.0f, 0.0f, 45.0f, 0.0f, 0.0f };

        // Pick the smallest weighted move: cents distance minus the degree bonus.
        float bestScore = 1.0e9f;
        for (int s = 0; s < 7; ++s)
        {
            const int   pc    = (keyPc + scale[s]) % 12;
            const float m     = nearestMidiOfPitchClass (midi, pc);
            const float dist  = std::abs (midi - m) * 100.0f; // cents
            const float score = dist - bonus[s];
            if (score < bestScore)
            {
                bestScore = score;
                t.midi    = m;
                t.pc      = pc;
            }
        }
        return t;
    }
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

    auto modeRow  = b.removeFromTop (46);
    auto keyCol   = modeRow.removeFromLeft (150);
    modeRow.removeFromLeft (18);
    auto scaleCol = modeRow.removeFromLeft (150);
    L.keyLabel = keyCol.removeFromTop (16); keyCol.removeFromTop (2);
    L.keyBox   = keyCol.removeFromTop (26);
    L.scaleLabel = scaleCol.removeFromTop (16); scaleCol.removeFromTop (2);
    L.scaleBox   = scaleCol.removeFromTop (26);
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

    keyBox.setBounds (L.keyBox);
    scaleBox.setBounds (L.scaleBox);
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
    const juce::String note = freqToNoteName (freq, centsNearest); // big chromatic readout
    const bool hasSignal = freq > 0.0f;

    const int keyIdx   = keyParam   != nullptr ? (int) (keyParam->load()   + 0.5f) : 0;
    const int scaleIdx = scaleParam != nullptr ? (int) (scaleParam->load() + 0.5f) : 0;

    const float midi = hasSignal ? 69.0f + 12.0f * std::log2 (freq / 440.0f) : 0.0f;
    const Target target = hasSignal ? resolveTarget (midi, keyIdx, scaleIdx) : Target {};

    // Signed cents of the drum relative to the target (+ = drum is sharp/above).
    const float cents  = hasSignal ? (midi - target.midi) * 100.0f : 0.0f;
    const bool  inTune = hasSignal && std::abs (cents) <= 5.0f;

    // Non-ASCII glyphs (CharPointer_UTF8 avoids mojibake).
    const juce::String midDot  (juce::CharPointer_UTF8 ("\xc2\xb7"));       // ·
    const juce::String arrowUp (juce::CharPointer_UTF8 ("\xe2\x86\x91"));   // ↑
    const juce::String arrowDn (juce::CharPointer_UTF8 ("\xe2\x96\xbc"));   // ▼
    const juce::String centCh  (juce::CharPointer_UTF8 ("\xc2\xa2"));       // ¢

    auto panel = area;
    g.setColour (juce::Colour (0xff262c36));
    g.fillRoundedRectangle (panel.toFloat(), 10.0f);
    // Accent bar along the top of the hero panel.
    g.setColour (accent.withAlpha (0.85f));
    g.fillRoundedRectangle ((float) panel.getX(), (float) panel.getY(),
                            (float) panel.getWidth(), 3.0f, 1.5f);

    auto inner = panel.reduced (18, 12);

    // --- Label row: hero label (+ key context) on the left, Hz on the right ---
    auto labelRow = inner.removeFromTop (18);
    juce::String heroLabel (heroLabelFor (currentDrum));
    if (keyIdx > 0)
        heroLabel += "  " + midDot + "  " + juce::String (pitchClassName (keyIdx - 1))
                   + (scaleIdx == 1 ? " minor" : " major");

    g.setColour (juce::Colour (0xff7f8b9c));
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    g.drawText (heroLabel, labelRow, juce::Justification::centredLeft);

    if (hasSignal)
    {
        g.setColour (juce::Colour (0xffb7c0cc));
        g.setFont (juce::Font (14.0f));
        g.drawText (juce::String (freq, 1) + " Hz", labelRow, juce::Justification::centredRight);
    }

    // --- Big chromatic note ---------------------------------------------------
    auto noteArea = inner.removeFromTop (66);
    g.setColour (hasSignal ? juce::Colours::white : juce::Colour (0xff3a4049));
    g.setFont (juce::Font (60.0f, juce::Font::bold));
    g.drawText (hasSignal ? note : juce::String ("--"),
                noteArea, juce::Justification::centredLeft);

    // --- Plain-language "turn this way" readout -------------------------------
    auto readoutArea = inner.removeFromTop (22);
    if (hasSignal)
    {
        juce::String readout;
        juce::Colour colour;

        juce::String targetTag;
        if (keyIdx > 0)
            targetTag = "  " + midDot + " to " + juce::String (pitchClassName (target.pc));

        if (inTune)
        {
            readout = "in tune" + targetTag;
            colour  = juce::Colour (0xff59c36a);
        }
        else
        {
            const bool  up  = cents < 0.0f;            // drum below target → raise it
            const int   c   = juce::roundToInt (std::abs (cents));
            const int   semis = c / 100;
            const int   rem   = c % 100;

            juce::String amt;
            if (semis > 0)
            {
                amt = juce::String (semis) + (semis == 1 ? " semitone" : " semitones");
                if (rem > 0)
                    amt += " + " + juce::String (rem) + centCh;
            }
            else
            {
                amt = juce::String (c) + centCh;
            }

            readout = (up ? arrowUp : arrowDn) + " turn " + (up ? "up" : "down") + " " + amt + targetTag;
            colour  = juce::Colour (0xffd7a24b);
        }

        g.setColour (colour);
        g.setFont (juce::Font (16.0f, juce::Font::bold));
        g.drawText (readout, readoutArea, juce::Justification::centredLeft);
    }

    // --- Tuning meter ---------------------------------------------------------
    auto scale = inner.removeFromTop (20).reduced (2, 5);
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

    // Title + byline logos.
    if (logoTitle.isValid())
        g.drawImageWithin (logoTitle, L.title.getX(), L.title.getY(),
                           L.title.getWidth(), L.title.getHeight(),
                           juce::RectanglePlacement::centred);
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

    // KEY / SCALE selector labels (uppercase, above each dropdown).
    const int keyIdx = keyParam != nullptr ? (int) (keyParam->load() + 0.5f) : 0;
    g.setColour (juce::Colour (0xff7f8b9c));
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText ("KEY", L.keyLabel, juce::Justification::centredLeft);
    if (keyIdx > 0) // Scale only applies once a key is chosen.
        g.drawText ("SCALE", L.scaleLabel, juce::Justification::centredLeft);

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
