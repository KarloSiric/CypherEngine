# //////////////////////////////////////////////////////////////////////////
# //
# //  CypherEngine Source Code
# //  Copyright (c) 2026 Karlo Siric. All rights reserved.
# //
# //  File: cmake/CypherResourceCompilerSmoke.cmake
# //  Purpose: Exercises CypherResourceCompiler as a real command-line process.
# //  Details: The smoke test validates VFS discovery, cooks deterministic output,
# //           checks completion and presentation contracts, and confirms malformed
# //           authored data returns precise automation-safe diagnostics.
# //
# //  History:
# //  - Created by Karlo Siric on 2026-08-12
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
if (NOT DEFINED CYPHER_RESOURCE_COMPILER_WORK_DIR OR
    CYPHER_RESOURCE_COMPILER_WORK_DIR STREQUAL "")
    message(FATAL_ERROR
        "CYPHER_RESOURCE_COMPILER_WORK_DIR must name a temporary directory."
    )
endif()

set(CYPHER_SOURCE_ROOT
    "${CYPHER_RESOURCE_COMPILER_WORK_DIR}/source"
)
set(CYPHER_OUTPUT_ROOT
    "${CYPHER_RESOURCE_COMPILER_WORK_DIR}/output"
)
set(CYPHER_SHADER_OUTPUT
    "${CYPHER_OUTPUT_ROOT}/shaders/world.cyshader_c"
)
set(CYPHER_SECOND_SHADER_OUTPUT
    "${CYPHER_OUTPUT_ROOT}/shaders/sub/secondary.cyshader_c"
)
set(CYPHER_MATERIAL_OUTPUT
    "${CYPHER_OUTPUT_ROOT}/materials/world.cymat_c"
)

file(REMOVE_RECURSE "${CYPHER_RESOURCE_COMPILER_WORK_DIR}")
file(MAKE_DIRECTORY
    "${CYPHER_SOURCE_ROOT}/materials"
    "${CYPHER_SOURCE_ROOT}/shaders"
    "${CYPHER_SOURCE_ROOT}/shaders/sub"
    "${CYPHER_SOURCE_ROOT}/textures"
)

file(WRITE "${CYPHER_SOURCE_ROOT}/shaders/world.cyshader" [=[@cykv 1
@schema "cypher.shader" 1
{
    language = "glsl"
    vertex = "shaders/world.vert"
    fragment = "shaders/world.frag"
    defines = ["CY_WORLD_PASS"]
}
]=])
file(WRITE "${CYPHER_SOURCE_ROOT}/shaders/world.vert" [=[#version 410 core
layout(location = 0) out vec2 vUv;
#if CY_WORLD_PASS
const float kWorldPass = 1.0;
#else
const float kWorldPass = 0.0;
#endif
void main()
{
    vUv = vec2(kWorldPass);
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
]=])
file(WRITE "${CYPHER_SOURCE_ROOT}/shaders/world.frag" [=[#version 410 core
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main()
{
    outColor = vec4(vUv, 0.0, 1.0);
}
]=])
file(WRITE "${CYPHER_SOURCE_ROOT}/shaders/sub/secondary.cyshader" [=[@cykv 1
@schema "cypher.shader" 1
{
    language = "glsl"
    vertex = "shaders/world.vert"
    fragment = "shaders/world.frag"
    defines = ["CY_SECONDARY_PASS"]
}
]=])
file(WRITE "${CYPHER_SOURCE_ROOT}/textures/world.cytex" [=[@cykv 1
@schema "cypher.texture" 1
{
    source = "textures/source/world.png"
    usage = "color"
    color_space = "srgb"
    generate_mips = true
}
]=])
file(WRITE "${CYPHER_SOURCE_ROOT}/materials/world.cymat" [=[@cykv 1
@schema "cypher.material" 1
{
    shader = "shaders/world.cyshader"
    textures = {
        AlbedoMap = "textures/world.cytex"
    }
    parameters = {
        Roughness = 0.5
        Tint = [1.0, 0.75, 0.5]
    }
}
]=])

function(cypher_run_success RESULT_PREFIX)
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
            "stdout:\n${CYPHER_STDOUT}\n"
            "stderr:\n${CYPHER_STDERR}"
        )
    endif()
    set("${RESULT_PREFIX}_STDOUT" "${CYPHER_STDOUT}" PARENT_SCOPE)
    set("${RESULT_PREFIX}_STDERR" "${CYPHER_STDERR}" PARENT_SCOPE)
endfunction()

# Product identity and discovery are process contracts, not decoration. Keep
# these checks ahead of compilation so registry/help failures remain isolated.
cypher_run_success(CYPHER_HELP --help)
string(FIND "${CYPHER_HELP_STDOUT}" "________      ___    ___" CYPHER_BANNER_INDEX)
string(FIND "${CYPHER_HELP_STDOUT}" "OFFLINE ASSET TOOLCHAIN  1.0.0" CYPHER_BANNER_VERSION_INDEX)
string(FIND "${CYPHER_HELP_STDOUT}" "Copyright (c) 2026 Karlo Siric" CYPHER_COPYRIGHT_INDEX)
string(FIND "${CYPHER_HELP_STDOUT}" "Proprietary and confidential" CYPHER_LICENSE_INDEX)
string(FIND "${CYPHER_HELP_STDOUT}" "list-compilers" CYPHER_HELP_COMPILERS_INDEX)
string(FIND "${CYPHER_HELP_STDOUT}" "CAPABILITIES" CYPHER_HELP_CAPABILITIES_INDEX)
if (CYPHER_BANNER_INDEX EQUAL -1 OR
    CYPHER_BANNER_VERSION_INDEX EQUAL -1 OR
    CYPHER_COPYRIGHT_INDEX EQUAL -1 OR
    CYPHER_LICENSE_INDEX EQUAL -1 OR
    CYPHER_HELP_COMPILERS_INDEX EQUAL -1 OR
    CYPHER_HELP_CAPABILITIES_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Root help is missing branded identity or command information.\n"
        "stdout:\n${CYPHER_HELP_STDOUT}"
    )
endif()

# The value printed after --target is a metavar, not a literal target. Keep the
# accepted target set descriptor-driven so a copied placeholder cannot fall
# through and produce the unrelated missing-input diagnostic.
execute_process(
    COMMAND "${CYPHER_RESOURCE_COMPILER}"
        validate
        --target PLATFORM-ARCH
        --color never
    RESULT_VARIABLE CYPHER_BAD_TARGET_RESULT
    OUTPUT_VARIABLE CYPHER_BAD_TARGET_STDOUT
    ERROR_VARIABLE CYPHER_BAD_TARGET_STDERR
)
if (NOT CYPHER_BAD_TARGET_RESULT MATCHES "^-?[0-9]+$" OR
    NOT CYPHER_BAD_TARGET_RESULT EQUAL 2)
    message(FATAL_ERROR
        "Invalid target returned '${CYPHER_BAD_TARGET_RESULT}', expected 2.\n"
        "stdout:\n${CYPHER_BAD_TARGET_STDOUT}\n"
        "stderr:\n${CYPHER_BAD_TARGET_STDERR}"
    )
endif()
string(FIND "${CYPHER_BAD_TARGET_STDERR}" "invalid option value" CYPHER_BAD_TARGET_INDEX)
string(FIND "${CYPHER_BAD_TARGET_STDERR}" "At least one input" CYPHER_BAD_TARGET_INPUT_INDEX)
if (CYPHER_BAD_TARGET_INDEX EQUAL -1 OR
    NOT CYPHER_BAD_TARGET_INPUT_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Invalid target was not diagnosed before input validation.\n"
        "stderr:\n${CYPHER_BAD_TARGET_STDERR}"
    )
endif()

execute_process(
    COMMAND "${CYPHER_RESOURCE_COMPILER}"
        validate
        --target host
        --color never
    RESULT_VARIABLE CYPHER_MISSING_INPUT_RESULT
    OUTPUT_VARIABLE CYPHER_MISSING_INPUT_STDOUT
    ERROR_VARIABLE CYPHER_MISSING_INPUT_STDERR
)
if (NOT CYPHER_MISSING_INPUT_RESULT MATCHES "^-?[0-9]+$" OR
    NOT CYPHER_MISSING_INPUT_RESULT EQUAL 2)
    message(FATAL_ERROR
        "Missing input returned '${CYPHER_MISSING_INPUT_RESULT}', expected 2.\n"
        "stdout:\n${CYPHER_MISSING_INPUT_STDOUT}\n"
        "stderr:\n${CYPHER_MISSING_INPUT_STDERR}"
    )
endif()
string(FIND "${CYPHER_MISSING_INPUT_STDERR}" "At least one input" CYPHER_MISSING_INPUT_INDEX)
if (CYPHER_MISSING_INPUT_INDEX EQUAL -1)
    message(FATAL_ERROR
        "A valid target with no input omitted the missing-input diagnostic.\n"
        "stderr:\n${CYPHER_MISSING_INPUT_STDERR}"
    )
endif()

cypher_run_success(CYPHER_VERSION --version)
string(STRIP "${CYPHER_VERSION_STDOUT}" CYPHER_VERSION_TEXT)
if (NOT CYPHER_VERSION_TEXT STREQUAL "CypherResourceCompiler 1.0.0")
    message(FATAL_ERROR
        "Unexpected ResourceCompiler version output: '${CYPHER_VERSION_TEXT}'."
    )
endif()

cypher_run_success(CYPHER_COMPILERS list-compilers --color never)
string(FIND "${CYPHER_COMPILERS_STDOUT}" "cypher.shader" CYPHER_SHADER_COMPILER_INDEX)
string(FIND "${CYPHER_COMPILERS_STDOUT}" "cypher.texture" CYPHER_TEXTURE_COMPILER_INDEX)
string(FIND "${CYPHER_COMPILERS_STDOUT}" "cypher.material" CYPHER_MATERIAL_COMPILER_INDEX)
if (CYPHER_SHADER_COMPILER_INDEX EQUAL -1 OR
    CYPHER_TEXTURE_COMPILER_INDEX EQUAL -1 OR
    CYPHER_MATERIAL_COMPILER_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Compiler discovery omitted a registered render-asset compiler.\n"
        "stdout:\n${CYPHER_COMPILERS_STDOUT}"
    )
endif()

cypher_run_success(CYPHER_COMPLETION completion zsh)
string(FIND "${CYPHER_COMPLETION_STDOUT}" "#compdef CypherResourceCompiler" CYPHER_COMPLETION_HEADER_INDEX)
string(FIND "${CYPHER_COMPLETION_STDOUT}" "_cypher_resource_compiler" CYPHER_COMPLETION_FUNCTION_INDEX)
string(FIND "${CYPHER_COMPLETION_STDOUT}" "--source-root" CYPHER_COMPLETION_SOURCE_ROOT_INDEX)
if (CYPHER_COMPLETION_HEADER_INDEX EQUAL -1 OR
    CYPHER_COMPLETION_FUNCTION_INDEX EQUAL -1 OR
    CYPHER_COMPLETION_SOURCE_ROOT_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Generated zsh completion omitted required definitions.\n"
        "stdout:\n${CYPHER_COMPLETION_STDOUT}"
    )
endif()

cypher_run_success(CYPHER_COMPILER_DESCRIPTION
    describe-compiler --color never cypher.shader
)
string(FIND "${CYPHER_COMPILER_DESCRIPTION_STDOUT}" "deterministic=yes" CYPHER_DETERMINISTIC_INDEX)
string(FIND "${CYPHER_COMPILER_DESCRIPTION_STDOUT}" "source=.cyshader" CYPHER_SOURCE_EXTENSIONS_INDEX)
if (CYPHER_DETERMINISTIC_INDEX EQUAL -1 OR CYPHER_SOURCE_EXTENSIONS_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Compiler description omitted capabilities or source extensions.\n"
        "stdout:\n${CYPHER_COMPILER_DESCRIPTION_STDOUT}"
    )
endif()

cypher_run_success(CYPHER_FORMATS_JSON list-formats --output-format json)
string(STRIP "${CYPHER_FORMATS_JSON_STDOUT}" CYPHER_FORMATS_JSON_RECORD)
string(JSON CYPHER_FORMATS_JSON_TYPE
    ERROR_VARIABLE CYPHER_FORMATS_JSON_ERROR
    TYPE "${CYPHER_FORMATS_JSON_RECORD}"
)
string(FIND "${CYPHER_FORMATS_JSON_RECORD}" ".cyshader -> .cyshader_c" CYPHER_FORMAT_MAPPING_INDEX)
string(FIND "${CYPHER_FORMATS_JSON_RECORD}" ".cytex -> .cytex_c" CYPHER_TEXTURE_FORMAT_INDEX)
string(FIND "${CYPHER_FORMATS_JSON_RECORD}" ".cymat -> .cymat_c" CYPHER_MATERIAL_FORMAT_INDEX)
string(ASCII 27 CYPHER_ESCAPE)
string(FIND "${CYPHER_FORMATS_JSON_RECORD}" "${CYPHER_ESCAPE}" CYPHER_FORMAT_ANSI_INDEX)
if (CYPHER_FORMATS_JSON_ERROR OR
    NOT CYPHER_FORMATS_JSON_TYPE STREQUAL "OBJECT" OR
    CYPHER_FORMAT_MAPPING_INDEX EQUAL -1 OR
    CYPHER_TEXTURE_FORMAT_INDEX EQUAL -1 OR
    CYPHER_MATERIAL_FORMAT_INDEX EQUAL -1 OR
    NOT CYPHER_FORMAT_ANSI_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Machine-readable format discovery is invalid or contaminated.\n"
        "stdout:\n${CYPHER_FORMATS_JSON_STDOUT}\n"
        "parser: ${CYPHER_FORMATS_JSON_ERROR}"
    )
endif()

cypher_run_success(CYPHER_VALIDATE
    validate
    --source-root "${CYPHER_SOURCE_ROOT}"
    --output-root "${CYPHER_OUTPUT_ROOT}"
    --progress none
    --color never
    shaders/world.cyshader
)
if (EXISTS "${CYPHER_OUTPUT_ROOT}")
    message(FATAL_ERROR "The validate command created the output tree.")
endif()
if (EXISTS "${CYPHER_SHADER_OUTPUT}")
    message(FATAL_ERROR "The validate command wrote a cooked artifact.")
endif()

# Literal wildcard patterns reach the executable unchanged here. This protects
# tool-owned wildcard expansion independently of any host shell behavior.
cypher_run_success(CYPHER_VALIDATE_GLOB
    validate
    --source-root "${CYPHER_SOURCE_ROOT}"
    --progress none
    --color never
    "shaders/*.cyshader"
)
string(FIND "${CYPHER_VALIDATE_GLOB_STDOUT}" "1 processed  |  1 succeeded" CYPHER_GLOB_TOTAL_INDEX)
if (CYPHER_GLOB_TOTAL_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Literal wildcard discovery did not validate the expected input.\n"
        "stdout:\n${CYPHER_VALIDATE_GLOB_STDOUT}"
    )
endif()

cypher_run_success(CYPHER_VALIDATE_REPEATABLE_INPUT
    validate
    --source-root "${CYPHER_SOURCE_ROOT}"
    --progress none
    --color never
    -i shaders/world.cyshader
    -i shaders/sub/secondary.cyshader
)
string(FIND "${CYPHER_VALIDATE_REPEATABLE_INPUT_STDOUT}" "2 processed  |  2 succeeded" CYPHER_REPEATABLE_TOTAL_INDEX)
if (CYPHER_REPEATABLE_TOTAL_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Repeatable -i input discovery did not validate both inputs.\n"
        "stdout:\n${CYPHER_VALIDATE_REPEATABLE_INPUT_STDOUT}"
    )
endif()

cypher_run_success(CYPHER_COMPILE_FIRST
    compile
    --source-root "${CYPHER_SOURCE_ROOT}"
    --output-root "${CYPHER_OUTPUT_ROOT}"
    --recursive
    --progress plain
    --color never
    shaders
)
string(FIND "${CYPHER_COMPILE_FIRST_STDOUT}" "[================================]" CYPHER_PROGRESS_BAR_INDEX)
string(FIND "${CYPHER_COMPILE_FIRST_STDOUT}" "RESULT  OK" CYPHER_RESULT_INDEX)
string(FIND "${CYPHER_COMPILE_FIRST_STDOUT}" "Diagnostics" CYPHER_SUMMARY_INDEX)
string(FIND "${CYPHER_COMPILE_FIRST_STDOUT}" "2 processed  |  2 succeeded  |  0 failed  |  0 skipped" CYPHER_TOTALS_INDEX)
string(FIND "${CYPHER_COMPILE_FIRST_STDOUT}" "CypherResourceCompiler 1.0.0" CYPHER_EXECUTION_IDENTITY_INDEX)
string(FIND "${CYPHER_COMPILE_FIRST_STDOUT}" "________      ___    ___" CYPHER_EXECUTION_BANNER_INDEX)
if (CYPHER_PROGRESS_BAR_INDEX EQUAL -1 OR
    CYPHER_RESULT_INDEX EQUAL -1 OR
    CYPHER_SUMMARY_INDEX EQUAL -1 OR
    CYPHER_TOTALS_INDEX EQUAL -1 OR
    CYPHER_EXECUTION_IDENTITY_INDEX EQUAL -1 OR
    NOT CYPHER_EXECUTION_BANNER_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Text mode presentation, aggregate totals, or banner policy regressed.\n"
        "stdout:\n${CYPHER_COMPILE_FIRST_STDOUT}"
    )
endif()
if (NOT EXISTS "${CYPHER_SHADER_OUTPUT}")
    message(FATAL_ERROR "The compile command did not write world.cyshader_c.")
endif()
if (NOT EXISTS "${CYPHER_SECOND_SHADER_OUTPUT}")
    message(FATAL_ERROR
        "Recursive directory compilation did not write secondary.cyshader_c."
    )
endif()
file(GLOB CYPHER_TEMPORARY_OUTPUTS "${CYPHER_SHADER_OUTPUT}.cytmp.*")
if (CYPHER_TEMPORARY_OUTPUTS)
    message(FATAL_ERROR
        "The compile command left temporary artifact files behind."
    )
endif()
file(SHA256 "${CYPHER_SHADER_OUTPUT}" CYPHER_FIRST_HASH)

cypher_run_success(CYPHER_COMPILE_SECOND
    compile
    --source-root "${CYPHER_SOURCE_ROOT}"
    --output-root "${CYPHER_OUTPUT_ROOT}"
    --progress none
    --color never
    shaders/world.cyshader
)
file(SHA256 "${CYPHER_SHADER_OUTPUT}" CYPHER_SECOND_HASH)
if (NOT CYPHER_FIRST_HASH STREQUAL CYPHER_SECOND_HASH)
    message(FATAL_ERROR
        "Identical shader inputs produced different cooked file hashes."
    )
endif()

# Material compilation is exercised through the process registry and the same
# VFS used by command-line discovery. The material compiler validates its typed
# shader and texture source dependencies without recursively cooking them.
cypher_run_success(CYPHER_COMPILE_MATERIAL
    compile
    --source-root "${CYPHER_SOURCE_ROOT}"
    --output-root "${CYPHER_OUTPUT_ROOT}"
    --progress none
    --color never
    materials/world.cymat
)
if (NOT EXISTS "${CYPHER_MATERIAL_OUTPUT}")
    message(FATAL_ERROR
        "The material compiler did not write materials/world.cymat_c.\n"
        "stdout:\n${CYPHER_COMPILE_MATERIAL_STDOUT}"
    )
endif()
string(FIND "${CYPHER_COMPILE_MATERIAL_STDOUT}" "1 processed  |  1 succeeded" CYPHER_MATERIAL_TOTAL_INDEX)
if (CYPHER_MATERIAL_TOTAL_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Material process compilation omitted successful aggregate totals.\n"
        "stdout:\n${CYPHER_COMPILE_MATERIAL_STDOUT}"
    )
endif()

cypher_run_success(CYPHER_JSON
    validate
    --source-root "${CYPHER_SOURCE_ROOT}"
    --output-root "${CYPHER_OUTPUT_ROOT}"
    --output-format json
    shaders/world.cyshader
)
string(FIND "${CYPHER_JSON_STDOUT}" "\"type\":\"progress\"" CYPHER_PROGRESS_INDEX)
string(FIND "${CYPHER_JSON_STDOUT}" "\"cypher.tool-report.v1\"" CYPHER_REPORT_INDEX)
if (CYPHER_PROGRESS_INDEX EQUAL -1 OR CYPHER_REPORT_INDEX EQUAL -1)
    message(FATAL_ERROR
        "JSON mode did not emit progress and report records.\n"
        "stdout:\n${CYPHER_JSON_STDOUT}"
    )
endif()

# JSON mode is a newline-delimited record stream so shell tools, CI, and Mason
# can consume records incrementally without buffering one large document.
string(REPLACE "\r\n" "\n" CYPHER_JSON_LINES "${CYPHER_JSON_STDOUT}")
string(REPLACE "\r" "\n" CYPHER_JSON_LINES "${CYPHER_JSON_LINES}")
string(REGEX MATCHALL "[^\n]+" CYPHER_JSON_RECORDS "${CYPHER_JSON_LINES}")
foreach (CYPHER_JSON_RECORD IN LISTS CYPHER_JSON_RECORDS)
    string(JSON CYPHER_JSON_RECORD_TYPE
        ERROR_VARIABLE CYPHER_JSON_RECORD_ERROR
        TYPE "${CYPHER_JSON_RECORD}"
    )
    if (CYPHER_JSON_RECORD_ERROR OR NOT CYPHER_JSON_RECORD_TYPE STREQUAL "OBJECT")
        message(FATAL_ERROR
            "JSON mode emitted an invalid record:\n${CYPHER_JSON_RECORD}\n"
            "parser: ${CYPHER_JSON_RECORD_ERROR}"
        )
    endif()
endforeach()

cypher_run_success(CYPHER_ANSI
    validate
    --source-root "${CYPHER_SOURCE_ROOT}"
    --output-root "${CYPHER_OUTPUT_ROOT}"
    --progress none
    --color always
    shaders/world.cyshader
)
string(ASCII 27 CYPHER_ESCAPE)
string(FIND "${CYPHER_ANSI_STDOUT}" "${CYPHER_ESCAPE}[" CYPHER_COLOR_INDEX)
if (CYPHER_COLOR_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Forced color mode did not emit an ANSI color sequence."
    )
endif()

file(WRITE "${CYPHER_SOURCE_ROOT}/shaders/bad-schema.cyshader" [=[@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    vertex = "shaders/world.vert"
    fragment = "shaders/world.frag"
}
]=])
execute_process(
    COMMAND "${CYPHER_RESOURCE_COMPILER}"
        validate
        --source-root "${CYPHER_SOURCE_ROOT}"
        --progress none
        --color never
        shaders/bad-schema.cyshader
    RESULT_VARIABLE CYPHER_SCHEMA_FAILURE_RESULT
    OUTPUT_VARIABLE CYPHER_SCHEMA_FAILURE_STDOUT
    ERROR_VARIABLE CYPHER_SCHEMA_FAILURE_STDERR
)
if (NOT CYPHER_SCHEMA_FAILURE_RESULT MATCHES "^-?[0-9]+$" OR
    NOT CYPHER_SCHEMA_FAILURE_RESULT EQUAL 1)
    message(FATAL_ERROR
        "Schema mismatch returned '${CYPHER_SCHEMA_FAILURE_RESULT}', expected 1.\n"
        "stdout:\n${CYPHER_SCHEMA_FAILURE_STDOUT}\n"
        "stderr:\n${CYPHER_SCHEMA_FAILURE_STDERR}"
    )
endif()
string(FIND
    "${CYPHER_SCHEMA_FAILURE_STDERR}"
    "shaders/bad-schema.cyshader:2:25: error["
    CYPHER_SCHEMA_LOCATION_INDEX
)
if (CYPHER_SCHEMA_LOCATION_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Schema mismatch did not identify its exact source token.\n"
        "stderr:\n${CYPHER_SCHEMA_FAILURE_STDERR}"
    )
endif()

file(WRITE "${CYPHER_SOURCE_ROOT}/shaders/world.vert" [=[#version 410 core
void main( {
]=])
execute_process(
    COMMAND "${CYPHER_RESOURCE_COMPILER}"
        validate
        --source-root "${CYPHER_SOURCE_ROOT}"
        --output-root "${CYPHER_OUTPUT_ROOT}"
        --progress none
        --color never
        shaders/world.cyshader
    RESULT_VARIABLE CYPHER_FAILURE_RESULT
    OUTPUT_VARIABLE CYPHER_FAILURE_STDOUT
    ERROR_VARIABLE CYPHER_FAILURE_STDERR
)
if (NOT CYPHER_FAILURE_RESULT MATCHES "^-?[0-9]+$" OR
    NOT CYPHER_FAILURE_RESULT EQUAL 1)
    message(FATAL_ERROR
        "Invalid GLSL returned '${CYPHER_FAILURE_RESULT}', expected 1.\n"
        "stdout:\n${CYPHER_FAILURE_STDOUT}\n"
        "stderr:\n${CYPHER_FAILURE_STDERR}"
    )
endif()
string(FIND "${CYPHER_FAILURE_STDERR}" "error[" CYPHER_ERROR_INDEX)
if (CYPHER_ERROR_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Invalid GLSL did not emit an error diagnostic to stderr.\n"
        "stderr:\n${CYPHER_FAILURE_STDERR}"
    )
endif()

file(REMOVE_RECURSE "${CYPHER_RESOURCE_COMPILER_WORK_DIR}")
