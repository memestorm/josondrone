# josondrone

An ambient meditation drone synthesizer running on an ESP32 microcontroller with a PCM5102 I2S DAC. Generates evolving, harmonically rich drone textures with bell chimes, binaural beats, and slow tuning drift — designed for deep listening, meditation, and relaxation.

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-D0WDQ6-V3, dual core 240MHz, 4MB flash |
| DAC | PCM5102 I2S stereo DAC, 16-bit, 44.1kHz |
| Serial | FTDI USB-serial adapter (no DTR/RTS wired to EN/BOOT) |
| Output | 3.5mm headphone jack (on PCM5102 breakout) |

## Wiring

| PCM5102 Pin | ESP32 Pin | Wire Colour |
|-------------|-----------|-------------|
| BCK | GPIO26 | Yellow |
| DIN | GPIO25 | Grey |
| LCK | GPIO33 | Mauve |
| SCK | GND | Black |
| GND | GND | Blue |
| VIN | 3.3V | Green |

### PCM5102 Solder Bridges (on back of breakout)

The breakout board used has these factory-default bridge settings (unmodified):

| Pad | Setting | Function |
|-----|---------|----------|
| H1L (FLT) | L | Normal latency filter |
| H2L (DEMP) | L | De-emphasis off |
| H3L (XSMT) | H | Soft mute disabled (DAC will be silent if this is set to L) |
| H4L (FMT) | L | I2S format |

**Note:** If your PCM5102 breakout has no sound, check H3L — it must be bridged to H. Many breakouts ship this way by default, but some don't.

## Flashing

### From source (Arduino CLI)

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 esp32/drone
arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/cu.usbserial-A50285BI esp32/drone
```

### From release binary

Download `drone.ino.merged.bin` from the [latest release](https://github.com/memestorm/josondrone/releases), then:

```bash
esptool --port /dev/cu.usbserial-A50285BI write_flash 0x0 drone.ino.merged.bin
```

**Note:** The FTDI adapter has no DTR/RTS wired to the ESP32 EN/BOOT pins. To flash, you must manually: hold BOOT → press RST → release BOOT → then run the upload command. After flashing, press RST to start.

## Version History

Each tagged release represents a distinct stage in the evolution of the synthesizer, building incrementally from a simple drone to a rich psychoacoustic instrument.

### v1.0 — Relaxing Drone

*Single-layer additive drone*

The foundation. Eight sine oscillators tuned to harmonics of 55Hz (A1): fundamental, octave, fifth, double octave, and upper partials at 660–1320Hz. Each harmonic has independent LFO amplitude modulation at prime-ish rates (0.03–0.23Hz), creating slow phasing and flanging as partials drift in and out of prominence. Eight pre-composed harmonic "scenes" crossfade over time, giving the impression of a living, breathing organ tone.

Musically, this is a pure just-intonation drone in A — all frequencies are exact integer multiples of 55Hz, so there are zero beating artifacts between partials. The result is a warm, stable, organ-like texture that evolves glacially. Psychoacoustically, the slow amplitude modulation sits below conscious rhythmic perception (~0.03–0.23Hz), creating a sense of movement without pulse — the brain registers change but can't lock onto a beat, which encourages a meditative, time-suspended state.

- Sample rate: 44100Hz, 16-bit stereo (mono signal)
- 1024-point interpolated sine wavetable
- Per-harmonic LFO modulation at independent rates

### v2.0 — Dual Drone

*Added ethereal shimmer layer*

A second drone layer enters at 220Hz (A3, two octaves above the root), with eight oscillators spanning 220–2200Hz. Crucially, each oscillator is detuned by +0.05Hz per harmonic number — so the 220Hz partial actually runs at 220.05Hz, creating a 0.05Hz beat against Layer A's 220Hz. This is a 20-second amplitude cycle, far too slow to hear as a "beat" but perceptible as a very gradual swelling and receding of the upper harmonics.

The two layers occupy complementary spectral regions: Layer A dominates the bass (55–220Hz), Layer B adds mid and high shimmer (220–2200Hz). Layer B runs at 40% of Layer A's volume, so it functions as overtone colouration rather than a competing voice. The micro-detuning between layers creates what's known in psychoacoustics as "first-order beating" — the ear perceives a single fused tone whose timbre slowly breathes, rather than two separate sources. This is the same principle behind chorus effects and Tibetan singing bowl interactions.

- Layer A: 55Hz root, 8 oscillators, volume 0.30
- Layer B: 220Hz root, 8 oscillators, volume 0.12, +0.05Hz detuning
- Independent scene morphing per layer

### v3.0 — Contemplative

*Spectral tilt and slower evolution*

Added three-band spectral tilt modulation: bass (harmonics 1–2), mid (harmonics 3–4), and high (harmonics 5–8) each have an independent gain envelope swept by very slow LFOs at 0.005, 0.007, and 0.009Hz. These rates are deliberately non-integer-related, so the three bands never align — the spectral centre of gravity wanders continuously through the frequency spectrum over periods of minutes.

This addresses a key psychoacoustic principle: the auditory system habituates rapidly to static spectra (you stop "hearing" a constant tone after seconds), but responds to spectral change. By slowly tilting the energy distribution between bass, mid, and high, the drone remains perceptually present without demanding attention. The rates are chosen to sit well below the ~0.5Hz threshold where the brain begins to perceive rhythmic modulation, keeping the evolution subliminal.

- Band gains sweep between configurable min/max (bass 0.3–1.0, mid 0.4–1.0, high 0.3–1.0)
- Three independent LFOs at incommensurate rates
- Morph speed reduced for more contemplative pacing

### v4.0 — Reverb

*Schroeder reverb processor*

Added a classic Schroeder reverb: four parallel comb filters with prime-length delay lines (1687, 1931, 2143, 2473 samples ≈ 38–56ms) feeding into two series allpass filters (347, 521 samples). Each comb filter has a one-pole low-pass filter in its feedback path, which progressively darkens the reverb tail — high frequencies decay faster than lows, mimicking the air absorption characteristics of a real room.

The prime-length delays are critical: non-prime or harmonically related delay lengths create metallic, ringing artefacts (the "spring reverb" sound). Prime lengths ensure the echo patterns never align, producing a smooth, diffuse tail. The allpass filters add temporal diffusion without changing the frequency balance, smearing the comb filter output into a dense, natural-sounding wash.

At 25% wet mix, the reverb adds spatial depth and sustain without muddying the harmonic detail. Psychoacoustically, reverberation triggers the brain's spatial processing — even through headphones, the listener perceives the sound as existing in a physical space rather than being generated inside their head. This "externalisation" effect reduces listening fatigue and promotes relaxation.

- 4 comb filters: prime delays, feedback 0.78–0.84, LP coefficient 0.4
- 2 allpass filters: prime delays, feedback 0.5
- 25% wet/dry mix
- Soft tanh clipping to prevent digital overs

### v5.0 — Fairy Dust

*Pure bell chimes*

Sparse, DX7-style bell tones triggered randomly every 15–25 seconds. Each bell has three partials at pure integer ratios (1:2:3 — fundamental, octave, octave+fifth) with amplitudes 1.0, 0.4, 0.15. The pitches are drawn from an A major triad spread across octaves: A4 (440Hz), C♯5 (554Hz), E5 (659Hz), A5 (880Hz), C♯6 (1109Hz), E6 (1319Hz) — all consonant with the A55Hz drone root.

The envelope is a pure exponential decay with a 12-second time constant (reaching -60dB after 12 seconds), with no attack ramp — the partial appears instantly at full amplitude, mimicking the impulse excitation of a struck bell or chime. The sharp transient followed by long decay is psychoacoustically significant: it activates the brain's orienting response (brief attention capture) followed by a tracking phase as the ear follows the decaying tone. This "notice then release" pattern mirrors the attentional cycle encouraged in mindfulness meditation.

The bell tones are deliberately set very quiet (4% of full scale) so they emerge from the drone texture as subtle events rather than foreground elements — fairy dust, not church bells. The existing reverb gives each strike a long, shimmering tail that blends back into the drone.

- 3-voice polyphony, voice stealing on quietest
- Pure integer partial ratios (1:2:3) — clean, not clangy
- 12-second exponential decay
- Random pitch from A major triad (440–1319Hz)
- 15–25 second random trigger interval
- 4% volume level

### v6.0 — Binaural

*Theta brainwave entrainment*

Added a 6Hz binaural beat implemented as opposing stereo amplitude modulation: the left channel is gently boosted while the right is attenuated, and vice versa, at 6 cycles per second with 15% modulation depth. This creates the perceptual illusion of the sound source slowly rotating or pulsing between the ears.

True binaural beating (different frequencies per ear) was attempted but exceeded CPU budget. The amplitude-modulation approach achieves a similar perceptual effect: the interaural level difference (ILD) oscillating at 6Hz causes the auditory cortex to track a 6Hz spatial movement, which research suggests can entrain neural oscillations in the theta band (4–8Hz). Theta activity is associated with deep relaxation, meditation, hypnagogic states, and enhanced creativity.

The 6Hz rate was chosen as a compromise: fast enough to be within the theta band's entrainment window, slow enough to not feel like a tremolo effect. At 15% modulation depth, the spatial movement is subliminal — felt as a gentle "aliveness" in the stereo field rather than an obvious left-right ping-pong.

- 6Hz amplitude modulation, anti-phase L/R
- 15% modulation depth
- Single additional sine lookup per sample (negligible CPU cost)
- Requires headphones for binaural effect

### v7.0 — Verdi Drift

*Tuning drift between A440 and Verdi A432*

The entire instrument slowly glides between standard concert pitch (A4=440Hz, root=55Hz) and Verdi tuning (A4=432Hz, root=54Hz) over a ~4-minute sinusoidal cycle. All oscillators in both layers track the drift proportionally, maintaining their harmonic relationships.

The 432Hz tuning (also called "Verdi pitch" after Giuseppe Verdi's advocacy) has mathematical appeal: 432 = 2⁴ × 3³, and its harmonics align with many natural and geometric ratios. While scientific evidence for special physiological effects is limited, the tuning is widely used in sound healing and meditation contexts, and many listeners report a subjective sense of warmth or groundedness compared to 440Hz.

The drift itself is the psychoacoustically interesting element. A 1Hz frequency shift (55→54Hz) represents about 32 cents — roughly a third of a semitone. This is below the threshold for perceiving a distinct pitch change in isolation, but after extended listening the ear calibrates to the current pitch centre, making the drift perceptible as a subtle "flattening" or "settling." The continuous, imperceptible pitch movement prevents auditory habituation and creates a sense of the drone being alive and breathing at a geological timescale.

- Sinusoidal interpolation between 55Hz and 54Hz root
- ~4-minute full cycle (0.004Hz LFO rate)
- All harmonics scale proportionally
- Both drone layers track the same tuning drift

## Architecture

```
┌─────────────┐  ┌─────────────┐
│  Layer A     │  │  Layer B     │
│  55Hz root   │  │  220Hz root  │
│  8 oscillators│  │  8 oscillators│
│  8 scenes    │  │  6 scenes    │
│  per-osc LFO │  │  per-osc LFO │
└──────┬───────┘  └──────┬───────┘
       │                 │
       └────────┬────────┘
                │
         ┌──────┴──────┐
         │ Spectral    │
         │ Tilt (3-band)│
         └──────┬──────┘
                │
         ┌──────┴──────┐
         │ + Bells     │
         │ (3 voices)  │
         └──────┬──────┘
                │
         ┌──────┴──────┐
         │ Binaural    │
         │ Pan (6Hz)   │
         │ L/R split   │
         └──────┬──────┘
                │
         ┌──────┴──────┐
         │ Reverb      │
         │ (mono sum)  │
         └──────┬──────┘
                │
         ┌──────┴──────┐
         │ Soft clip   │
         │ (tanh)      │
         └──────┬──────┘
                │
         ┌──────┴──────┐
         │ I2S out     │
         │ 44.1kHz/16b │
         └─────────────┘
```

## Resource Usage

- CPU: ~83%
- RAM: 22% (74KB of 328KB)
- Flash: 23% (307KB of 1.3MB)

## License

MIT
