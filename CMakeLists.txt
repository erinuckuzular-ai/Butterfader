cmake_minimum_required(VERSION 3.22)

project(Butterfader VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
option(BUTTERFADER_COPY_PLUGINS "Install plug-ins into ~/Library/Audio/Plug-Ins after building" ON)

# Reuse ArrowEch's JUCE checkout when present so we don't download it twice.
set(_arrowech_juce "$ENV{HOME}/ArrowEch /build/_deps/juce-src")
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_JUCE AND EXISTS "${_arrowech_juce}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${_arrowech_juce}" CACHE PATH "")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(BUTTERFADER_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

set(BUTTERFADER_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

juce_add_plugin(Butterfader
    COMPANY_NAME "Arrow"
    PLUGIN_MANUFACTURER_CODE Arrw
    PLUGIN_CODE BtFd
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Butterfader"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Butterfader needs your microphone so the standalone app can level live vocals."
    COPY_PLUGIN_AFTER_BUILD ${BUTTERFADER_COPY_PLUGINS})

target_sources(Butterfader PRIVATE ${BUTTERFADER_SOURCES})
target_compile_definitions(Butterfader PUBLIC ${BUTTERFADER_DEFS})
target_link_libraries(Butterfader
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP test: cmake --build build --target DSPTest && ./build/DSPTest_artefacts/Release/DSPTest
juce_add_console_app(DSPTest PRODUCT_NAME "DSPTest")
target_sources(DSPTest PRIVATE Tests/DSPTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(DSPTest PRIVATE ${BUTTERFADER_DEFS} JucePlugin_Name="Butterfader")
target_link_libraries(DSPTest PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

# Renders the editor to PNG: ./build/UISnapshot_artefacts/Release/UISnapshot <outDir>
juce_add_console_app(UISnapshot PRODUCT_NAME "UISnapshot")
target_sources(UISnapshot PRIVATE Tests/UISnapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(UISnapshot PRIVATE ${BUTTERFADER_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Butterfader")
target_link_libraries(UISnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
