#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

/**
    Detects the three most prominent low-frequency spectral peaks of the
    incoming signal (i.e. the kick drum's fundamentals) and exposes them,
    ordered from lowest frequency to highest.

    Overlapping frames are analysed continuously and averaged, reflecting the
    whole hit.

    Stability comes from a noise gate (holds the reading between hits), spectral
    averaging (loud body dominates, noise averages out) and per-note frequency
    smoothing. The gate level and the smoothing amount are set at runtime from
    the plugin's parameters.

    Threading model (the standard JUCE spectrum-analyser pattern):
      - pushSample()      is called from the AUDIO thread only.
      - processIfReady()  is called from the MESSAGE thread only (editor timer).
      - setters are called from the AUDIO thread (processBlock) but only touch
        atomics, so they are safe from anywhere.
      - Results are published through atomics so the editor reads them safely.
*/
class KickAnalyzer
{
public:
    // 2^14 = 16384. At 44.1 kHz that is a ~2.7 Hz bin width, which the
    // parabolic interpolation then refines to well under 1 Hz for strong peaks.
    static constexpr int fftOrder        = 14;
    static constexpr int fftSize         = 1 << fftOrder;
    static constexpr int hopSize         = fftSize / 4;   // 75% overlap
    static constexpr int numFundamentals = 3;

    KickAnalyzer();

    void prepare (double sampleRate);

    /** Audio thread: feed one (mono) sample. */
    void pushSample (float sample) noexcept;

    /** Message thread: run the FFT + peak pick if a fresh block is ready. */
    void processIfReady();

    /** Frequency of the Nth fundamental in Hz (0 = lowest). 0.0f if none. */
    float getFrequency (int index) const noexcept;

    /** Level of the Nth fundamental in dB, relative to the strongest peak. */
    float getLevelDb (int index) const noexcept;

    // --- Runtime parameters (set from processBlock) --------------------------
    /** Noise-gate threshold in dBFS. Holds the last reading between hits. */
    void setGateDb (float db) noexcept
    {
        paramGateDb.store (db);
    }

    /** 0 = fast/responsive, 1 = steady/rock-solid. Drives both smoothers. */
    void setResponse (float amount) noexcept
    {
        const float r = juce::jlimit (0.0f, 1.0f, amount);
        paramMagSmoothing .store (0.40f + r * (0.92f - 0.40f));
        paramFreqSmoothing.store (0.30f + r * (0.90f - 0.30f));
    }

    /** Frequency window searched for the drum's partials (per drum type). */
    void setFreqRange (float loHz, float hiHz) noexcept
    {
        paramMinFreq.store (loHz);
        paramMaxFreq.store (hiHz);
    }

    /** false: main readout is the LOWEST partial (kick/toms/snare).
        true:  main readout is the LOUDEST partial (cymbals — the dominant ring
        the ear latches onto, which isn't necessarily the lowest). */
    void setMainIsLoudest (bool b) noexcept { paramMainLoudest.store (b); }

private:
    void analyseCurrentBlock();
    bool stageBlockFrom (const std::array<float, fftSize>& src, int startIndex) noexcept;

    // Cymbal (mainIsLoudest) estimator: accumulate per-band energy over time and
    // select the sustained-dominant band with hysteresis (see .cpp).
    void updateBandEnergies (float minFreqHz, float maxFreqHz) noexcept;
    int  binForBandEdge (int edgeIndex) const noexcept;

    double sampleRateHz = 44100.0;

    juce::dsp::FFT                       fft { fftOrder };
    juce::dsp::WindowingFunction<float>  window { (size_t) fftSize,
                                                  juce::dsp::WindowingFunction<float>::hann };

    // Continuous capture: circular buffer staged every hopSize samples.
    std::array<float, fftSize>       ring {};
    int               writePos   = 0;
    int               hopCounter = 0;

    std::array<float, fftSize * 2>   fftData {};
    std::atomic<bool> nextBlockReady { false };

    // Message-thread analysis state.
    std::array<float, fftSize / 2 + 1>  smoothedMag {};   // spectral EMA
    std::array<float, numFundamentals>  shownFreq {};     // per-slot freq EMA
    bool haveShown = false;

    // How far below the strongest peak a partial can be and still count.
    static constexpr float relativeThresholdDb  = -40.0f;

    // --- Cymbal stable-dominant estimator ------------------------------------
    // A hat is a dense cloud of fluctuating inharmonic partials, so a plain
    // "loudest bin" pick hops between far-apart frequencies frame to frame.
    // Instead we accumulate energy into fixed log-spaced bands over time and
    // pick the consistently-strongest band, only switching once a challenger is
    // clearly and sustainedly louder (selection hysteresis). The pitch is then
    // the refined peak inside the chosen band.
    static constexpr int   numBands        = 30;       // ~2 semitones per band
    static constexpr float bandRangeLoHz   = 500.0f;   // fixed span (indices stay stable)
    static constexpr float bandRangeHiHz   = 16000.0f;
    static constexpr float bandSwitchRatio = 1.6f;     // challenger must beat selected by this...
    static constexpr int   bandHoldFrames  = 4;        // ...for this many frames before switching

    std::array<float, numBands + 1> bandEdgeHz {};   // log-spaced edges (set in prepare)
    std::array<float, numBands>     bandAccum  {};   // per-band energy EMA
    int selectedBand  = -1;
    int bandSwitchCtr = 0;

    // Runtime parameters (see setters). Defaults match the kick.
    std::atomic<float> paramGateDb        { -55.0f };
    std::atomic<float> paramMagSmoothing  {  0.82f };
    std::atomic<float> paramFreqSmoothing {  0.78f };
    std::atomic<float> paramMinFreq       {  30.0f };
    std::atomic<float> paramMaxFreq       { 250.0f };
    std::atomic<bool>  paramMainLoudest   { false };

    std::array<std::atomic<float>, numFundamentals> freqHz;
    std::array<std::atomic<float>, numFundamentals> levelDb;
};
