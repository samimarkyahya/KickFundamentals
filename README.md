# Drum Fundamentals — by Faderhead

A **VST3 / AU / Standalone** metering plugin. Drop it on a drum channel as an
insert and it continuously shows the tuning of the hit: a big main note with a
±50-cent tuning dot, plus two secondary partials — each with its exact frequency
and cents deviation.

Pick the drum with the **tab bar** (Kick · Snare · Toms/Perc · Cymbals/Metal).
Each tab recolours the interface and switches the analysis to that drum's range:

| Tab | Colour | Range | Main note = |
|---|---|---|---|
| Kick | blue | 30–250 Hz | lowest fundamental |
| Snare | red | 120–500 Hz | lowest fundamental |
| Toms / Perc | green | 60–500 Hz | lowest fundamental |
| Cymbals / Metal | light grey | 1000–15000 Hz | **loudest** ring partial |

The **Toms / Perc** tab suits any pitched percussion with a clear fundamental —
toms, congas, bongos, timbales, djembe, rototoms. Metallic percussion (cowbell,
tambourine, bells) has no low fundamental, so use the **Cymbals / Metal** tab.

Pitched drums (kick/toms/snare) read the lowest fundamental as the main note.
Cymbals have no low fundamental, so the ear follows the loudest ring — that tab
tracks the strongest partial instead, letting you match a hat/ride to the key.

It is a *display-only* insert: audio passes through completely untouched, so it
is safe to leave anywhere in the chain.

## How it works

- Audio is summed to mono and fed through a windowed FFT (16384-point, Hann).
- Spectral peaks inside the selected drum's range are detected. The three
  strongest are kept, then ordered for display.
- Each peak is refined with **parabolic interpolation** on the log-magnitude
  spectrum, so the reported frequency is accurate to well under 1 Hz even though
  the raw FFT bins are ~2.7 Hz apart.
- Frequency is converted to the nearest musical note: `midi = 69 + 12·log2(f/440)`.

Because a kick has a pitch envelope (it glides down in the first few tens of ms),
the readout reflects the sustained body of the hit, which is what you usually
mean by "the note of the kick".

**Cymbals** have no low fundamental — the ear follows the loudest ring, but a hat
is a dense cloud of fluctuating inharmonic partials, so a plain "loudest bin" pick
hops around frame to frame. The Cymbals/Metal tab therefore has a **MODE** switch:

- **Ring** (default) — the pitch comes from the **sustained-dominant band**: energy
  is accumulated into fixed log-spaced bands over time, and the readout only
  switches bands once a challenger stays clearly louder (selection hysteresis).
  Response sets how sticky that is. Best for cymbals and ringing metal.
- **Brightness** — the pitch is the **spectral centroid** (energy-weighted mean
  frequency). It is an average rather than a selection, so it stays steady even on
  short, dry metallic hits that have no sustained ring to lock onto. There are no
  partials in this mode (the centroid is a single value), and the hero reads
  "BRIGHTNESS".

## Building

You need [CMake](https://cmake.org/) (≥ 3.22) and a C++17 compiler. JUCE is
downloaded automatically the first time you configure — nothing to vendor.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The built VST3 is copied to your user plugin folder automatically
(`COPY_PLUGIN_AFTER_BUILD`). A Standalone app is also built so you can test
without a DAW.

Already have JUCE checked out locally? Skip the download:

```bash
cmake -B build -DJUCE_LOCAL_DIR=/path/to/JUCE -DCMAKE_BUILD_TYPE=Release
```

### Cross-compiling a Windows VST3 from Linux (MinGW)

A Windows `.vst3` can be built on Linux with the MinGW-w64 toolchain. JUCE 8's
mandatory Direct2D 1.3 headers aren't in MinGW, so the cross build pins JUCE 7:

```bash
sudo apt-get install -y mingw-w64
cmake -B build-win \
  -DCMAKE_TOOLCHAIN_FILE=mingw-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DKF_JUCE_TAG=7.0.12
cmake --build build-win --config Release --target KickFundamentals_VST3 -j4
```

`mingw-toolchain.cmake` static-links the C++ runtime and explicitly links the
Win32 libraries JUCE normally auto-links via MSVC `#pragma comment(lib)`. The
result is a self-contained `Kick Fundamentals.vst3` under
`build-win/KickFundamentals_artefacts/Release/VST3/`. Note MinGW-built VST3s
occasionally fail to load in MSVC-only hosts; a native Visual Studio build is the
guaranteed-clean route.

### AU build (macOS)

`AU` is already in the `FORMATS` list, so building **on a Mac** produces the
Audio Unit (`.component`) alongside the VST3 automatically — that's the format
Logic Pro and GarageBand load. On Windows/Linux the AU format is simply ignored.
AU can only be built on macOS (it needs Apple's AudioUnit SDK); it cannot be
cross-compiled.

## Tuning

All knobs live near the top of `Source/KickAnalyzer.h`:

| Setting | Meaning | Default |
|---|---|---|
| `fftOrder` | FFT size = `2^fftOrder`. Larger = finer low-end resolution but slower updates. | `14` (16384) |
| `minFreqHz` / `maxFreqHz` | Frequency window searched for fundamentals. | `30` / `300` Hz |
| `relativeThresholdDb` | How far below the strongest peak a partial can be and still count. | `-40` dB |

## Controls

| Control | What it does |
|---|---|
| **Key** | `Auto` reads the nearest chromatic note. Pick your song's key and the hero shows the *smallest* tuning nudge that lands the drum on an in-key note. |
| **Scale** | Only shown once a key is chosen. `Major` / `Minor` sets which seven notes count as in-key; the plugin always snaps to the closest one, so the drum barely moves while still fitting the track. |
| **Gate** | Below this input level the display holds its last reading instead of tracking the silence between hits. |
| **Response** | Fast ↔ steady. Trades responsiveness for a rock-steady readout (drives the smoothing amounts, and the cymbal band-selection stickiness). |

All are saved with the project and exposed as host-automatable parameters.

## Layout

```
CMakeLists.txt              # build + JUCE fetch + logo asset embedding
Assets/                     # embedded logo artwork (BinaryData)
Source/
  KickAnalyzer.{h,cpp}      # FFT + peak detection (the DSP)
  PluginProcessor.{h,cpp}   # passthrough insert, feeds the analyser
  PluginEditor.{h,cpp}      # the live note display
```
