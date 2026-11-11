cmake_minimum_required(VERSION 3.22)

project(PhraseArp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PHRASEARP_BUILD_PLUGIN "Build the JUCE VST3 and Standalone targets" ON)

set(PHRASEARP_ENGINE_SOURCES
    src/engine/Scale.cpp
    src/engine/Chord.cpp
    src/engine/Harmony.cpp
    src/engine/Pattern.cpp
    src/engine/Session.cpp
    src/engine/Generator.cpp
    src/engine/Feel.cpp
    src/engine/Render.cpp
    src/engine/MidiFile.cpp
    src/engine/Importer.cpp
    src/engine/PreviewSynth.cpp)

add_library(phrasearp_engine STATIC ${PHRASEARP_ENGINE_SOURCES})
set_target_properties(phrasearp_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(phrasearp_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(phrasearp_engine PUBLIC cxx_std_20)
target_compile_options(phrasearp_engine PRIVATE -Wall -Wextra -Wno-unused-function)

add_executable(phrasearp_tests tests/test_main.cpp)
target_link_libraries(phrasearp_tests PRIVATE phrasearp_engine)
target_compile_features(phrasearp_tests PRIVATE cxx_std_20)
target_compile_options(phrasearp_tests PRIVATE -Wall -Wextra -Wno-unused-function)

enable_testing()
add_test(NAME engine COMMAND phrasearp_tests)

if(PHRASEARP_BUILD_PLUGIN)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.17-security
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(PhraseArp
        COMPANY_NAME "ARPIN"
        PLUGIN_MANUFACTURER_CODE Arpn
        PLUGIN_CODE PhAr
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        FORMATS VST3 Standalone
        PRODUCT_NAME "PhraseArp"
        VST3_CATEGORIES Instrument Synth)

    target_sources(PhraseArp PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp)

    target_compile_definitions(PhraseArp PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0)

    target_link_libraries(PhraseArp PRIVATE
        phrasearp_engine
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
