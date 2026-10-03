# SSB's sound engine: sources shared by the plugin and the engine tests / render tool.
#
# An INTERFACE library rather than a static one: it uses JUCE modules (juce_dsp for convolution),
# and JUCE modules are compiled into each final target. A static library linking them too would
# define every JUCE symbol twice.
add_library(ssb_engine INTERFACE)
target_sources(ssb_engine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ModelJson.cpp)
target_include_directories(ssb_engine INTERFACE include)
