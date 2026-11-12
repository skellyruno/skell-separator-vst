#include "StemgenRTBridge.h"
#include <cmath>
#include <utility>

namespace skell {
StemgenRTBridge::StemgenRTBridge(std::unique_ptr<StemgenHopModel> model) noexcept
    : model_(std::move(model)) { reset(); }

bool StemgenRTBridge::valid() const noexcept { return model_ && model_->valid(); }

void StemgenRTBridge::reset() noexcept {
    inputFill_ = read_ = write_ = available_ = framesSeen_ = 0;
    failed_ = false;
    inputHop_ = {};
    if (model_) model_->reset();
}

void StemgenRTBridge::process(const NeuralFrame& input, NeuralFrame& output) noexcept {
    // First outer frame primes both the model and the 480-sample transport.
    // Never allow a partial frame to inherit contents from an earlier epoch.
    output = {};
    output.valid = valid() && !failed_;
    if (!output.valid) return;

    for (std::size_t i = 0; i < neuralFrameSize; ++i) {
        for (std::size_t ch = 0; ch < 2; ++ch) {
            const float value = input.audio[ch][i];
            if (!std::isfinite(value) || std::abs(value) > 8.f) {
                output.valid = false;
                failed_ = true;
                return;
            }
            inputHop_[ch][inputFill_] = value;
        }
        if (++inputFill_ != stemgenHopSize) continue;
        inputFill_ = 0;
        StemgenStereoHop predicted {};
        const bool ready = model_->infer(inputHop_, predicted);
        if (!model_->valid()) {
            output.valid = false;
            failed_ = true;
            return;
        }
        if (!ready) continue; // exactly one ignored model pre-roll hop
        if (available_ + stemgenHopSize > fifoSize) {
            output.valid = false;
            failed_ = true;
            return;
        }
        for (std::size_t j = 0; j < stemgenHopSize; ++j) {
            for (std::size_t ch = 0; ch < 2; ++ch) {
                const float value = predicted[ch][j];
                if (!std::isfinite(value) || std::abs(value) > 8.f) {
                    output.valid = false;
                    failed_ = true;
                    return;
                }
                vocalFifo_[ch][(write_ + j) % fifoSize] = value;
            }
        }
        write_ = (write_ + stemgenHopSize) % fifoSize;
        available_ += stemgenHopSize;
    }
    if (++framesSeen_ == 1) return; // discarded by StreamingNeural anyway
    if (available_ < neuralFrameSize) {
        output.valid = false;
        failed_ = true;
        return;
    }
    for (std::size_t i = 0; i < neuralFrameSize; ++i)
        for (std::size_t ch = 0; ch < 2; ++ch)
            output.audio[ch][i] = vocalFifo_[ch][(read_ + i) % fifoSize];
    read_ = (read_ + neuralFrameSize) % fifoSize;
    available_ -= neuralFrameSize;
}
}
