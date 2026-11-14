#include "StemgenRTBridge.h"

#if defined(SKELL_WITH_STEMGENRT) && SKELL_WITH_STEMGENRT
#include <onnxruntime_cxx_api.h>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {
using Shape = std::vector<std::int64_t>;
constexpr std::size_t kTensorCount = 14;
constexpr std::size_t kStates = 13;
// Names and dimensions are verified against StemgenRT 0.8's model contract,
// not inferred from arbitrary untrusted ONNX graph metadata.
constexpr std::array<const char*, kTensorCount> kInputNames {{
    "audio_chunk", "drums_audio_history", "drums_local_hidden",
    "drums_global_hidden", "drums_spectral_numerator_tail",
    "drums_waveform_numerator_tail", "bass_audio_history",
    "bass_local_hidden", "bass_global_hidden", "bass_spectral_numerator_tail",
    "vocals_audio_history", "vocals_local_hidden", "vocals_global_hidden",
    "vocals_spectral_numerator_tail"
}};
constexpr std::array<const char*, kTensorCount> kOutputNames {{
    "separated_chunk", "next_drums_audio_history", "next_drums_local_hidden",
    "next_drums_global_hidden", "next_drums_spectral_numerator_tail",
    "next_drums_waveform_numerator_tail", "next_bass_audio_history",
    "next_bass_local_hidden", "next_bass_global_hidden", "next_bass_spectral_numerator_tail",
    "next_vocals_audio_history", "next_vocals_local_hidden", "next_vocals_global_hidden",
    "next_vocals_spectral_numerator_tail"
}};
const std::array<Shape, kTensorCount> kInputShapes {{
    {1,2,128},{1,2,896},{2,20,96},{2,1,192},{1,1,2,128},
    {1,1,2,128},{1,2,3968},{2,17,80},{2,1,160},{1,1,2,128},
    {1,2,896},{2,20,96},{2,1,192},{1,1,2,128}
}};
const std::array<Shape, kTensorCount> kOutputShapes {{
    {1,4,2,128},{1,2,896},{2,20,96},{2,1,192},{1,1,2,128},
    {1,1,2,128},{1,2,3968},{2,17,80},{2,1,160},{1,1,2,128},
    {1,2,896},{2,20,96},{2,1,192},{1,1,2,128}
}};
std::size_t count(const Shape& shape) {
    std::size_t n = 1;
    for (const auto d : shape) n *= static_cast<std::size_t>(d);
    return n;
}

class OrtHopModel final : public skell::StemgenHopModel {
public:
    explicit OrtHopModel(const char* path)
        : env_(ORT_LOGGING_LEVEL_WARNING, "SkellSeparator-Research"),
          memory_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        const auto file = juce::File(juce::String::fromUTF8(path));
        // Only the exact documented model size is accepted. The local setup
        // script additionally verifies SHA-256 before installing it.
        if (!file.existsAsFile() || file.getSize() != 31946241) return;
        // Re-verify the exact published graph at every initialization: users
        // may replace files after the separate install-time hash check.
        juce::FileInputStream stream(file);
        if (!stream.openedOk()) return;
        const juce::SHA256 digest(stream);
        if (digest.toHexString() != "0a49388dd2a47986ab9a161c8764c22853b4d601c188ac052eb530f076d513c7") return;
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        options.SetInterOpNumThreads(1);
        options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        options.DisableMemPattern();
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#if JUCE_WINDOWS
        session_ = std::make_unique<Ort::Session>(env_, file.getFullPathName().toWideCharPointer(), options);
#else
        session_ = std::make_unique<Ort::Session>(env_, file.getFullPathName().toRawUTF8(), options);
#endif
        if (session_->GetInputCount() != kTensorCount || session_->GetOutputCount() != kTensorCount) return;
        Ort::AllocatorWithDefaultOptions allocator;
        for (std::size_t i = 0; i < kTensorCount; ++i) {
            const auto inName = session_->GetInputNameAllocated(i, allocator);
            const auto outName = session_->GetOutputNameAllocated(i, allocator);
            if (std::string(inName.get()) != kInputNames[i] ||
                std::string(outName.get()) != kOutputNames[i]) return;
            const auto inputInfo = session_->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
            const auto outputInfo = session_->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo();
            if (inputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                outputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                inputInfo.GetShape() != kInputShapes[i] ||
                outputInfo.GetShape() != kOutputShapes[i]) return;
        }
        inputTensors_.reserve(kTensorCount);
        outputTensors_.reserve(kTensorCount);
        for (std::size_t i = 0; i < kTensorCount; ++i) {
            inputData_[i].resize(count(kInputShapes[i]));
            outputData_[i].resize(count(kOutputShapes[i]));
            inputTensors_.emplace_back(Ort::Value::CreateTensor<float>(
                memory_, inputData_[i].data(), inputData_[i].size(),
                kInputShapes[i].data(), kInputShapes[i].size()));
            outputTensors_.emplace_back(Ort::Value::CreateTensor<float>(
                memory_, outputData_[i].data(), outputData_[i].size(),
                kOutputShapes[i].data(), kOutputShapes[i].size()));
        }
        ready_ = true;
    }

    bool valid() const noexcept override { return ready_; }
    void reset() noexcept override {
        primed_ = false;
        for (std::size_t i = 1; i < kTensorCount; ++i)
            std::fill(inputData_[i].begin(), inputData_[i].end(), 0.0f);
        ready_ = session_ != nullptr && inputTensors_.size() == kTensorCount;
    }
    bool infer(const skell::StemgenStereoHop& input,
               skell::StemgenStereoHop& vocals) noexcept override {
        if (!ready_) return false;
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t i = 0; i < skell::stemgenHopSize; ++i)
                inputData_[0][ch * skell::stemgenHopSize + i] = input[ch][i];
        try {
            session_->Run(Ort::RunOptions{nullptr}, kInputNames.data(), inputTensors_.data(),
                          kTensorCount, kOutputNames.data(), outputTensors_.data(), kTensorCount);
        } catch (const Ort::Exception&) {
            ready_ = false; return false;
        } catch (...) {
            ready_ = false; return false;
        }
        // Validate returned states BEFORE committing them. Never persist NaNs.
        for (std::size_t i = 0; i < kTensorCount; ++i)
            for (const float value : outputData_[i])
                if (!std::isfinite(value)) { ready_ = false; return false; }
        // Graph output layout [1,4,2,128], with Vocals at source index 2.
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t i = 0; i < skell::stemgenHopSize; ++i)
                vocals[ch][i] = outputData_[0][2 * 2 * skell::stemgenHopSize +
                                                ch * skell::stemgenHopSize + i];
        for (std::size_t i = 1; i < kTensorCount; ++i)
            std::copy(outputData_[i].begin(), outputData_[i].end(), inputData_[i].begin());
        const bool wasPrimed = primed_;
        primed_ = true;
        return wasPrimed;
    }

private:
    Ort::Env env_;
    Ort::MemoryInfo memory_;
    std::unique_ptr<Ort::Session> session_;
    std::array<std::vector<float>, kTensorCount> inputData_ {}, outputData_ {};
    std::vector<Ort::Value> inputTensors_, outputTensors_;
    bool ready_ = false, primed_ = false;
};
} // anonymous namespace

namespace skell {
std::unique_ptr<StemgenHopModel> createStemgenRTHopModel(const char* localModelPath) {
    if (localModelPath == nullptr || *localModelPath == '\0') return {};
    try {
        auto model = std::make_unique<OrtHopModel>(localModelPath);
        return model->valid() ? std::move(model) : nullptr;
    } catch (...) {
        // Unsupported/missing runtime or graph: fail closed to Classical.
        return {};
    }
}
std::unique_ptr<NeuralFrameBackend> createStemgenRTBackend(const char* localModelPath) {
    auto model = createStemgenRTHopModel(localModelPath);
    if (!model) return {};
    auto bridge = std::make_unique<StemgenRTBridge>(std::move(model));
    return bridge->valid() ? std::move(bridge) : nullptr;
}
}
#else
namespace skell {
std::unique_ptr<StemgenHopModel> createStemgenRTHopModel(const char*) { return {}; }
std::unique_ptr<NeuralFrameBackend> createStemgenRTBackend(const char*) { return {}; }
}
#endif
