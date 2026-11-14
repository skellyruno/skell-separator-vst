# Skell Separator LIVE polish — research branch

## UI contract
- Default editor starts approximately 25% smaller than the previous 785×448 layout: design scale 56%, approximately 586×334. Original 75%, 100%, 125% and 150% sizes remain selectable; the existing `scale` state property is respected when restoring projects.
- Main accent is Skell Preset neon green (#39FF3A), including the tutorial overlay because its palette comes from `Theme::accent`.
- The LIVE bottom segment meter retains its existing orange (#FF4A14), independent of the interface accent. No stem parameter ID, knob, control layout, DAW plugin identity or engine semantics are replaced.
- Popup is anchored to the actual top-right menu button (not the entire header). Menu colors and section headers use Skell Preset's dark/neon-green palette; submenu arrows, checked-state dots and selected rows are visible.

## Deuce REC comparison
The source-backed REC chain is Fruity Parametric EQ 2 → Fruity Limiter → Soundgoodizer (Mode A, raw amount 43) → Fruity Limiter. The two limiter gate parameter readouts (threshold, release, attack, hold) have **not** been decoded numerically; their screenshot positions are not exact values. Consequently these changes are *tuning informed by the reference*, not a copy of an unverified setting.

## LIVE DSP adjustments
- Broadband gate detector release: 25ms → 14ms, opening smoothing: 0.30ms → 0.20ms.
- Add a gate-state hysteresis (1.12× threshold open; 0.68× close) for phrase/breath stability.
- Shorter gate hold and tail-controlled release (hold 12–147ms vs 18–188ms). Clean remains a separate eight-band processor; Gate=0 and Clean=0 remain fully dry.
- No lookahead, allocations, neural inference or FFT in LIVE. Track Tails 35% remains the existing default.
- Explicit regression on short split syllables, room noise in gaps, quick reopening, and Tails behavior at 44.1/48/96kHz; existing DSP/neural tests retained.

## Known limits
This is an adaptive expander rather than true noise cancellation or dereverb of active singing. Deuce recording-only processing includes EQ/limiter/color and cannot be used to infer exact Fruity Limiter gate values from RMS alone. Host-monitored FL Studio audio and subjective listening remain necessary after the Windows build; no real Windows hardware latency or round-trip measurement is claimed.
