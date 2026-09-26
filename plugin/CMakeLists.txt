# ---------------------------------------------------------------------------
# Pitch Lane plugin (JUCE 8). AU is the priority format (Logic Pro); Standalone is for
# quick testing; VST3 is one option away (-DPITCHLANE_VST3=ON).
# ---------------------------------------------------------------------------
set(PITCHLANE_FORMATS "")
if(APPLE)
    list(APPEND PITCHLANE_FORMATS AU)
endif()
if(PITCHLANE_STANDALONE)
    list(APPEND PITCHLANE_FORMATS Standalone)
endif()
if(PITCHLANE_VST3)
    list(APPEND PITCHLANE_FORMATS VST3)
endif()
if(NOT PITCHLANE_FORMATS)
    set(PITCHLANE_FORMATS Standalone)
endif()
message(STATUS "Pitch Lane plugin formats: ${PITCHLANE_FORMATS}")

juce_add_plugin(PitchLane
    PRODUCT_NAME "PitchLane"               # -> PitchLane.component
    PLUGIN_NAME "Pitch Lane"               # name shown in Logic
    COMPANY_NAME "Pak209"                  # manufacturer name shown in Logic
    BUNDLE_ID "com.dkimoto.pitchlane"
    PLUGIN_MANUFACTURER_CODE Pk20
    PLUGIN_CODE Plne
    DESCRIPTION "Real-time singing coach: live pitch vs reference melody"
    VERSION ${PROJECT_VERSION}
    FORMATS ${PITCHLANE_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_SANDBOX_SAFE FALSE
    VST3_CATEGORIES Fx Analyzer
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Pitch Lane listens to your voice to show its pitch."
    COPY_PLUGIN_AFTER_BUILD ${PITCHLANE_COPY_AFTER_BUILD})

# Embedded assets: Inter font (SIL Open Font License 1.1, see resources/fonts/OFL.txt and docs/LICENSES.md).
juce_add_binary_data(PitchLaneAssets
    HEADER_NAME PitchLaneBinaryData.h
    NAMESPACE PitchLaneBinaryData
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter-Medium.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf)
set_target_properties(PitchLaneAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# All plugin sources live in an INTERFACE library so the unit tests can compile the very
# same processor code without the plugin wrappers.
add_library(PitchLaneShared INTERFACE)
target_sources(PitchLaneShared INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PianoRoll.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ReferenceModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/StateCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnalysisManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Params.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Markers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UiModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LiveNotePanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SettingsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Log.cpp)
target_include_directories(PitchLaneShared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Build id shown in the diagnostics log (the release workflow passes the full version).
if(NOT PITCHLANE_BUILD_ID)
    execute_process(COMMAND git rev-parse --short=7 HEAD
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE _pl_git_sha OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(PITCHLANE_BUILD_ID "${_pl_git_sha}")
    if(NOT PITCHLANE_BUILD_ID)
        set(PITCHLANE_BUILD_ID "local")
    endif()
endif()
target_compile_definitions(PitchLaneShared INTERFACE
    PITCHLANE_BUILD_ID="${PITCHLANE_BUILD_ID}"
    PITCHLANE_VERSION="${CMAKE_PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
target_link_libraries(PitchLaneShared INTERFACE
    pitchlane_core
    PitchLaneAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_basics
    juce::juce_dsp)

target_link_libraries(PitchLane
    PRIVATE
        PitchLaneShared
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(PITCHLANE_BUILD_TESTS)
    juce_add_console_app(PitchLaneTests PRODUCT_NAME "PitchLaneTests")
    target_sources(PitchLaneTests PRIVATE tests/PluginTests.cpp)
    target_compile_definitions(PitchLaneTests PRIVATE
        JucePlugin_Name="Pitch Lane"
        PITCHLANE_TESTS=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        PITCHLANE_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(PitchLaneTests PRIVATE
        PitchLaneShared
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    if(NOT MSVC)
        target_compile_options(PitchLaneTests PRIVATE -Wno-float-equal -Wno-sign-conversion)
    endif()
    add_test(NAME PitchLaneTests COMMAND PitchLaneTests)

    # Dev tool: render the editor to a PNG (not part of the test suite).
    juce_add_console_app(PitchLaneSnapshot PRODUCT_NAME "PitchLaneSnapshot")
    target_sources(PitchLaneSnapshot PRIVATE tests/Snapshot.cpp)
    target_compile_definitions(PitchLaneSnapshot PRIVATE JucePlugin_Name="Pitch Lane" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(PitchLaneSnapshot PRIVATE PitchLaneShared juce::juce_recommended_config_flags)
    set_target_properties(PitchLaneSnapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
