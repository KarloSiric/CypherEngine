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

file(GLOB_RECURSE CYPHERENGINE_SOURCES CONFIGURE_DEPENDS
    "${CYPHERENGINE_SRC_DIR}/*.cpp"
    "${CYPHERENGINE_SRC_DIR}/*.cxx"
    "${CYPHERENGINE_SRC_DIR}/*.cc"
)

if (CYPHERENGINE_SOURCES)
    add_executable(CypherEngine ${CYPHERENGINE_SOURCES})
    target_include_directories(CypherEngine PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
    if (CYPHERENGINE_LINK_LIBS)
        target_link_libraries(CypherEngine PRIVATE ${CYPHERENGINE_LINK_LIBS})
    endif()
else()
    message(STATUS "src/ has no C++ sources yet. Add your first entry file at src/main.cpp to enable executable target.")
endif()

set(CYPHERCOMMON_TIER0_SERVICE_SOURCES
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Atomic.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_BuildId.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CacheHints.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CommandLineBase.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUMonitoring.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUDetect.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Crash.cpp"
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
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_ProgressBar.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Semaphore.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Simd.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_SourceLocation.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_StackTrace.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Stats.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_SystemInfo.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_TestThread.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Timer.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_TLS.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_TsList.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Validator.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_WideChar.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_Char.cpp"
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_String.cpp"
)

option(CYPHERENGINE_BUILD_TESTS "Build CypherEngine test targets" OFF)

if (CYPHERENGINE_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 CONFIG QUIET)

    add_executable(cypher_common_tier0_smoke
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Smoke.cpp"
        ${CYPHERCOMMON_TIER0_SERVICE_SOURCES}
    )
    target_include_directories(cypher_common_tier0_smoke PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
    if (CMAKE_DL_LIBS)
        target_link_libraries(cypher_common_tier0_smoke PRIVATE ${CMAKE_DL_LIBS})
    endif()
    add_test(NAME cypher_common_tier0_smoke COMMAND cypher_common_tier0_smoke)

    if (Catch2_FOUND)
        add_executable(cypher_common_tier0_align_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Align_Tests.cpp"
        )
        target_include_directories(cypher_common_tier0_align_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_align_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_align_tests COMMAND cypher_common_tier0_align_tests)

        add_executable(cypher_common_tier0_atomic_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Atomic_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Atomic.cpp"
        )
        target_include_directories(cypher_common_tier0_atomic_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_atomic_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_atomic_tests COMMAND cypher_common_tier0_atomic_tests)

        add_executable(cypher_common_tier0_bits_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Bits_Tests.cpp"
        )
        target_include_directories(cypher_common_tier0_bits_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_bits_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_bits_tests COMMAND cypher_common_tier0_bits_tests)

        add_executable(cypher_common_tier0_endian_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Endian_Tests.cpp"
        )
        target_include_directories(cypher_common_tier0_endian_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_endian_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_endian_tests COMMAND cypher_common_tier0_endian_tests)

        add_executable(cypher_common_tier0_error_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Error_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Error.cpp"
        )
        target_include_directories(cypher_common_tier0_error_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_error_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_error_tests COMMAND cypher_common_tier0_error_tests)

        add_executable(cypher_common_tier0_event_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Event_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Event.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
        )
        target_include_directories(cypher_common_tier0_event_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_event_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_event_tests COMMAND cypher_common_tier0_event_tests)

        add_executable(cypher_common_tier0_cpudetect_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_CPUDetect_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUDetect.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
        )
        target_include_directories(cypher_common_tier0_cpudetect_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_cpudetect_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_cpudetect_tests COMMAND cypher_common_tier0_cpudetect_tests)

        add_executable(cypher_common_tier0_simd_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Simd_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUDetect.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Simd.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
        )
        target_include_directories(cypher_common_tier0_simd_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_simd_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_simd_tests COMMAND cypher_common_tier0_simd_tests)

        add_executable(cypher_common_tier0_memoryops_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_MemoryOps_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_MemoryOps.cpp"
        )
        target_include_directories(cypher_common_tier0_memoryops_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_memoryops_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_memoryops_tests COMMAND cypher_common_tier0_memoryops_tests)

        add_executable(cypher_common_tier0_mutex_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Mutex_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Mutex.cpp"
        )
        target_include_directories(cypher_common_tier0_mutex_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_mutex_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_mutex_tests COMMAND cypher_common_tier0_mutex_tests)

        add_executable(cypher_common_tier0_semaphore_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Semaphore_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Semaphore.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
        )
        target_include_directories(cypher_common_tier0_semaphore_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_semaphore_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_semaphore_tests COMMAND cypher_common_tier0_semaphore_tests)

        add_executable(cypher_common_tier0_services_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Services_Tests.cpp"
            ${CYPHERCOMMON_TIER0_SERVICE_SOURCES}
        )
        target_include_directories(cypher_common_tier0_services_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_services_tests PRIVATE Catch2::Catch2WithMain)
        if (CMAKE_DL_LIBS)
            target_link_libraries(cypher_common_tier0_services_tests PRIVATE ${CMAKE_DL_LIBS})
        endif()
        add_test(NAME cypher_common_tier0_services_tests COMMAND cypher_common_tier0_services_tests)

        add_executable(cypher_common_tier0_stacktrace_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_StackTrace_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_StackTrace.cpp"
        )
        target_include_directories(cypher_common_tier0_stacktrace_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_stacktrace_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_stacktrace_tests COMMAND cypher_common_tier0_stacktrace_tests)

        add_executable(cypher_common_tier0_timer_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Timer_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Timer.cpp"
        )
        target_include_directories(cypher_common_tier0_timer_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_timer_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_timer_tests COMMAND cypher_common_tier0_timer_tests)

        add_executable(cypher_common_tier0_tls_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_TLS_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_TLS.cpp"
        )
        target_include_directories(cypher_common_tier0_tls_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_tls_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_tls_tests COMMAND cypher_common_tier0_tls_tests)

        add_executable(cypher_common_tier0_systeminfo_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_SystemInfo_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUDetect.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_SystemInfo.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_Char.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_String.cpp"
        )
        target_include_directories(cypher_common_tier0_systeminfo_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_systeminfo_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_systeminfo_tests COMMAND cypher_common_tier0_systeminfo_tests)

        add_executable(cypher_common_tier0_thread_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier0/CypherCommon_Tier0_Thread_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Timer.cpp"
        )
        target_include_directories(cypher_common_tier0_thread_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier0_thread_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier0_thread_tests COMMAND cypher_common_tier0_thread_tests)

        add_executable(cypher_common_tier1_char_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier1/CypherCommon_Tier1_Char_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_Char.cpp"
        )
        target_include_directories(cypher_common_tier1_char_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier1_char_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier1_char_tests COMMAND cypher_common_tier1_char_tests)

        add_executable(cypher_common_tier1_string_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/Tier1/CypherCommon_Tier1_String_Tests.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_Char.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_String.cpp"
        )
        target_include_directories(cypher_common_tier1_string_tests PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tier1_string_tests PRIVATE Catch2::Catch2WithMain)
        add_test(NAME cypher_common_tier1_string_tests COMMAND cypher_common_tier1_string_tests)
    else()
        message(STATUS "Catch2 not found. Skipping Catch2-based CypherCommon Tier1 string tests.")
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
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUDetect.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_SystemInfo.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_Char.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_String.cpp"
        )
        target_include_directories(cypher_perf_system_report PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})

        add_executable(cypher_common_char_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier1/CypherCommon_Char_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_Char.cpp"
        )
        target_include_directories(cypher_common_char_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_char_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_align_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Align_Bench.cpp"
        )
        target_include_directories(cypher_common_align_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_align_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_atomic_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Atomic_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Atomic.cpp"
        )
        target_include_directories(cypher_common_atomic_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_atomic_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_bits_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Bits_Bench.cpp"
        )
        target_include_directories(cypher_common_bits_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_bits_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_endian_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Endian_Bench.cpp"
        )
        target_include_directories(cypher_common_endian_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_endian_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_event_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Event_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Event.cpp"
        )
        target_include_directories(cypher_common_event_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_event_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_timer_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Timer_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Timer.cpp"
        )
        target_include_directories(cypher_common_timer_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_timer_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_thread_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Thread_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
        )
        target_include_directories(cypher_common_thread_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_thread_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_tls_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_TLS_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_TLS.cpp"
        )
        target_include_directories(cypher_common_tls_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_tls_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_semaphore_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Semaphore_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Semaphore.cpp"
        )
        target_include_directories(cypher_common_semaphore_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_semaphore_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_services_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Services_Bench.cpp"
            ${CYPHERCOMMON_TIER0_SERVICE_SOURCES}
        )
        target_include_directories(cypher_common_services_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_services_bench PRIVATE benchmark::benchmark_main)
        if (CMAKE_DL_LIBS)
            target_link_libraries(cypher_common_services_bench PRIVATE ${CMAKE_DL_LIBS})
        endif()

        add_executable(cypher_common_stacktrace_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_StackTrace_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_StackTrace.cpp"
        )
        target_include_directories(cypher_common_stacktrace_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_stacktrace_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_cpudetect_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_CPUDetect_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUDetect.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
        )
        target_include_directories(cypher_common_cpudetect_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_cpudetect_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_simd_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Simd_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUDetect.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Simd.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
        )
        target_include_directories(cypher_common_simd_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_simd_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_systeminfo_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_SystemInfo_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_CPUDetect.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_SystemInfo.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Thread.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_Char.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_String.cpp"
        )
        target_include_directories(cypher_common_systeminfo_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_systeminfo_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_string_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier1/CypherCommon_String_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_Char.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier1/CypherCommon_String.cpp"
        )
        target_include_directories(cypher_common_string_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_string_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_memoryops_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_MemoryOps_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_MemoryOps.cpp"
        )
        target_include_directories(cypher_common_memoryops_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_memoryops_bench PRIVATE benchmark::benchmark_main)

        add_executable(cypher_common_mutex_bench
            "${CYPHERENGINE_ROOT_DIR}/benchmarks/CypherCommon/Tier0/CypherCommon_Mutex_Bench.cpp"
            "${CYPHERENGINE_SRC_DIR}/CypherCommon/Tier0/CypherCommon_Mutex.cpp"
        )
        target_include_directories(cypher_common_mutex_bench PRIVATE ${CYPHERENGINE_INCLUDE_DIRS})
        target_link_libraries(cypher_common_mutex_bench PRIVATE benchmark::benchmark_main)

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
