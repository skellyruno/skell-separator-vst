# LIVE Clean: real-time, frequency-aware noise reduction (research)

This implementation upgrades the **Clean** knob in the established LIVE / SEPARATE
three-column interface. No UI geometry, existing parameter IDs, default values,
or Separate-mode DSP is changed. LIVE retains `Clean`, `Gate`, and `Tails`.

## How it works

- **Clean:** a zero-lookahead, four-band, FFT-free IIR noise reducer. Three
  second-order high-pass filters form complementary residual bands. The
  unattenuated bands sum to the original sample. Each band has a separate
  noise-floor estimate, level detector, and smoothed attenuation. The noise
  estimator learns primarily during quiet passages and freezes during louder
  singing. A 6 ms dry-to-clean onset guard protects consonants, with a smooth
  3 ms transition back to the estimated gains.
- **Gate:** independent adaptive broadband expander/phrase gate. It retains its
  fast opening and avoids sharp release clicks. Gate=0 disables this path.
- **Tails:** changes Gate hold and release, not the model's noise estimate.

No STFT, FFT, lookahead buffer, model download, ONNX dependency, or extra plugin
latency is introduced. LIVE with Clean=0 and Gate=0 passes all finite input
samples sample-exactly through the processing algorithm. Automation parameters
stay independent, and SEPARATE's neural and classical engines are untouched.

## Verified scope

`bash tests/run_live_cleanup.sh` exercises 44.1/48/96 kHz, immediate onset,
transient/stereo/silence handling, state corruption, independent Gate and Tails,
quiet noise suppression, and low-vowel + high-hiss interference while the
broadband detector is open. A Clang AddressSanitizer/UBSan run is part of CI.

One synthetic test uses a 220 Hz vowel surrogate with 8.3 kHz continuous tone
interference. It measures ~3 dB high-band attenuation under the vowel without
meaningful vowel level change. A separate low-level steady-tone fixture shows
~12 dB Clean suppression. These are **NOT results on real recorded singing**,
and tonal interference is not general broadband-noise removal.

## Limitations and safe use

This is **adaptive multiband expansion**, not neural source separation,
Bertom's original DSP, NS1, SPL De-Verb Plus, or true real-time dereverberation.
Overlapping noise and vocals within the same frequency band remain inseparable.
Strong settings can alter vocal timbre or quiet consonants. The zero-sample
algorithmic delay does not remove audio-interface or host-buffer latency.

Before a stable release, run ABX/level-matched listening on quiet breaths,
consonants, long vowels, fan noise, AC, traffic, and short room reflections.
Measure false gate closes, musical noise/tonal ringing, CPU/denormals under
Windows, and real FL Studio 44.1/48/96 kHz recording with multiple instances.
Never bundle StemgenRT's noncommercial pretrained ONNX weights in a commercial
Skell release.
