# Skell Separator — Live Track Clean + low-latency research

## Why this is different from full separation

Live Track is now a dedicated **zero-added-sample** microphone path. The normal
spectral separator / ONNX workers do not run on the track callback; after returning
to full separation, spectral history re-primes on the audio thread. Change tracking
mode while the DAW transport is stopped because host PDC switches between zero and
2,880 samples.

Track / DRY preserves all finite mic samples verbatim, including signals over
0 dBFS on floating-point DAW buses. Track / CLEAN is a lightweight adaptive
**downward expander**. It suppresses steady room tone and decays/reverb *between*
phrases, not under the words themselves. It cannot separate live vocals from
instrumental audio, nor remove overlapping reverb reflections during singing.
It is not the proprietary NS1 algorithm, nor a neural denoiser.

### Settings

- **Track / DRY**: enable monitoring, Clean=0%, total plugin PDC=0.
- **Track / CLEAN**: enable monitoring with at least Clean=50%, total plugin PDC=0.
- **Live Clean Amount**: 0/25/50/75/100%. Adaptive floor-relative attenuation;
  0% is a bit-transparent path for valid samples, including stereo/polarity.
- **Tail Suppression**: 0/25/35/50/75/100%. Shortens the gate hold and decay
  between phrases, not a trained vocal dereverb model.
- **GR** indicator in the header: estimated current applied gain reduction (dB);
  overload still takes priority.

Threshold follows quiet room-tone levels, but deliberately does not learn during
strong vocal energy. Instant reopening avoids the usual slow-attack first-word
cutoff at the cost of letting some consonant/noise onset through. The common
stereo gain preserves stereo balance. If quiet syllables are attenuated, lower
Clean/Tails or use DRY. Strong noise at vocal-like levels and noise underneath
sustained words cannot be removed reliably with this expander.

**Signal and latency:** Audio is processed at the present input sample, with no
lookahead; any interface / driver / FL Studio buffer / other effects still add
latency. The fast path never performs FFT or runs ONNX inference. While track is
active, Bypass passes the direct dry microphone sample unmodified.

### Automated checks (not substituted for studio listening)

`bash tests/run_live_cleanup.sh` at 44.1, 48, 96 kHz. Runs with GCC/Clang and
Clang ASan/UBSan. Checks no nonfinites, dry transparency, opposite-polarity
stereo imaging, noise attenuation, vowel preservation, onset recovery and rapid
parameter changes. Existing neural, bridge, and screenshot tests remain.

## Fast separation: separate research milestone

StemgenRT's 128-sample ONNX hops could in principle avoid the current 480-sample
Skell transport. Target *reported host PDC* for its 44.1-kHz source model, if
Windows can meet deadlines under real host scheduling:

| Host buffer | Proposed PDC at 44.1 kHz | Approx. milliseconds |
|---|---:|---:|
| 64 samples | 320 samples | 7.26 |
| 128 samples | 256 samples | 5.80 |
| 256 samples | 384 samples | 8.71 |
| 512 samples | 640 samples | 14.51 |

These are **theoretical scheduling budgets from the upstream implementation**, not
latency measurements of Skell Separator. Streaming runtime still must add
correct epoch resets, nonblocking results, fallback, pre-roll, worker ownership,
variable-size buffers, in-place stereo reconstruction, DAW PDC updates and
stress tests. At 48 kHz, sample rate conversion adds its own timing and CPU
cost and requires a new qualified adapter. Offline renders remain deterministic.

### Gates before user-facing fast AI mode

1. Verify ONNX inference with a locally licensed graph; no model bundled.
2. Test sample-perfect alignment and missing-deadline behavior across buffers.
3. Test CPU and sustained underrun counts on actual Windows Ryzen + FL Studio,
   then multiple instances (model upstream still misses Windows deadlines).
4. Compare singing-vocal leakage/consonants/reverb to classical and TinyVocal.
5. Clear model weights and training-data commercial rights before distribution.

Upstream code: https://github.com/sweetspotsoundsystem/stemgen-rt
Weights limitations: upstream `licenses/WEIGHTS-LICENSE.md` (CC BY-NC-SA 4.0).
