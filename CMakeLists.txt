cmake_minimum_required(VERSION 3.22)

project(Epi VERSION 0.9.0 LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# Project-wide settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Universal binary on Apple silicon + Intel for distributable plugins. Can be
# overridden on the command line (e.g. -DCMAKE_OSX_ARCHITECTURES=arm64) for
# fast local iteration builds.
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    endif()
endif()

# -----------------------------------------------------------------------------
# Version / build identity baked into the binary.
# -----------------------------------------------------------------------------
find_package(Git QUIET)
set(EPI_GIT_HASH "unknown")
set(EPI_GIT_BRANCH "unknown")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE EPI_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE EPI_GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

# -----------------------------------------------------------------------------
# Code shared by every plugin in this repository: the preset system and the
# product-agnostic half of the WebView editor. Compiled into each plugin target
# rather than built as its own library, so each one keeps its own JUCE module
# configuration.
# -----------------------------------------------------------------------------
set(EPI_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/WebViewSupport.cpp)

# -----------------------------------------------------------------------------
# JUCE (vendored as a git submodule under external/JUCE)
# -----------------------------------------------------------------------------
add_subdirectory(external/JUCE)

# clap-juce-extensions wraps the JUCE plugin as a CLAP alongside the
# AU/VST3/Standalone builds. EXCLUDE_FROM_ALL so only the CLAP target we
# attach below participates in the build.
add_subdirectory(external/clap-juce-extensions EXCLUDE_FROM_ALL)

# Local-install copy is on by default for developer iteration; CI builds set
# -DPLUGIN_INSTALL_LOCAL=OFF so they don't try to write into the user's
# plug-in directories.
option(PLUGIN_INSTALL_LOCAL "Copy built plugins into the user's plug-in dirs" ON)

# =============================================================================
# Epi — physically modeled pianos.
# =============================================================================
juce_add_plugin(Epi
    COMPANY_NAME            "DatanoiseTV"
    BUNDLE_ID               "com.datanoisetv.epi"
    PLUGIN_MANUFACTURER_CODE Dnse
    PLUGIN_CODE             Epi1
    FORMATS                 AU VST3 Standalone
    PRODUCT_NAME            "Epi"
    DESCRIPTION             "Physically modeled pianos"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    NEEDS_WEB_BROWSER       TRUE
    NEEDS_WEBVIEW2          TRUE
    COPY_PLUGIN_AFTER_BUILD ${PLUGIN_INSTALL_LOCAL}
    VST3_CATEGORIES         Instrument Synth Piano
    AU_MAIN_TYPE            kAudioUnitType_MusicDevice)

juce_generate_juce_header(Epi)

clap_juce_extensions_plugin(TARGET Epi
    CLAP_ID              "com.datanoisetv.epi"
    CLAP_FEATURES        instrument synthesizer piano "physical-modeling"
    CLAP_MANUAL_URL      "https://github.com/DatanoiseTV/epi"
    CLAP_SUPPORT_URL     "https://github.com/DatanoiseTV/epi/issues")

file(GLOB EPI_UI_SRC    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/epi/*")
file(GLOB EPI_UI_VENDOR CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/vendor/*")
juce_add_binary_data(EpiUI
    SOURCES     ${EPI_UI_SRC} ${EPI_UI_VENDOR}
    NAMESPACE   EpiUIData
    HEADER_NAME EpiUIData.h)
set_target_properties(EpiUI PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(Epi PRIVATE
    src/epi/PluginProcessor.cpp
    src/epi/dsp/EpiEngine.cpp
    src/epi/presets/FactoryPresets.cpp
    src/epi/ui/UiBridge.cpp
    src/epi/ui/WebEditor.cpp
    ${EPI_COMMON_SOURCES})

target_include_directories(Epi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(Epi
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
        $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>
    PRIVATE
        EPI_VERSION="${PROJECT_VERSION}"
        EPI_GIT_HASH="${EPI_GIT_HASH}"
        EPI_GIT_BRANCH="${EPI_GIT_BRANCH}")

target_link_libraries(Epi PRIVATE
    EpiUI
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
# Unit tests (DSP cores + state round-trip). Built only when top-level.
# -----------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(tests)
endif()
