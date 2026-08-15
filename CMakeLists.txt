cmake_minimum_required(VERSION 3.22)

# Both of these are read while project() works out the compiler, so setting them
# afterwards is silently ignored and the build comes out arm64 only.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# FL Studio ships universal and may be launched under Rosetta, so a plugin that
# is arm64 only is invisible to half the ways the host can start. Build both
# unless someone asks otherwise: there are no external libraries to match.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")

project(PromptSlice VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is fetched at the version this was built and tested against. Point
# JUCE_PATH at an existing checkout to build against that instead:
#   cmake -B build -DJUCE_PATH=/path/to/JUCE
set(JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout (optional)")

if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(PromptSlice
    PRODUCT_NAME                "PromptSlice"
    COMPANY_NAME                "Vasyl Naumenko"
    BUNDLE_ID                   "com.vn.promptslice"
    PLUGIN_MANUFACTURER_CODE    Vnau
    PLUGIN_CODE                 Prsl
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    # The prompt field has to receive keystrokes while the editor sits inside
    # the host's own window, and without this the host eats them.
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     TRUE)

# Everything from "a wav exists" onwards — playing it, marking it, cutting it,
# dragging the pieces out. Nothing in here knows where the wav came from, which
# is why it sits apart from the half that asks ElevenLabs for one.
set(SHARED_SOURCES
    Source/Shared/Config.cpp
    Source/Shared/Look.cpp
    Source/Shared/SliceExporter.cpp
    Source/Shared/SliceProcessor.cpp
    Source/Shared/SliceStrip.cpp
    Source/Shared/WaveformView.cpp)

target_include_directories(PromptSlice PRIVATE Source)

target_sources(PromptSlice PRIVATE
    ${SHARED_SOURCES}
    Source/Eleven/BatchNote.cpp
    Source/Eleven/Generator.cpp
    Source/Eleven/PromptProcessor.cpp
    Source/Eleven/PromptEditor.cpp)

target_compile_definitions(PromptSlice PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PromptSlice PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Slicing is the one part that writes files a person keeps, so it is checked on
# its own rather than by looking at the result in the host. Needs nothing
# external: it cuts a synthetic tone.
juce_add_console_app(SliceTest PRODUCT_NAME "SliceTest")

target_sources(SliceTest PRIVATE
    Tests/SliceTest.cpp
    Source/Shared/Config.cpp
    Source/Shared/SliceExporter.cpp)

target_include_directories(SliceTest PRIVATE Source/Shared)

target_compile_definitions(SliceTest PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(SliceTest PRIVATE
    juce::juce_audio_formats
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# One real request to ElevenLabs, checking that what comes back opens as a wav
# at the rate that was asked for. Skips when there is no key in the environment,
# so it is safe to run always.
juce_add_console_app(GenTest PRODUCT_NAME "GenTest")

target_sources(GenTest PRIVATE
    Tests/GenTest.cpp
    Source/Eleven/BatchNote.cpp
    Source/Eleven/Generator.cpp)

target_include_directories(GenTest PRIVATE Source/Eleven)

target_compile_definitions(GenTest PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    # Only the test pumps the loop by hand; a plugin must never be allowed to.
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(GenTest PRIVATE
    juce::juce_audio_formats
    juce::juce_gui_basics
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
