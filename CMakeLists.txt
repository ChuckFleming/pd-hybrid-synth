cmake_minimum_required(VERSION 3.22)

project(PDHybridSynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# JUCE (fetched automatically -- no manual SDK install needed).
# Bump GIT_TAG to a newer tag (e.g. 8.0.x) if you prefer JUCE 8.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# DSP core: pure C++, NO JUCE dependency. Everything the test harness targets
# lives here so tests run headless, deterministic, and fast.
# ---------------------------------------------------------------------------
add_library(pdhybrid_dsp STATIC
    src/dsp/PhaseDistortionOscillator.cpp
    src/dsp/AnalogOscillator.cpp
    src/dsp/OscillatorUnit.cpp
    src/dsp/OscEq.cpp
    src/dsp/LadderFilter.cpp
    src/dsp/StateVariableFilter.cpp
    src/dsp/Waveshaper.cpp
    src/dsp/Oversampler.cpp
    src/dsp/OverdriveAmp.cpp
    src/dsp/MultiStageEnvelope.cpp
    src/dsp/Lfo.cpp
    src/dsp/ModMatrix.cpp
    src/dsp/Voice.cpp
    src/dsp/SynthEngine.cpp
    src/dsp/PhaseDistortionResonator.cpp
    src/dsp/CombFilter.cpp
    src/dsp/AllpassDispersion.cpp
    src/dsp/FilterUnit.cpp
    src/dsp/Compressor.cpp
    src/dsp/Delay.cpp
)
target_include_directories(pdhybrid_dsp PUBLIC src)
target_compile_features(pdhybrid_dsp PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# Plugin: JUCE wrapper around the DSP core. Ships VST3 + Standalone.
# ---------------------------------------------------------------------------
juce_add_plugin(PDHybridSynth
    PRODUCT_NAME              "PD Hybrid Synth"
    COMPANY_NAME              "PDHybrid"
    FORMATS                   VST3 Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    PLUGIN_MANUFACTURER_CODE  Pdhy
    PLUGIN_CODE               Pdh1
    VST3_CATEGORIES           "Instrument" "Synth"
)

target_sources(PDHybridSynth PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
)
target_include_directories(PDHybridSynth PRIVATE src)
target_compile_definitions(PDHybridSynth PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(PDHybridSynth
    PRIVATE
        pdhybrid_dsp
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
