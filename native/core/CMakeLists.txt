# The part of SSB that needs no plugin API and no JUCE: the queues between the audio thread and
# the page, and (as it grows) the sound engine. Built and tested on its own.
add_library(ssb_core STATIC
    src/MidiQueue.cpp)

target_include_directories(ssb_core PUBLIC include)
target_compile_features(ssb_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(ssb_core PRIVATE /W4)
else()
    target_compile_options(ssb_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
