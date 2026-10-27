# Skell Separator v1.8 — neural frame integrity and transport safety

## Scope and backward compatibility

This is a reliability-focused update to v1.7. It does **not** increase reported latency, change the separation model, host parameter IDs, factory gain defaults, or UI size. New instances use 785 × 448 pixels and Ambience/Voice/Voice Reverb at 0 dB. The classical fallback is still 1,024 samples, and RNNoise-enabled builds still report 2,880 samples. RNNoise remains optional: no third-party binaries, model weights, or audio fixtures are bundled.

## Source changes

1. The neural worker now marks a complete 480-sample stereo output frame invalid if **any** of its samples are non-finite or exceed the conservative ±8-float safety range. It initializes each destination with NaN sentinels before inference, so incomplete model writes are also rejected. Rejection is determined **on the worker thread**, not by scanning each sample in the audio callback.
2. The worker resets a misbehaving recurrent model after bad output, re-primes its previous-frame output contract, and retains absolute sample timestamps. An invalid or missed frame is never delivered to the audio thread as a valid zero-valued Voice stem. The pre-existing 240-sample fade to classical Voice remains the fallback. The About dialog displays a separate *Rejected corrupt frames* diagnostic alongside missed/queued frames.
3. RNNoise input, which uses signed-16-like sample scaling, is bounded to ±1 before multiplying by 32768. The normal audio path is not limited this way. This only prevents out-of-range host samples from overdriving the optional neural denoiser.
4. The production classical Separator also guards its DSP input boundary against NaN, infinity, and overrange audio, even for standalone/offline callers that do not use PluginProcessor's input guard.
5. The About menu and CMake version have been updated to 1.8.

These guards are not a new music-vocal neural model. They don't prove stronger vocal isolation or dereverberation. Extra worker validation may add a small amount of worker processing overhead, but no lookahead or additional reported latency.

## Verification executed locally

- GCC core DSP and neural mock/transport tests passed.
- Clang AddressSanitizer/UndefinedBehaviorSanitizer tests passed for DSP, neural transport, neural blend, and mock RNNoise API prior to packaging; the added incomplete-output test also passed on GCC. Run the CI workflows to repeat the tests on the final source.
- Injected neural NaN, >8-valued, and partially written model frames were all rejected; the test verifies recovery with correctly aligned later frames.
- Injected NaN, infinity, and ±17 float input into the real classical Separator, testing finite output and complementary reconstruction after the fixed 1,024-sample delay.
- The controlled speaker1 + office/traffic 0/5/10 dB SNR benchmark remained **+6.187 dB mean improvement** relative to the noisy input. This is spoken-speech denoising, NOT singing-instrument separation.
- A proposed shorter noise history and a more permissive side-tonality mask were **rejected** because their measured SI-SDR worsened; the proven v1.7 masks remain unchanged.
- Classical CPU profiler, this Linux container: 48 kHz / 256, 5 s, mean callback 125.227 µs (2.348% of deadline), p99 242.758 µs, 0 misses; 96 kHz / 256, 5 s, mean 126.654 µs (4.750%), p99 290.509 µs, 0 misses. This is a single shared-machine snapshot, *not* RNNoise inference, Windows or DAW timing; host CPU can differ.

## Remaining limitations

A real Windows JUCE build, linked RNNoise binary/weights, FL Studio host tests, and paired isolated *singing*-vocal benchmarks remain unverified. The optional neural path denoises speech and does not replace a trained multi-stem vocal/reverb separator. The fallback masks are unchanged specifically because candidate tweaks did not show convincing objective gains.
