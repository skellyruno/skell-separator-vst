# v1.10 UI + neural work scheduling

## Changes

The header's compact engine badge now toggles the **existing** `engineMode`
parameter through normal JUCE automation gestures. It indicates whether a
48 kHz neural model backend is available; outside 48 kHz it cannot be turned on
from this control. The menu still exposes the engine, quality, and strength.
Stem cards use vector graphics, not new bitmap or font assets. Solo highlights
its card and each card identifies the estimated source; these labels must not
be interpreted as ground-truth stems.

The neural producer no longer feeds the worker while the user selects the
Classical engine or engages Bypass. The worker thread remains alive and may
have a handful of queued frames in flight immediately after a switch. This
change avoids steady-state model inference in Classical mode without blocking
the audio thread or destroying the backend on a real-time host callback.

After a gap, the input collector waits for the next original 480-sample
boundary before sending a frame. The worker detects the discontinuity, resets
model context, and primes one look-behind frame. Missing output is handled by
the existing complementary 240-sample fallback crossfade. Worker and audio
thread still share the fixed 2880-sample host delay; this is not a low-latency
mode and does not resample unsupported sample rates.

## Verification

Standalone DSP regression tests cover unity reconstruction, mono/stereo,
polarity-inverted sources, sample rates 44.1 to 192 kHz and all three classical
settings. Neural mock transport tests cover worker deadlines, invalid output,
whole-frame dropout recovery and the new Classic/pause/resume transitions. Tiny
network tests verify WOLA alignment and nonzero finite output. Sanitizer and
Windows JUCE/FL Studio results must be recorded separately; GitHub Actions
is the authoritative Windows build check.

## Limitations

The bundled 64-band research model is trained only on procedural harmonic
mixtures. No real-singing quality improvement is claimed. CPU savings here
mean **not running unused inference**, not faster inference while AI is active.
The plugin does not implement a real-singing-trained dereverberation model.

## Separation experiment rejected

A trial with asymmetric neural-mask attack/release coefficients (0.52 rising,
0.24 falling versus the original 0.35 symmetric) slightly worsened all three
held-out procedural benchmark conditions (about 0.01–0.02 dB SI-SDR). It was
**reverted** rather than shipping an unproven mask change. The original Tiny
Vocal model weights and mask smoothing remain intact.
