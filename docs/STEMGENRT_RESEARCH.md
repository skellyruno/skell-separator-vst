# StemgenRT experimental vocal backend — research branch

This is a **code integration and compatibility test**, not a release of the
pretrained model. Skell Separator retains its original Classical and TinyVocalNet
paths, resizable UI, existing VST3 identity, existing `engineMode` parameter and
fixed 2,880-sample host PDC. A *new* `stemgenResearch` parameter is additive;
older project automation retains its meaning.

## Upstream and redistribution

- Original source/model architecture: https://github.com/sweetspotsoundsystem/stemgen-rt
- Training and independent-model ONNX contract: https://github.com/sweetspotsoundsystem/StemgenRT-5.8
- Their code is MIT; original bridge, C++ ONNX wrapper, and tests here are new
  Skell code rather than copies of their plugin implementation.
- **Pretrained weights are CC BY-NC-SA 4.0**, with underlying MUSDB18-HQ and
  MoisesDB rights **not commercially cleared**. See upstream
  `licenses/WEIGHTS-LICENSE.md`. **Do not bundle, resell, upload, or deploy those
  weights commercially without additional permissions.**
- The ONNX graph is intentionally **not included** here or in GitHub CI artifacts.

## Model contract and sample rate

The independent graph, unchanged across StemgenRT 0.7/0.8, is:

- SHA-256: `0a49388dd2a47986ab9a161c8764c22853b4d601c188ac052eb530f076d513c7`;
  bytes: `31,946,241`.
- Stereo input `[1,2,128]` at 44,100 Hz, input `audio_chunk`.
- Output `[1,4,2,128]` at `separated_chunk`, order Drums/Bass/**Vocals**/Other.
- 13 state tensors with precisely checked names, shapes and float32 types.
- Every model hop predicts the **previous** hop; the first is discarded.

The adapter accumulates 480 host samples into 128-sample ONNX hops. It returns
480 samples for the **previous** outer frame, matching Skell's current transport.
No resampling is attempted. The existing 2,880-sample delay becomes **65.31 ms
at 44.1 kHz**, not 60 ms. This is deliberate and provides fixed CPU headroom.
The 44.1-kHz StemgenRT mode stays unavailable at other sample rates. The existing
48-kHz TinyVocalNet remains distinct. `Voice Reverb` is still the original
heuristic estimator, **not a separate StemgenRT neural reverb stem**. Complementary
three-stem reconstruction is preserved by Skell's mixer.

## Build (research only)

Supply your own **ONNX Runtime v1.26.0 CPU SDK** and enable the option explicitly:

```powershell
cmake -S . -B build-stemgen -A x64 -DSKELL_ENABLE_STEMGENRT=ON -DONNXRUNTIME_ROOT="C:/deps/onnxruntime-win-x64-1.26.0"
cmake --build build-stemgen --config Release --parallel 2
```

You must also make ONNX Runtime's DLL resolvable by Windows for this optional
build (for instance, alongside the VST3 binary; observe its own license). The
standard release is built with this option **off** and has no ONNX dependency.
When the model is missing/invalid, the plugin keeps Classical available.

Download the model only if its usage rights permit your intended experiments.
After obtaining `independent.onnx` from upstream, verify and install it locally:

```powershell
python tools/install_stemgenrt_model.py C:/path/to/independent.onnx --acknowledge-noncommercial
```

This verifies SHA-256 and size, then installs under
`%APPDATA%/Skell/Separator/Models/independent.onnx`. On model startup the plugin
also verifies those same bytes and validates every input/output tensor. In the
plugin, select **Processing Engine → StemgenRT (noncommercial research)** at
44.1 kHz, or use the header's DSP/AI switch when the model is ready.

## Performance and validation gates

Completed locally: a fake one-hop lookbehind model tests 480/128 alignment,
left/right channel preservation, complete frames, resets, invalid input,
fixed 2,880-sample latency and no audio-thread allocations in the bridge.
Original Skell neural-transport and TinyVocalNet tests are retained.

The ONNX-backed wrapper **needs a build and end-to-end test with the real SDK
and independently licensed graph** before any quality, performance or
production-readiness claim. The test CI compiles the optional C++ path without
redistributing weights, but cannot establish vocal isolation or CPU deadlines.
Real singing tests should report SI-SDR, vocal leakage during instrumental-only
sections, consonant/transient preservation, reverb stem quality, and missed
worker deadlines across 44.1-kHz FL Studio buffer sizes 64/128/256/512 at
multiple instances. Upstream's Windows x86 measurements still had underruns;
reduced-latency claims cannot be carried over to Skell.
