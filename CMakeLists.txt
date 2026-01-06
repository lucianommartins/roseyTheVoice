cmake_minimum_required(VERSION 3.20)
project(RoseyTheVoice VERSION 0.1.0 LANGUAGES CXX C)

# =============================================================================
# Build Configuration
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags for Intel Core Ultra 7 (AVX2, no AVX-512)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -mavx2 -mfma")
endif()

# =============================================================================
# Options
# =============================================================================
option(RTV_BUILD_TESTS "Build unit tests" ON)
option(RTV_ENABLE_SANITIZERS "Enable ASan/UBSan in debug builds" OFF)

# =============================================================================
# FetchContent for AEC3
# =============================================================================
include(FetchContent)

# WebRTC AEC3 - Echo Cancellation (standalone extraction by ewan-xu)
FetchContent_Declare(
    aec3
    GIT_REPOSITORY https://github.com/ewan-xu/AEC3.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

message(STATUS "Fetching WebRTC AEC3...")
FetchContent_GetProperties(aec3)
if(NOT aec3_POPULATED)
    FetchContent_Populate(aec3)
endif()

# whisper.cpp - Speech-to-Text (OpenAI Whisper port)
FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG v1.7.4
    GIT_SHALLOW TRUE
)

set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

message(STATUS "Fetching whisper.cpp...")
FetchContent_MakeAvailable(whisper)

# cpp-httplib - Header-only HTTP library
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
    GIT_SHALLOW TRUE
)
message(STATUS "Fetching cpp-httplib...")
FetchContent_MakeAvailable(httplib)

# nlohmann_json - JSON library
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
message(STATUS "Fetching nlohmann_json...")
FetchContent_MakeAvailable(json)

# =============================================================================
# Build AEC3 library from sources
# =============================================================================
# Collect C++ sources
file(GLOB_RECURSE AEC3_CXX_SOURCES
    "${aec3_SOURCE_DIR}/audio_processing/*.cc"
    "${aec3_SOURCE_DIR}/base/rtc_base/*.cc"
    "${aec3_SOURCE_DIR}/base/system_wrappers/*.cc"
    "${aec3_SOURCE_DIR}/base/jsoncpp/src/lib_json/*.cpp"
    "${aec3_SOURCE_DIR}/api/*.cc"
)

# Collect C sources separately
file(GLOB_RECURSE AEC3_C_SOURCES
    "${aec3_SOURCE_DIR}/audio_processing/*.c"
)

# Remove unit tests and problematic files
list(FILTER AEC3_CXX_SOURCES EXCLUDE REGEX ".*_unittest\\.cc$")
list(FILTER AEC3_CXX_SOURCES EXCLUDE REGEX ".*_test\\.cc$")
list(FILTER AEC3_CXX_SOURCES EXCLUDE REGEX ".*/test/.*")
list(FILTER AEC3_CXX_SOURCES EXCLUDE REGEX ".*/logging/wav.*\\.cc$")
list(FILTER AEC3_CXX_SOURCES EXCLUDE REGEX ".*/utility/pffft.*\\.cc$")

add_library(aec3_lib STATIC ${AEC3_CXX_SOURCES} ${AEC3_C_SOURCES})

target_include_directories(aec3_lib PUBLIC
    ${aec3_SOURCE_DIR}
    ${aec3_SOURCE_DIR}/base
    ${aec3_SOURCE_DIR}/base/abseil
    ${aec3_SOURCE_DIR}/base/jsoncpp/include
    ${aec3_SOURCE_DIR}/audio_processing
)

target_compile_definitions(aec3_lib PUBLIC
    WEBRTC_POSIX
    WEBRTC_LINUX
)

# Suppress warnings and fix missing includes (C++ only)
set_source_files_properties(${AEC3_CXX_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-w;-include;${CMAKE_CURRENT_SOURCE_DIR}/include/rtv/aec3_compat.hpp"
)

# C files need C compat header
set_source_files_properties(${AEC3_C_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-w;-include;${CMAKE_CURRENT_SOURCE_DIR}/include/rtv/aec3_c_compat.h"
)

# =============================================================================
# Find System Dependencies
# =============================================================================
find_package(SQLite3 REQUIRED)
find_package(Boost 1.74 REQUIRED COMPONENTS system)

# pkg-config for PortAudio
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# libfvad - Voice Activity Detection (REQUIRED)
pkg_check_modules(FVAD fvad)
if(NOT FVAD_FOUND)
    find_library(FVAD_LIBRARIES fvad)
    find_path(FVAD_INCLUDE_DIRS fvad.h)
    if(FVAD_LIBRARIES AND FVAD_INCLUDE_DIRS)
        set(FVAD_FOUND TRUE)
        message(STATUS "Found libfvad: ${FVAD_LIBRARIES}")
    else()
        message(FATAL_ERROR "libfvad not found!")
    endif()
endif()

# =============================================================================
# Porcupine Wake Word Detection
# =============================================================================
set(PORCUPINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/porcupine)
if(EXISTS ${PORCUPINE_DIR}/lib/linux/x86_64/libpv_porcupine.so)
    set(PORCUPINE_FOUND TRUE)
    set(PORCUPINE_INCLUDE_DIR ${PORCUPINE_DIR}/include)
    set(PORCUPINE_LIBRARY ${PORCUPINE_DIR}/lib/linux/x86_64/libpv_porcupine.so)
    message(STATUS "Found Porcupine: ${PORCUPINE_LIBRARY}")
else()
    set(PORCUPINE_FOUND FALSE)
    message(WARNING "Porcupine not found - wake word detection disabled")
endif()

# =============================================================================
# Main Library
# =============================================================================
add_library(rtv_core STATIC
    src/audio/AudioEngine.cpp
    src/audio/AudioPipeline.cpp
    src/audio/RingBuffer.cpp
    src/audio/VADProcessor.cpp
    src/stt/STTEngine.cpp
    src/llm/LLMClient.cpp
    src/llm/ActionDetector.cpp
    src/llm/ConversationEngine.cpp
    src/llm/EmbeddingEngine.cpp
    src/tts/TTSEngine.cpp
    src/tts/TTSStreamer.cpp
    src/cache/CacheManager.cpp
    src/ipc/SharedMemoryIPC.cpp
    src/orchestrator/Orchestrator.cpp
)

# Add WakeWordDetector if Porcupine is available
if(PORCUPINE_FOUND)
    target_sources(rtv_core PRIVATE src/wakeword/WakeWordDetector.cpp)
    target_include_directories(rtv_core PUBLIC ${PORCUPINE_INCLUDE_DIR})
    target_link_libraries(rtv_core PUBLIC ${PORCUPINE_LIBRARY})
    target_compile_definitions(rtv_core PUBLIC RTV_HAS_PORCUPINE=1)
endif()

target_include_directories(rtv_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PORTAUDIO_INCLUDE_DIRS}
    ${FVAD_INCLUDE_DIRS}
)

target_link_libraries(rtv_core PUBLIC
    SQLite::SQLite3
    Boost::system
    ${PORTAUDIO_LIBRARIES}
    ${FVAD_LIBRARIES}
    aec3_lib
    whisper
    httplib::httplib
    nlohmann_json::nlohmann_json
)

# =============================================================================
# Main Executable
# =============================================================================
add_executable(rtv src/main.cpp)
target_link_libraries(rtv PRIVATE rtv_core)

# Interactive demo
add_executable(rtv_interactive src/rtv_interactive.cpp)
target_link_libraries(rtv_interactive PRIVATE rtv_core)

# =============================================================================
# Tests
# =============================================================================
if(RTV_BUILD_TESTS)
    enable_testing()
    
    add_executable(test_ring_buffer tests/audio/test_ring_buffer.cpp)
    target_link_libraries(test_ring_buffer PRIVATE rtv_core)
    add_test(NAME RingBufferTest COMMAND test_ring_buffer)
    
    add_executable(test_aec3_pipeline tests/audio/test_aec3_pipeline.cpp)
    target_link_libraries(test_aec3_pipeline PRIVATE rtv_core)
    add_test(NAME AEC3PipelineTest COMMAND test_aec3_pipeline)
    
    add_executable(rtv_stt_test tests/stt/test_stt.cpp)
    target_link_libraries(rtv_stt_test PRIVATE rtv_core)
    
    add_executable(rtv_e2e_test tests/integration/test_e2e.cpp)
    target_link_libraries(rtv_e2e_test PRIVATE rtv_core)
    
    add_executable(rtv_audio_test tests/audio/test_audio.cpp)
    target_link_libraries(rtv_audio_test PRIVATE rtv_core)
    
    add_executable(rtv_live_transcription tests/integration/test_live_transcription.cpp)
    target_link_libraries(rtv_live_transcription PRIVATE rtv_core)
    
    add_executable(rtv_llm_test tests/llm/test_llm.cpp)
    target_link_libraries(rtv_llm_test PRIVATE rtv_core)
    
    add_executable(rtv_tts_test tests/tts/test_tts.cpp)
    target_link_libraries(rtv_tts_test PRIVATE rtv_core)
    
    add_executable(rtv_orchestrator_test tests/orchestrator/test_orchestrator.cpp)
    target_link_libraries(rtv_orchestrator_test PRIVATE rtv_core)
endif()

# =============================================================================
# Install
# =============================================================================
install(TARGETS rtv DESTINATION bin)
install(DIRECTORY include/rtv DESTINATION include)

# =============================================================================
# Summary
# =============================================================================
message(STATUS "")
message(STATUS "=== Rosey The Voice (RTV) Build Configuration ===")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests:    ${RTV_BUILD_TESTS}")
message(STATUS "  PortAudio:      ${PORTAUDIO_VERSION}")
message(STATUS "  libfvad:        ${FVAD_FOUND}")
message(STATUS "  AEC3:           ${aec3_SOURCE_DIR}")
message(STATUS "")
