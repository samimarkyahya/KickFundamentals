#include "KickAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <vector>

KickAnalyzer::KickAnalyzer()
{
    for (auto& f : freqHz)  f.store (0.0f);
    for (auto& l : levelDb) l.store (-100.0f);
}

void KickAnalyzer::prepare (double sampleRate)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 44100.0;

    writePos   = 0;
    hopCounter = 0;
    ring.fill (0.0f);
    fftData.fill (0.0f);
    smoothedMag.fill (0.0f);
    shownFreq.fill (0.0f);
    haveShown = false;
    nextBlockReady.store (false);

    // Onset detection / body-capture timing.
    envelope          = 0.0f;
    envDecay          = (float) std::exp (-1.0 / (0.050 * sampleRateHz)); // ~50 ms release
    wasOverThreshold  = false;
    samplesSinceOnset = 1 << 30;
    minOnsetGap       = (int) (0.100 * sampleRateHz);  // ignore re-triggers for 100 ms
    bodyDelaySamples  = (int) (0.055 * sampleRateHz);  // skip the first ~55 ms (punch/glide)
    bodyArmed         = false;
    bodyDelayCounter  = 0;
    captureIndex      = 0;
    captureBuf.fill (0.0f);
}

bool KickAnalyzer::stageBlockFrom (const std::array<float, fftSize>& src, int startIndex) noexcept
{
    // Copy fftSize samples starting (circularly) at startIndex into fftData,
    // but only if the message thread has consumed the previous block.
    if (nextBlockReady.load())
        return false;

    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = src[(size_t) ((startIndex + i) % fftSize)];

    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    nextBlockReady.store (true);
    return true;
}

void KickAnalyzer::pushSample (float sample) noexcept
{
    const bool bodyOnly = paramBodyOnly.load();

    // --- Onset detection (peak-follower with release) ------------------------
    const float a = std::abs (sample);
    envelope = juce::jmax (a, envelope * envDecay);

    const float onsetThreshold = juce::Decibels::decibelsToGain (paramGateDb.load());
    const bool  over = envelope > onsetThreshold;

    if (over && ! wasOverThreshold && samplesSinceOnset > minOnsetGap)
    {
        samplesSinceOnset = 0;
        if (bodyOnly && ! bodyArmed)
        {
            bodyArmed        = true;
            bodyDelayCounter = bodyDelaySamples;
            captureIndex     = 0;
        }
    }
    wasOverThreshold = over;
    if (samplesSinceOnset < (1 << 30)) ++samplesSinceOnset;

    if (bodyOnly)
    {
        // Fill a dedicated window with the sustained body only.
        if (bodyArmed)
        {
            if (bodyDelayCounter > 0)
            {
                --bodyDelayCounter;
            }
            else
            {
                captureBuf[(size_t) captureIndex++] = sample;
                if (captureIndex >= fftSize)
                {
                    stageBlockFrom (captureBuf, 0);
                    bodyArmed    = false;
                    captureIndex = 0;
                }
            }
        }
    }
    else
    {
        // Whole-hit path: continuous overlapping analysis.
        ring[(size_t) writePos] = sample;
        writePos = (writePos + 1) % fftSize;

        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;
            // writePos points at the oldest sample: read the window in order.
            stageBlockFrom (ring, writePos);
        }
    }
}

void KickAnalyzer::processIfReady()
{
    if (! nextBlockReady.load())
        return;

    analyseCurrentBlock();
    nextBlockReady.store (false);
}

void KickAnalyzer::analyseCurrentBlock()
{
    // --- Noise gate: hold the last reading between hits ----------------------
    double sumSq = 0.0;
    for (int i = 0; i < fftSize; ++i)
        sumSq += (double) fftData[(size_t) i] * (double) fftData[(size_t) i];

    const float rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (sumSq / (double) fftSize));

    if (rmsDb < paramGateDb.load())
        return; // too quiet — keep displaying the previous kick's notes

    const float magSmoothing  = paramMagSmoothing.load();
    const float freqSmoothing = paramFreqSmoothing.load();

    // --- FFT -----------------------------------------------------------------
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    // fftData[0 .. fftSize/2] now holds magnitudes.

    // --- Spectral averaging: loud, stable body dominates; noise averages out -
    const int halfSize = fftSize / 2;
    for (int i = 0; i <= halfSize; ++i)
        smoothedMag[(size_t) i] = magSmoothing * smoothedMag[(size_t) i]
                                + (1.0f - magSmoothing) * fftData[(size_t) i];

    const auto binToFreq = [this] (double bin) { return bin * sampleRateHz / (double) fftSize; };

    const float minFreqHz = paramMinFreq.load();
    const float maxFreqHz = paramMaxFreq.load();

    const int minBin = juce::jmax (1, (int) std::floor (minFreqHz * fftSize / sampleRateHz));
    const int maxBin = juce::jmin (halfSize - 1, (int) std::ceil (maxFreqHz * fftSize / sampleRateHz));

    // Reference level = strongest bin in the search range (of the smoothed spectrum).
    float maxMag = 1.0e-9f;
    for (int i = minBin; i <= maxBin; ++i)
        maxMag = juce::jmax (maxMag, smoothedMag[(size_t) i]);

    const float threshold = maxMag * juce::Decibels::decibelsToGain (relativeThresholdDb);

    struct Peak { float freq; float mag; };
    std::vector<Peak> peaks;

    for (int i = minBin; i <= maxBin; ++i)
    {
        const float m = smoothedMag[(size_t) i];

        const bool isLocalMax = m > smoothedMag[(size_t) (i - 1)]
                             && m >= smoothedMag[(size_t) (i + 1)]
                             && m >= threshold;
        if (! isLocalMax)
            continue;

        // Parabolic (quadratic) interpolation on log magnitudes refines the
        // peak location to a fractional bin — essential for accurate low notes.
        const float la = std::log (juce::jmax (smoothedMag[(size_t) (i - 1)], 1.0e-9f));
        const float lb = std::log (juce::jmax (smoothedMag[(size_t) i],       1.0e-9f));
        const float lc = std::log (juce::jmax (smoothedMag[(size_t) (i + 1)], 1.0e-9f));

        const float denom = la - 2.0f * lb + lc;
        float delta = denom != 0.0f ? 0.5f * (la - lc) / denom : 0.0f;
        delta = juce::jlimit (-0.5f, 0.5f, delta);

        peaks.push_back ({ (float) binToFreq (i + delta), m });
    }

    // Keep the three strongest peaks...
    std::sort (peaks.begin(), peaks.end(),
               [] (const Peak& x, const Peak& y) { return x.mag > y.mag; });
    if ((int) peaks.size() > numFundamentals)
        peaks.resize (numFundamentals);

    // ...then order them for display. Pitched drums (kick/toms/snare) read the
    // LOWEST partial as the main note. Cymbals have no low fundamental, so the
    // ear follows the LOUDEST ring — keep the magnitude order (main = loudest).
    if (! paramMainLoudest.load())
        std::sort (peaks.begin(), peaks.end(),
                   [] (const Peak& x, const Peak& y) { return x.freq < y.freq; });

    for (int i = 0; i < numFundamentals; ++i)
    {
        if (i < (int) peaks.size())
        {
            float f = peaks[(size_t) i].freq;

            // Smooth each slot's frequency so the cents/Hz readout settles
            // rather than dithering around the true value.
            if (haveShown && shownFreq[(size_t) i] > 0.0f)
                f = freqSmoothing * shownFreq[(size_t) i] + (1.0f - freqSmoothing) * f;

            shownFreq[(size_t) i] = f;
            freqHz[(size_t) i].store (f);
            levelDb[(size_t) i].store (juce::Decibels::gainToDecibels (peaks[(size_t) i].mag / maxMag));
        }
        else
        {
            shownFreq[(size_t) i] = 0.0f;
            freqHz[(size_t) i].store (0.0f);
            levelDb[(size_t) i].store (-100.0f);
        }
    }

    haveShown = true;
}

float KickAnalyzer::getFrequency (int index) const noexcept
{
    if (index < 0 || index >= numFundamentals) return 0.0f;
    return freqHz[(size_t) index].load();
}

float KickAnalyzer::getLevelDb (int index) const noexcept
{
    if (index < 0 || index >= numFundamentals) return -100.0f;
    return levelDb[(size_t) index].load();
}
