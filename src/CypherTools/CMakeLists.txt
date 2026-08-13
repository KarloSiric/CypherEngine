##########################################################################
#
#  CypherEngine Source Code
#  Copyright (c) 2026 Karlo Siric. All rights reserved.
#
#  File: src/CypherTools/CMakeLists.txt
#  Purpose: Owns build registration for Cypher command-line and Qt tools.
#  Details: Product directories opt in only after they contain a real target.
#           Shared tool behavior remains in CypherCommon/ToolFramework and
#           type-specific compiler core libraries, never in this registry file.
#
#  History:
#  - Created by Karlo Siric on 2026-08-12
#
#  This file is proprietary and confidential. See LICENSE for details.
#
##########################################################################

# Tool product targets are intentionally registered explicitly. Do not glob
# placeholder directories: a product enters the build when its executable or
# reusable implementation library has a concrete source contract and tests.

cypher_configure_shader_tool_dependencies(CYPHER_SHADER_TOOL_LINK_LIBS)

add_library(CypherShaderCompiler STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherShaderCompiler/CypherShaderCompiler.cpp"
)
add_library(Cypher::ShaderCompiler ALIAS CypherShaderCompiler)
target_compile_features(CypherShaderCompiler PUBLIC cxx_std_20)
target_include_directories(CypherShaderCompiler
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherShaderCompiler"
)
target_link_libraries(CypherShaderCompiler
    PUBLIC
        Cypher::RenderFormats
        Cypher::ToolFramework
        Cypher::VirtualFileSystem
    PRIVATE
        ${CYPHER_SHADER_TOOL_LINK_LIBS}
)

add_library(CypherResourceCompilerCore STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherResourceCompiler/CypherResourceCompiler.cpp"
)
add_library(Cypher::ResourceCompilerCore ALIAS CypherResourceCompilerCore)
target_compile_features(CypherResourceCompilerCore PUBLIC cxx_std_20)
target_include_directories(CypherResourceCompilerCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherResourceCompiler"
)
target_link_libraries(CypherResourceCompilerCore
    PUBLIC
        Cypher::ShaderCompiler
)

add_executable(CypherResourceCompiler
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherResourceCompiler/main.cpp"
)
target_compile_features(CypherResourceCompiler PRIVATE cxx_std_20)
target_link_libraries(CypherResourceCompiler PRIVATE Cypher::ResourceCompilerCore)

if (CYPHERENGINE_BUILD_TESTS)
    add_executable(cypher_shader_compiler_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherShaderCompiler/CypherShaderCompiler_Tests.cpp"
    )
    target_link_libraries(cypher_shader_compiler_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::ShaderCompiler
    )
    add_test(NAME cypher_shader_compiler_tests COMMAND cypher_shader_compiler_tests)

    add_test(
        NAME cypher_resource_compiler_help
        COMMAND CypherResourceCompiler --help
    )
    add_test(
        NAME cypher_resource_compiler_version
        COMMAND CypherResourceCompiler --version
    )
    add_test(
        NAME cypher_resource_compiler_list_compilers
        COMMAND CypherResourceCompiler list-compilers --color never
    )
    add_test(
        NAME cypher_resource_compiler_describe_shader
        COMMAND CypherResourceCompiler describe-compiler --color never cypher.shader
    )
    add_test(
        NAME cypher_resource_compiler_list_formats_json
        COMMAND CypherResourceCompiler list-formats --output-format json
    )
    add_test(
        NAME cypher_resource_compiler_smoke
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCYPHER_RESOURCE_COMPILER=$<TARGET_FILE:CypherResourceCompiler>"
            "-DCYPHER_RESOURCE_COMPILER_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/CypherResourceCompilerSmoke"
            -P "${CYPHERENGINE_ROOT_DIR}/cmake/CypherResourceCompilerSmoke.cmake"
    )
    add_test(
        NAME cypher_resource_compiler_shader_corpus
        COMMAND
            CypherResourceCompiler
            validate
            --source-root
            "${CYPHERENGINE_ROOT_DIR}/tests/fixtures/CypherTools/CypherResourceCompiler/ShaderCorpus/source"
            --recursive
            --progress
            none
            --color
            never
            shaders/corpus
    )
    set_tests_properties(
        cypher_resource_compiler_help
        cypher_resource_compiler_version
        cypher_resource_compiler_list_compilers
        cypher_resource_compiler_describe_shader
        cypher_resource_compiler_list_formats_json
        cypher_resource_compiler_smoke
        cypher_resource_compiler_shader_corpus
        PROPERTIES LABELS "CypherTools;Integration"
    )
endif()

set(CYPHER_REGISTERED_TOOL_PRODUCTS
    "CypherShaderCompiler;CypherResourceCompiler"
    CACHE INTERNAL
    "Cypher tool products currently registered with CMake"
    FORCE
)
