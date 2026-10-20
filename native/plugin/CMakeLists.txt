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
target_link_libraries(RoliDrummer PRIVATE DrummerFactoryData drummer_core drummer_library ssb_core ssb_engine ${RD_SQLITE}
 juce::juce_audio_utils juce::juce_cryptography juce::juce_dsp juce::juce_gui_extra PUBLIC juce::juce_recommended_config_flags)
foreach(format AU VST3 Standalone)
 if(TARGET RoliDrummer_${format})
  ssb_bundle_web(RoliDrummer_${format})
 endif()
endforeach()
if(BUILD_TESTING)
 add_executable(drummer-parallel-keys ../drummer/tests/ParallelKeysTest.cpp)
 target_include_directories(drummer-parallel-keys PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" . ../engine/include ../core/include ../drummer/include)
 target_link_libraries(drummer-parallel-keys PRIVATE RoliDrummer)
 add_test(NAME parallel-keys COMMAND drummer-parallel-keys)
 set_tests_properties(parallel-keys PROPERTIES ENVIRONMENT "RD_DATA_DIR=/private/tmp/rolidrummer-parallel-keys-state")
 add_executable(drummer-kit-preview ../drummer/tests/KitPreviewTest.cpp)
 target_include_directories(drummer-kit-preview PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" . ../engine/include ../core/include ../drummer/include)
 target_link_libraries(drummer-kit-preview PRIVATE RoliDrummer)
 add_test(NAME kit-preview COMMAND drummer-kit-preview)
 set_tests_properties(kit-preview PROPERTIES ENVIRONMENT "RD_DATA_DIR=/private/tmp/rolidrummer-kit-preview-state")
 add_executable(drummer-monitor ../drummer/tests/MonitorTest.cpp)
 target_include_directories(drummer-monitor PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" . ../engine/include ../core/include ../drummer/include)
 target_link_libraries(drummer-monitor PRIVATE RoliDrummer)
 add_test(NAME monitor COMMAND drummer-monitor)
 set_tests_properties(monitor PROPERTIES ENVIRONMENT "RD_DATA_DIR=/private/tmp/rolidrummer-monitor-state")
 add_executable(drummer-integration)
 target_sources(drummer-integration PRIVATE ../drummer/tests/IntegrationTest.cpp)
 target_include_directories(drummer-integration PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" . ../engine/include ../core/include ../drummer/include)
 target_link_libraries(drummer-integration PRIVATE RoliDrummer)
 add_test(NAME integration COMMAND drummer-integration)
 set_tests_properties(integration PROPERTIES ENVIRONMENT "RD_DATA_DIR=/private/tmp/rolidrummer-test-state")
 add_executable(drummer-refresh-tests ../drummer/tests/RefreshTest.cpp)
 target_include_directories(drummer-refresh-tests PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" . ../engine/include ../core/include ../drummer/include)
 target_link_libraries(drummer-refresh-tests PRIVATE RoliDrummer)
 add_test(NAME refresh COMMAND drummer-refresh-tests)
 set_tests_properties(refresh PROPERTIES ENVIRONMENT "RD_DATA_DIR=/private/tmp/rolidrummer-refresh-state")
 add_executable(drummer-bank-tests ../drummer/tests/PatchBankTest.cpp)
 target_include_directories(drummer-bank-tests PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" ../engine/include ../core/include ../drummer/include)
 target_link_libraries(drummer-bank-tests PRIVATE RoliDrummer)
 add_test(NAME bank COMMAND drummer-bank-tests)
 if(TARGET drummer-neural-tests)
  add_executable(drummer-part-tests ../drummer/tests/PartCoverageTest.cpp)
  target_include_directories(drummer-part-tests PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" ../engine/include ../core/include ../drummer/include)
  target_link_libraries(drummer-part-tests PRIVATE RoliDrummer)
  add_test(NAME parts COMMAND drummer-part-tests "${RD_NEURAL_ASSET_DIR}")
  set_tests_properties(parts PROPERTIES ENVIRONMENT "RD_DATA_DIR=/private/tmp/rolidrummer-part-coverage-state")
 endif()
endif()

if(BUILD_TESTING)
 add_executable(drummer-expression ../drummer/tests/ExpressionTest.cpp)
 target_include_directories(drummer-expression PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" ../engine/include ../core/include ../drummer/include)
 target_link_libraries(drummer-expression PRIVATE RoliDrummer)
 add_test(NAME expression COMMAND drummer-expression)
 set_tests_properties(expression PROPERTIES ENVIRONMENT "RD_DATA_DIR=/private/tmp/rolidrummer-expression-state")
endif()

# Explicit opt-in target, available even when release unit tests are disabled.
add_executable(drummer-benchmark EXCLUDE_FROM_ALL ../drummer/tests/PerformanceBenchmark.cpp)
target_include_directories(drummer-benchmark PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" ../engine/include ../core/include)
target_link_libraries(drummer-benchmark PRIVATE RoliDrummer)

add_executable(drummer-neural-render EXCLUDE_FROM_ALL ../tools/neural-render.cpp)
target_include_directories(drummer-neural-render PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" . ../engine/include ../core/include ../drummer/include)
target_link_libraries(drummer-neural-render PRIVATE RoliDrummer)

add_executable(drummer-neural-benchmark EXCLUDE_FROM_ALL ../tools/neural-benchmark.cpp)
target_include_directories(drummer-neural-benchmark PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" . ../engine/include ../core/include ../drummer/include ../drummer/src)
target_link_libraries(drummer-neural-benchmark PRIVATE RoliDrummer)

# Load the exact development AU factory, without replacing an installed AU or
# asking the system registry to choose a potentially older bundle.
if(APPLE)
 add_executable(drummer-au-host EXCLUDE_FROM_ALL ../tools/au-host.mm)
 target_link_libraries(drummer-au-host PRIVATE "-framework AudioToolbox" "-framework Foundation")
endif()

# The same editor-free combined workload provides a correctness check and an
# opt-in release callback benchmark without loading the library/model on audio.
if(BUILD_TESTING)
 add_executable(drummer-intent-performance ../drummer/tests/IntentPerformance.cpp)
 add_test(NAME intent-performance COMMAND drummer-intent-performance)
 set_tests_properties(intent-performance PROPERTIES ENVIRONMENT "RD_DATA_DIR=/private/tmp/rolidrummer-intent-performance-state")
else()
 add_executable(drummer-intent-performance EXCLUDE_FROM_ALL ../drummer/tests/IntentPerformance.cpp)
endif()
target_include_directories(drummer-intent-performance PRIVATE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>" . ../engine/include ../core/include ../drummer/include)
target_link_libraries(drummer-intent-performance PRIVATE RoliDrummer)
