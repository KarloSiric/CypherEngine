# //////////////////////////////////////////////////////////////////////////
# //
# //  CypherEngine Source Code
# //  Copyright (c) 2026 Karlo Siric. All rights reserved.
# //
# //  File: cmake/CypherResourceCompilerOutputContract.cmake
# //  Purpose: Verifies ResourceCompiler discovery and presentation output.
# //  Details: Runs the executable as a user would and ensures every registered
# //           compiler remains visible through help, inspection, and completion.
# //
# //  History:
# //  - Created by Karlo Siric on 2026-08-14
# //
# //  This file is proprietary and confidential. See LICENSE for details.
# //
# //////////////////////////////////////////////////////////////////////////

cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED CYPHER_RESOURCE_COMPILER OR
    CYPHER_RESOURCE_COMPILER STREQUAL "")
    message(FATAL_ERROR
        "CYPHER_RESOURCE_COMPILER must name the ResourceCompiler executable."
    )
endif()

function(cypher_run_resource_compiler RESULT_PREFIX)
    execute_process(
        COMMAND "${CYPHER_RESOURCE_COMPILER}" ${ARGN}
        RESULT_VARIABLE CYPHER_RESULT
        OUTPUT_VARIABLE CYPHER_STDOUT
        ERROR_VARIABLE CYPHER_STDERR
    )
    if (NOT CYPHER_RESULT MATCHES "^-?[0-9]+$" OR
        NOT CYPHER_RESULT EQUAL 0)
        message(FATAL_ERROR
            "ResourceCompiler returned '${CYPHER_RESULT}', expected 0.\n"
            "command arguments: ${ARGN}\n"
            "stdout:\n${CYPHER_STDOUT}\n"
            "stderr:\n${CYPHER_STDERR}"
        )
    endif()
    set("${RESULT_PREFIX}_STDOUT" "${CYPHER_STDOUT}" PARENT_SCOPE)
    set("${RESULT_PREFIX}_STDERR" "${CYPHER_STDERR}" PARENT_SCOPE)
endfunction()

function(cypher_require_contains OUTPUT_VALUE OUTPUT_NAME)
    foreach(CYPHER_EXPECTED IN LISTS ARGN)
        string(FIND
            "${OUTPUT_VALUE}"
            "${CYPHER_EXPECTED}"
            CYPHER_EXPECTED_INDEX
        )
        if (CYPHER_EXPECTED_INDEX EQUAL -1)
            message(FATAL_ERROR
                "${OUTPUT_NAME} is missing '${CYPHER_EXPECTED}'.\n"
                "output:\n${OUTPUT_VALUE}"
            )
        endif()
    endforeach()
endfunction()

cypher_run_resource_compiler(CYPHER_HELP --help --color never)
cypher_require_contains(
    "${CYPHER_HELP_STDOUT}"
    "root help"
    "________      ___    ___"
    "R E S O U R C E   C O M P I L E R"
    "3 compiler modules, 3 source formats"
    ".cyshader"
    ".cyshader_c"
    ".cytex"
    ".cytex_c"
    ".cymat"
    ".cymat_c"
    "cypher.shader"
    "cypher.texture"
    "cypher.material"
)

cypher_run_resource_compiler(
    CYPHER_COMPILERS
    list-compilers
    --color
    never
)
cypher_require_contains(
    "${CYPHER_COMPILERS_STDOUT}"
    "compiler listing"
    "REGISTERED COMPILERS"
    "3 compiler modules, 3 source formats"
    "Cypher Shader Compiler"
    "Cypher Texture Compiler"
    "Cypher Material Compiler"
    "source=.cyshader  cooked=.cyshader_c"
    "source=.cytex  cooked=.cytex_c"
    "source=.cymat  cooked=.cymat_c"
)

cypher_run_resource_compiler(
    CYPHER_FORMATS
    list-formats
    --color
    never
)
cypher_require_contains(
    "${CYPHER_FORMATS_STDOUT}"
    "format listing"
    "REGISTERED FORMATS"
    ".cyshader -> .cyshader_c"
    ".cytex -> .cytex_c"
    ".cymat -> .cymat_c"
)

cypher_run_resource_compiler(
    CYPHER_TEXTURE_DESCRIPTION
    describe-compiler
    --color
    never
    cypher.texture
)
cypher_require_contains(
    "${CYPHER_TEXTURE_DESCRIPTION_STDOUT}"
    "texture compiler description"
    "cypher.texture"
    "name=Cypher Texture Compiler"
    "resource=texture"
    "source=.cytex"
    "cooked=.cytex_c"
    "capabilities:"
)

cypher_run_resource_compiler(CYPHER_COMPLETION completion zsh)
cypher_require_contains(
    "${CYPHER_COMPLETION_STDOUT}"
    "zsh completion"
    "cypher.shader"
    "cypher.texture"
    "cypher.material"
)
