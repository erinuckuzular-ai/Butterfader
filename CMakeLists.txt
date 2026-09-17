cmake_minimum_required(VERSION 3.22)

project(Butterfader VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
option(BUTTERFADER_COPY_PLUGINS "Install plug-ins into ~/Library/Audio/Plug-Ins after building" ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Butterfader
    COMPANY_NAME "Butterfader"
    PLUGIN_MANUFACTURER_CODE Bttr
    PLUGIN_CODE BtFd
    FORMATS AU VST3
    PRODUCT_NAME "Butterfader"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics Mastering
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${BUTTERFADER_COPY_PLUGINS})

target_sources(Butterfader PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Butterfader PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Butterfader
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP checks (loudness accuracy, true-peak ceiling, auto-level):
#   cmake --build build --target DSPTest && ./build/DSPTest_artefacts/Release/DSPTest
juce_add_console_app(DSPTest PRODUCT_NAME "DSPTest")
target_sources(DSPTest PRIVATE Tests/DSPTest.cpp Source/PluginProcessor.cpp)
target_compile_definitions(DSPTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 BUTTERFADER_NO_EDITOR=1 JucePlugin_Name="Butterfader")
target_link_libraries(DSPTest PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

# Renders the editor to PNG files for visual checks: UISnapshot <outDir>
juce_add_console_app(UISnapshot PRODUCT_NAME "UISnapshot")
target_sources(UISnapshot PRIVATE Tests/UISnapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(UISnapshot PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Butterfader")
target_link_libraries(UISnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
