cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")

project(Slip VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT SLIP_JUCE_PATH)
    if(DEFINED ENV{JUCE_PATH})
        set(SLIP_JUCE_PATH "$ENV{JUCE_PATH}")
    elseif(WIN32)
        set(SLIP_JUCE_PATH "C:/JUCE")
    else()
        set(SLIP_JUCE_PATH "$ENV{HOME}/JUCE")
    endif()
endif()
add_subdirectory("${SLIP_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

set(SLIP_FORMATS VST3)
if(APPLE)
    list(APPEND SLIP_FORMATS AU)
endif()

# The engine: plain C++, used by the plug-in and the tests.
add_library(slip_engine STATIC source/engine/SlipEngine.cpp)
target_include_directories(slip_engine PUBLIC source)

# An audio effect that also takes MIDI (gestures on notes, the pitch wheel as jog).
juce_add_plugin(Slip
    COMPANY_NAME            "LeverStudios"
    PLUGIN_MANUFACTURER_CODE Lvrs
    PLUGIN_CODE             Slip
    BUNDLE_ID               "com.leverstudios.slip"
    PRODUCT_NAME            "Slip"
    FORMATS                 ${SLIP_FORMATS}
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES         Fx Delay
    AU_MAIN_TYPE            kAudioUnitType_MusicEffect)

add_library(slip_plugin_code INTERFACE)
target_sources(slip_plugin_code INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/plugin/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/plugin/PluginEditor.cpp)
target_include_directories(slip_plugin_code INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(slip_plugin_code INTERFACE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(slip_plugin_code INTERFACE slip_engine juce::juce_audio_utils)

target_link_libraries(Slip PRIVATE slip_plugin_code PUBLIC
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(SLIP_BUILD_TESTS "Build tests" ON)
if(SLIP_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.7.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
    juce_add_console_app(SlipTests PRODUCT_NAME "slip_tests")
    target_sources(SlipTests PRIVATE tests/test_main.cpp tests/engine_tests.cpp tests/plugin_tests.cpp)
    target_compile_definitions(SlipTests PRIVATE JucePlugin_Name="Slip" JUCE_STANDALONE_APPLICATION=1)
    target_include_directories(SlipTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(SlipTests PRIVATE slip_plugin_code Catch2::Catch2 juce::juce_recommended_config_flags)
    add_test(NAME slip_tests COMMAND SlipTests)
endif()
