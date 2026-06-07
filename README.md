# ANATOMY
![Release](https://img.shields.io/badge/release-v1.0.0-blue)
![License](https://img.shields.io/badge/license-GPLv3-green)
![JUCE](https://img.shields.io/badge/JUCE-8.0.x-blue)
![Platform](https://img.shields.io/badge/platform-Windows%2064bit-lightgrey)
![Downloads](https://img.shields.io/github/downloads/OTODESK4193/ANATOMY/total.svg)

---

## ⚠️ **IMPORTANT: Audio Safety Notice**

> **This plugin can generate LOUD audio output. HEARING PROTECTION IS YOUR RESPONSIBILITY.**
>
> * Always monitor output levels carefully when using speakers or headphones
> * Start with LOW volume and gradually increase
> * **NEVER wear headphones at maximum volume**
> * Take regular breaks during extended use
> * Prolonged exposure to loud sound (≥85 dB SPL) causes permanent hearing loss
> * By using this plugin, you assume FULL responsibility for your hearing safety and equipment protection
>
> **For detailed safety information, see the [Disclaimer](#%EF%B8%8F-disclaimer) section.**

---

##
<img src="Source/Assets/Screenshot.jpg" width="600">

## Overview

**ANATOMY** is a high-performance, open-source VST3 plugin that performs **real-time Transient/Tonal audio separation**, routing each component through its own independent multi-effect chain. Designed for drum processing, sound design, and stem-level mastering in Ableton Live, ANATOMY gives you surgical control over the *anatomy* of your sound — transient click, tonal sustain, and the full mix — each with a dedicated suite of 6 professional-grade effects.

Process each layer with **ADAA Saturation**, **BitCrusher**, **Noise Generator**, **OTT Multiband Compression**, **Glue Compressor**, and **Limiter** independently, then **drag finished stems directly into your DAW timeline** via one-click WAV export. ANATOMY is engineered for extreme real-time safety and Ableton Live stability.

## 🎬 Demo Videos

<p align="center">
  <b>Introduction (概要編)</b><br>
  <a href="https://youtu.be/PLACEHOLDER_1">
    <img src="https://img.youtube.com/vi/PLACEHOLDER_1/maxresdefault.jpg"
         alt="ANATOMY - Introduction" width="640" height="360">
  </a>
</p>

## Key Features

### 🔬 Real-Time Transient / Tonal Separation

ANATOMY splits any incoming audio into three independently processable signal paths:

* **TRANSIENT lane:** The percussive attack/click component of your signal — transient punch, snap, and impact.
* **TONAL lane:** The sustained, harmonic, and pitched content — body, tone, and decay.
* **FULL MIX lane:** The unprocessed combined signal, available for parallel processing or final shaping.

Each lane features independent **Pitch**, **Gain**, and **shape controls**, giving you precise sculpting power over separation behaviour before effects are even applied.

### 🎛️ 6-Effect Chain per Lane (18 Total)

Every lane has access to the same 6 professional-grade DSP modules. Effects are **inserted in any order** via drag-and-drop within the Effect Rack, and their parameters are adjusted in real time via the Parameter Dock:

| # | Effect | Category | Description |
|---|---|---|---|
| 1 | **ADAA Saturation** | Distortion | Anti-Derivative Anti-Aliased soft saturation with Drive, Asymmetry, Output Trim, and Pre-HPF |
| 2 | **BitCrusher** | Lo-Fi | Bit-depth reduction and sample-rate decimation with Jitter for vintage digital character |
| 3 | **Noise Generator** | Texture | Transient noise burst (WHITE / PINK / BROWN / BLUE) with Attack, Decay, Gain, and Band-Pass frequency |
| 4 | **OTT Multiband** | Dynamics | 3-band upward/downward compressor with per-band Up/Down/Gain, crossover tuning, Time, Gate Floor, and Depth |
| 5 | **Glue Compressor** | Dynamics | Bus-style compressor with Threshold, Ratio, Attack, Release, Makeup Gain, and Depth |
| 6 | **Limiter** | Dynamics | Transparent brick-wall limiter with adjustable Ceiling and Dry/Wet |

### 🔀 Drag-and-Drop Effect Ordering

The Effect Rack displays active effects as a **vertically ordered chip list** showing the exact processing chain. Simply drag chips up or down to rearrange the processing order in real time — no menus, no guessing. Right-click any chip for **Remove**, **Move Up**, or **Move Down** options.

### 📤 One-Click Stem Export

Each lane has a dedicated **EXPORT** button. Press once to record the next note/clip playback, then **drag the rendered WAV directly into your Ableton Live timeline** — or any folder on your desktop. Zero plugin freezing, zero rendering dialogs.

### 🎨 Real-Time Waveform Visualization

Three high-resolution waveform displays (FULL MIX / TRANSIENT / TONAL) update in real time as audio plays. A **BEFORE** button allows instant A/B comparison against the unprocessed signal.

### 🔊 Professional Audio Architecture

* **Zero heap allocation on audio thread** — all buffers pre-allocated in `prepareToPlay()`
* **Lock-free SPSC queues** for UI→audio parameter communication
* **`juce::ScopedNoDenormals`** applied at every `processBlock` entry
* **`juce::SmoothedValue`** on all gain parameters to eliminate zipper noise
* Full **Ableton Live** compatibility with VST3 parameter sync and safe destructor ordering

---

## 🖥️ Signal Flow

```
Stereo Input (L/R)
    │
    ├─► [Transient/Tonal Splitter]
    │   ├── TRANSIENT: Click curve, Pitch, Gain
    │   └── TONAL:     Sustain curve, Pitch, Gain, Release
    │
    ├─► [TRANSIENT Effect Chain]         ← Up to 6 effects, user-defined order
    │   ├── ADAA Saturation (optional)
    │   ├── BitCrusher      (optional)
    │   ├── Noise Generator (optional)
    │   ├── OTT Multiband   (optional)
    │   ├── Glue Compressor (optional)
    │   └── Limiter         (optional)
    │
    ├─► [TONAL Effect Chain]             ← Identical structure, independent chain
    │
    ├─► [FULL MIX Effect Chain]          ← Runs on original unsplit signal
    │
    ├─► [Stem Export Recorder]           ← Per-lane WAV capture (lock-free)
    │
    └─► Stereo Output (L/R)
        └── Waveform Visualizers (3×)
```

---

## Parameter Reference

### Lane Controls (Transient / Tonal)

| Parameter | Range | Description |
|---|---|---|
| Click Length | 0.0 – 1.0 | Duration of the transient extraction window |
| Click Curve | 0.0 – 1.0 | Envelope shape of the transient (sharp → smooth) |
| Pitch | -24 – +24 semitones | Pitch shift applied to the lane output |
| Gain | -24 – +24 dB | Output gain of the lane |
| Sustain Release | 0.0 – 1.0 | Tonal decay tail length |

### Effect Parameters

#### 🟣 ADAA Saturation

| Parameter | Range | Default | Description |
|---|---|---|---|
| DRIVE | 1.0 – 16.0 | 2.0 | Saturation drive amount |
| ASYMMETRY | 0.0 – 1.0 | 0.0 | Waveform asymmetry (even harmonic content) |
| OUT TRIM | -12 – +12 dB | 0 dB | Output level trim post-saturation |
| PRE HPF | 20 – 2000 Hz | 20 Hz | High-pass filter before saturation (DC/low-end protection) |
| DRY/WET | 0.0 – 1.0 | 1.0 | Parallel dry/wet blend |

#### 🟣 BitCrusher

| Parameter | Range | Default | Description |
|---|---|---|---|
| BITS | 2 – 24 bit | 8 | Bit depth reduction |
| DOWNSAMPLE | 1 – 32× | 4 | Sample rate decimation factor |
| JITTER | 0.0 – 1.0 | 0.0 | Sample-timing jitter amount (vintage instability) |
| DRY/WET | 0.0 – 1.0 | 1.0 | Parallel dry/wet blend |

#### 🟣 Noise Generator

| Parameter | Range | Default | Description |
|---|---|---|---|
| DECAY ms | 1 – 1000 ms | 100 ms | Noise burst decay time |
| GAIN dB | -60 – 0 dB | 0 dB | Noise output level |
| ATTACK ms | 0 – 50 ms | 0 ms | Noise burst attack time |
| BP FREQ | 0 – 4000 Hz | 0 Hz | Band-pass filter center frequency (0 = bypass) |
| TYPE | WHITE / PINK / BROWN / BLUE | WHITE | Noise spectral colour |
| DRY/WET | 0.0 – 1.0 | 1.0 | Parallel dry/wet blend |

#### 🟣 OTT Multiband

**Main Parameters:**

| Parameter | Range | Default | Description |
|---|---|---|---|
| TIME | 0.1 – 10.0 | 1.0 | Attack/release time constant |
| LO/MI XO | 40 – 1000 Hz | 200 Hz | Low/Mid band crossover frequency |
| MI/HI XO | 1000 – 15000 Hz | 2500 Hz | Mid/High band crossover frequency |
| GATE dB | -70 – -20 dB | -45 dB | Noise gate floor |
| DEPTH | 0.0 – 1.0 | 1.0 | Overall compression depth (Dry/Wet) |

**Per-Band Parameters (LOW / MID / HIGH):**

| Parameter | Range | Default | Description |
|---|---|---|---|
| UP | 0.0 – 1.0 | 1.0 | Upward compression amount |
| DOWN | 0.0 – 1.0 | 1.0 | Downward compression amount |
| GAIN dB | -24 – +24 dB | 0 dB | Per-band output gain trim |

#### 🟣 Glue Compressor

| Parameter | Range | Default | Description |
|---|---|---|---|
| THR dBFS | -40 – 0 dB | -18 dB | Compression threshold |
| RATIO | 1.0 – 20.0 | 2.0 | Compression ratio |
| ATK ms | 1 – 100 ms | 30 ms | Attack time |
| REL ms | 10 – 1000 ms | 200 ms | Release time |
| MAKEUP dB | -12 – +12 dB | 0 dB | Makeup gain post-compression |
| DEPTH | 0.0 – 1.0 | 1.0 | Parallel blend (Dry/Wet) |

#### 🟣 Limiter

| Parameter | Range | Default | Description |
|---|---|---|---|
| CEILING dB | -24 – 0 dB | -0.1 dB | Brick-wall output ceiling |
| DRY/WET | 0.0 – 1.0 | 1.0 | Parallel dry/wet blend |

---

## Installation

### VST3 Plugin Installation

1. Download the latest `ANATOMY.vst3` from the [Releases](https://github.com/OTODESK4193/ANATOMY/releases/latest) page.
2. Copy the `.vst3` folder to your VST3 plugin directory:
   ```
   C:\Program Files\Common Files\VST3\
   ```
3. Rescan plugins in Ableton Live (or your DAW of choice).

### Standalone Application

A standalone executable is also provided. Run `ANATOMY.exe` directly — no DAW required.

---

## Build from Source

### Requirements

* **JUCE** 8.0.x — place at `C:/JUCE` or update the path in `CMakeLists.txt`
* **CMake** 3.24 or higher
* **Visual Studio 2022** (MSVC, C++20)
* **AVX2-capable CPU** (required for SIMD-optimized DSP)

### Build Steps

```bash
git clone https://github.com/OTODESK4193/ANATOMY.git
cd ANATOMY
cmake -S . -B out/build/x64-Release -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/x64-Release --config Release
```

The built `.vst3` will appear in `out/build/x64-Release/ANATOMY_artefacts/Release/VST3/`.

---

## System Requirements

* **OS:** Windows 10 / Windows 11 (64-bit)
* **Format:** VST3 / Standalone
* **CPU:** AVX2 support required (Intel Haswell 2013+ / AMD Ryzen 2017+)
* **RAM:** 256 MB minimum
* **Disk:** 50 MB

> ⚠️ **Compatibility Notice:** Compiled and optimized exclusively for Windows 64-bit with AVX2. Verified operation confirmed in **Ableton Live 11 / 12**. Other DAWs (FL Studio, Bitwig, Studio One, Cubase, Reaper) may work but are currently unverified.

---

## Technical Architecture

### Real-Time Safety

ANATOMY adheres to the strictest real-time audio thread constraints:

* **Zero Heap Allocation on Audio Thread** — all buffers pre-allocated in `prepareToPlay()`. No `new`, `malloc`, or `std::vector::push_back` in `processBlock()`.
* **Lock-Free Thread Communication** — `juce::AbstractFifo`-based SPSC queues and `std::atomic` for all UI→audio data transfer.
* **Sample Rate Guard** — `processBlock` validates the current sample rate against the initialized rate on every callback; mismatches trigger an immediate safe re-initialization (Ableton Live protection).
* **Buffer Zero-Clear** — explicit `juce::FloatVectorOperations::clear` on every buffer resize regardless of whether size changed, preventing residual data click artifacts.
* **`ScopedNoDenormals`** — applied at the top of every `processBlock` call to suppress denormal-induced CPU spikes.
* **`juce::SmoothedValue`** — all gain and mix parameters are sample-accurate interpolated to eliminate zipper noise under automation.
* **Safe Destructor Ordering** — `AudioProcessorEditor` explicitly calls `stopTimer()` and `removeAllChildren()` in its destructor to survive Ableton Live's non-standard plugin removal sequence.

### DSP Modules

| Module | Purpose |
|---|---|
| **Transient/Tonal Splitter** | Real-time signal decomposition into percussive and tonal components |
| **ADAA_Saturation** | Anti-Derivative Anti-Aliased waveshaper (2nd order ADAA) |
| **BitCrusher** | Integer quantization + sample-rate decimation with jitter |
| **NoiseGenerator** | Triggered noise burst (WHITE/PINK/BROWN/BLUE) with envelope and band-pass |
| **OTT_Multiband** | 3-band upward + downward compression (TPT crossover, ZDF per-band dynamics) |
| **GlueCompressor** | Feed-forward RMS bus compressor with soft-knee |
| **Limiter** | Peak-hold brick-wall limiter with adjustable ceiling |
| **EffectChain** | Lock-free ordered chain dispatcher with per-lane snapshot management |
| **WaveformComponent** | Real-time rasterized waveform renderer (VBlank-synchronized) |
| **ExportRecorder** | Lock-free per-lane WAV capture with DAW drag-and-drop delivery |

---

## ⚠️ Disclaimer

### Software Warranty

This software is provided "as-is", without any warranty of any kind. While extreme care has been taken to ensure real-time safety, audio stability, and Ableton Live compatibility through rigorous testing and professional DSP practices, unexpected behavior may still occur in edge cases or unsupported hosts. Use at your own risk in mission-critical production environments.

### 🔊 Audio Output & Hearing Protection — Critical Notice

**This plugin can generate loud audio output. User bears sole responsibility for safe audio monitoring.**

* **HEARING DAMAGE RISK:** Prolonged exposure to loud sound (≥85 dB SPL) can cause permanent hearing loss. This risk applies regardless of equipment type or volume settings.
* **SPEAKER / HEADPHONE USE:** Always monitor output levels carefully. Start with low volume and gradually increase. Never wear headphones at maximum volume. Take regular breaks during extended use.
* **SELF RESPONSIBILITY:** The user assumes complete and exclusive responsibility for:
  - Setting appropriate monitoring levels
  - Protecting their own hearing and that of others
  - Equipment safety and damage risk
  - All consequences of audio output usage
* **NO LIABILITY:** The developer(s) and distributor(s) of this software assume no liability for:
  - Hearing loss, tinnitus, or any physical harm
  - Equipment damage due to audio output
  - Any injury or damage caused by improper use
* **USE AT YOUR OWN RISK:** By using this plugin, you acknowledge and accept all audio-related risks inherent to audio production software.

---

**Your hearing is irreplaceable. Prioritize hearing protection at all times.** 🎧

---

## License

This project is free and open-source, distributed under the **GPLv3 License** (inherited via the JUCE framework). You are free to study, modify, and redistribute the source code under the same terms.

---

## Credits

**Developer:** @OTODESK

**Music Production Background:** Electronic Music, Sound Design, DSP Engineering, JUCE plugin development

**Target DAW:** Ableton Live 11 / 12

**Framework:** JUCE 8.0.x

**DSP References:**
- Zavalishin — *"The Art of VA Filter Design"* (2018)
- Parker & Bilbao — *"Field of a Hamiltonian Tonebender"*, DAFx (2013)
- Esqueda, Välimäki & Pekonen — *"Aliasing Reduction in Clipped Signals"*, IEEE (2016)
- Germain & Kronland-Martinet — *"Multiband Compression in the Perceptual Domain"* (2006)

---

## Support

* **Social / Demo:** [@OTODESK](https://x.com/kijyoumusic)
* [![Website](https://img.shields.io/badge/Official%20Website-OTODESK-blue?style=for-the-badge)](https://otodesk4193.github.io/OTODESK_SITE/)

---

**Dissect your sound. 🔬**
