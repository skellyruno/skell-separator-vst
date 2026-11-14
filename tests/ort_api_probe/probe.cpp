// Standalone Windows ORT 1.26 environment/API smoke test.
// No JUCE, no training weights, and no DAW dependency.
#include <onnxruntime_cxx_api.h>
#include <cstdio>
#include <exception>
int main() {
    std::fprintf(stderr, "[ORT probe] program entry\n"); std::fflush(stderr);
    const OrtApiBase* base = OrtGetApiBase();
    if (!base) { std::fprintf(stderr, "NULL OrtApiBase\n"); return 10; }
    const OrtApi* api = base->GetApi(ORT_API_VERSION);
    std::fprintf(stderr, "[ORT probe] version=%s expected_api=%u ptr=%p\n",
                 base->GetVersionString(), static_cast<unsigned>(ORT_API_VERSION),
                 static_cast<const void*>(api)); std::fflush(stderr);
    if (!api) return 11;
    std::fprintf(stderr, "[ORT probe] C CreateEnv\n"); std::fflush(stderr);
    OrtEnv* rawEnv = nullptr;
    OrtStatus* status = api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "SkellORTProbe", &rawEnv);
    if (status) {
        std::fprintf(stderr, "C CreateEnv error: %s\n", api->GetErrorMessage(status));
        api->ReleaseStatus(status); return 12;
    }
    if (!rawEnv) return 13;
    api->ReleaseEnv(rawEnv);
    std::fprintf(stderr, "[ORT probe] C environment OK; C++ Env begins\n"); std::fflush(stderr);
    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "SkellORTProbeCPP");
        std::fprintf(stderr, "[ORT probe] C++ environment OK; memory info begins\n"); std::fflush(stderr);
        auto info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::fprintf(stderr, "[ORT probe] C++ memory info OK\n"); std::fflush(stderr);
    } catch (const Ort::Exception& e) {
        std::fprintf(stderr, "[ORT probe] Ort::Exception: %s\n", e.what()); return 14;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[ORT probe] std::exception: %s\n", e.what()); return 15;
    }
    return 0;
}