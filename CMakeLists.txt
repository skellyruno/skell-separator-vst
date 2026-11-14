cmake_minimum_required(VERSION 3.22)
project(SkellSeparator VERSION 1.13.0 LANGUAGES C CXX)

# Uses the same JUCE FetchContent/Windows runner arrangement as the known-good
# StopDial starter. Optional -DJUCE_PATH=... supports offline/local builds.
include(FetchContent)
find_package(Threads REQUIRED)
if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Pinned shared UI tutorial headers; leave existing audio engine untouched.
FetchContent_Declare(SkellGuidedTourHeaders
  GIT_REPOSITORY https://github.com/skellyruno/skell-vst-template.git
  GIT_TAG eb56702153c4373799a4e8e2663e0f158c0115a6
  GIT_SHALLOW FALSE
  SOURCE_SUBDIR _guided_tour_headers_only)
FetchContent_MakeAvailable(SkellGuidedTourHeaders)

juce_add_plugin(SkellSeparator
    COMPANY_NAME "Skell Audio"
    BUNDLE_ID "audio.skell.separator"
    PLUGIN_MANUFACTURER_CODE SkAu
    PLUGIN_CODE Ss01
    PRODUCT_NAME "Skell Separator"
    DESCRIPTION "Three-stem spectral voice and ambience separator"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone)

juce_generate_juce_header(SkellSeparator)

target_sources(SkellSeparator PRIVATE
    Source/PluginProcessor.h Source/PluginProcessor.cpp
    Source/PluginEditor.h Source/PluginEditor.cpp
    Source/dsp/Separator.h Source/dsp/Separator.cpp
    Source/dsp/MaskEstimator.h
    Source/dsp/StreamingNeural.h Source/dsp/StreamingNeural.cpp
    Source/dsp/LatencyAlignedStems.h
    Source/dsp/NeuralVoiceBlend.h
    Source/dsp/StemMixer.h Source/dsp/StemMixer.cpp
    Source/dsp/SpectralAnalyzer.h Source/dsp/SpectralAnalyzer.cpp
    Source/ui/Theme.h Source/ui/Theme.cpp
    Source/ui/LookAndFeel.h Source/ui/LookAndFeel.cpp
    Source/ui/TickKnob.h Source/ui/TickKnob.cpp
    Source/ui/StemMeter.h Source/ui/StemMeter.cpp
    Source/ui/StemColumn.h Source/ui/StemColumn.cpp
    Source/ui/SoftButton.h Source/ui/SoftButton.cpp
    Source/ui/RoundIconButton.h Source/ui/RoundIconButton.cpp
    Source/ui/HeaderBar.h Source/ui/HeaderBar.cpp)

target_include_directories(SkellSeparator PRIVATE "${skellguidedtourheaders_SOURCE_DIR}/SkellCore/include")
target_compile_features(SkellSeparator PRIVATE cxx_std_17)
target_compile_definitions(SkellSeparator PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(SkellSeparator PRIVATE
    Threads::Threads
    juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_gui_extra juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# RNNoise is a separate, optional BSD-3-Clause neural denoiser (not a source
# separator). Default Windows workflow builds the tested classical fallback.
# Enable with: -DSKELL_ENABLE_RNNOISE=ON -DRNNOISE_ROOT=<installed SDK>
# Its C API + weights remain under the RNNoise upstream license; see docs.
option(SKELL_ENABLE_RNNOISE "Enable the optional 48 kHz streaming RNNoise model" OFF)
if(SKELL_ENABLE_RNNOISE)
    find_path(RNNOISE_INCLUDE_DIR rnnoise.h HINTS "${RNNOISE_ROOT}" PATH_SUFFIXES include)
    find_library(RNNOISE_LIBRARY NAMES rnnoise librnnoise HINTS "${RNNOISE_ROOT}" PATH_SUFFIXES lib lib64)
    if(NOT RNNOISE_INCLUDE_DIR OR NOT RNNOISE_LIBRARY)
        message(FATAL_ERROR "RNNoise SDK missing: set RNNOISE_ROOT with include/rnnoise.h and lib/rnnoise")
    endif()
    target_include_directories(SkellSeparator PRIVATE "${RNNOISE_INCLUDE_DIR}")
    target_link_libraries(SkellSeparator PRIVATE "${RNNOISE_LIBRARY}")
    target_compile_definitions(SkellSeparator PRIVATE SKELL_WITH_RNNOISE=1)
    message(STATUS "Skell RNNoise enabled; fixed reported latency is 2880 samples")
endif()

# Tiny neural mask runs natively in C++, using original, reproducible synthetic
# training weights. It is an EXPERIMENTAL music-vocal demonstration, not a
# commercially benchmarked singing separator. RNNoise remains optional.
option(SKELL_ENABLE_TINY_NEURAL "Compile original experimental trained vocal-mask model" ON)
if(SKELL_ENABLE_TINY_NEURAL)
    target_sources(SkellSeparator PRIVATE Source/dsp/TinyVocalNet.h Source/dsp/TinyVocalNet.cpp
        models/TinyVocalWeights.h)
    target_compile_definitions(SkellSeparator PRIVATE SKELL_WITH_TINY_VOCAL=1)
    message(STATUS "Skell Tiny Neural compiled; latency 2880 samples (60 ms at 48 kHz), default engine Classical")
endif()

# Optional real-editor visual regression. Production VST3/Standalone builds do
# not fetch SkellCore; only the screenshot target needs the shared harness.
# Pin the same known revision used by Skell-Preset's verified renderer.
option(SKELL_BUILD_UI_RENDERER "Build SkellCore real-editor PNG validation harness" OFF)
if(SKELL_BUILD_UI_RENDERER)
    FetchContent_Declare(SkellCoreScreenshot
        GIT_REPOSITORY https://github.com/skellyruno/skell-vst-template.git
        GIT_TAG eb56702153c4373799a4e8e2663e0f158c0115a6
        SOURCE_SUBDIR SkellCore)
    FetchContent_MakeAvailable(SkellCoreScreenshot)
    # Plain executable, not juce_add_console_app: otherwise JUCE's standalone
    # flags conflict with the linked plugin SharedCode target.
    add_executable(SkellSeparatorUIRenderer tests/ui_renderer.cpp)
    target_include_directories(SkellSeparatorUIRenderer PRIVATE
    "${skellguidedtourheaders_SOURCE_DIR}/SkellCore/include"
        "${CMAKE_CURRENT_BINARY_DIR}/SkellSeparator_artefacts/JuceLibraryCode")
    target_link_libraries(SkellSeparatorUIRenderer PRIVATE
        SkellSeparator SkellCore::UIRenderer
        juce::juce_audio_utils juce::juce_gui_extra)
    target_compile_definitions(SkellSeparatorUIRenderer PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_compile_features(SkellSeparatorUIRenderer PRIVATE cxx_std_17)
endif()

# Optional external StemgenRT 0.7/0.8 graph. Research only; weights aren't bundled.
option(SKELL_ENABLE_STEMGENRT "Enable external StemgenRT ONNX research backend" OFF)
target_sources(SkellSeparator PRIVATE
    Source/dsp/StemgenRTBridge.h Source/dsp/StemgenRTBridge.cpp
    Source/dsp/StemgenORT.cpp)
if(SKELL_ENABLE_STEMGENRT)
    find_path(SKELL_ORT_INCLUDE_DIR onnxruntime_cxx_api.h
              HINTS "${ONNXRUNTIME_ROOT}" PATH_SUFFIXES include include/onnxruntime/core/session)
    find_library(SKELL_ORT_LIBRARY NAMES onnxruntime
                 HINTS "${ONNXRUNTIME_ROOT}" PATH_SUFFIXES lib lib64)
    if(NOT SKELL_ORT_INCLUDE_DIR OR NOT SKELL_ORT_LIBRARY)
        message(FATAL_ERROR "StemgenRT requires ONNX Runtime SDK (set ONNXRUNTIME_ROOT)")
    endif()
    target_include_directories(SkellSeparator PRIVATE "${SKELL_ORT_INCLUDE_DIR}")
    target_link_libraries(SkellSeparator PRIVATE "${SKELL_ORT_LIBRARY}" juce::juce_cryptography)
    target_compile_definitions(SkellSeparator PRIVATE SKELL_WITH_STEMGENRT=1)
    message(STATUS "StemgenRT research adapter enabled: SHA-256 verified graph required at 44.1 kHz")
endif()

# Optional native model-timing diagnostic (research only; no weights bundled).
option(SKELL_BUILD_STEMGEN_HOP_TIMING "Build external-model Windows research latency diagnostic" OFF)
if(SKELL_BUILD_STEMGEN_HOP_TIMING)
    if(NOT SKELL_ENABLE_STEMGENRT)
        message(FATAL_ERROR "SKELL_BUILD_STEMGEN_HOP_TIMING requires SKELL_ENABLE_STEMGENRT=ON")
    endif()
    target_compile_definitions(SkellSeparator PRIVATE SKELL_STEMGEN_TIMING_DIAGNOSTICS=1)
    add_executable(SkellStemgenHopTiming tests/stemgen_native_timing.cpp)
    target_link_libraries(SkellStemgenHopTiming PRIVATE
        SkellSeparator Threads::Threads
        juce::juce_audio_utils juce::juce_cryptography)
    target_compile_features(SkellStemgenHopTiming PRIVATE cxx_std_17)
    target_compile_definitions(SkellStemgenHopTiming PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
endif()
