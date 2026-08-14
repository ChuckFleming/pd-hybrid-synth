# Catch2 (fetched automatically).
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# Offline CPU benchmark (not part of ctest): used to chase glitching.
add_executable(pdhybrid_bench bench/bench_main.cpp)
target_include_directories(pdhybrid_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pdhybrid_bench PRIVATE pdhybrid_dsp)
target_compile_features(pdhybrid_bench PRIVATE cxx_std_17)

# Note-on -> audio latency, so "it feels delayed" can be answered with numbers
# instead of impressions. Not part of ctest.
add_executable(pdhybrid_latency bench/latency_main.cpp)
target_include_directories(pdhybrid_latency PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pdhybrid_latency PRIVATE pdhybrid_dsp)
target_compile_features(pdhybrid_latency PRIVATE cxx_std_17)

# Layout check: builds a real editor headlessly and asserts every tab page fits
# its window without scrolling. Links the plugin's shared code, so unlike the
# other test targets it needs JUCE.
add_executable(pdhybrid_uicheck bench/uicheck_main.cpp)
target_include_directories(pdhybrid_uicheck PRIVATE ${CMAKE_SOURCE_DIR}/src)
# The plugin links its JUCE modules PRIVATE, so they do not come through the
# shared-code target; name them here too.
target_link_libraries(pdhybrid_uicheck
    PRIVATE
        PDHybridSynth
        pdhybrid_dsp
        pdhybrid_theme
        juce::juce_audio_utils
        juce::juce_dsp
)
target_compile_definitions(pdhybrid_uicheck PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
)
target_compile_features(pdhybrid_uicheck PRIVATE cxx_std_17)

add_executable(pdhybrid_tests
    dsp/oscillator_tests.cpp
    dsp/analog_oscillator_tests.cpp
    dsp/vps_oscillator_tests.cpp
    dsp/scanned_oscillator_tests.cpp
    dsp/vosim_oscillator_tests.cpp
    dsp/walsh_oscillator_tests.cpp
    dsp/new_oscillator_tests.cpp
    dsp/new_filter_tests.cpp
    dsp/batch3_tests.cpp
    dsp/gain_structure_tests.cpp
    dsp/pluck_resonator_tests.cpp
    dsp/filter_tests.cpp
    dsp/svf_tests.cpp
    dsp/amp_tests.cpp
    dsp/envelope_tests.cpp
    dsp/synth_tests.cpp
    dsp/modulation_tests.cpp
    dsp/nontraditional_filter_tests.cpp
    dsp/compressor_tests.cpp
    dsp/delay_tests.cpp
    dsp/osc_eq_tests.cpp
    dsp/global_eq_tests.cpp
    dsp/mono_bass_tests.cpp
    dsp/master_stage_tests.cpp
    dsp/voice_mode_tests.cpp
    dsp/chorus_tests.cpp
    dsp/reverb_tests.cpp
    dsp/arpeggiator_tests.cpp
    dsp/chord_mode_tests.cpp
    dsp/chord_namer_tests.cpp
    theme/theme_palette_tests.cpp
    harness/Fft.cpp
    harness/Spectrum.cpp
)

target_include_directories(pdhybrid_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src      # dsp/... headers
    ${CMAKE_SOURCE_DIR}/tests    # harness/... headers
)

target_link_libraries(pdhybrid_tests PRIVATE
    pdhybrid_dsp
    pdhybrid_theme
    Catch2::Catch2WithMain
)

# Register individual test cases with CTest.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(pdhybrid_tests)

# The layout check runs under ctest with everything else, so CI picks it up
# without the workflow needing to know about it. It builds a real editor, which
# needs a display: the Linux runners are headless, so it goes through xvfb
# there. Failure means a tab page no longer fits its window.
if(UNIX AND NOT APPLE)
    add_test(NAME layout_pages_fit
             COMMAND xvfb-run -a $<TARGET_FILE:pdhybrid_uicheck>)
else()
    add_test(NAME layout_pages_fit COMMAND pdhybrid_uicheck)
endif()
