juce_add_binary_data(DrummerFactoryData HEADER_NAME FactoryData.h NAMESPACE drummerfactory SOURCES ../../src/data/factoryKit.json)
if(APPLE)
 set(RD_SQLITE sqlite3)
else()
 find_package(SQLite3 REQUIRED)
 set(RD_SQLITE SQLite::SQLite3)
endif()
juce_add_plugin(RoliDrummer
 COMPANY_NAME WaveContour BUNDLE_ID com.wavecontour.rolidrummer
 PLUGIN_MANUFACTURER_CODE WvCt PLUGIN_CODE Rld1
 PRODUCT_NAME "Roli Drummer" DESCRIPTION "MPE drum pattern instrument"
 IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
 EDITOR_WANTS_KEYBOARD_FOCUS TRUE VST3_CATEGORIES Instrument Sampler
 AU_MAIN_TYPE kAudioUnitType_MusicDevice FORMATS AU VST3 Standalone
 COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(RoliDrummer PRIVATE PluginProcessor.cpp PluginEditor.cpp PluginPaths.cpp
 ../drummer/src/State.cpp ../drummer/src/SharedStore.cpp ../drummer/src/Assets.cpp ../drummer/src/Preview.cpp)
target_compile_definitions(RoliDrummer PUBLIC JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(RoliDrummer PRIVATE DrummerFactoryData drummer_core ssb_core ssb_engine ${RD_SQLITE}
 juce::juce_audio_utils juce::juce_cryptography juce::juce_dsp juce::juce_gui_extra PUBLIC juce::juce_recommended_config_flags)
foreach(format AU VST3 Standalone)
 if(TARGET RoliDrummer_${format})
  ssb_bundle_web(RoliDrummer_${format})
 endif()
endforeach()
if(BUILD_TESTING)
 add_executable(drummer-integration)
 target_sources(drummer-integration PRIVATE ../drummer/tests/IntegrationTest.cpp)
 target_include_directories(drummer-integration PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" . ../engine/include ../core/include ../drummer/include)
 target_link_libraries(drummer-integration PRIVATE RoliDrummer)
 add_test(NAME integration COMMAND drummer-integration)
 set_tests_properties(integration PROPERTIES ENVIRONMENT "RD_DATA_DIR=/private/tmp/rolidrummer-test-state")
endif()

if(BUILD_TESTING)
 add_executable(drummer-expression ../drummer/tests/ExpressionTest.cpp)
 target_include_directories(drummer-expression PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" ../engine/include ../core/include ../drummer/include)
 target_link_libraries(drummer-expression PRIVATE RoliDrummer)
 add_test(NAME expression COMMAND drummer-expression)
endif()

# Explicit opt-in target, available even when release unit tests are disabled.
add_executable(drummer-benchmark EXCLUDE_FROM_ALL ../drummer/tests/PerformanceBenchmark.cpp)
target_include_directories(drummer-benchmark PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" ../engine/include ../core/include)
target_link_libraries(drummer-benchmark PRIVATE RoliDrummer)
