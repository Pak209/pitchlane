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
option(PITCHLANE_DISTRIBUTION "Release build for Developer ID signing (see docs/RELEASING.md)" OFF)

# Signing policy: the build never hard-codes a signature. With the copy step OFF (default), JUCE
# adds no post-build codesign; the only signature on the product is the linker's automatic ad-hoc
# signature on the arm64 slice, which `codesign --force` replaces. With the copy step ON, JUCE
# ad-hoc signs the whole bundle after every build (handy for local testing, wrong for release),
# so a distribution build refuses it.
if(PITCHLANE_DISTRIBUTION AND PITCHLANE_COPY_AFTER_BUILD)
    message(FATAL_ERROR "PITCHLANE_DISTRIBUTION=ON requires PITCHLANE_COPY_AFTER_BUILD=OFF: the JUCE copy "
                        "step ad-hoc signs the bundle, which must not end up in a Developer ID release.")
endif()
if(PITCHLANE_DISTRIBUTION AND CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "PITCHLANE_DISTRIBUTION=ON is meant for the Ninja/Makefiles build used by "
                        ".github/workflows/release.yml (the Xcode generator adds its own signing step).")
endif()

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
