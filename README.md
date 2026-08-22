# ANATOMY
![Release](https://img.shields.io/badge/release-v1.1.0-blue)
![License](https://img.shields.io/badge/license-AGPLv3-green)
![JUCE](https://img.shields.io/badge/JUCE-8.0.x-blue)
![Platform](https://img.shields.io/badge/platform-Windows%2064bit-lightgrey)
![Downloads](https://img.shields.io/github/downloads/OTODESK4193/ANATOMY/total.svg)

---

### **[V1.1.0] - 2026-08-22 (Granular Style Modern Edition)**
- **4 Lane Architecture**: Introduced a new `Layer` lane with Pitch/Gain control, expanding sound design capabilities alongside FullMix, Transient, and Tonal lanes.
- **High-Precision Pitch & Transient Engine**:
  - Upgraded pitch shifting to 4-point Hermite cubic interpolation and 4-tap Hann phase-distributed granular rotation, eliminating aliasing and comb-filtering distortions.
  - Upgraded Transient Shaper to stereo-linked dual-branch tracker with soft-knee saturation protection.
- **Advanced Dynamics**:
  - `Limiter`: Added an `IN GAIN` knob (0.0dB to +24.0dB) to drive signals into the ceiling threshold.
  - `Limiter`: Added a `MODE` switch (Limit / Clip). Limit provides transparent peak reduction; Clip provides instantaneous soft-knee waveshaping and aggressive punch.
- **GUI & Stability**:
  - Solved critical multi-instance crash (ExportRecordingCore state synchronization).
  - Fixed DAW shutdown crash caused by illegal LookAndFeel pointer destruction.
  - Improved UI colors and lane rendering behavior.

---

## **IMPORTANT: Audio Safety Notice**

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

**ANATOMY** is a high-performance, open-source VST3 plugin that performs **Transient/Tonal audio separation** using a cos² crossfade algorithm, routing each component through its own independent multi-effect chain. ANATOMY gives you surgical control over the *anatomy* of your sound — transient click, tonal sustain, and the full mix — each with a dedicated suite of 6 professional-grade effects.

### Workflow

1. **Drag & Drop** a WAV file onto the plugin window to load audio.
2. **Shape** the transient and tonal components using separation controls, pitch shift, and gain.
3. **Process** each lane with independent effect chains.
4. **Export** finished stems as WAV — drag directly into your DAW timeline or sampler.

ANATOMY automatically **saves and restores all loaded audio data** with your DAW project session, so your sounds are preserved when you reopen the project.

> **ANATOMY is a sound design tool — not a real-time mix insert.**
> Shape your sound → export as WAV via EXPORT → load into a high-quality sampler (Ableton Simpler / Sampler, Native Instruments Kontakt, etc.) for playback.

Process each layer with **ADAA Saturation**, **BitCrusher**, **Noise Generator**, **OTT Multiband Compression**, **Glue Compressor**, and **Limiter** independently. ANATOMY is engineered for extreme real-time safety and Ableton Live stability.

## Demo Videos

<p align="center">
  <b>Introduction</b><br>
  <a href="https://youtu.be/eWfDeOArrfU">
    <img src="Source/Assets/youtube.jpg"
         alt="ANATOMY - Introduction" width="640" height="360">
  </a>
</p>

## Key Features

### Real-Time Transient / Tonal Separation (cos² Crossfade)

ANATOMY splits any loaded audio into three independently processable signal paths using a mathematically perfect cos² crossfade separation. This guarantees that `transient + tonal = original input` with zero energy loss or overlap artifacts.

* **TRANSIENT lane:** The percussive attack/click component — transient punch, snap, and impact.
* **TONAL lane:** The sustained, harmonic, and pitched content — body, tone, and decay.
* **FULL MIX lane:** The recombined signal with 2-color energy ratio visualization (cyan = transient, magenta = tonal).

Each lane features independent **Pitch**, **Gain**, and **shape controls**, giving you precise sculpting power over separation behaviour before effects are applied.

### Drag & Drop Audio Loading

Drag any WAV file directly onto the ANATOMY plugin window. The audio is loaded, analyzed, and separated automatically. All loaded audio data is **saved with your DAW project** — reopen and everything is restored.

### Tonal Offset Control

When Transient Pitch is raised, the transient plays faster and becomes shorter, potentially creating a gap in the Full Mix. The **Tonal Offset** slider (-500ms to +500ms) lets you shift the tonal playback start position forward or backward to close gaps or create intentional overlap. The slider uses a symmetric skew so the ±50ms range is easy to fine-tune.

### Waveform Zoom

All three waveform displays (Full Mix, Transient, Tonal) feature **+/- zoom buttons** in the bottom-right corner. Zoom in up to 32x to inspect short transients in detail. The zoom expands from the left edge, making transient inspection intuitive.

### Custom Sample Replacement (Transient / Tonal Browser)

Replace the separated transient or tonal component with your own custom sample using the **Browse** button on each lane. Use **Reset** to revert to the original separated signal.

### 6-Effect Chain per Lane (18 Total)

Every lane has access to the same 6 professional-grade DSP modules. Effects are **inserted in any order** via drag-and-drop within the Effect Rack, and their parameters are adjusted in real time via the Parameter Dock:

| # | Effect | Category | Description |
|---|---|---|---|
| 1 | **ADAA Saturation** | Distortion | Anti-Derivative Anti-Aliased soft saturation with Drive, Asymmetry, Output Trim, and Pre-HPF |
| 2 | **BitCrusher** | Lo-Fi | Bit-depth reduction and sample-rate decimation with Jitter for vintage digital character |
| 3 | **Noise Generator** | Texture | Transient noise burst (WHITE / PINK / BROWN / BLUE) with Attack, Decay, Gain, and Band-Pass frequency |
| 4 | **OTT Multiband** | Dynamics | 3-band upward/downward compressor with per-band Up/Down/Gain, crossover tuning, Time, Gate Floor, and Depth |
| 5 | **Glue Compressor** | Dynamics | Bus-style compressor with Threshold, Ratio, Attack, Release, Makeup Gain, and Depth |
| 6 | **Limiter** | Dynamics | Transparent brick-wall limiter with adjustable Ceiling and Dry/Wet |

### Drag-and-Drop Effect Ordering

The Effect Rack displays active effects as a **vertically ordered chip list** showing the exact processing chain. Simply drag chips up or down to rearrange the processing order in real time — no menus, no guessing. Right-click any chip for **Remove**, **Move Up**, or **Move Down** options.

### Stem Export (3 Lanes)

Each lane has a dedicated **EXPORT** button. Press once to record the next playback, then **drag the rendered WAV into a sampler** (Ableton Simpler / Sampler, Kontakt, etc.) for instant playback. Additionally, each waveform has a **DRAG EXPORT** button for direct waveform-to-DAW drag & drop. The Tonal Offset setting is reflected in all three individual exports (Full Mix, Transient, Tonal).

### Real-Time Waveform Visualization

Three high-resolution waveform displays update in real time. The Full Mix waveform uses a **2-color energy ratio display** — cyan for transient energy, magenta for tonal energy — giving you visual feedback on the separation balance. A **BEFORE** button allows instant A/B comparison against the unprocessed signal.

### DAW Project Save / Restore

All loaded audio data, custom sample replacements, separation parameters, effect states, and START/END trim positions are **automatically saved and restored** with your DAW project. Reopen your project and ANATOMY is exactly as you left it.

---

## Signal Flow

```
WAV File (Drag & Drop)
    │
    ├─► [cos² Crossfade Separator]
    │   ├── TRANSIENT: Click Hold, Sustain Fade-In, Pitch, Gain
    │   └── TONAL:     Tonal Offset, Pitch, Gain, Release
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
    ├─► [FULL MIX Effect Chain]          ← Runs on recombined signal
    │
    ├─► [Stem Export Recorder]           ← Per-lane WAV capture (lock-free)
    │
    └─► Stereo Output (L/R)
        └── Waveform Visualizers (3×, with zoom)
```

---

## Parameter Reference

### Lane Controls (Transient / Tonal)

| Parameter | Range | Default | Description |
|---|---|---|---|
| Click Hold (ms) | 0.0 – 50.0 ms | 10.0 | Duration of the transient hold window before crossfade begins |
| Sustain Fade-In (ms) | 1.0 – 100.0 ms | 5.0 | cos² crossfade duration from transient to tonal |
| Transient Pitch (st) | -12 – +12 semitones | 0.0 | Pitch shift applied to the transient lane |
| Tonal Pitch (st) | -12 – +12 semitones | 0.0 | Pitch shift applied to the tonal lane |
| Transient Gain (dB) | -60 – +6 dB | 0.0 | Output gain of the transient lane |
| Tonal Gain (dB) | -60 – +6 dB | 0.0 | Output gain of the tonal lane |
| Sustain Release (ms) | 10 – 5000 ms | 500 | Tonal decay tail length |
| Tonal Offset (ms) | -500 – +500 ms | 0.0 | Shifts tonal playback start position relative to transient (skewed for ±50ms precision) |

### Effect Parameters

#### ADAA Saturation

| Parameter | Range | Default | Description |
|---|---|---|---|
| DRIVE | 1.0 – 16.0 | 2.0 | Saturation drive amount |
| ASYMMETRY | 0.0 – 1.0 | 0.0 | Waveform asymmetry (even harmonic content) |
| OUT TRIM | -12 – +12 dB | 0 dB | Output level trim post-saturation |
| PRE HPF | 20 – 2000 Hz | 20 Hz | High-pass filter before saturation (DC/low-end protection) |
| DRY/WET | 0.0 – 1.0 | 1.0 | Parallel dry/wet blend |

#### BitCrusher

| Parameter | Range | Default | Description |
|---|---|---|---|
| BITS | 2 – 24 bit | 8 | Bit depth reduction |
| DOWNSAMPLE | 1 – 32x | 4 | Sample rate decimation factor |
| JITTER | 0.0 – 1.0 | 0.0 | Sample-timing jitter amount (vintage instability) |
| DRY/WET | 0.0 – 1.0 | 1.0 | Parallel dry/wet blend |

#### Noise Generator

| Parameter | Range | Default | Description |
|---|---|---|---|
| DECAY ms | 1 – 1000 ms | 100 ms | Noise burst decay time |
| GAIN dB | -60 – 0 dB | 0 dB | Noise output level |
| ATTACK ms | 0 – 50 ms | 0 ms | Noise burst attack time |
| BP FREQ | 0 – 4000 Hz | 0 Hz | Band-pass filter center frequency (0 = bypass) |
| TYPE | WHITE / PINK / BROWN / BLUE | WHITE | Noise spectral colour |
| DRY/WET | 0.0 – 1.0 | 1.0 | Parallel dry/wet blend |

#### OTT Multiband

**Main Parameters:**

| Parameter | Range | Default | Description |
|---|---|---|---|
| DEPTH | 0.0 – 1.0 | 0.35 | Overall compression depth (Dry/Wet) |
| TIME | 0.1 – 10.0 | 1.35 | Attack/release time constant multiplier |
| GATE dB | -70 – -20 dB | -45 dB | Noise gate floor |

**Per-Band Parameters (LOW / MID / HIGH):**

| Parameter | Range | Default (L/M/H) | Description |
|---|---|---|---|
| UP | 0.0 – 1.0 | 0.60 / 0.40 / 0.15 | Upward compression amount |
| DOWN | 0.0 – 1.0 | 0.75 / 0.70 / 0.60 | Downward compression amount |
| GAIN dB | -24 – +24 dB | 0 dB | Per-band output gain trim |

#### Glue Compressor

| Parameter | Range | Default | Description |
|---|---|---|---|
| THR dBFS | -40 – 0 dB | -18 dB | Compression threshold |
| RATIO | 1.0 – 20.0 | 2.0 | Compression ratio |
| ATK ms | 1 – 100 ms | 30 ms | Attack time |
| REL ms | 10 – 1000 ms | 200 ms | Release time |
| MAKEUP dB | -12 – +12 dB | 0 dB | Makeup gain post-compression |
| DEPTH | 0.0 – 1.0 | 1.0 | Parallel blend (Dry/Wet) |

#### Limiter

| Parameter | Range | Default | Description |
|---|---|---|---|
| CEILING dB | -24 – 0 dB | -0.1 dB | Brick-wall output ceiling |
| DRY/WET | 0.0 – 1.0 | 1.0 | Parallel dry/wet blend |

---

## User Guide

Comprehensive user manuals covering every control, effect parameter, stem export workflow, and advanced techniques are included in this repository.

[ ![Manual Markdown (JP)](https://img.shields.io/badge/Manual-Markdown_(JP)-blue?style=for-the-badge&logo=markdown) ](Source/Assets/ANATOMY_UserManual_JP.md)
[ ![Manual Markdown (EN)](https://img.shields.io/badge/Manual-Markdown_(EN)-blue?style=for-the-badge&logo=markdown) ](Source/Assets/ANATOMY_UserManual_EN.md)


---

## Installation

### VST3 Plugin Installation

1. Download the latest `ANATOMY.vst3` from the [Releases](https://github.com/OTODESK4193/ANATOMY/releases/latest) page.
2. Copy the `.vst3` folder to your VST3 plugin directory:
   ```
   C:\Program Files\Common Files\VST3\
   ```
3. Rescan plugins in Ableton Live (or your DAW of choice).

---

## System Requirements

* **OS:** Windows 10 / Windows 11 (64-bit)
* **Format:** VST3 
* **CPU:** AVX2 support required (Intel Haswell 2013+ / AMD Ryzen 2017+)
* **RAM:** 256 MB minimum
* **Disk:** 50 MB

> **Compatibility Notice:** Compiled and optimized exclusively for Windows 64-bit with AVX2. Verified operation confirmed in **Ableton Live 11 / 12**. Other DAWs (FL Studio, Bitwig, Studio One, Cubase, Reaper) may work but are currently unverified.

---

## Technical Architecture

### Separation Algorithm

ANATOMY uses a **cos² crossfade** separation in the time domain:
- **Hold region** (Click Hold ms): `transient = input`, `tonal = 0`
- **Fade region** (Sustain Fade-In ms): `transient = input × cos²(θ)`, `tonal = input × sin²(θ)`
- **Post-fade region**: `transient = 0`, `tonal = input`

This guarantees `transient + tonal = input` at every sample — mathematically perfect reconstruction with zero energy loss.

### DSP Modules

| Module | Purpose |
|---|---|
| **cos² Crossfade Separator** | Time-domain signal decomposition with perfect reconstruction |
| **GranularPitchShifter** | Per-lane pitch shifting with granular windowing |
| **ADAA_Saturation** | Anti-Derivative Anti-Aliased waveshaper (2nd order ADAA) |
| **BitCrusher** | Integer quantization + sample-rate decimation with jitter |
| **NoiseGenerator** | Triggered noise burst (WHITE/PINK/BROWN/BLUE) with envelope and band-pass |
| **OTT_Multiband** | 3-band upward + downward compression (TPT crossover, ZDF per-band dynamics) |
| **GlueCompressor** | Feed-forward RMS bus compressor with soft-knee |
| **Limiter** | Peak-hold brick-wall limiter with adjustable ceiling |
| **EffectChain** | Lock-free ordered chain dispatcher with atomic snapshot swapping |
| **WaveformComponent** | Real-time rasterized waveform renderer with zoom (up to 32x) |
| **ExportRecorder** | Lock-free per-lane WAV capture with DAW drag-and-drop delivery |

### Real-Time Safety

- **No dynamic memory allocation in processBlock** — all buffers pre-allocated in `prepareToPlay`
- **Lock-free thread communication** — `std::atomic` and SPSC patterns for UI↔audio
- **Denormal protection** — `ScopedNoDenormals` prevents CPU spikes
- **Ableton Live failsafe** — sample rate mismatch detection in processBlock with automatic re-initialization
- **Safe teardown** — `std::unique_ptr` resource management with explicit editor cleanup

---

## Disclaimer

### Software Warranty

This software is provided "as-is", without any warranty of any kind. While extreme care has been taken to ensure real-time safety, audio stability, and Ableton Live compatibility through rigorous testing and professional DSP practices, unexpected behavior may still occur in edge cases or unsupported hosts. Use at your own risk in mission-critical production environments.

### Audio Output & Hearing Protection — Critical Notice

**This plugin can generate loud audio output. User bears sole responsibility for safe audio monitoring.**

* **HEARING DAMAGE RISK:** Prolonged exposure to loud sound (≥85 dB SPL) can cause permanent hearing loss. This risk applies regardless of equipment type or volume settings.
* **SPEAKER / HEADPHONE USE:** Always monitor output levels carefully. Start with low volume and gradually increase. Never wear headphones at maximum volume. Take regular breaks during extended use.
* **SELF RESPONSIBILITY:** The user assumes complete and exclusive responsibility for setting appropriate monitoring levels, protecting their own hearing and that of others, equipment safety and damage risk, and all consequences of audio output usage.
* **NO LIABILITY:** The developer(s) and distributor(s) of this software assume no liability for hearing loss, tinnitus, or any physical harm, equipment damage due to audio output, or any injury or damage caused by improper use.
* **USE AT YOUR OWN RISK:** By using this plugin, you acknowledge and accept all audio-related risks inherent to audio production software.

---

**Your hearing is irreplaceable. Prioritize hearing protection at all times.**

---


## License

This project is licensed under the GNU Affero General Public License v3.0 (AGPLv3) - see the [LICENSE](LICENSE) file for details.
This software is built using the **JUCE 8** framework. In accordance with JUCE 8's open-source licensing terms, this entire project is distributed under the AGPLv3.


## Credits

**Developer:** @OTODESK

**Music Production Background:** Electronic Music, Sound Design, DSP Engineering, JUCE plugin development

**Target DAW:** Ableton Live 11 / 12

**Framework:** JUCE 8.0.x

---

## Support

* **Social / Demo:** [@OTODESK](https://x.com/kijyoumusic)
* [![Website](https://img.shields.io/badge/Official%20Website-OTODESK-blue?style=for-the-badge)](https://otodesk4193.github.io/OTODESK_SITE/)

---

**Dissect your sound.**
