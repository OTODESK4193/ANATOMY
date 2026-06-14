# ANATOMY User Manual

**Version 1.1** | Target DAW: Ableton Live 11 / 12 | Format: VST3 (Windows 64-bit)

---

## Table of Contents

1. [About ANATOMY](#1-about-anatomy)
2. [System Requirements & Installation](#2-system-requirements--installation)
3. [Interface Overview](#3-interface-overview)
4. [Quick Start Guide](#4-quick-start-guide)
5. [Core Controls Reference](#5-core-controls-reference)
6. [Waveform Display & Zoom](#6-waveform-display--zoom)
7. [Custom Sample Replacement](#7-custom-sample-replacement)
8. [Working with Effects](#8-working-with-effects)
9. [Effect Reference](#9-effect-reference)
10. [Stem Export](#10-stem-export)
11. [DAW Project Save & Restore](#11-daw-project-save--restore)
12. [Techniques & Workflow Tips](#12-techniques--workflow-tips)
13. [Troubleshooting](#13-troubleshooting)
14. [Technical Specifications](#14-technical-specifications)

---

## 1. About ANATOMY

**ANATOMY** is a high-performance VST3 plugin that splits audio into **Transient** and **Tonal** components, routing each through its own independent multi-effect chain.

The separation uses a **cos² crossfade** algorithm, mathematically guaranteeing that `transient + tonal = original input` at every sample — zero energy loss, zero overlap artifacts.

### Core Workflow

1. **Drag & Drop** a WAV file onto the plugin window to load audio
2. **Shape** transient and tonal components using separation controls, pitch, and gain
3. **Process** each lane with independent effect chains
4. **Export** finished stems as WAV — drag directly into your DAW or sampler

> **ANATOMY is a sound design tool, not a real-time insert.** Shape your sounds, export as WAV via EXPORT, and load into a sampler (Ableton Simpler/Sampler, Kontakt, etc.) for playback.

### The Three Signal Lanes

| Lane | Content | Use Case |
|---|---|---|
| **TRANSIENT** | Attack / click component | Shaping punch, snap, and impact |
| **TONAL** | Sustained / harmonic component | Sculpting body, tone, and decay |
| **FULL MIX** | Recombined signal (2-color ratio display) | Parallel processing, final limiting |

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

### Installation

1. Download the latest `ANATOMY.vst3` from the [Releases page](https://github.com/OTODESK4193/ANATOMY/releases/latest)
2. Copy the `.vst3` folder to `C:\Program Files\Common Files\VST3\`
3. In Ableton Live, go to Preferences → Plug-ins → Rescan
4. Drag **ANATOMY** from the Browser onto a track

---

## 3. Interface Overview

```
┌──────────────────────────────────────────────────────────┬──────────┐
│ [Full Mix] [Trans Solo] [Tonal Solo] [BEFORE]            │ Effect   │
├──────────────────────────────────────────────────────────┤ Rack     │
│ CLICK HOLD  TRANS PITCH  TRANS GAIN │ FADE-IN  REL  TONAL PITCH GAIN│(right) │
│   (knobs)    (knobs)     (knobs)   │  (knobs)                      │        │
├──────────────────────────────────┬───────────────────────┤          │
│ 1. FULL MIX WAVEFORM (2-color)   │ TONAL OFFSET [─●─]  │          │
│                         [+][-]   │ [EXPORT]             │          │
├──────────────────────────────────┼───────────────────────┤          │
│ 2. TRANSIENT WAVEFORM    [+][-]  │ [Browse] [Reset]     │          │
│                                   │ [EXPORT]             │          │
├──────────────────────────────────┼───────────────────────┤          │
│ 3. TONAL WAVEFORM        [+][-]  │ [Browse] [Reset]     │          │
│                                   │ [EXPORT]             │          │
├──────────────────────────────────┴───────────────────────┤          │
│ Parameter Dock (knobs for selected effect)               │          │
└──────────────────────────────────────────────────────────┴──────────┘
```

### 3.1 Top Bar (Output Mode)

| Button | Function |
|---|---|
| **Full Mix** | Monitor the mixed output of all three lanes |
| **Transient Solo** | Solo the TRANSIENT lane only |
| **Tonal Solo** | Solo the TONAL lane only |
| **BEFORE** | A/B compare against the unprocessed input (bypasses effects and gain) |

### 3.2 Core Controls (Knob Area)

Rotary knobs below the top bar. Left half = Transient parameters, right half = Tonal parameters.

### 3.3 Waveform Displays

Three waveform rows, each with **+/- zoom buttons** in the bottom-right corner:

- **FULL MIX (top)** — 2-color energy ratio display (cyan = transient, magenta = tonal). Tonal Offset slider and EXPORT button on the right side.
- **TRANSIENT (center)** — Cyan waveform. Browse/Reset and EXPORT on the right.
- **TONAL (bottom)** — Magenta waveform. Browse/Reset and EXPORT on the right.

### 3.4 Effect Rack (right panel)

Six effect buttons (SATU / CRUSH / NOISE / OTT / GLUE / LIMIT) and a Chip Bar for each of the three lanes.

### 3.5 Parameter Dock (bottom)

Displays knobs for the currently selected effect.

---

## 4. Quick Start Guide

### Step 1 — Load Audio

Drag & drop a WAV file onto the ANATOMY plugin window. The waveforms appear and the audio is automatically separated into Transient and Tonal.

### Step 2 — Audition the Separation

1. Press **Transient Solo** to hear just the transients
2. Press **Tonal Solo** to hear just the tonal content
3. Press **BEFORE** to compare against the unprocessed original

### Step 3 — Adjust Separation Parameters

- **CLICK HOLD** — Duration of the transient hold region (ms)
- **SUSTAIN FADE-IN** — cos² crossfade duration from transient to tonal (ms)

### Step 4 — Add Effects

1. Click an effect button in the Effect Rack to add it
2. Click its chip in the Chip Bar to select it → adjust in the Parameter Dock
3. Drag chips to reorder the processing chain

### Step 5 — Export

Press **EXPORT**, play audio in the DAW, then drag the finished WAV to the timeline.

---

## 5. Core Controls Reference

### 5.1 Transient Controls

| Parameter | Range | Default | Description |
|---|---|---|---|
| **CLICK HOLD (ms)** | 0.0 – 50.0 | 10.0 | Transient hold duration. During this window: `transient = input`, `tonal = 0` |
| **TRANSIENT PITCH (st)** | -12 – +12 | 0.0 | Pitch shift applied to the transient lane (semitones) |
| **TRANSIENT GAIN (dB)** | -60 – +6 | 0.0 | Output gain of the transient lane |

### 5.2 Tonal Controls

| Parameter | Range | Default | Description |
|---|---|---|---|
| **SUSTAIN FADE-IN (ms)** | 1.0 – 100.0 | 5.0 | cos² crossfade duration from transient to tonal |
| **SUSTAIN RELEASE (ms)** | 10 – 5000 | 500 | Tonal decay tail length |
| **TONAL PITCH (st)** | -12 – +12 | 0.0 | Pitch shift applied to the tonal lane (semitones) |
| **TONAL GAIN (dB)** | -60 – +6 | 0.0 | Output gain of the tonal lane |

### 5.3 Tonal Offset

| Parameter | Range | Default | Description |
|---|---|---|---|
| **TONAL OFFSET (ms)** | -500 – +500 | 0.0 | Shifts tonal playback start position relative to transient |

Located as a horizontal slider on the right side of the Full Mix waveform area. A symmetric skew makes the ±50ms range easy to fine-tune.

- **Negative (left)**: Tonal starts earlier — use to close gaps when Transient Pitch is raised
- **Positive (right)**: Tonal starts later — delays tonal onset

This setting is reflected in all three individual exports (Full Mix, Transient, Tonal).

---

## 6. Waveform Display & Zoom

### 6.1 2-Color Energy Ratio (Full Mix only)

The Full Mix waveform visualizes the transient/tonal energy ratio with two colors: cyan for transient contribution, magenta for tonal contribution.

### 6.2 Zoom

All three waveform displays have **+** / **-** buttons in the bottom-right corner.

- **+** click: Zoom in 2x (from the left edge)
- **-** click: Zoom out 2x
- Range: x1 to x32
- Current zoom level is displayed next to the buttons

Zoom in to inspect very short transients in detail while adjusting parameters.

### 6.3 START/END Trimming

Drag the yellow (START) and red (END) markers on each waveform to trim the playback region.

### 6.4 DRAG EXPORT

Each waveform's top-right **DRAG EXPORT** button lets you drag the offline-rendered waveform directly into your DAW timeline.

---

## 7. Custom Sample Replacement

The TRANSIENT and TONAL lanes each have **Browse** and **Reset** buttons:

- **Browse**: Load any WAV file to replace the separated component with your own sample
- **Reset**: Clear the custom sample and revert to the original cos² separation result

Custom samples work with all export features — the replaced audio is correctly included in exports.

---

## 8. Working with Effects

### 8.1 Adding an Effect

Click any of the six effect buttons (**SATU / CRUSH / NOISE / OTT / GLUE / LIMIT**) in a lane's Effect Rack. The button lights up and a chip appears in the Chip Bar.

### 8.2 Selecting an Effect

Click a chip in the Chip Bar to select it (highlighted). The Parameter Dock loads that effect's controls.

### 8.3 Reordering Effects

Drag chips vertically within the Chip Bar to rearrange processing order. Same lane only.

### 8.4 Removing an Effect

Right-click a chip for the context menu: **Remove** / **Move Up** / **Move Down**.

### 8.5 Processing Order

Effects are processed top-to-bottom in the Chip Bar. All six can be active simultaneously on a single lane.

---

## 9. Effect Reference

### 9.1 ADAA Saturation (SATU)

High-quality Anti-Derivative Anti-Aliased soft saturation. Eliminates aliasing artifacts mathematically for clean harmonics even at high drive.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **DRIVE** | 1.0 – 16.0 | 2.0 | Saturation drive amount |
| **ASYMMETRY** | 0.0 – 1.0 | 0.0 | Waveform asymmetry (even harmonics) |
| **OUT TRIM** | -12 – +12 dB | 0 | Output level trim |
| **PRE HPF** | 20 – 2000 Hz | 20 | Pre-saturation high-pass filter |
| **DRY/WET** | 0.0 – 1.0 | 1.0 | Parallel blend |

### 9.2 BitCrusher (CRUSH)

Lo-Fi effect with bit-depth reduction and sample-rate decimation.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **BITS** | 2 – 24 | 8 | Bit depth |
| **DOWNSAMPLE** | 1 – 32x | 4 | Sample rate decimation factor |
| **JITTER** | 0.0 – 1.0 | 0.0 | Sample timing jitter |
| **DRY/WET** | 0.0 – 1.0 | 1.0 | Parallel blend |

### 9.3 Noise Generator (NOISE)

Triggered noise burst effect.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **DECAY ms** | 1 – 1000 | 100 | Noise burst decay time |
| **GAIN dB** | -60 – 0 | 0 | Noise output level |
| **ATTACK ms** | 0 – 50 | 0 | Noise burst attack time |
| **BP FREQ** | 0 – 4000 Hz | 0 | Band-pass center frequency (0 = bypass) |
| **TYPE** | WHITE / PINK / BROWN / BLUE | WHITE | Noise spectral character |
| **DRY/WET** | 0.0 – 1.0 | 1.0 | Parallel blend |

### 9.4 OTT Multiband Compressor (OTT)

3-band upward/downward dynamics processor.

**Main Parameters:**

| Parameter | Range | Default | Description |
|---|---|---|---|
| **DEPTH** | 0.0 – 1.0 | 0.35 | Overall compression depth |
| **TIME** | 0.1 – 10.0 | 1.35 | Time constant multiplier |
| **GATE dB** | -70 – -20 | -45 | Noise gate floor |

**Per-Band Parameters (LOW / MID / HIGH):**

| Parameter | Range | Default (L/M/H) | Description |
|---|---|---|---|
| **UP** | 0.0 – 1.0 | 0.60 / 0.40 / 0.15 | Upward compression amount |
| **DOWN** | 0.0 – 1.0 | 0.75 / 0.70 / 0.60 | Downward compression amount |
| **GAIN dB** | -24 – +24 | 0 | Per-band output gain |

### 9.5 Glue Compressor (GLUE)

Feed-forward RMS bus compressor.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **THR dBFS** | -40 – 0 | -18 | Threshold |
| **RATIO** | 1.0 – 20.0 | 2.0 | Compression ratio |
| **ATK ms** | 1 – 100 | 30 | Attack time |
| **REL ms** | 10 – 1000 | 200 | Release time |
| **MAKEUP dB** | -12 – +12 | 0 | Makeup gain |
| **DEPTH** | 0.0 – 1.0 | 1.0 | Parallel blend |

### 9.6 Limiter (LIMIT)

Brick-wall peak limiter.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **CEILING dB** | -24 – 0 | -0.1 | Output ceiling |
| **DRY/WET** | 0.0 – 1.0 | 1.0 | Parallel blend |

---

## 10. Stem Export

### EXPORT Button Method

1. Click a lane's **EXPORT** button (turns orange — recording armed)
2. Play audio in the DAW
3. When recording is done, the button turns green (DRAG OK!)
4. Drag the WAV from the button into the DAW timeline or desktop

### DRAG EXPORT Method

Each waveform's top-right **DRAG EXPORT** button lets you drag the offline-rendered waveform directly into the DAW.

### Notes

- Exported signal includes all applied effects
- File format: 32-bit float WAV
- Tonal Offset is reflected in all three individual exports (Full Mix, Transient, Tonal)
- Do not change DAW tempo or playback rate during recording

---

## 11. DAW Project Save & Restore

ANATOMY automatically saves the following data with your DAW project:

- Loaded audio data (the WAV content)
- File sample rate
- Custom replacement samples (Transient and Tonal)
- All parameter values (including Tonal Offset)
- START/END trim positions

When you reopen the project, all buffers are restored and the cos² separation re-runs automatically in the background. No need to re-import audio files.

---

## 12. Techniques & Workflow Tips

### 12.1 Closing Gaps When Transient Pitch Is Raised

Raising Transient Pitch makes the transient play faster and shorter. If a gap appears between Transient and Tonal in the Full Mix, move the **TONAL OFFSET** slider to the left (negative) to shift the tonal start position earlier.

### 12.2 Kick Attack Enhancement

1. Set CLICK HOLD to 5–15ms
2. Keep SUSTAIN FADE-IN short (2–5ms) for a sharp split
3. Add SATU to the TRANSIENT lane (DRIVE 3.0)
4. Raise TRANSIENT GAIN by +3 to +6 dB

### 12.3 Snare Body Shaping

1. Add OTT to the TONAL lane, DEPTH 0.3–0.5
2. Add GLUE to smooth the sustain
3. Adjust SUSTAIN RELEASE for the desired tail length

### 12.4 Sample Layering via Custom Replacement

1. Use TRANSIENT lane's **Browse** to load a different attack sample
2. Adjust Transient Pitch and Gain to blend
3. Export the combined sound (original tonal + custom transient)

### 12.5 A/B Comparison

Press **BEFORE** to instantly hear the unprocessed signal. If processing sounds overdone, reduce individual effect DRY/WET values rather than removing effects entirely.

---

## 13. Troubleshooting

### No audio output

- Confirm a WAV file is loaded (waveforms should be visible)
- Check that Gain values are not severely reduced
- Make sure BEFORE is not accidentally active

### Plugin not showing in DAW

- Confirm `.vst3` is in `C:\Program Files\Common Files\VST3\`
- Rescan plugins in the DAW
- Confirm you're using 64-bit DAW (32-bit not supported)

### High CPU usage

- Increase DAW buffer size to 256–512 samples
- Remove unused effects from chains

### Samples lost after reopening project

- Update to v1.1 or later. v1.0 did not include DAW project save support
- Re-save the project and reopen

### OTT produces extremely loud output

- Fixed in v1.1. Please update to the latest version

---

## 14. Technical Specifications

### Separation Algorithm

cos² crossfade separation (time-domain):
- **Hold region** (Click Hold ms): `transient = input`, `tonal = 0`
- **Fade region** (Sustain Fade-In ms): `transient = input × cos²(θ)`, `tonal = input × sin²(θ)`
- **Post-fade region**: `transient = 0`, `tonal = input`

Mathematically guarantees `transient + tonal = input` at every sample.

### Signal Processing

| Item | Specification |
|---|---|
| Sample precision | 32-bit float |
| Channels | Stereo (2 in / 2 out) |
| Latency | 0 samples |
| Supported sample rates | 44100 / 48000 / 88200 / 96000 Hz |
| Filter topology | ZDF / TPT (Zero-Delay Feedback) |
| Parameter smoothing | Sample-accurate via SmoothedValue |

### Real-Time Safety

- No dynamic memory allocation in processBlock — all buffers pre-allocated in prepareToPlay
- `std::atomic` and lock-free patterns for UI↔audio communication
- `ScopedNoDenormals` prevents CPU spikes from subnormal floats
- Ableton Live sample rate mismatch failsafe in processBlock
- `std::unique_ptr` resource management with explicit editor cleanup

---

*ANATOMY User Manual v1.1 | Developer: @OTODESK | Framework: JUCE 8.0.x | Target: Ableton Live 11/12*
