# //////////////////////////////////////////////////////////////////////////
# //
# //  CypherEngine Source Code
# //  Copyright (c) 2026 Karlo Siric. All rights reserved.
# //
# //  File: cmake/CypherBootstrapVcpkg.cmake
# //  Purpose: Bootstraps the exact vcpkg revision declared by the project manifest.
# //  Details: This script keeps local and CI dependency acquisition deterministic
# //           without committing vcpkg or downloaded package sources to the engine
# //           repository.
# //
# //  History:
# //  - Created by Karlo Siric on 2026-08-03
# //
# //  This file is proprietary and confidential. See LICENSE for details.
# //
# //////////////////////////////////////////////////////////////////////////

cmake_minimum_required(VERSION 3.20)

get_filename_component(CYPHER_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CYPHER_VCPKG_MANIFEST "${CYPHER_ROOT_DIR}/vcpkg.json")

if (NOT DEFINED CYPHER_VCPKG_ROOT OR CYPHER_VCPKG_ROOT STREQUAL "")
    set(CYPHER_VCPKG_ROOT "${CYPHER_ROOT_DIR}/.deps/vcpkg")
endif()

file(READ "${CYPHER_VCPKG_MANIFEST}" CYPHER_VCPKG_MANIFEST_JSON)
string(JSON CYPHER_VCPKG_COMMIT GET "${CYPHER_VCPKG_MANIFEST_JSON}" "builtin-baseline")

find_program(CYPHER_GIT_EXECUTABLE git REQUIRED)
get_filename_component(CYPHER_VCPKG_PARENT_DIR "${CYPHER_VCPKG_ROOT}" DIRECTORY)
file(MAKE_DIRECTORY "${CYPHER_VCPKG_PARENT_DIR}")

if (EXISTS "${CYPHER_VCPKG_ROOT}" AND NOT EXISTS "${CYPHER_VCPKG_ROOT}/.git")
    message(FATAL_ERROR
        "${CYPHER_VCPKG_ROOT} exists but is not a Git checkout. "
        "Move it aside or set CYPHER_VCPKG_ROOT to a clean location."
    )
endif()

if (NOT EXISTS "${CYPHER_VCPKG_ROOT}/.git")
    execute_process(
        COMMAND "${CYPHER_GIT_EXECUTABLE}" clone --depth=1 --filter=blob:none
                https://github.com/microsoft/vcpkg.git "${CYPHER_VCPKG_ROOT}"
        RESULT_VARIABLE CYPHER_VCPKG_CLONE_RESULT
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()

execute_process(
    COMMAND "${CYPHER_GIT_EXECUTABLE}" -C "${CYPHER_VCPKG_ROOT}" status --porcelain
    OUTPUT_VARIABLE CYPHER_VCPKG_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

if (NOT CYPHER_VCPKG_STATUS STREQUAL "")
    message(FATAL_ERROR
        "The vcpkg checkout at ${CYPHER_VCPKG_ROOT} has local modifications. "
        "CypherEngine will not overwrite them."
    )
endif()

execute_process(
    COMMAND "${CYPHER_GIT_EXECUTABLE}" -C "${CYPHER_VCPKG_ROOT}"
            fetch --depth=1 origin "${CYPHER_VCPKG_COMMIT}"
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND "${CYPHER_GIT_EXECUTABLE}" -C "${CYPHER_VCPKG_ROOT}"
            checkout --detach "${CYPHER_VCPKG_COMMIT}"
    COMMAND_ERROR_IS_FATAL ANY
)

if (WIN32)
    execute_process(
        COMMAND cmd /c "${CYPHER_VCPKG_ROOT}/bootstrap-vcpkg.bat" -disableMetrics
        COMMAND_ERROR_IS_FATAL ANY
    )
    set(CYPHER_VCPKG_EXECUTABLE "${CYPHER_VCPKG_ROOT}/vcpkg.exe")
else()
    execute_process(
        COMMAND sh "${CYPHER_VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
        COMMAND_ERROR_IS_FATAL ANY
    )
    set(CYPHER_VCPKG_EXECUTABLE "${CYPHER_VCPKG_ROOT}/vcpkg")
endif()

if (NOT EXISTS "${CYPHER_VCPKG_EXECUTABLE}")
    message(FATAL_ERROR "vcpkg bootstrap completed without producing ${CYPHER_VCPKG_EXECUTABLE}.")
endif()

message(STATUS "CypherEngine vcpkg root: ${CYPHER_VCPKG_ROOT}")
message(STATUS "CypherEngine vcpkg revision: ${CYPHER_VCPKG_COMMIT}")
