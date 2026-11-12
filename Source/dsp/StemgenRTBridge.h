#pragma once
// Original Skell integration adapter.  StemgenRT's ONNX inference contract is
// 44.1 kHz, stereo, 128-sample hops, with one-hop previous-input alignment.
// We keep Skell's existing 480-sample NeuralFrame transport and 2880-sample PDC.
// The model and ONNX Runtime are optional; no third-party weights are bundled.
#include "StreamingNeural.h"
#include <array>
#include <cstddef>
#include <memory>

namespace skell {
constexpr int stemgenSampleRate = 44100;
constexpr int stemgenHopSize = 128;

using StemgenStereoHop = std::array<std::array<float, stemgenHopSize>, 2>;

// A hop predictor returns false for the model's one invalid pre-roll hop.
// All persistent model states belong to the predictor and are reset on gaps.
class StemgenHopModel {
public:
    virtual ~StemgenHopModel() = default;
    virtual bool valid() const noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual bool infer(const StemgenStereoHop& input, StemgenStereoHop& previousVocals) noexcept = 0;
};

class StemgenRTBridge final : public NeuralFrameBackend {
public:
    explicit StemgenRTBridge(std::unique_ptr<StemgenHopModel> model) noexcept;
    bool valid() const noexcept override;
    void reset() noexcept override;
    void process(const NeuralFrame& input, NeuralFrame& output) noexcept override;

private:
    // 2048 covers the first-frame accumulation (including one model-hop delay),
    // plus all jitter from 480/128 frame subdivision, with a checked bound.
    static constexpr std::size_t fifoSize = 2048;
    std::unique_ptr<StemgenHopModel> model_;
    StemgenStereoHop inputHop_ {};
    std::array<std::array<float, fifoSize>, 2> vocalFifo_ {};
    std::size_t inputFill_ = 0, read_ = 0, write_ = 0, available_ = 0;
    std::size_t framesSeen_ = 0;
    bool failed_ = false;
};

// The trained model is accepted ONLY when an independently installed, verified
// StemgenRT 0.7/0.8 ONNX graph and optional ONNX Runtime SDK are available.
// Returns null when disabled, absent, incompatible, or unlicensed for the use.
std::unique_ptr<NeuralFrameBackend> createStemgenRTBackend(const char* localModelPath);
}
