cmake_minimum_required (VERSION 3.22)

# Universal binary (Apple Silicon + Intel), macOS 11 and later.
# These two must be set BEFORE project(): set after it, they are ignored and
# the plugin only runs on the build machine's chip and macOS version.
set (CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set (CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project (Gloriaverb VERSION 0.3.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)


# ---- JUCE ------------------------------------------------------------------
# Either point JUCE_PATH to a JUCE folder you already have, or leave it empty
# and CMake downloads JUCE (it includes the AAX SDK, nothing else to fetch).
set (JUCE_PATH "" CACHE PATH "Local JUCE folder (empty = download)")

if (JUCE_PATH)
    add_subdirectory ("${JUCE_PATH}" JUCE)
else()
    include (FetchContent)
    FetchContent_Declare (JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.11
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable (JUCE)
endif()

# Formats: everything on Mac and Windows; on Linux (test builds only) VST3.
if (APPLE)
    set (GV_FORMATS AAX AU VST3 Standalone)
elseif (WIN32)
    set (GV_FORMATS AAX VST3 Standalone)
else()
    set (GV_FORMATS VST3)
endif()

# ---- Plugin ----------------------------------------------------------------
# Planet Gloria: same company name and manufacturer code (Pglo) for every
# plugin, so hosts list them together.
juce_add_plugin (Gloriaverb
    COMPANY_NAME              "Planet Gloria"
    BUNDLE_ID                 "com.planetgloria.gloriaverb"
    PLUGIN_MANUFACTURER_CODE  Pglo
    PLUGIN_CODE               Glvb
    PRODUCT_NAME              "Gloriaverb"
    FORMATS                   ${GV_FORMATS}
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AAX_IDENTIFIER            com.planetgloria.gloriaverb
    AAX_CATEGORY              AAX_ePlugInCategory_Reverb
    AU_MAIN_TYPE              kAudioUnitType_Effect
    VST3_CATEGORIES           Fx Reverb
    COPY_PLUGIN_AFTER_BUILD   FALSE)

target_sources (Gloriaverb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/SketchLookAndFeel.cpp)

target_include_directories (Gloriaverb PRIVATE Source)

# Hand-written fonts embedded in the plugin (Patrick Hand and Caveat, SIL OFL).
file (GLOB GLORIAVERB_FONTS CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Fonts/*.ttf"
      "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Fonts/*.otf")
if (GLORIAVERB_FONTS)
    juce_add_binary_data (GloriaverbAssets SOURCES ${GLORIAVERB_FONTS})
    target_link_libraries (Gloriaverb PRIVATE GloriaverbAssets)
    target_compile_definitions (Gloriaverb PRIVATE GLORIAVERB_HAS_FONTS=1)
endif()

target_compile_definitions (Gloriaverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if (UNIX AND NOT APPLE)
    # Linux test builds: no audio device code needed for a plug-in.
    target_compile_definitions (Gloriaverb PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
endif()

target_link_libraries (Gloriaverb
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline test of the reverb engine (no JUCE): tools/engine_test.cpp
option (GLORIAVERB_TESTS "Build the offline engine test" OFF)
if (GLORIAVERB_TESTS)
    add_executable (engine_test tools/engine_test.cpp)
    target_include_directories (engine_test PRIVATE Source)
endif()
if (GLORIAVERB_TESTS AND UNIX AND NOT APPLE)
    # Linux only: runs the processor offline and saves a picture of the window.
    juce_add_console_app (ui_snapshot PRODUCT_NAME "ui_snapshot")
    target_sources (ui_snapshot PRIVATE tools/ui_snapshot.cpp Source/PluginProcessor.cpp
                    Source/PluginEditor.cpp Source/ui/SketchLookAndFeel.cpp)
    target_include_directories (ui_snapshot PRIVATE Source)
    target_compile_definitions (ui_snapshot PRIVATE JucePlugin_Name="Gloriaverb" JUCE_ALSA=0 JUCE_JACK=0
                                JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 GLORIAVERB_HAS_FONTS=1)
    target_link_libraries (ui_snapshot PRIVATE GloriaverbAssets juce::juce_audio_utils juce::juce_dsp
                           juce::juce_recommended_config_flags)
endif()
