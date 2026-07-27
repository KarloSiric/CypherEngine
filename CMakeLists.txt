# //////////////////////////////////////////////////////////////////////////
# //
# //  CypherEngine Source Code
# //  Copyright (c) 2026 Karlo Siric. All rights reserved.
# //
# //  File: CMakeLists.txt
# //  Purpose: Defines the main CMake build graph for CypherEngine.
# //  Details: This file wires engine, tests, benchmarks, and approved dependencies into
# //           one build graph. Keep target ownership clear so subsystems can grow
# //           without accidental coupling.
# //
# //  History:
# //  - Created by Karlo Siric on 2026-04-18
# //
# //  This file is proprietary and confidential. See LICENSE for details.
# //
# //////////////////////////////////////////////////////////////////////////

cmake_minimum_required(VERSION 3.20)

project(CypherEngine
    VERSION 0.1.0
    DESCRIPTION "CypherEngine from-scratch C++20 3D engine inspired by idTech, GoldSrc, and early CryEngine"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    add_compile_options(/Zc:preprocessor)
endif()

# Keep binaries and intermediate files in the build directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(CYPHERENGINE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CYPHERENGINE_SRC_DIR "${CYPHERENGINE_ROOT_DIR}/src")
set(CYPHERENGINE_THIRDPARTY_DIR "${CYPHERENGINE_ROOT_DIR}/thirdparty")
set(CYPHER_BUILD_BRANCH "local" CACHE STRING "Source branch recorded in build identity")
set(CYPHER_BUILD_COMMIT "local" CACHE STRING "Source revision recorded in build identity")
set(CYPHER_BUILD_NUMBER "0" CACHE STRING "CI/build number recorded in build identity")

set(CYPHERENGINE_INCLUDE_DIRS
    "${CYPHERENGINE_SRC_DIR}"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherCommon"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier2"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier3"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier4"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherAI"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherAnimation"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherAudio"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherCommand"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherConfig"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherConsole"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherCVar"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherEntity"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherHost"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherInput"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherLog"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherMath"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherMemory"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherNetwork"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPak"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPhysics"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPlatform"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherProfile"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherRender"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherResource"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherScript"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherSystem"
    "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherWorld"
    "${CYPHERENGINE_SRC_DIR}/CypherClient"
    "${CYPHERENGINE_SRC_DIR}/CypherEditor"
    "${CYPHERENGINE_SRC_DIR}/CypherGame"
    "${CYPHERENGINE_SRC_DIR}/CypherServer"
    "${CYPHERENGINE_SRC_DIR}/CypherTools"
    "${CYPHERENGINE_SRC_DIR}/rengine"
    "${CYPHERENGINE_SRC_DIR}/rengine/math"
)

# Optional dependencies: only enabled when the corresponding files exist.
set(CYPHERENGINE_LINK_LIBS "")
if (CMAKE_DL_LIBS)
    list(APPEND CYPHERENGINE_LINK_LIBS ${CMAKE_DL_LIBS})
endif()

if (EXISTS "${CYPHERENGINE_THIRDPARTY_DIR}/glad/src/gl.c")
    message(STATUS "Found vendored glad")
    add_library(glad STATIC "${CYPHERENGINE_THIRDPARTY_DIR}/glad/src/gl.c")
    target_include_directories(glad PUBLIC "${CYPHERENGINE_THIRDPARTY_DIR}/glad/include")
    list(APPEND CYPHERENGINE_LINK_LIBS glad)
else()
    message(STATUS "No vendored glad yet. Add thirdparty/glad before enabling modern OpenGL loading.")
endif()

if (EXISTS "${CYPHERENGINE_THIRDPARTY_DIR}/raylib/CMakeLists.txt")
    message(STATUS "Found vendored raylib")
    add_subdirectory(${CYPHERENGINE_THIRDPARTY_DIR}/raylib raylib_build)
    list(APPEND CYPHERENGINE_LINK_LIBS raylib)
elseif (EXISTS "${CYPHERENGINE_THIRDPARTY_DIR}/raylib/src")
    message(STATUS "Found raylib tree but no CMakeLists.txt; skipping raylib add_subdirectory")
else()
    message(STATUS "No vendored raylib. CypherEngine currently uses SDL3/OpenGL directly.")
endif()

if (EXISTS "${CYPHERENGINE_THIRDPARTY_DIR}/flecs/include/flecs.h")
    add_library(flecs INTERFACE)
    target_include_directories(flecs INTERFACE "${CYPHERENGINE_THIRDPARTY_DIR}/flecs/include")
    list(APPEND CYPHERENGINE_LINK_LIBS flecs)
else()
    message(STATUS "No vendored Flecs headers yet. Add thirdparty/flecs/include/flecs.h.")
endif()

if (EXISTS "${CYPHERENGINE_THIRDPARTY_DIR}/glm/glm/glm.hpp")
    add_library(glm INTERFACE)
    target_include_directories(glm INTERFACE "${CYPHERENGINE_THIRDPARTY_DIR}/glm")
    list(APPEND CYPHERENGINE_LINK_LIBS glm)
else()
    message(STATUS "No vendored GLM headers yet. Add thirdparty/glm/glm/glm.hpp.")
endif()

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
list(APPEND CYPHERENGINE_LINK_LIBS SDL3::SDL3)

find_package(Threads REQUIRED)

set(CYPHERCOMMON_TIER0_SOURCES
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Assert.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Atomic.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_BuildId.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CacheHints.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CommandLineBase.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUMonitoring.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUDetect.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Crash.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Debug.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_DynamicLibrary.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Environment.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Error.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Event.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Handle.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Log.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_LogToggle.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_MemoryDebug.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_MemoryOps.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_MemoryTracker.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Minidump.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Module.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Mutex.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_PageAllocator.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_PerformanceCounter.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_PlatformMemory.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Process.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Profile.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Semaphore.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Simd.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_SourceLocation.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_StackTrace.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Stats.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_SystemInfo.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Timer.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_TLS.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_TsList.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Validator.cpp"
)

set(CYPHERCOMMON_TIER1_SOURCES
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_Char.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_String.cpp"
)

add_library(CypherCommonTier0 STATIC ${CYPHERCOMMON_TIER0_SOURCES})
add_library(Cypher::CommonTier0 ALIAS CypherCommonTier0)
target_compile_features(CypherCommonTier0 PUBLIC cxx_std_20)
target_include_directories(CypherCommonTier0
    PUBLIC
        "${CYPHERENGINE_SRC_DIR}"
        "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0"
)
target_compile_definitions(CypherCommonTier0
    PUBLIC
        CYPHER_COMMON_STATIC=1
        $<$<CONFIG:Debug>:CYPHER_CONFIG_DEBUG=1>
        $<$<CONFIG:RelWithDebInfo>:CYPHER_CONFIG_DEVELOPMENT=1>
        $<$<CONFIG:Release>:CYPHER_CONFIG_RELEASE=1>
        $<$<OR:$<CONFIG:MinSizeRel>,$<CONFIG:Shipping>>:CYPHER_CONFIG_SHIPPING=1>
)
set_source_files_properties(
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_BuildId.cpp"
    PROPERTIES
        COMPILE_DEFINITIONS
            "CYPHER_ENGINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};CYPHER_ENGINE_VERSION_MINOR=${PROJECT_VERSION_MINOR};CYPHER_ENGINE_VERSION_PATCH=${PROJECT_VERSION_PATCH};CYPHER_ENGINE_VERSION_STRING=\"${PROJECT_VERSION}\";CYPHER_BUILD_BRANCH=\"${CYPHER_BUILD_BRANCH}\";CYPHER_BUILD_COMMIT=\"${CYPHER_BUILD_COMMIT}\";CYPHER_BUILD_NUMBER=${CYPHER_BUILD_NUMBER}"
)
target_link_libraries(CypherCommonTier0 PRIVATE Threads::Threads)
if (CMAKE_DL_LIBS)
    target_link_libraries(CypherCommonTier0 PRIVATE ${CMAKE_DL_LIBS})
endif()

add_library(CypherCommonTier1 STATIC ${CYPHERCOMMON_TIER1_SOURCES})
add_library(Cypher::CommonTier1 ALIAS CypherCommonTier1)
target_compile_features(CypherCommonTier1 PUBLIC cxx_std_20)
target_include_directories(CypherCommonTier1
    PUBLIC
        "${CYPHERENGINE_SRC_DIR}"
        "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1"
)
target_link_libraries(CypherCommonTier1 PUBLIC Cypher::CommonTier0)

file(GLOB_RECURSE CYPHERENGINE_SOURCES CONFIGURE_DEPENDS
    "${CYPHERENGINE_SRC_DIR}/*.cpp"
    "${CYPHERENGINE_SRC_DIR}/*.cxx"
    "${CYPHERENGINE_SRC_DIR}/*.cc"
)
list(FILTER CYPHERENGINE_SOURCES
    EXCLUDE REGEX "^${CYPHERENGINE_SRC_DIR}/CypherCommon/"
)

if (CYPHERENGINE_SOURCES)
    add_executable(CypherEngine ${CYPHERENGINE_SOURCES})
    target_include_directories(CypherEngine PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
    target_link_libraries(CypherEngine PRIVATE Cypher::CommonTier1)
    if (CYPHERENGINE_LINK_LIBS)
        target_link_libraries(CypherEngine PRIVATE ${CYPHERENGINE_LINK_LIBS})
    endif()
else()
    message(STATUS "src/ has no C++ sources yet. Add your first entry file at src/main.cpp to enable executable target.")
endif()

option(CYPHERENGINE_BUILD_TESTS "Build CypherEngine test targets" OFF)

if (CYPHERENGINE_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 CONFIG QUIET)

    add_executable(cypher_common_tier0_smoke
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Smoke.cpp"
    )
    target_link_libraries(cypher_common_tier0_smoke PRIVATE Cypher::CommonTier0)
    add_test(NAME cypher_common_tier0_smoke COMMAND cypher_common_tier0_smoke)

    if (Catch2_FOUND)
        function(cypher_add_common_catch_test target_name source_file common_target)
            add_executable(${target_name} "${source_file}")
            target_link_libraries(${target_name}
                PRIVATE
                    Catch2::Catch2WithMain
                    ${common_target}
            )
            add_test(NAME ${target_name} COMMAND ${target_name})
        endfunction()

        cypher_add_common_catch_test(
            cypher_common_tier0_align_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Align_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_annotations_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Annotations_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_atomic_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Atomic_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_api_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_API_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_assert_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Assert_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_basetypes_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_BaseTypes_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_bits_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Bits_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_buildconfig_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_BuildConfig_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_buildid_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_BuildId_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_cachehints_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_CacheHints_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_compiler_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Compiler_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_commandlinebase_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_CommandLineBase_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_crash_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Crash_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_defines_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Defines_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_debug_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Debug_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_dynamiclibrary_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_DynamicLibrary_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_endian_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Endian_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_environment_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Environment_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_error_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Error_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_event_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Event_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_handle_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Handle_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_cpudetect_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_CPUDetect_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_cpumonitoring_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_CPUMonitoring_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_simd_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Simd_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_sourcelocation_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_SourceLocation_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_memoryops_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_MemoryOps_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_memorydebug_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_MemoryDebug_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_memorytracker_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_MemoryTracker_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_minidump_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Minidump_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_log_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Log_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_logtoggle_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_LogToggle_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_platform_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Platform_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_platformmemory_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_PlatformMemory_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_process_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Process_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_profile_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Profile_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_pageallocator_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_PageAllocator_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_performancecounter_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_PerformanceCounter_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_mutex_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Mutex_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_module_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Module_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_semaphore_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Semaphore_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_services_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Services_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_stacktrace_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_StackTrace_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_stats_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Stats_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_staticanalysis_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_StaticAnalysis_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_timer_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Timer_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_tls_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_TLS_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_tslist_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_TsList_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_warnings_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Warnings_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_systeminfo_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_SystemInfo_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_thread_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Thread_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_typetraits_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_TypeTraits_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier0_validator_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Validator_Tests.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_catch_test(
            cypher_common_tier1_char_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier1/CypherCommon_Tier1_Char_Tests.cpp"
            Cypher::CommonTier1
        )
        cypher_add_common_catch_test(
            cypher_common_tier1_string_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier1/CypherCommon_Tier1_String_Tests.cpp"
            Cypher::CommonTier1
        )
    else()
        message(STATUS "Catch2 not found. Skipping Catch2-based CypherCommon tests.")
    endif()

    set(CYPHERENGINE_FILESYSTEM_SMOKE_SOURCES
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEngine/CypherFileSystem/CypherFileSystem_Smoke.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem_Async.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem_Diagnostics.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem_Discovery.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem_Directory.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem_Mount.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem_Package.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem_Path.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem_Runtime.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem_Watch.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPak/CypherPak_Compression.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPak/CypherPak_Reader.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPak/CypherPak_Writer.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherLog/CypherLog.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherLog/CypherLog_Format.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherSystem/CypherSystem_Platform.cpp"
    )

    add_executable(cypher_filesystem_smoke ${CYPHERENGINE_FILESYSTEM_SMOKE_SOURCES})
    target_include_directories(cypher_filesystem_smoke PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
    add_test(NAME cypher_filesystem_smoke COMMAND cypher_filesystem_smoke)

    set(CYPHERENGINE_PAK_SMOKE_SOURCES
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEngine/CypherPak/CypherPak_Smoke.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPak/CypherPak_Compression.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPak/CypherPak_Reader.cpp"
        "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPak/CypherPak_Writer.cpp"
    )

    add_executable(cypher_pak_smoke ${CYPHERENGINE_PAK_SMOKE_SOURCES})
    target_include_directories(cypher_pak_smoke PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
    add_test(NAME cypher_pak_smoke COMMAND cypher_pak_smoke)
endif()

option(CYPHERENGINE_BUILD_BENCHMARKS "Build CypherEngine benchmark targets" OFF)

if (CYPHERENGINE_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG QUIET)

    if (benchmark_FOUND)
        add_executable(cypher_perf_system_report
            "${CYPHERENGINE_ROOT_DIR}/tools/perf/CypherPerf_SystemReport.cpp"
        )
        target_link_libraries(cypher_perf_system_report PRIVATE Cypher::CommonTier0)

        function(cypher_add_common_benchmark target_name source_file common_target)
            add_executable(${target_name} "${source_file}")
            target_link_libraries(${target_name}
                PRIVATE
                    benchmark::benchmark_main
                    ${common_target}
            )
        endfunction()

        cypher_add_common_benchmark(
            cypher_common_char_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier1/CypherCommon_Char_Bench.cpp"
            Cypher::CommonTier1
        )
        cypher_add_common_benchmark(
            cypher_common_align_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Align_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_atomic_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Atomic_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_bits_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Bits_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_endian_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Endian_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_event_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Event_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_timer_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Timer_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_thread_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Thread_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_tls_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_TLS_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_semaphore_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Semaphore_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_services_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Services_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_stacktrace_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_StackTrace_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_cpudetect_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_CPUDetect_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_simd_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Simd_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_systeminfo_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_SystemInfo_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_string_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier1/CypherCommon_String_Bench.cpp"
            Cypher::CommonTier1
        )
        cypher_add_common_benchmark(
            cypher_common_memoryops_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_MemoryOps_Bench.cpp"
            Cypher::CommonTier0
        )
        cypher_add_common_benchmark(
            cypher_common_mutex_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Mutex_Bench.cpp"
            Cypher::CommonTier0
        )

        add_executable(cypher_memory_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherEngine/CypherMemory/CypherMemory_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherMemory/CypherMemory_Arena.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherMemory/CypherMemory_Pool.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherLog/CypherLog.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherLog/CypherLog_Format.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherSystem/CypherSystem_Platform.cpp"
        )
        target_include_directories(cypher_memory_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_memory_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_filesystem_path_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherEngine/CypherFileSystem/CypherFileSystem_Path_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherFileSystem/CypherFileSystem_Path.cpp"
        )
        target_include_directories(cypher_filesystem_path_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_filesystem_path_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_pak_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherEngine/CypherPak/CypherPak_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPak/CypherPak_Compression.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPak/CypherPak_Reader.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherEngine/CypherPak/CypherPak_Writer.cpp"
        )
        target_include_directories(cypher_pak_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_pak_bench PRIVATE benchmark::benchmark_main)
    else()
        message(STATUS "Google Benchmark not found. Skipping CypherEngine benchmark targets.")
    endif()
endif()

if (EXISTS "${CYPHERENGINE_ROOT_DIR}/tools/CMakeLists.txt")
    option(CYPHERENGINE_BUILD_TOOLS "Build tool targets" OFF)
    if (CYPHERENGINE_BUILD_TOOLS)
        add_subdirectory("${CYPHERENGINE_ROOT_DIR}/tools")
    endif()
else()
    message(STATUS "No tools/CMakeLists.txt yet. Tool targets will be added in a later phase.")
endif()
