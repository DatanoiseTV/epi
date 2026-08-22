# -----------------------------------------------------------------------------
# Acoustic tests for the physical model. The model is framework-free, so
# these build without JUCE. They render audio and measure pitch, spectrum and
# decay, so they take tens of seconds rather than milliseconds.
# -----------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Epi DSP cores. Framework-free, so these build with no JUCE at all.
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

# ---------------------------------------------------------------------------
# Epi grand reference rows. The acoustic grand's string + bridge + board
# stage, measured against the plan's own Salamander C5 numbers. Framework-
# free and engine-free: it drives GrandVoice and GrandBoard directly, so it
# builds with no JUCE and runs before the engine integration exists.
# ---------------------------------------------------------------------------
add_executable(epi_grand_tests test_epi_grand.cpp)
target_include_directories(epi_grand_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(epi_grand_tests PRIVATE cxx_std_20)
add_test(NAME epi_grand COMMAND epi_grand_tests)
set_tests_properties(epi_grand PROPERTIES TIMEOUT 900)

# ---------------------------------------------------------------------------
# Epi Wurlitzer 200A reference rows. Reed + electrostatic pickup + 200A chain,
# measured against docs/wurlitzer-implementation-plan.md section 10. Framework-
# free and engine-free: it drives WurliVoice and the WurliChain classes
# directly, so it builds with no JUCE and runs before the engine integration
# exists.
# ---------------------------------------------------------------------------
add_executable(epi_wurli_tests test_epi_wurli.cpp)
target_include_directories(epi_wurli_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(epi_wurli_tests PRIVATE cxx_std_20)
add_test(NAME epi_wurli COMMAND epi_wurli_tests)
set_tests_properties(epi_wurli PROPERTIES TIMEOUT 900)

# ---------------------------------------------------------------------------
# Epi Clavinet D6 reference rows. Struck-and-held string + twin bar pickups +
# tone rockers and preamp, measured against docs/clavinet-implementation-plan.md
# section 8. Framework-free and engine-free: it drives ClavinetVoice and the
# ClavinetChain classes directly, so it builds with no JUCE and runs before
# the engine integration exists.
# ---------------------------------------------------------------------------
add_executable(epi_clavinet_tests test_epi_clavinet.cpp)
target_include_directories(epi_clavinet_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(epi_clavinet_tests PRIVATE cxx_std_20)
add_test(NAME epi_clavinet COMMAND epi_clavinet_tests)
set_tests_properties(epi_clavinet PROPERTIES TIMEOUT 900)
