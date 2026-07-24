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

    // Log-spaced band edges for the cymbal stable-dominant estimator.
    for (int b = 0; b <= numBands; ++b)
        bandEdgeHz[(size_t) b] = bandRangeLoHz
            * std::pow (bandRangeHiHz / bandRangeLoHz, (float) b / (float) numBands);
    bandAccum.fill (0.0f);
    selectedBand  = -1;
    bandSwitchCtr = 0;
}

int KickAnalyzer::binForBandEdge (int edgeIndex) const noexcept
{
    const int e = juce::jlimit (0, numBands, edgeIndex);
    return juce::jlimit (1, fftSize / 2 - 1,
                         (int) std::lround (bandEdgeHz[(size_t) e] * fftSize / sampleRateHz));
}

// Accumulate each band's energy into a slow EMA, then choose the sustained
// dominant band. The selection only moves when a different band stays clearly
// louder (bandSwitchRatio) for bandHoldFrames consecutive blocks, so the pick
// is stable even while individual partials flicker.
void KickAnalyzer::updateBandEnergies (float minFreqHz, float maxFreqHz) noexcept
{
    const float acc = paramMagSmoothing.load(); // steadier Response = slower accumulation

    // Energy comes from the RAW magnitude spectrum (fftData), so this EMA is the
    // only time-smoothing on the selection — steady but not double-lagged.
    for (int b = 0; b < numBands; ++b)
    {
        const int lo = binForBandEdge (b);
        const int hi = binForBandEdge (b + 1);
        float e = 0.0f;
        for (int i = lo; i < hi; ++i)
            e += fftData[(size_t) i] * fftData[(size_t) i];

        bandAccum[(size_t) b] = acc * bandAccum[(size_t) b] + (1.0f - acc) * e;
    }

    const auto centreInRange = [this, minFreqHz, maxFreqHz] (int b)
    {
        const float c = std::sqrt (bandEdgeHz[(size_t) b] * bandEdgeHz[(size_t) (b + 1)]);
        return c >= minFreqHz && c <= maxFreqHz;
    };

    // Strongest band whose centre lies inside the active search range.
    int   challenger = -1;
    float best       = 0.0f;
    for (int b = 0; b < numBands; ++b)
        if (centreInRange (b) && bandAccum[(size_t) b] > best)
        {
            best       = bandAccum[(size_t) b];
            challenger = b;
        }

    if (challenger < 0)
        return; // no energy in range — hold the current selection

    // (Re)seat the selection if we have none, it has gone silent, or it drifted
    // out of the current range (e.g. after a drum/range change).
    if (selectedBand < 0
        || bandAccum[(size_t) selectedBand] <= 0.0f
        || ! centreInRange (selectedBand))
    {
        selectedBand  = challenger;
        bandSwitchCtr = 0;
        return;
    }

    if (challenger != selectedBand
        && bandAccum[(size_t) challenger] > bandAccum[(size_t) selectedBand] * bandSwitchRatio)
    {
        if (++bandSwitchCtr >= bandHoldFrames)
        {
            selectedBand  = challenger;
            bandSwitchCtr = 0;
        }
    }
    else
    {
        bandSwitchCtr = 0;
    }
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
    // Continuous overlapping analysis.
    ring[(size_t) writePos] = sample;
    writePos = (writePos + 1) % fftSize;

    if (++hopCounter >= hopSize)
    {
        hopCounter = 0;
        // writePos points at the oldest sample: read the window in order.
        stageBlockFrom (ring, writePos);
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

    // Gate on level to hold the last reading between hits.
    if (rmsDb < paramGateDb.load())
        return; // too quiet — keep displaying the previous reading

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

    // Parabolic (quadratic) interpolation on log magnitudes refines a peak bin
    // to a fractional bin — essential for an accurate frequency readout.
    const auto refinePeakInBins = [&] (int lo, int hi) -> Peak
    {
        lo = juce::jmax (lo, minBin);
        hi = juce::jmin (hi, maxBin);
        int   bestBin = -1;
        float bestMag = 0.0f;
        for (int i = lo; i <= hi; ++i)
            if (smoothedMag[(size_t) i] > bestMag) { bestMag = smoothedMag[(size_t) i]; bestBin = i; }

        if (bestBin < 1 || bestBin >= halfSize)
            return { 0.0f, 0.0f };

        const float la = std::log (juce::jmax (smoothedMag[(size_t) (bestBin - 1)], 1.0e-9f));
        const float lb = std::log (juce::jmax (smoothedMag[(size_t) bestBin],       1.0e-9f));
        const float lc = std::log (juce::jmax (smoothedMag[(size_t) (bestBin + 1)], 1.0e-9f));

        const float denom = la - 2.0f * lb + lc;
        float delta = denom != 0.0f ? 0.5f * (la - lc) / denom : 0.0f;
        delta = juce::jlimit (-0.5f, 0.5f, delta);

        return { (float) binToFreq (bestBin + delta), smoothedMag[(size_t) bestBin] };
    };

    if (paramMainLoudest.load() && paramUseCentroid.load())
    {
        // --- Cymbals / Brightness: spectral centroid -------------------------
        // Energy-weighted mean frequency over the range. It is an average, not a
        // selection, so it stays stable even on short, dry, noisy hits that have
        // no clear partial to lock onto. Bins near the noise floor are excluded.
        double wsum = 0.0, fsum = 0.0;
        for (int i = minBin; i <= maxBin; ++i)
        {
            const float m = smoothedMag[(size_t) i];
            if (m < threshold)
                continue;
            const double w = (double) m * (double) m;
            wsum += w;
            fsum += w * binToFreq (i);
        }
        if (wsum > 0.0)
            peaks.push_back ({ (float) (fsum / wsum), maxMag });
    }
    else if (paramMainLoudest.load())
    {
        // --- Cymbals / Ring: sustained-dominant band with hysteresis ---------
        updateBandEnergies (minFreqHz, maxFreqHz);

        // Main = refined peak inside the currently selected band.
        if (selectedBand >= 0)
        {
            const Peak p = refinePeakInBins (binForBandEdge (selectedBand),
                                             binForBandEdge (selectedBand + 1));
            if (p.freq > 0.0f)
                peaks.push_back (p);
        }

        // 2nd/3rd = next strongest bands, ranked by accumulated energy, skipping
        // the selection and any band that refines to a near-duplicate frequency.
        std::array<int, numBands> order {};
        for (int b = 0; b < numBands; ++b) order[(size_t) b] = b;
        std::sort (order.begin(), order.end(),
                   [this] (int a, int b) { return bandAccum[(size_t) a] > bandAccum[(size_t) b]; });

        for (int idx = 0; idx < numBands && (int) peaks.size() < numFundamentals; ++idx)
        {
            const int b = order[(size_t) idx];
            if (b == selectedBand || bandAccum[(size_t) b] <= 0.0f)
                continue;

            // Only bands whose centre sits inside the active search range.
            const float centre = std::sqrt (bandEdgeHz[(size_t) b] * bandEdgeHz[(size_t) (b + 1)]);
            if (centre < minFreqHz || centre > maxFreqHz)
                continue;

            const Peak p = refinePeakInBins (binForBandEdge (b), binForBandEdge (b + 1));
            if (p.freq <= 0.0f)
                continue;

            bool duplicate = false;
            for (const auto& e : peaks)
                if (juce::jmax (p.freq, e.freq) / juce::jmax (1.0f, juce::jmin (p.freq, e.freq)) < 1.12f)
                    { duplicate = true; break; } // within ~2 semitones of an existing pick
            if (! duplicate)
                peaks.push_back (p);
        }
    }
    else
    {
        // --- Pitched drums (kick/toms/snare): local maxima, main = LOWEST ----
        for (int i = minBin; i <= maxBin; ++i)
        {
            const float m = smoothedMag[(size_t) i];

            const bool isLocalMax = m > smoothedMag[(size_t) (i - 1)]
                                 && m >= smoothedMag[(size_t) (i + 1)]
                                 && m >= threshold;
            if (! isLocalMax)
                continue;

            const Peak p = refinePeakInBins (i - 1, i + 1);
            if (p.freq > 0.0f)
                peaks.push_back ({ p.freq, m });
        }

        // Keep the three strongest, then order lowest-first for display.
        std::sort (peaks.begin(), peaks.end(),
                   [] (const Peak& x, const Peak& y) { return x.mag > y.mag; });
        if ((int) peaks.size() > numFundamentals)
            peaks.resize (numFundamentals);
        std::sort (peaks.begin(), peaks.end(),
                   [] (const Peak& x, const Peak& y) { return x.freq < y.freq; });
    }

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
