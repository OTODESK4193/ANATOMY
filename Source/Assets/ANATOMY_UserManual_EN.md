# ANATOMY User Manual

**Version 1.0** | Target DAW: Ableton Live 11 / 12 | Format: VST3 (Windows 64-bit)

---

## Table of Contents

1. [About ANATOMY](#1-about-anatomy)
2. [System Requirements & Installation](#2-system-requirements--installation)
3. [Interface Overview](#3-interface-overview)
4. [Quick Start Guide](#4-quick-start-guide)
5. [Core Controls Reference](#5-core-controls-reference)
6. [Working with Effects](#6-working-with-effects)
7. [Effect Reference](#7-effect-reference)
8. [Stem Export](#8-stem-export)
9. [Techniques & Workflow Tips](#9-techniques--workflow-tips)
10. [Troubleshooting](#10-troubleshooting)
11. [Technical Specifications](#11-technical-specifications)

---

## 1. About ANATOMY

**ANATOMY** is a high-performance, real-time VST3 plugin that splits any incoming audio into **Transient** and **Tonal** components, routing each through its own independent multi-effect chain.

Traditional compressors and saturators act on the entire signal at once. ANATOMY lets you work on the drum attack (transient) and the body (tonal) entirely separately — saturating just the click while applying OTT multiband compression only to the sustain, for example. Three parallel signal lanes give you complete, surgical control.

> ⚠️ **ANATOMY is a sound design tool, not a real-time insert.** The recommended workflow is to shape your sounds using ANATOMY's separation and effects, **export them as WAV via the EXPORT function**, and load those stems into a high-quality sampler (Ableton Simpler / Sampler, Native Instruments Kontakt, etc.) for playback and further use.

### The Three Signal Lanes

| Lane | Content | Use Case |
|---|---|---|
| **TRANSIENT** | Attack / click component | Shaping punch, snap, and impact |
| **TONAL** | Sustained / harmonic component | Sculpting body, tone, and decay |
| **FULL MIX** | Unprocessed combined signal | Parallel processing, final limiting |

Up to 6 effects per lane — in any order you choose — and finished stems can be exported as WAV and dragged directly into your DAW timeline.

---

## 2. System Requirements & Installation

### System Requirements

| Item | Requirement |
|---|---|
| **OS** | Windows 10 / 11 (64-bit) |
| **Format** | VST3 |
| **CPU** | AVX2 required (Intel Haswell 2013+ / AMD Ryzen 2017+) |
| **RAM** | 256 MB minimum |
| **Disk** | 50 MB |
| **Recommended DAW** | Ableton Live 11 / 12 |

> ⚠️ **Note:** ANATOMY will not run on CPUs that lack AVX2 support. Check your processor's specifications before installing.

### Installation

1. Download the latest `ANATOMY.vst3` from the [Releases page](https://github.com/OTODESK4193/ANATOMY/releases/latest).
2. Copy the `.vst3` folder to your VST3 plugin directory:
   ```
   C:\Program Files\Common Files\VST3\
   ```
3. Open Ableton Live, go to **Preferences → Plug-ins → VST3 Folder**, then click **Rescan**.
4. Find **ANATOMY** in Live's Browser under Plug-ins and drag it onto a track.

---

## 3. Interface Overview

The ANATOMY window is divided into four main areas:

```
┌──────────────────────────────────────────────────────┐
│ [Output Mode Buttons]   [BEFORE]       [Global]      │  ← Top Bar
├───────────────┬──────────────────────────────────────┤
│               │ FULL MIX WAVEFORM                    │  ← Waveform Displays
│  Core         ├──────────────────────────────────────┤
│  Controls     │ TRANSIENT WAVEFORM                   │
│               ├──────────────────────────────────────┤
│               │ TONAL WAVEFORM                       │
├───────────────┴──────────────────┬───────────────────┤
│ Parameter Dock                   │ Effect Rack       │  ← Operation Area
└──────────────────────────────────┴───────────────────┘
```

### 3.1 Top Bar

| Control | Function |
|---|---|
| **FULL MIX** | Monitor the final mixed output of all three lanes |
| **TRANS SOLO** | Solo the TRANSIENT lane only |
| **TONAL SOLO** | Solo the TONAL lane only |
| **BEFORE** | A/B compare the unprocessed input against the current processing |

### 3.2 Core Controls

The left-hand area of the plugin window. Contains the primary per-lane shaping parameters that are applied before effects. See [Section 5](#5-core-controls-reference) for full details.

### 3.3 Waveform Displays

Three real-time waveform displays in the center of the window:

- **FULL MIX (top)** — Full mix input or output waveform
- **TRANSIENT (center)** — Isolated transient component waveform
- **TONAL (bottom)** — Isolated tonal component waveform

### 3.4 Effect Rack (right side)

For each lane (TRANSIENT / TONAL / FULL MIX), six effect toggle buttons and a **Chip Bar** (ordered list of active effects) are displayed. See [Section 6](#6-working-with-effects) for full details.

### 3.5 Parameter Dock (bottom)

When you select an effect, its parameter knobs and sliders appear here. Adjust all parameters in real time for the selected effect.

---

## 4. Quick Start Guide

Follow these steps to master the ANATOMY workflow in under 5 minutes.

### Step 1 — Insert the Plugin

Add ANATOMY as an Audio Effect on a drum track or drum bus channel in Ableton Live.

### Step 2 — Check the Separation

1. Play your audio.
2. Watch the waveform displays — TRANSIENT and TONAL should show clearly different signals.
3. Press **TRANS SOLO** to audition just the transients.
4. Press **TONAL SOLO** to audition just the tonal content.

### Step 3 — Add an Effect

1. In the Effect Rack, click the **SATU** button in the **TRANSIENT lane**.
   - The button lights up and a "SATU" chip appears in the Chip Bar.
2. Click the chip to select it (it highlights blue).
3. The Parameter Dock shows the ADAA Saturation parameters.
4. Turn up the **DRIVE** knob to taste.

### Step 4 — Reorder Effects

Drag chips up or down in the Chip Bar to change the processing order in real time.
Example: To change `SATU → OTT` to `OTT → SATU`, drag the OTT chip above the SATU chip.

### Step 5 — Export a Stem

1. Press the **EXPORT** button on any lane to arm it for recording.
2. Play your track in Ableton Live for the desired number of bars.
3. When done, drag the WAV from the EXPORT button directly into Live's timeline.

---

## 5. Core Controls Reference

These parameters shape each lane's signal *before* any effects are applied.

### 5.1 Transient Controls

| Parameter | Range | Description |
|---|---|---|
| **Click Length** | 0.0 – 1.0 | Length of the transient extraction window. Higher values capture a longer attack region as the "transient" |
| **Click Curve** | 0.0 – 1.0 | Envelope shape of the transient. 0 = sharp spike, 1 = smooth fade |
| **Transient Pitch** | -24 – +24 semitones | Pitch shift applied to the TRANSIENT lane output |
| **Transient Gain** | -24 – +24 dB | Output gain of the TRANSIENT lane |

### 5.2 Tonal Controls

| Parameter | Range | Description |
|---|---|---|
| **Sustain Release** | 0.0 – 1.0 | Length of the tonal decay tail. Higher values preserve longer sustain |
| **Tonal Pitch** | -24 – +24 semitones | Pitch shift applied to the TONAL lane output |
| **Tonal Gain** | -24 – +24 dB | Output gain of the TONAL lane |

### 5.3 Usage Tips

**To enhance kick drum attack:**
- Set Click Length around 0.3 to extract just the punch
- Lower Click Curve for a sharper transient spike
- Raise Transient Gain by +3 to +6 dB to bring the attack forward

**To tone-shape a snare body separately:**
- Set Sustain Release to 0.4–0.6 to capture enough tail
- Insert a Glue Compressor on the TONAL lane to smooth and control the body

---

## 6. Working with Effects

### 6.1 Adding an Effect

1. Click any of the six effect buttons (**SATU / CRUSH / NOISE / OTT / GLUE / LIMIT**) in a lane's Effect Rack.
2. The button lights up and a chip for that effect appears in the Chip Bar below.

> **Note:** Clicking the button again does **not** toggle it off. To remove an effect, right-click its chip (see below).

### 6.2 Selecting an Effect for Editing

Click any chip in the Chip Bar to select it (highlighted in blue). The Parameter Dock will immediately load that effect's controls.

### 6.3 Reordering Effects (Drag & Drop)

Drag chips vertically within the Chip Bar to rearrange the processing chain order.

- **While dragging:** A drop indicator line shows the insertion point.
- **Same lane only:** Cross-lane drag & drop is not supported.

### 6.4 Removing an Effect

Right-click any chip in the Chip Bar to open the context menu:

- **Remove** — Removes the effect from the chain
- **Move Up** — Shifts the effect one position earlier in the chain
- **Move Down** — Shifts the effect one position later in the chain

### 6.5 Using Multiple Effects

All six effects can be active simultaneously on a single lane. The processing order is top-to-bottom in the Chip Bar (the topmost chip is processed first).

**Example: Typical transient chain for drums:**
```
SATU → OTT → LIMIT
```
① Saturation adds harmonic character to the attack
② OTT controls dynamics and adds density
③ Limiter protects against output clipping

---

## 7. Effect Reference

### 7.1 🟣 ADAA Saturation (SATU)

A high-quality soft saturation built on Anti-Derivative Anti-Aliasing (ADAA) technology. Unlike conventional waveshapers, ADAA mathematically eliminates aliasing artifacts, keeping the high end clean and transparent even at high drive amounts.

**Parameters:**

| Parameter | Range | Default | Description |
|---|---|---|---|
| **DRIVE** | 1.0 – 16.0 | 2.0 | Saturation drive amount. Higher values produce more harmonic distortion |
| **ASYMMETRY** | 0.0 – 1.0 | 0.0 | Waveform asymmetry. Increases even-order harmonics (2nd, 4th) for a warmer, more "tube-like" character |
| **OUT TRIM** | -12 – +12 dB | 0 dB | Output level trim after saturation |
| **PRE HPF** | 20 – 2000 Hz | 20 Hz | High-pass filter applied before saturation. Raising this protects low-end from being distorted |
| **DRY/WET** | 0.0 – 1.0 | 1.0 | Parallel blend with the dry signal. 0.5 = 50/50 parallel saturation |

**Tips:**
- For drum transients, start with DRIVE 2–4, ASYMMETRY 0.2–0.4 for a subtle, musical result
- Set PRE HPF to ~200 Hz to leave kick low-end untouched while saturating the click

---

### 7.2 🟣 BitCrusher (CRUSH)

A classic Lo-Fi effect combining bit-depth reduction and sample-rate decimation.

**Parameters:**

| Parameter | Range | Default | Description |
|---|---|---|---|
| **BITS** | 2 – 24 bit | 8 | Bit depth. Lower values create coarser quantization noise (8-bit = classic game console sound) |
| **DOWNSAMPLE** | 1 – 32× | 4 | Sample-rate decimation factor. Higher values reduce effective sample rate, creating aliasing artifacts |
| **JITTER** | 0.0 – 1.0 | 0.0 | Sample timing jitter. Adds randomized instability for vintage hardware feel |
| **DRY/WET** | 0.0 – 1.0 | 1.0 | Parallel blend with the dry signal |

**Tips:**
- Apply to the TRANSIENT lane with BITS 6–10 for a classic drum machine attack texture
- Keep DOWNSAMPLE low (2–4) with DRY/WET around 0.3 for a subtle digital flavor

---

### 7.3 🟣 Noise Generator (NOISE)

A triggered noise burst generator. Detects input transients and fires a shaped noise envelope — perfect for adding snare wire texture, room ambience, or layered noise character.

**Parameters:**

| Parameter | Range | Default | Description |
|---|---|---|---|
| **DECAY ms** | 1 – 1000 ms | 100 ms | Noise burst decay time |
| **GAIN dB** | -60 – 0 dB | 0 dB | Noise output level |
| **ATTACK ms** | 0 – 50 ms | 0 ms | Noise burst attack time |
| **BP FREQ** | 0 – 4000 Hz | 0 Hz | Band-pass filter center frequency. 0 = bypass (full spectrum) |
| **TYPE** | WHITE / PINK / BROWN / BLUE | WHITE | Noise spectral character |
| **DRY/WET** | 0.0 – 1.0 | 1.0 | Parallel blend with the dry signal |

**Noise Type Guide:**

| Type | Spectral Character | Typical Use |
|---|---|---|
| **WHITE** | Flat across all frequencies | Bright, crisp "shhh" texture |
| **PINK** | -3 dB/octave above | Natural room ambience |
| **BROWN** | -6 dB/octave above | Low, warm, thuddy texture |
| **BLUE** | +3 dB/octave above | Ultra-bright, airy sheen |

---

### 7.4 🟣 OTT Multiband Compressor (OTT)

A 3-band upward/downward dynamics processor in the style of the famous "OTT" preset. Upward compression lifts quiet signals, downward compression tames loud ones — together they create a dense, hyper-compressed sound full of movement.

#### Main Parameters

| Parameter | Range | Default | Description |
|---|---|---|---|
| **TIME** | 0.1 – 10.0 | 1.0 | Attack/release time constant. Lower = faster response |
| **LO/MI XO** | 40 – 1000 Hz | 200 Hz | Low/Mid band crossover frequency |
| **MI/HI XO** | 1000 – 15000 Hz | 2500 Hz | Mid/High band crossover frequency |
| **GATE dB** | -70 – -20 dB | -45 dB | Noise gate floor. Signals below this are not compressed |
| **DEPTH** | 0.0 – 1.0 | 1.0 | Overall compression depth (dry/wet blend) |

#### The BANDS Button

Click the **BANDS** button (top-right of the OTT Parameter Dock) to access per-band controls for all three frequency bands.

#### Per-Band Parameters (LOW / MID / HIGH)

| Parameter | Range | Default | Description |
|---|---|---|---|
| **UP** | 0.0 – 1.0 | 1.0 | Upward compression amount (lifts quiet signals) |
| **DOWN** | 0.0 – 1.0 | 1.0 | Downward compression amount (tames loud signals) |
| **GAIN dB** | -24 – +24 dB | 0 dB | Per-band output gain trim |

**Tips:**
- On the TRANSIENT lane, OTT evens out attack dynamics across frequency bands for a punchy, focused drum sound
- Keep DEPTH at 0.3–0.5 to blend with the original signal and preserve naturalness

---

### 7.5 🟣 Glue Compressor (GLUE)

A feed-forward RMS bus compressor modeled on classic hardware bus compressors. The "glue" effect comes from its smooth, musically natural behavior that binds multiple elements together without sounding heavy-handed.

**Parameters:**

| Parameter | Range | Default | Description |
|---|---|---|---|
| **THR dBFS** | -40 – 0 dB | -18 dB | Compression threshold |
| **RATIO** | 1.0 – 20.0 | 2.0 | Compression ratio |
| **ATK ms** | 1 – 100 ms | 30 ms | Attack time. Shorter = transients are compressed too; longer = transients pass through |
| **REL ms** | 10 – 1000 ms | 200 ms | Release time |
| **MAKEUP dB** | -12 – +12 dB | 0 dB | Makeup gain applied after compression |
| **DEPTH** | 0.0 – 1.0 | 1.0 | Parallel blend (New York compression) |

**Tips:**
- Insert on the FULL MIX lane to add cohesion to the final mixed signal
- ATK 30–50 ms lets transients punch through while controlling sustain
- DEPTH 0.5–0.7 blends parallel compression while preserving original dynamics

---

### 7.6 🟣 Limiter (LIMIT)

A transparent peak-hold brick-wall limiter. Use as the final stage of any effect chain to prevent output clipping.

**Parameters:**

| Parameter | Range | Default | Description |
|---|---|---|---|
| **CEILING dB** | -24 – 0 dB | -0.1 dB | Output ceiling. Signals above this level are hard-limited |
| **DRY/WET** | 0.0 – 1.0 | 1.0 | Parallel blend with the dry signal |

**Tips:**
- Setting CEILING to -1.0 dB provides clean protection against digital clipping
- DRY/WET at 0.7–0.9 achieves transparent limiting that preserves the original dynamic character

---

## 8. Stem Export

ANATOMY records the output of any lane in real time as a WAV file, which you can drag directly into your DAW's timeline.

### Export Workflow

1. Click the **EXPORT** button on the lane you want to capture.
   - The button lights up, indicating recording mode is armed.
2. Play back audio in Ableton Live for the desired number of bars.
3. Stop playback when you have captured the desired amount.
4. Click the EXPORT button again (or drag from it) to deliver the WAV file to Live's timeline or your Desktop.

### Notes

- The exported stem is the post-effect signal (effects applied).
- Files are exported as 32-bit float WAV.
- Do not change Ableton's tempo or playback rate during recording.

---

## 9. Techniques & Workflow Tips

> **Core Workflow:** Shape sounds in ANATOMY → **EXPORT as WAV** → Load into a high-quality sampler. ANATOMY is a sound design tool. Continuous real-time use as a mix insert is not the intended workflow.

### 9.1 Snare Saturation + Noise Layering

1. Add `SATU` to the TRANSIENT lane (DRIVE 4.0, ASYMMETRY 0.3)
2. Add `NOISE` to the same TRANSIENT lane (TYPE: PINK, DECAY 80ms, BP FREQ 3000 Hz)
3. In the Chip Bar, ensure `SATU → NOISE` order (saturate first, then layer noise)

### 9.2 Kick Low-End Enhancement

1. Add `OTT` to the TONAL lane, then use BANDS to boost the LOW band UP parameter
2. Set Transient Pitch to -2 to -4 semitones to lower the attack slightly
3. Raise Transient Gain by +3 dB to push the punch forward

### 9.3 Parallel Compression (New York Style)

1. Add `GLUE` to the FULL MIX lane (RATIO 4:1, ATK 10ms, MAKEUP +6dB)
2. Set DEPTH to 0.4 (40% compressed, 60% dry blend)
3. This adds powerful punch while preserving the original dynamic range

### 9.4 A/B Comparison with the BEFORE Button

Whenever you're unsure if processing is helping or hurting, hit the **BEFORE** button to instantly compare against the unprocessed input. If things sound over-processed, reduce the DRY/WET of individual effects rather than disabling them entirely.

### 9.5 Creative Pitch Separation

ANATOMY's per-lane pitch control enables creative sound design beyond traditional processing:

- Raise **Transient Pitch** by +7 semitones to create an octave-above click that sits in a different frequency space
- Lower **Tonal Pitch** by -12 semitones to create a sub-octave body underneath
- Blend Tonal Gain carefully to control how prominent the sub content becomes

---

## 10. Troubleshooting

### No audio output

- Confirm ANATOMY is correctly inserted as an Audio Effect in Ableton Live
- Check that TRANSIENT, TONAL, and FULL MIX Gain values are not severely reduced
- Make sure the BEFORE button is not latched active (click to toggle off)

### Plugin not showing in Ableton Live

- Confirm `ANATOMY.vst3` is in `C:\Program Files\Common Files\VST3\`
- In Live's Preferences → Plug-ins, verify the VST3 folder path and click **Rescan**
- Confirm you are running 64-bit Ableton Live (32-bit is not supported)

### High CPU usage

- Increase Ableton's audio buffer size to 256–512 samples
- Remove any unused effects from chains
- Reduce the number of active effects on the FULL MIX lane

### Effect parameters not showing in the Dock

- Click the chip in the Chip Bar to select the effect (it should highlight blue)
- If no chips are in the Chip Bar, click the effect button to add the effect first

### OTT BANDS button not visible

- Try widening the plugin window if your DAW supports it
- Confirm the OTT effect chip is selected (highlighted blue) in the Chip Bar

### Exported WAV is empty or silent

- Make sure you played audio **after** pressing the EXPORT button (while it was lit)
- Try a longer playback segment and re-export

---

## 11. Technical Specifications

### Signal Processing

| Item | Specification |
|---|---|
| Sample precision | 32-bit float |
| Channels | Stereo (2 in / 2 out) |
| Latency | 0 samples (zero-latency) |
| Supported sample rates | 44100 / 48000 / 88200 / 96000 Hz |
| Filter topology | ZDF / TPT (Zero-Delay Feedback) |
| Parameter smoothing | Sample-accurate via SmoothedValue |

### Real-Time Safety Design

ANATOMY is engineered for zero-compromise audio-thread safety:

- **No dynamic memory allocation in processBlock** — all buffers pre-allocated at startup
- **Lock-free thread communication** — UI↔audio communication via SPSC queues
- **Denormal protection** — `ScopedNoDenormals` prevents CPU spikes from subnormal floats
- **Ableton Live failsafe** — sample rate mismatch detection with automatic re-initialization
- **Safe teardown** — `unique_ptr` resource management with explicit editor cleanup

### Full Parameter Reference

All parameters and ranges are documented in the [README Parameter Reference](../../README.md#parameter-reference).

---

*ANATOMY User Manual v1.0 | Developer: @OTODESK | Framework: JUCE 8.0.x | Target: Ableton Live 11/12*
