add_library(pitchlane_core STATIC
    src/NoteMath.cpp
    src/Yin.cpp
    src/PitchDetector.cpp
    src/PitchSmoother.cpp
    src/ReferenceNotes.cpp
    src/OfflineAnalyzer.cpp
    src/MidiFile.cpp
    src/Transport.cpp)

target_include_directories(pitchlane_core PUBLIC include)
target_compile_features(pitchlane_core PUBLIC cxx_std_17)
set_target_properties(pitchlane_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(pitchlane_core PRIVATE /W4)
else()
    target_compile_options(pitchlane_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(PITCHLANE_BUILD_TESTS)
    add_executable(pitchlane_core_tests
        tests/TestMain.cpp
        tests/TestSignals.h
        tests/NoteMathTests.cpp
        tests/PitchDetectorTests.cpp
        tests/SmootherTests.cpp
        tests/OfflineAnalyzerTests.cpp
        tests/MidiFileTests.cpp
        tests/TransportTests.cpp
        tests/ReferenceNotesTests.cpp)
    target_link_libraries(pitchlane_core_tests PRIVATE pitchlane_core)
    add_test(NAME pitchlane_core_tests COMMAND pitchlane_core_tests)
endif()
