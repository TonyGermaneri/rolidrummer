add_executable(ssb-tests
    main.cpp
    MidiQueueTest.cpp)

target_link_libraries(ssb-tests PRIVATE ssb_core)
add_test(NAME core COMMAND ssb-tests)

# The engine, driven without a host: unit tests, and `ssb-render` for rendering a board to WAV.
if(SSB_BUILD_PLUGIN)
    juce_add_console_app(ssb-engine-tests PRODUCT_NAME "ssb-engine-tests")
    target_sources(ssb-engine-tests PRIVATE engine_main.cpp EngineTest.cpp)
    target_compile_definitions(ssb-engine-tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(ssb-engine-tests PRIVATE
        ssb_engine
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags)
    add_test(NAME engine COMMAND ssb-engine-tests)
endif()
