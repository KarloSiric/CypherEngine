# //////////////////////////////////////////////////////////////////////////
# //
# //  CypherEngine Source Code
# //  Copyright (c) 2026 Karlo Siric. All rights reserved.
# //
# //  File: cmake/CypherExpectProcessResult.cmake
# //  Purpose: Verifies an isolated test helper's process termination result.
# //  Details: Normal exits may require an exact code, while traps and aborts only
# //           require abnormal termination. Captured diagnostics are reported on
# //           expectation failure.
# //
# //  History:
# //  - Created by Karlo Siric on 2026-08-12
# //
# //  This file is proprietary and confidential. See LICENSE for details.
# //
# //////////////////////////////////////////////////////////////////////////

cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED CYPHER_TEST_HELPER OR CYPHER_TEST_HELPER STREQUAL "")
    message(FATAL_ERROR "CYPHER_TEST_HELPER must name the death-test executable.")
endif()
if (NOT DEFINED CYPHER_TEST_MODE OR CYPHER_TEST_MODE STREQUAL "")
    message(FATAL_ERROR "CYPHER_TEST_MODE must select one helper operation.")
endif()

execute_process(
    COMMAND "${CYPHER_TEST_HELPER}" "${CYPHER_TEST_MODE}"
    RESULT_VARIABLE CYPHER_TEST_RESULT
    OUTPUT_VARIABLE CYPHER_TEST_STDOUT
    ERROR_VARIABLE CYPHER_TEST_STDERR
)

if (DEFINED CYPHER_EXPECT_EXIT_CODE)
    if (NOT CYPHER_TEST_RESULT MATCHES "^-?[0-9]+$" OR
        NOT CYPHER_TEST_RESULT EQUAL CYPHER_EXPECT_EXIT_CODE)
        message(FATAL_ERROR
            "${CYPHER_TEST_MODE} returned '${CYPHER_TEST_RESULT}', expected "
            "${CYPHER_EXPECT_EXIT_CODE}.\nstdout:\n${CYPHER_TEST_STDOUT}\n"
            "stderr:\n${CYPHER_TEST_STDERR}"
        )
    endif()
elseif (CYPHER_TEST_RESULT STREQUAL "0")
    message(FATAL_ERROR
        "${CYPHER_TEST_MODE} returned normally; abnormal termination was expected.\n"
        "stdout:\n${CYPHER_TEST_STDOUT}\nstderr:\n${CYPHER_TEST_STDERR}"
    )
endif()
