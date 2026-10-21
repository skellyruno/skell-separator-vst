#include "Separator.h"
#include <cmath>
#include <algorithm>

Separator::Separator(SpectralAnalyzer& a) : analyzer(a)
{
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t>(i)] = std::sqrt(0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i) / fftSize));
    // Frequency lookup is built in prepare(), so 44.1/48/96/192 kHz
    // show the same frequencies in the same UI bands.
    reset();
}

void Separator::prepare(double sampleRate)
{
    sr = sampleRate;
    const double safeRate = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    const double lowHz = 40.0;
    const double highHz = std::max(lowHz * 2.0, std::min(16000.0, safeRate * 0.5));
    const double logRange = std::log(highHz / lowHz);
    displayBandCounts.fill(0.0f);
    for (int k = 0; k < spectralBins; ++k)
    {
        const double hz = std::max(lowHz, static_cast<double>(k) * safeRate / fftSize);
        const double norm = std::clamp(std::log(hz / lowHz) / logRange, 0.0, 1.0);
        const int band = std::clamp(static_cast<int>(norm * (SpectralAnalyzer::bands - 1)),
                                    0, SpectralAnalyzer::bands - 1);
        displayBands[static_cast<size_t>(k)] = band;
        displayBandCounts[static_cast<size_t>(band)] += 1.0f;
    }
    for (auto& count : displayBandCounts) count = 1.0f / std::max(1.0f, count);
    estimator.prepare(safeRate);
    reset();
}

void Separator::reset() noexcept
{
    currentSample = 0;
    for (auto& v : inputHistory) v.fill(0);
    for (auto& v : dryOla) v.fill(0);
    for (auto& v : reverbOla) v.fill(0);
    for (auto& v : delayedInput) v.fill(0);
    for (auto& v : rawDryDelay) v.fill(0);
    estimator.reset();
    dryMask.fill(0.0f);
    wetMask.fill(0.0f);
}

void Separator::processSample(float left, float right, float monoBlend, int quality,
                              Stems& output, std::array<float, channels>& originalDelayed) noexcept
{
    const auto i = currentSample;
    const auto ringIndex = static_cast<size_t>(i & (ringSize - 1));
    const auto historyIndex = static_cast<size_t>(i & (fftSize - 1));
    const float middle = 0.5f * (left + right);
    const float channelInput[2] { left + monoBlend * (middle - left),
                                   right + monoBlend * (middle - right) };
    rawDryDelay[0][ringIndex] = left;
    rawDryDelay[1][ringIndex] = right;

    for (int ch = 0; ch < channels; ++ch)
    {
        inputHistory[static_cast<size_t>(ch)][historyIndex] = channelInput[ch];
        delayedInput[static_cast<size_t>(ch)][ringIndex] = channelInput[ch];
    }
    // Do not synthesize a partially-filled first FFT frame: it can produce
    // pre-echo in the 1024-sample latency warm-up interval.
    if (i >= fftSize - 1 && (i & (hopSize - 1)) == hopSize - 1)
        processFrame(quality);

    const size_t delayedIndex = static_cast<size_t>((i - latency) & (ringSize - 1));
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        const float delayed = delayedInput[c][delayedIndex];
        originalDelayed[c] = rawDryDelay[c][delayedIndex];
        const float voice = dryOla[c][ringIndex];
        const float rev = reverbOla[c][ringIndex];
        dryOla[c][ringIndex] = 0.0f;
        reverbOla[c][ringIndex] = 0.0f;
        // Exact complementary residual. At unity gains, each sample sums to
        // the delayed original regardless of imperfect STFT normalization.
        output[0][c] = delayed - voice - rev;
        output[1][c] = voice;
        output[2][c] = rev;
    }
    ++currentSample;
}

void Separator::processFrame(int quality) noexcept
{
    const int64_t frameStart = currentSample - (fftSize - 1);
    for (int ch = 0; ch < channels; ++ch)
    {
        auto& time = forward[static_cast<size_t>(ch)];
        for (int j = 0; j < fftSize; ++j)
        {
            time[static_cast<size_t>(j)] = {
                inputHistory[static_cast<size_t>(ch)][static_cast<size_t>((frameStart + j) & (fftSize - 1))]
                    * window[static_cast<size_t>(j)], 0.0f };
        }
        fft.perform(time.data(), spectrum[static_cast<size_t>(ch)].data(), false);
    }

    // Shared L/R masks preserve the stereo placement of each component.
    // Mid/side estimates and masks are strictly bounded by MaskEstimator.
    for (int k = 0; k < spectralBins; ++k)
    {
        const auto z = static_cast<size_t>(k);
        const auto mid = 0.5f * (spectrum[0][z] + spectrum[1][z]);
        const auto side = 0.5f * (spectrum[0][z] - spectrum[1][z]);
        const float mr = mid.real(), mi = mid.imag();
        const float sr0 = side.real(), si0 = side.imag();
        const float midSquared = mr * mr + mi * mi;
        const float sideSquared = sr0 * sr0 + si0 * si0;
        // Parseval-consistent stereo energy: identical for full in-phase
        // and polarity-inverted signals of the same per-channel amplitude.
        // Masking on mid alone erased all anti-phase voices into ambience.
        analysisMagnitude[static_cast<size_t>(k)] = std::sqrt(midSquared + sideSquared);
        sideMagnitude[static_cast<size_t>(k)] = std::sqrt(sideSquared);
    }
    estimator.process(analysisMagnitude, sideMagnitude, quality, dryMask, wetMask);

    SpectralAnalyzer::Frame frame;
    for (int k = 0; k < spectralBins; ++k)
    {
        const auto idx = static_cast<size_t>(k);
        const float mag = analysisMagnitude[idx];
        const float dry = dryMask[idx];
        const float wet = wetMask[idx];
        const float factors[3] { 1.0f - dry - wet, dry, wet };
        const auto band = static_cast<size_t>(displayBands[idx]);
        for (int s = 0; s < 3; ++s)
            frame.magnitudes[static_cast<size_t>(s)][band] += factors[s] * mag * displayBandCounts[band];
    }
    for (int stem = 0; stem < 3; ++stem)
    {
        float peak = 0.0f;
        for (float value : frame.magnitudes[static_cast<size_t>(stem)])
            peak = std::max(peak, value);
        frame.levels[static_cast<size_t>(stem)] = std::clamp(peak * 0.018f, 0.0f, 1.0f);
    }
    analyzer.publish(frame);

    for (int ch = 0; ch < channels; ++ch)
        for (int stem = 1; stem <= 2; ++stem)
            runInverse(ch, stem, frameStart);
}

void Separator::runInverse(int ch, int stem, int64_t frameStart) noexcept
{
    const auto& spec = spectrum[static_cast<size_t>(ch)];
    for (int k = 0; k < fftSize; ++k)
    {
        const int mirrored = (k <= fftSize / 2 ? k : fftSize - k);
        const float mask = stem == 1 ? dryMask[static_cast<size_t>(mirrored)]
                                     : wetMask[static_cast<size_t>(mirrored)];
        maskSpectrum[static_cast<size_t>(k)] = spec[static_cast<size_t>(k)] * mask;
    }
    fft.perform(maskSpectrum.data(), ifftBuffer.data(), true);
    auto& ola = stem == 1 ? dryOla[static_cast<size_t>(ch)]
                          : reverbOla[static_cast<size_t>(ch)];
    for (int j = 0; j < fftSize; ++j)
    {
        const auto destination = static_cast<size_t>((frameStart + j + latency) & (ringSize - 1));
        // Hann perfect overlap-add at hop N/4 has sum 2 (once primed).
        ola[destination] += 0.5f * window[static_cast<size_t>(j)]
                            * ifftBuffer[static_cast<size_t>(j)].real();
    }
}
