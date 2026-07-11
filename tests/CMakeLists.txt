# Catch2 (fetched automatically).
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(pdhybrid_tests
    dsp/oscillator_tests.cpp
    dsp/analog_oscillator_tests.cpp
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
    harness/Fft.cpp
    harness/Spectrum.cpp
)

target_include_directories(pdhybrid_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src      # dsp/... headers
    ${CMAKE_SOURCE_DIR}/tests    # harness/... headers
)

target_link_libraries(pdhybrid_tests PRIVATE
    pdhybrid_dsp
    Catch2::Catch2WithMain
)

# Register individual test cases with CTest.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(pdhybrid_tests)
