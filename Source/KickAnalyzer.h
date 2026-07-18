#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

/**
    Detects the three most prominent low-frequency spectral peaks of the
    incoming signal (i.e. the kick drum's fundamentals) and exposes them,
    ordered from lowest frequency to highest.

    Two analysis modes:
      - Whole hit  : overlapping frames are analysed continuously and averaged,
                     reflecting the whole kick.
      - Body only  : on each detected onset the transient/punch is skipped and a
                     single window of the sustained body is analysed, giving the
                     kick's settled resting pitch.

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
    /** Noise-gate / onset threshold in dBFS. */
    void setGateDb (float db) noexcept { paramGateDb.store (db); }

    /** 0 = fast/responsive, 1 = steady/rock-solid. Drives both smoothers. */
    void setResponse (float amount) noexcept
    {
        const float r = juce::jlimit (0.0f, 1.0f, amount);
        paramMagSmoothing .store (0.40f + r * (0.92f - 0.40f));
        paramFreqSmoothing.store (0.30f + r * (0.90f - 0.30f));
    }

    /** false = analyse the whole hit; true = analyse only the settled body. */
    void setBodyOnly (bool b) noexcept { paramBodyOnly.store (b); }

private:
    void analyseCurrentBlock();
    bool stageBlockFrom (const std::array<float, fftSize>& src, int startIndex) noexcept;

    double sampleRateHz = 44100.0;

    juce::dsp::FFT                       fft { fftOrder };
    juce::dsp::WindowingFunction<float>  window { (size_t) fftSize,
                                                  juce::dsp::WindowingFunction<float>::hann };

    // Whole-hit path: circular capture buffer staged every hopSize samples.
    std::array<float, fftSize>       ring {};
    int               writePos   = 0;
    int               hopCounter = 0;

    // Body-only path: onset detection + a dedicated single-shot capture.
    float envelope          = 0.0f;
    float envDecay          = 0.0f;   // per-sample release coefficient
    bool  wasOverThreshold  = false;
    int   samplesSinceOnset = 1 << 30;
    int   minOnsetGap       = 0;      // debounce between onsets (samples)
    int   bodyDelaySamples  = 0;      // skip this much of the punch after onset
    bool  bodyArmed         = false;
    int   bodyDelayCounter  = 0;
    int   captureIndex      = 0;
    std::array<float, fftSize>       captureBuf {};

    std::array<float, fftSize * 2>   fftData {};
    std::atomic<bool> nextBlockReady { false };

    // Message-thread analysis state.
    std::array<float, fftSize / 2 + 1>  smoothedMag {};   // spectral EMA
    std::array<float, numFundamentals>  shownFreq {};     // per-slot freq EMA
    bool haveShown = false;

    // Kick fundamentals live comfortably inside this range.
    static constexpr float minFreqHz            = 30.0f;
    static constexpr float maxFreqHz            = 300.0f;
    static constexpr float relativeThresholdDb  = -40.0f;

    // Runtime parameters (see setters). Defaults match the previous fixed knobs.
    std::atomic<float> paramGateDb        { -55.0f };
    std::atomic<float> paramMagSmoothing  {  0.82f };
    std::atomic<float> paramFreqSmoothing {  0.78f };
    std::atomic<bool>  paramBodyOnly      { false };

    std::array<std::atomic<float>, numFundamentals> freqHz;
    std::array<std::atomic<float>, numFundamentals> levelDb;
};
