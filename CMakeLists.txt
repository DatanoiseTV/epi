cmake_minimum_required(VERSION 3.22)

project(Didge VERSION 0.1.0 LANGUAGES C CXX)

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
set(DIDGE_GIT_HASH "unknown")
set(DIDGE_GIT_BRANCH "unknown")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE DIDGE_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE DIDGE_GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE
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

juce_add_plugin(Didge
    COMPANY_NAME            "DatanoiseTV"
    BUNDLE_ID               "com.datanoisetv.didge"
    PLUGIN_MANUFACTURER_CODE Dnse
    PLUGIN_CODE             Ddge
    FORMATS                 AU VST3 Standalone
    PRODUCT_NAME            "Didge"
    DESCRIPTION             "Physically modeled didgeridoo"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    NEEDS_WEB_BROWSER       TRUE   # pulls webkit2gtk + gtk pkg-config on Linux
    NEEDS_WEBVIEW2          TRUE   # Windows: statically link the WebView2 loader
    COPY_PLUGIN_AFTER_BUILD ${PLUGIN_INSTALL_LOCAL}
    VST3_CATEGORIES         Instrument Synth
    AU_MAIN_TYPE            kAudioUnitType_MusicDevice)

juce_generate_juce_header(Didge)

# -----------------------------------------------------------------------------
# CLAP wrapper — builds Didge.clap from the same shared plugin code. Creates a
# `Didge_CLAP` target (NOT part of Didge_All, so build it explicitly).
# -----------------------------------------------------------------------------
clap_juce_extensions_plugin(TARGET Didge
    CLAP_ID              "com.datanoisetv.didge"
    CLAP_FEATURES        instrument synthesizer "physical-modeling"
    CLAP_MANUAL_URL      "https://github.com/DatanoiseTV/didge"
    CLAP_SUPPORT_URL     "https://github.com/DatanoiseTV/didge/issues")

# -----------------------------------------------------------------------------
# UI assets — index.html / CSS / JSX / vendored React + Babel-standalone +
# the JUCE WebView frontend library. Served at runtime by
# WebBrowserComponent's resource provider, all bundled as BinaryData so the
# plugin never reaches the network. The vendored React UMDs and
# babel-standalone are MIT-licensed.
# -----------------------------------------------------------------------------
file(GLOB DIDGE_UI_SRC    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/didge/*")
file(GLOB DIDGE_UI_VENDOR CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/vendor/*")
juce_add_binary_data(DidgeUI
    SOURCES     ${DIDGE_UI_SRC} ${DIDGE_UI_VENDOR}
    NAMESPACE   DidgeUIData
    HEADER_NAME DidgeUIData.h)
set_target_properties(DidgeUI PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------
target_sources(Didge PRIVATE
    src/PluginProcessor.cpp
    src/dsp/DidgeEngine.cpp
    src/presets/FactoryPresets.cpp
    src/ui/WebEditor.cpp
    ${EPI_COMMON_SOURCES})

target_include_directories(Didge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(Didge
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
        # Windows: NEEDS_WEBVIEW2 only LINKS the WebView2 loader; the module
        # compile flag must be set separately or juce_gui_extra falls back to
        # the legacy IE backend, which has no resource provider.
        $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>
    PRIVATE
        DIDGE_VERSION="${PROJECT_VERSION}"
        DIDGE_GIT_HASH="${DIDGE_GIT_HASH}"
        DIDGE_GIT_BRANCH="${DIDGE_GIT_BRANCH}")

target_link_libraries(Didge PRIVATE
    DidgeUI
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# =============================================================================
# Epi — physically modeled electric pianos (Rhodes / Wurlitzer / Clavinet /
# CP-70). A second product sharing this repository's plugin shell.
#
# The plugin target is not wired up yet: the DSP cores under src/epi/dsp are
# built and measured by epi_dsp_tests, and the JUCE wrapper follows once the
# Rhodes voice passes across the whole compass. See CHANGELOG.md for what is
# verified and what is not.
# =============================================================================

# -----------------------------------------------------------------------------
# Unit tests (DSP cores + state round-trip). Built only when top-level.
# -----------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(tests)
endif()
