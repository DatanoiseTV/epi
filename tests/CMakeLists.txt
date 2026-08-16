# -----------------------------------------------------------------------------
# Acoustic tests for the physical model. The model is framework-free, so
# these build without JUCE. They render audio and measure pitch, spectrum and
# decay, so they take tens of seconds rather than milliseconds.
# -----------------------------------------------------------------------------
add_executable(didge_dsp_tests
    test_dsp.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/DidgeEngine.cpp)

target_include_directories(didge_dsp_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(didge_dsp_tests PRIVATE cxx_std_20)

add_test(NAME dsp COMMAND didge_dsp_tests)

# -----------------------------------------------------------------------------
# State / preset tests. Linking `Didge` cascades JUCE's INTERFACE_SOURCES into
# the test target; free on macOS/Windows, but on Linux it would force a
# webkit2gtk build — so Apple/Windows hosts only (the logic is platform-
# independent; macOS CI catches regressions).
# -----------------------------------------------------------------------------
if(APPLE OR WIN32)
    add_executable(didge_state_tests test_state.cpp)
    target_include_directories(didge_state_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(didge_state_tests PRIVATE cxx_std_20)
    target_link_libraries(didge_state_tests PRIVATE
        Didge
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    add_test(NAME state COMMAND didge_state_tests)

    # Windows: the cascaded juce_gui_extra.cpp + the PUBLIC
    # JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING define need the WebView2 SDK
    # in the test target too.
    if(WIN32)
        target_link_libraries(didge_state_tests PRIVATE juce::juce_webview2)
    endif()
endif()


# ---------------------------------------------------------------------------
# Epi DSP cores. Framework-free like the didgeridoo's, so this builds with no
# JUCE at all.
# ---------------------------------------------------------------------------
add_executable(epi_dsp_tests test_epi_dsp.cpp)
target_include_directories(epi_dsp_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(epi_dsp_tests PRIVATE cxx_std_20)
add_test(NAME epi_dsp COMMAND epi_dsp_tests)

# ---------------------------------------------------------------------------
# Epi acoustic reference suite. Renders the whole instrument and compares it,
# property by property, against measurements taken off a real Rhodes Mark I.
# Each row maps to a numbered entry in docs/acoustic-checklist.md.
#
# This one renders minutes of audio, so it is the slow test in the suite.
# ---------------------------------------------------------------------------
add_executable(epi_reference_tests
    test_epi_reference.cpp
    ${CMAKE_SOURCE_DIR}/src/epi/dsp/EpiEngine.cpp)
target_include_directories(epi_reference_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(epi_reference_tests PRIVATE cxx_std_20)
add_test(NAME epi_reference COMMAND epi_reference_tests)
set_tests_properties(epi_reference PROPERTIES TIMEOUT 900)
