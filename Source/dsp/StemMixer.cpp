#include "StemMixer.h"

void StemMixer::prepare(double sampleRate) noexcept
{
    for (int s = 0; s < 3; ++s)
    {
        gains[static_cast<size_t>(s)].reset(sampleRate, 0.020);
        gates[static_cast<size_t>(s)].reset(sampleRate, 0.020);
        gains[static_cast<size_t>(s)].setCurrentAndTargetValue(1.0f);
        gates[static_cast<size_t>(s)].setCurrentAndTargetValue(1.0f);
    }
    bypassCrossfade.reset(sampleRate, 0.010);
    bypassCrossfade.setCurrentAndTargetValue(0.0f);
}

void StemMixer::snapToTargets() noexcept
{
    for (auto& gain : gains) gain.setCurrentAndTargetValue(gain.getTargetValue());
    for (auto& gate : gates) gate.setCurrentAndTargetValue(gate.getTargetValue());
    bypassCrossfade.setCurrentAndTargetValue(bypassCrossfade.getTargetValue());
}

void StemMixer::setTargets(const std::array<float, 3>& db, const std::array<bool, 3>& solo,
                           const std::array<bool, 3>& mute, bool bypass) noexcept
{
    const bool anySolo = solo[0] || solo[1] || solo[2];
    for (int s = 0; s < 3; ++s)
    {
        const auto idx = static_cast<size_t>(s);
        const float gain = db[idx] <= -39.95f ? 0.0f : juce::Decibels::decibelsToGain(db[idx]);
        gains[idx].setTargetValue(gain);
        gates[idx].setTargetValue((!mute[idx] && (!anySolo || solo[idx])) ? 1.0f : 0.0f);
    }
    bypassCrossfade.setTargetValue(bypass ? 1.0f : 0.0f);
}

std::array<float, 2> StemMixer::mix(const Separator::Stems& stems,
                                    const std::array<float, 2>& delayed) noexcept
{
    std::array<float, 3> factors {};
    for (int s = 0; s < 3; ++s)
        factors[static_cast<size_t>(s)] = gains[static_cast<size_t>(s)].getNextValue()
                                        * gates[static_cast<size_t>(s)].getNextValue();
    const float dryMix = bypassCrossfade.getNextValue();
    std::array<float, 2> result {};
    for (int ch = 0; ch < 2; ++ch)
    {
        float sum = 0.0f;
        for (int s = 0; s < 3; ++s)
            sum += stems[static_cast<size_t>(s)][static_cast<size_t>(ch)] * factors[static_cast<size_t>(s)];
        result[static_cast<size_t>(ch)] = sum + dryMix * (delayed[static_cast<size_t>(ch)] - sum);
    }
    return result;
}
