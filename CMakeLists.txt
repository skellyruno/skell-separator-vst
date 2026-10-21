cmake_minimum_required(VERSION 3.22)
project(SkellSeparator VERSION 1.4.0 LANGUAGES C CXX)

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
