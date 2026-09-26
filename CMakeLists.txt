cmake_minimum_required(VERSION 3.22)

project(PitchLane VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

option(PITCHLANE_BUILD_PLUGIN "Build the JUCE plugin (fetches JUCE)" ON)
option(PITCHLANE_BUILD_TESTS  "Build unit tests" ON)
option(PITCHLANE_VST3         "Also build the VST3 format" OFF)
option(PITCHLANE_STANDALONE   "Also build the Standalone app" ON)
option(PITCHLANE_COPY_AFTER_BUILD "Copy the plugin into the user plug-in folder after building" OFF)

if(PITCHLANE_BUILD_TESTS)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# Portable core (no JUCE): DSP, analysis, model, MIDI file I/O, transport maths
# ---------------------------------------------------------------------------
add_subdirectory(core)

# ---------------------------------------------------------------------------
# JUCE plugin
# ---------------------------------------------------------------------------
if(PITCHLANE_BUILD_PLUGIN)
    include(FetchContent)
    # JUCE 8, pinned. For offline/local builds pass
    #   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)

    add_subdirectory(plugin)
endif()
