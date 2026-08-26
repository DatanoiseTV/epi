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

# ---------------------------------------------------------------------------
# Epi factory preset verification. Renders every preset in PresetData.h
# through the shared EngineParamMap and holds it to level, spectrum and
# character bounds. Framework-free: no JUCE.
# ---------------------------------------------------------------------------
add_executable(epi_preset_tests
    test_epi_presets.cpp
    ${CMAKE_SOURCE_DIR}/src/epi/dsp/EpiEngine.cpp)
target_include_directories(epi_preset_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(epi_preset_tests PRIVATE cxx_std_20)
add_test(NAME epi_presets COMMAND epi_preset_tests)
set_tests_properties(epi_presets PROPERTIES TIMEOUT 900)

# ---------------------------------------------------------------------------
# Epi engine integration suite. The layer the voice and chain suites cannot
# see: instrument-switch seams, the pending-note guarantee, pedals, the
# material x transducer coupling matrix, knob-sweep click fences, the output
# rail under fortissimo abuse, MIDI robustness and rate independence.
# Framework-free: no JUCE.
# ---------------------------------------------------------------------------
add_executable(epi_engine_tests
    test_epi_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/epi/dsp/EpiEngine.cpp)
target_include_directories(epi_engine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(epi_engine_tests PRIVATE cxx_std_20)
add_test(NAME epi_engine COMMAND epi_engine_tests)
# The sweep sections render a couple of hundred short takes, so this one is
# minutes rather than seconds, and a loaded CI runner stretches it further.
set_tests_properties(epi_engine PROPERTIES TIMEOUT 2400)

# ---------------------------------------------------------------------------
# Epi state round-trips. The one suite that is NOT framework-free: it
# instantiates the real EpiAudioProcessor and proves that host project
# state, user presets, factory loads and legacy states all restore the
# benches -- mic stage, pair trims, velocity map -- exactly as saved.
# Compiled from the plugin's own sources so it tests the artifact, not a
# replica.
# ---------------------------------------------------------------------------
# Built HEADLESS: no editor, so no browser module, so no GTK headers on
# Linux and no WebView2 package on Windows -- both of which broke the whole
# build for a suite that only asks what a saved project restores.
juce_add_console_app(epi_state_tests PRODUCT_NAME "epi_state_tests")
target_sources(epi_state_tests PRIVATE
    test_epi_state.cpp
    ${CMAKE_SOURCE_DIR}/src/epi/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/epi/dsp/EpiEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/epi/presets/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/src/common/PresetManager.cpp)
target_include_directories(epi_state_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(epi_state_tests PRIVATE cxx_std_20)
target_compile_definitions(epi_state_tests PRIVATE
    EPI_HEADLESS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>
    EPI_VERSION="${PROJECT_VERSION}"
    EPI_GIT_HASH="${EPI_GIT_HASH}"
    EPI_GIT_BRANCH="${EPI_GIT_BRANCH}")
target_link_libraries(epi_state_tests PRIVATE
    EpiUI
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
add_test(NAME epi_state COMMAND epi_state_tests)
set_tests_properties(epi_state PROPERTIES TIMEOUT 300)

# ---------------------------------------------------------------------------
# Per-note tuning: the MPE reception path and what it does to pitch. Framework-
# free -- it drives the engine directly and measures f0 out of the rendered
# audio, so it fences the physics rather than the MIDI parsing. (The one row
# that exercises JUCE's own parsing lives in the state suite, which is the
# suite that has a processor.)
# ---------------------------------------------------------------------------
add_executable(epi_tuning_tests
    test_epi_tuning.cpp
    ${CMAKE_SOURCE_DIR}/src/epi/dsp/EpiEngine.cpp)
target_include_directories(epi_tuning_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(epi_tuning_tests PRIVATE cxx_std_20)
add_test(NAME epi_tuning COMMAND epi_tuning_tests)
set_tests_properties(epi_tuning PROPERTIES TIMEOUT 900)


# ---------------------------------------------------------------------------
# Radiation: the near field a close microphone sits inside, and what the mic
# stage does with it. Framework-free -- it drives the radiator and the stage
# directly, so it fences the acoustics rather than the engine around them.
# ---------------------------------------------------------------------------
add_executable(epi_radiation_tests test_epi_radiation.cpp)
target_include_directories(epi_radiation_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(epi_radiation_tests PRIVATE cxx_std_20)
add_test(NAME epi_radiation COMMAND epi_radiation_tests)
set_tests_properties(epi_radiation PROPERTIES TIMEOUT 600)
