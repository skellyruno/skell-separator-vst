#include "StemgenRTBridge.h"

#if defined(SKELL_WITH_STEMGENRT) && SKELL_WITH_STEMGENRT
#include <onnxruntime_cxx_api.h>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#if defined(SKELL_STEMGEN_TIMING_DIAGNOSTICS) && SKELL_STEMGEN_TIMING_DIAGNOSTICS
#include <cstdio>
#define SKELL_ORT_TRACE(text) do { std::fprintf(stderr, "[Skell ORT] %s\n", text); std::fflush(stderr); } while (false)
#else
#define SKELL_ORT_TRACE(text) do {} while (false)
#endif

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

// Separate the two ONNX wrapper initializers so a native Windows crash can be
// attributed to its actual call site (and not mislabeled as model inference).
Ort::Env initialiseEnvironment() {
    SKELL_ORT_TRACE("before ONNX environment constructor");
    auto env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "SkellSeparator-Research");
    SKELL_ORT_TRACE("ONNX environment constructed");
    return env;
}
Ort::MemoryInfo initialiseMemoryInfo() {
    SKELL_ORT_TRACE("before ONNX memory info allocation");
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    SKELL_ORT_TRACE("ONNX memory info allocated");
    return memory;
}
class OrtHopModel final : public skell::StemgenHopModel {
public:
    explicit OrtHopModel(const char* path, int researchIntraOpThreads)
        : env_(initialiseEnvironment()),
          memory_(initialiseMemoryInfo()) {
        SKELL_ORT_TRACE("env and allocator constructed");
        const auto file = juce::File(juce::String::fromUTF8(path));
        // Only the exact documented model size is accepted. The local setup
        // script additionally verifies SHA-256 before installing it.
        if (!file.existsAsFile() || file.getSize() != 31946241) return;
        // Re-verify the exact published graph at every initialization: users
        // may replace files after the separate install-time hash check.
        juce::FileInputStream stream(file);
        if (!stream.openedOk()) return;
        SKELL_ORT_TRACE("model bytes validated; hashing");
        const juce::SHA256 digest(stream);
        if (digest.toHexString() != "0a49388dd2a47986ab9a161c8764c22853b4d601c188ac052eb530f076d513c7") return;
        SKELL_ORT_TRACE("SHA-256 passed; create session options");
        Ort::SessionOptions options;
        // Default/live remains 1 thread; controlled research-only timing can explore 2/3.
        options.SetIntraOpNumThreads(std::clamp(researchIntraOpThreads, 1, 3));
        options.SetInterOpNumThreads(1);
        options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        options.DisableMemPattern();
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        SKELL_ORT_TRACE("options ready; constructing model session");
#if JUCE_WINDOWS
        session_ = std::make_unique<Ort::Session>(env_, file.getFullPathName().toWideCharPointer(), options);
#else
        session_ = std::make_unique<Ort::Session>(env_, file.getFullPathName().toRawUTF8(), options);
#endif
        SKELL_ORT_TRACE("model session ready; validating inputs/outputs");
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
        SKELL_ORT_TRACE("model I/O metadata valid; create tensor buffers");
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
        SKELL_ORT_TRACE("model initialized and ready");
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
        if (!primed_) SKELL_ORT_TRACE("first inference begins");
        try {
            session_->Run(Ort::RunOptions{nullptr}, kInputNames.data(), inputTensors_.data(),
                          kTensorCount, kOutputNames.data(), outputTensors_.data(), kTensorCount);
        } catch (const Ort::Exception&) {
            ready_ = false; return false;
        } catch (...) {
            ready_ = false; return false;
        }
        if (!primed_) SKELL_ORT_TRACE("first inference complete; checking state");
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
std::unique_ptr<StemgenHopModel> createStemgenRTHopModel(const char* localModelPath, int researchIntraOpThreads) {
    if (localModelPath == nullptr || *localModelPath == '\0') return {};
    // The C API is probed before any C++ wrapper touches it. If the SDK header
    // and loaded runtime do not share this API version, fail closed instead of
    // dereferencing a null OrtApi pointer inside a wrapper constructor.
    SKELL_ORT_TRACE("factory: probing ONNX C API");
    const auto* base = OrtGetApiBase();
    if (base == nullptr) return {};
    // The hosted runner loaded ORT 1.5.2 with 1.26.0 headers. Fail closed
    // before Ort::Env dereferences an incompatible/null C API table.
    if (base->GetVersionString() == nullptr ||
        std::strcmp(base->GetVersionString(), "1.26.0") != 0) {
        SKELL_ORT_TRACE("Refusing unexpected ONNX Runtime version");
        return {};
    }
    const auto* api = base->GetApi(ORT_API_VERSION);
#if defined(SKELL_STEMGEN_TIMING_DIAGNOSTICS) && SKELL_STEMGEN_TIMING_DIAGNOSTICS
    std::fprintf(stderr, "[Skell ORT] runtime=%s requested_api=%u api=%s\n",
                 base->GetVersionString(), static_cast<unsigned>(ORT_API_VERSION),
                 api == nullptr ? "MISSING" : "available");
    std::fflush(stderr);
#endif
    if (api == nullptr) return {};
    try {
        auto model = std::make_unique<OrtHopModel>(localModelPath, researchIntraOpThreads);
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
std::unique_ptr<StemgenHopModel> createStemgenRTHopModel(const char*, int) { return {}; }
std::unique_ptr<NeuralFrameBackend> createStemgenRTBackend(const char*) { return {}; }
}
#endif
