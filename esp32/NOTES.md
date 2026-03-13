# ESP32 + PCM5102 Audio Notes

## Hardware
- ESP32-D0WDQ6-V3, dual core 240MHz, 4MB flash
- PCM5102 I2S DAC (stereo, 32-bit capable)
- FTDI USB-serial adapter (no DTR/RTS reset — use physical RST button)
- Serial port: /dev/cu.usbserial-A50285BI @ 115200 baud

## Wiring
| PCM5102 | ESP32 |
|---------|-------|
| BCK     | GPIO26 |
| DIN     | GPIO25 |
| LCK     | GPIO33 |
| SCK     | GND    |
| GND     | GND    |
| VIN     | 3.3V   |

## PCM5102 solder bridges (on back)
- H1L (FLT) → L
- H2L (DEMP) → L
- H3L (XSMT) → H ← **critical: must be HIGH or DAC is muted**
- H4L (FMT) → L

## What works
- Pre-generated buffers played via `audio_out.write(buf)` in a loop
- Sample rate: 22050Hz, 16-bit stereo
- Buffer must be exact multiple of base waveform period for seamless looping
  - e.g. 55Hz at 22050 SR → period = 401 samples → 1604 bytes stereo
- Tiny buffers (~1-2KB) loop perfectly, no clicks
- Multiple buffers can alternate for scene changes
- ~40KB max practical buffer size (ESP32 RAM limit after MicroPython overhead)

## What doesn't work
- Real-time `math.sin()` per sample: too slow, causes clicking/underruns
  - Even at 11025Hz with wavetable lookup, still too slow for >2 oscillators
- Large pre-generated buffers (>50KB): OOM crashes silently
- Fade-in/fade-out at loop edges: causes audible thumping

## Key technique
Use exact-period buffers for click-free looping:
```
freq = 55  # Hz
SR = 22050
N = SR // freq  # = 401 samples = one perfect cycle
```
All harmonics must be integer multiples of base freq so they also
complete exact cycles within the buffer.

## Current "Harmonic Drone" sketch (MicroPython, sounds good)

### Concept
Evolving ambient drone built from pre-generated single-cycle wavetable buffers.
8 variations of the same base tone (55Hz) with different harmonic mixes,
played sequentially with ~4 seconds each. Creates a slowly shifting organ-like
texture with subtle high-frequency shimmer.

### Architecture
- Sample rate: 22050Hz, 16-bit stereo
- Base frequency: 55Hz (A1)
- Buffer: exactly 401 samples (one cycle of 55Hz = 22050/55)
- All harmonics are integer multiples of 55Hz so they complete exact
  cycles within the buffer → seamless zero-crossing loop, no clicks
- 8 buffers pre-generated at boot, each ~1.6KB
- Playback: just `audio_out.write(buf)` in a tight loop — no real-time math

### The 8 harmonic scenes
Each scene has the 55Hz fundamental at 1.0, then varies:

| # | 110 | 165 | 220 | High freqs |
|---|-----|-----|-----|------------|
| 1 | 0.50 | 0.30 | 0.15 | 880@0.03, 1100@0.02 |
| 2 | 0.55 | 0.25 | 0.20 | 880@0.02, 1320@0.03 |
| 3 | 0.60 | 0.20 | 0.25 | 990@0.03, 1100@0.01 |
| 4 | 0.65 | 0.15 | 0.30 | 660@0.03, 1320@0.02 |
| 5 | 0.60 | 0.20 | 0.25 | 770@0.02, 1100@0.03 |
| 6 | 0.55 | 0.25 | 0.20 | 880@0.03, 990@0.02 |
| 7 | 0.50 | 0.30 | 0.15 | 1100@0.02, 1320@0.02 |
| 8 | 0.45 | 0.35 | 0.10 | 660@0.02, 880@0.03 |

Volume: VOL=2200, divided by 2.5 in the mix.

### What to improve in C++ version
- Real-time crossfade between scenes (MicroPython too slow for this)
- Per-harmonic slow LFO amplitude modulation at different rates (phasing/flanging)
- Slow vibrato (pitch modulation) on individual harmonics
- More harmonics / richer textures
- Granular shimmer layer on top
- Possibly stereo separation (different mix L vs R)

## Workflow (MicroPython)
1. Edit esp32/main.py locally
2. Stop running code: send Ctrl-C via serial
3. Upload: `ampy --port /dev/cu.usbserial-A50285BI --delay 2 put esp32/main.py main.py`
4. Reset: press physical RST button on ESP32
