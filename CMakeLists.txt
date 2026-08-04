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
    src/dsp/VpsOscillator.cpp
    src/dsp/ScannedOscillator.cpp
    src/dsp/VosimOscillator.cpp
    src/dsp/WalshOscillator.cpp
    src/dsp/SupersawOscillator.cpp
    src/dsp/HarmonicOscillator.cpp
    src/dsp/PafOscillator.cpp
    src/dsp/GranularOscillator.cpp
    src/dsp/WavetableOscillator.cpp
    src/dsp/OscillatorUnit.cpp
    src/dsp/OscEq.cpp
    src/dsp/GlobalEq.cpp
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
    src/dsp/FormantFilter.cpp
    src/dsp/DiodeLadderFilter.cpp
    src/dsp/BandSplitFilter.cpp
    src/dsp/PluckResonator.cpp
    src/dsp/FilterUnit.cpp
    src/dsp/Compressor.cpp
    src/dsp/Delay.cpp
    src/dsp/MonoBass.cpp
    src/dsp/MasterStage.cpp
    src/dsp/Chorus.cpp
    src/dsp/Reverb.cpp
    src/dsp/Arpeggiator.cpp
    src/dsp/ChordMode.cpp
    src/dsp/ChordNamer.cpp
)
target_include_directories(pdhybrid_dsp PUBLIC src)
target_compile_features(pdhybrid_dsp PUBLIC cxx_std_17)
# This static lib is linked into the plugin's shared object (VST3 .so on Linux),
# so its objects must be position-independent.
set_target_properties(pdhybrid_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# JUCE-free theme palettes. Separate from the plugin so the tests -- which do
# not link JUCE -- can check every theme's contrast.
# ---------------------------------------------------------------------------
add_library(pdhybrid_theme STATIC
    src/theme/ThemePalette.cpp
)
target_include_directories(pdhybrid_theme PUBLIC src)
target_compile_features(pdhybrid_theme PUBLIC cxx_std_17)
set_target_properties(pdhybrid_theme PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Plugin: JUCE wrapper around the DSP core. Ships VST3 + Standalone.
# ---------------------------------------------------------------------------
juce_add_plugin(PDHybridSynth
    PRODUCT_NAME              "PD Hybrid Synth"
    COMPANY_NAME              "PDHybrid"
    FORMATS                   VST3 Standalone
    ICON_BIG                  "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
    ICON_SMALL                "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_small.png"
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
    src/plugin/PresetManager.cpp
    src/plugin/CrtOverlay.cpp
    src/plugin/Theme.cpp
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
        pdhybrid_theme
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
