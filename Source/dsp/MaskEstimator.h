#pragma once

// Pure C++ estimator: no JUCE dependency, heap allocations, mutexes or system calls.
// This is a heuristic spectral mask estimator, NOT a trained voice separator.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

class MaskEstimator final
{
public:
    static constexpr int bins = 513;
    static constexpr int historyWindows = 8;
    static constexpr int baseFramesPerWindow = 16; // legacy 48 kHz reference
    using Spectrum = std::array<float, bins>;

    MaskEstimator() { prepare(48000.0); }

    void prepare(double sampleRate) noexcept
    {
        // Express time constants in seconds, not STFT frames: the same preset
        // should behave consistently at 44.1, 48, 96 and 192 kHz.
        const double safeRate = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 384000.0) : 48000.0;
        const float hzPerBin = static_cast<float>(safeRate / 1024.0);
        const auto coefficient = [safeRate](double seconds) noexcept
        {
            return static_cast<float>(1.0 - std::exp(-256.0 / (safeRate * seconds)));
        };
        noiseFramesPerWindow = std::clamp(static_cast<int>(std::lround(0.085 * safeRate / 256.0)), 2, 256);
        silenceResetFrames = std::clamp(static_cast<int>(std::lround(0.256 * safeRate / 256.0)), 8, 512);
        magnitudeAttack = coefficient(0.015);
        priorAttack = coefficient(0.012);
        priorRelease = coefficient(0.065);
        tailDecay = static_cast<float>(std::exp(-256.0 / (safeRate * 0.23)));
        dryAttack = coefficient(0.006);
        dryRelease = coefficient(0.020);
        wetAttack = coefficient(0.019);
        wetRelease = coefficient(0.042);
        for (int k = 0; k < bins; ++k)
        {
            const float frequency = static_cast<float>(k) * hzPerBin;
            // Retain more low-register vocal fundamentals while fully rejecting DC.
            // This changes ONLY the speech-likelihood prior, not the waveform.
            const float highPass = frequency < 25.0f ? 0.0f
                : 0.35f + 0.65f * std::clamp((frequency - 25.0f) / 175.0f, 0.0f, 1.0f);
            const float highShelf = 1.0f - 0.60f * std::clamp((frequency - 9000.0f) / 11000.0f, 0.0f, 1.0f);
            bandWeight[static_cast<std::size_t>(k)] = highPass * highShelf;
        }
        reset();
    }

    void reset() noexcept
    {
        frames = 0;
        silentFrames = 0;
        currentMin.fill(std::numeric_limits<float>::max());
        for (auto& window : history) window.fill(std::numeric_limits<float>::max());
        smoothDry.fill(0.0f);
        smoothWet.fill(0.0f);
        smoothedMagnitude.fill(0.0f);
        priorSpeechSnr.fill(0.0f);
        lateMemory.fill(0.0f);
        dryRaw.fill(0.0f);
        wetRaw.fill(0.0f);
    }

    // inputMag: sqrt(|mid|^2 + |side|^2), with mid=(L+R)/2.
    // This is the stereo RMS magnitude, so a polarity-inverted or hard-panned
    // source is not erased from the voice detector. sideMag is used only as
    // a diffuse/stereo-width cue; a wide source is not necessarily reverb.
    // dry+wet is <= 0.97 for every bin. ambience is complementary in Separator.
    void process(const Spectrum& inputMag, const Spectrum& sideMag, int quality,
                 Spectrum& outDry, Spectrum& outWet) noexcept
    {
        // Quality modifies masking strength only. It never changes STFT timing.
        const float snrFloor = quality <= 0 ? 1.25f : (quality >= 2 ? 1.80f : 1.50f);
        const float tailAmount = quality <= 0 ? 0.15f : (quality >= 2 ? 0.38f : 0.26f);
        // Once the FFT history has become completely silent, discard stale
        // source/noise history so the next transport start is deterministic.
        bool frameSilent = true;
        for (int k = 0; k < bins; ++k)
            if ((std::isfinite(inputMag[static_cast<size_t>(k)]) && inputMag[static_cast<size_t>(k)] > 1.0e-7f) ||
                (std::isfinite(sideMag[static_cast<size_t>(k)]) && sideMag[static_cast<size_t>(k)] > 1.0e-7f))
            { frameSilent = false; break; }
        silentFrames = frameSilent ? std::min(10000, silentFrames + 1) : 0;
        if (silentFrames == silenceResetFrames) reset();
        const auto writeWindow = static_cast<size_t>((frames / static_cast<std::uint64_t>(noiseFramesPerWindow)) % historyWindows);
        if (frames > 0 && frames % static_cast<std::uint64_t>(noiseFramesPerWindow) == 0)
        {
            history[writeWindow] = currentMin;
            currentMin.fill(std::numeric_limits<float>::max());
        }

        for (int k = 0; k < bins; ++k)
        {
            const auto i = static_cast<std::size_t>(k);
            // Nonfinite audio cannot poison the rolling noise or mask state.
            const float magnitude = std::isfinite(inputMag[i]) ? std::max(0.0f, inputMag[i]) : 0.0f;
            const float side = std::isfinite(sideMag[i]) ? std::max(0.0f, sideMag[i]) : 0.0f;
            float& smooth = smoothedMagnitude[i];
            smooth += magnitudeAttack * (magnitude - smooth);
            // Ignore virtually silent windows. A literal zero from transport
            // silence would otherwise poison the minimum noise estimate and
            // label the next full-band onset almost entirely as "voice".
            if (smooth > 1.0e-4f)
                currentMin[i] = std::min(currentMin[i], smooth);
            // Robust order statistic: the 2nd-smallest nonempty window avoids
            // one anomalously quiet FFT frame dominating ~0.7 s of masking.
            float first = std::numeric_limits<float>::max();
            float second = first;
            const auto insert = [&first, &second](float v) noexcept
            {
                if (!std::isfinite(v) || v == std::numeric_limits<float>::max()) return;
                if (v < first) { second = first; first = v; }
                else if (v < second) second = v;
            };
            insert(currentMin[i]);
            for (const auto& window : history) insert(window[i]);
            const float noise = second != std::numeric_limits<float>::max() ? second
                              : first != std::numeric_limits<float>::max() ? first
                              : magnitude;

            // Wiener-like soft suppression; band limiting reduces bass rumble and hiss
            // in the estimated speech. Avoid a hard binary mask (musical noise).
            const float snr = magnitude / (snrFloor * noise + 1.0e-5f);
            float fraction = (snr - 0.48f) / (snr + 0.9f);
            fraction = std::clamp(fraction, 0.0f, 0.97f);
            // Decision-directed prior SNR: weak, isolated noise peaks are less
            // likely to produce a strong voice mask than sustained harmonic
            // evidence. No look-ahead, allocations or extra FFTs. This is a
            // lightweight classical estimator, NOT a pretrained voice model.
            const float posterior = std::clamp(snr * snr - 1.0f, 0.0f, 200.0f);
            auto& prior = priorSpeechSnr[i];
            prior += (posterior > prior ? priorAttack : priorRelease) * (posterior - prior);
            const float wienerPrior = prior / (prior + 0.85f);
            fraction *= 0.78f + 0.22f * wienerPrior;
            // Frequency weights are precomputed for the actual sample rate.
            const float broadBand = bandWeight[i];
            const float stereoSideRatio = side / (side + magnitude + 1.0e-6f);
            const float centreBias = 1.0f - 0.55f * std::clamp(stereoSideRatio, 0.0f, 1.0f);
            fraction *= broadBand * centreBias;

            // Causal voice-conditioned tail estimate. Unlike a generic peak
            // follower, this remembers only bins plausibly belonging to voice;
            // a stereo-wide ambience hit does not automatically become reverb.
            // This is STILL heuristic decay subtraction, not dereverberation AI.
            auto& tail = lateMemory[i];
            const float vocalEnergy = fraction * magnitude;
            const float earlierVoice = tail * tailDecay;
            tail = std::max(vocalEnergy, earlierVoice);
            const float decaying = std::clamp((earlierVoice - vocalEnergy) /
                                                  (earlierVoice + 1.0e-5f), 0.0f, 1.0f);
            // Diffuse/side content is more consistent with late reflections;
            // keep the mask modest since side != reverb in general.
            const float diffuseness = std::clamp(side / (side + magnitude + 1.0e-6f), 0.0f, 1.0f);
            // Require an actual falling spectral envelope as well as earlier
            // voice evidence. A stationary tone/noise should not be labeled
            // reverb just because the noise estimate or voice mask drifts.
            const float falling = std::clamp((smooth - magnitude) / (smooth + 1.0e-4f), 0.0f, 1.0f);
            const float wetEvidence = tailAmount * decaying * std::min(1.0f, 3.5f * falling)
                                      * (0.55f + 0.45f * diffuseness);
            wetRaw[i] = std::min(0.32f, wetEvidence) *
                         std::clamp(magnitude / (noise * 1.5f + 1.0e-5f), 0.0f, 1.0f);
            dryRaw[i] = fraction * (1.0f - 0.40f * decaying);
        }

        // Energy-aware three-bin smoothing: avoid spreading a harmonic mask
        // into an adjacent spectral hole. Ordinary 0.25/0.50/0.25 blur can
        // leak singing-voice harmonics across empty FFT bins, yielding a
        // metallic sideband in the complementary ambience stem. All masking
        // remains causal and zero additional look-ahead is introduced.
        for (int k = 0; k < bins; ++k)
        {
            const auto i = static_cast<std::size_t>(k);
            const auto lo = static_cast<std::size_t>(std::max(0, k - 1));
            const auto hi = static_cast<std::size_t>(std::min(bins - 1, k + 1));
            const float centre = std::isfinite(inputMag[i]) ? std::max(0.0f, inputMag[i]) : 0.0f;
            const float lower = std::isfinite(inputMag[lo]) ? std::max(0.0f, inputMag[lo]) : 0.0f;
            const float upper = std::isfinite(inputMag[hi]) ? std::max(0.0f, inputMag[hi]) : 0.0f;
            const float lowWeight = 0.22f * std::min(1.0f, centre / (lower + 1.0e-5f));
            const float highWeight = 0.22f * std::min(1.0f, centre / (upper + 1.0e-5f));
            const float inverseWeight = 1.0f / (1.0f + lowWeight + highWeight);
            const float dryTarget = (dryRaw[i] + lowWeight * dryRaw[lo]
                                   + highWeight * dryRaw[hi]) * inverseWeight;
            const float wetTarget = (wetRaw[i] + lowWeight * wetRaw[lo]
                                   + highWeight * wetRaw[hi]) * inverseWeight;
            const float dryRate = dryTarget > smoothDry[i] ? dryAttack : dryRelease;
            const float wetRate = wetTarget > smoothWet[i] ? wetAttack : wetRelease;
            smoothDry[i] += dryRate * (dryTarget - smoothDry[i]);
            smoothWet[i] += wetRate * (wetTarget - smoothWet[i]);
            const float total = smoothDry[i] + smoothWet[i];
            const float trim = total > 0.97f ? 0.97f / total : 1.0f;
            outDry[i] = smoothDry[i] * trim;
            outWet[i] = smoothWet[i] * trim;
        }
        ++frames;
    }

private:
    std::uint64_t frames = 0; // no signed-overflow UB in multi-day host sessions
    int silentFrames = 0;
    int noiseFramesPerWindow = baseFramesPerWindow;
    int silenceResetFrames = 48;
    float magnitudeAttack = 0.36f, tailDecay = 0.95f;
    float priorAttack = 0.30f, priorRelease = 0.08f;
    float dryAttack = 0.58f, dryRelease = 0.24f;
    float wetAttack = 0.30f, wetRelease = 0.14f;
    Spectrum currentMin {}, smoothedMagnitude {}, priorSpeechSnr {}, lateMemory {}, dryRaw {}, wetRaw {}, bandWeight {};
    Spectrum smoothDry {}, smoothWet {};
    std::array<Spectrum, historyWindows> history {};
};
