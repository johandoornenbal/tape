cmake_minimum_required(VERSION 3.22)
project(ToTapeStudio VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
add_library(tape_engine Source/TapeEngine.cpp)
set_target_properties(tape_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(tape_engine PUBLIC Source)
if(MSVC)
  target_compile_definitions(tape_engine PUBLIC _USE_MATH_DEFINES)
endif()
if(BUILD_TESTING)
  add_executable(dsp_tests tests/DspTests.cpp)
  target_link_libraries(dsp_tests PRIVATE tape_engine)
  add_test(NAME tape_dsp COMMAND dsp_tests)
  add_executable(reference_tests tests/ReferenceTests.cpp tests/ReferenceEngine.cpp)
  target_link_libraries(reference_tests PRIVATE tape_engine)
  target_include_directories(reference_tests PRIVATE tests/shim)
  add_test(NAME upstream_reference COMMAND reference_tests)
endif()
option(TAPE_DSP_ONLY "Build DSP tests without GUI dependencies" OFF)
if(TAPE_DSP_ONLY)
  return()
endif()
include(FetchContent)
set(JUCE_PATH "" CACHE PATH "Optional existing JUCE 8.0.12 checkout")
if(JUCE_PATH)
  add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
else()
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850 GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(JUCE)
endif()
set(TAPE_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND TAPE_FORMATS AU)
endif()
juce_add_plugin(ToTapeStudio
  COMPANY_NAME "Doornenbal Audio"
  BUNDLE_ID "nl.doornenbal.totapestudio"
  PLUGIN_MANUFACTURER_CODE Jdau
  PLUGIN_CODE Ttp9
  FORMATS ${TAPE_FORMATS}
  PRODUCT_NAME "ToTape Studio"
  IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx Distortion)
juce_generate_juce_header(ToTapeStudio)
target_sources(ToTapeStudio PRIVATE Source/Plugin.cpp)
target_compile_definitions(ToTapeStudio PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(ToTapeStudio PRIVATE tape_engine juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(BUILD_TESTING)
  add_executable(plugin_checks tests/PluginChecks.cpp)
  target_include_directories(plugin_checks PRIVATE "${JUCE_MODULES_DIR}")
  target_compile_definitions(plugin_checks PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
  target_link_libraries(plugin_checks PRIVATE ToTapeStudio)
  add_test(NAME plugin_wrapper COMMAND plugin_checks)
endif()
