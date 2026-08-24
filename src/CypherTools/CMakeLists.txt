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
cypher_configure_texture_tool_dependencies(CYPHER_TEXTURE_TOOL_LINK_LIBS)

# File codecs are an authoring dependency rather than a runtime image concern.
# Keeping them in a separate target lets compilers and GUI tools share imports
# without forcing PNG/JPEG/EXR libraries into every engine executable.
add_library(CypherImageCodec STATIC
    "${CYPHERENGINE_SRC_DIR}/CypherCommon/RenderSystem/Image/CypherCommon_ImageCodec.cpp"
)
add_library(Cypher::ImageCodec ALIAS CypherImageCodec)
target_compile_features(CypherImageCodec PUBLIC cxx_std_20)
target_include_directories(CypherImageCodec
    PUBLIC
        "${CYPHERENGINE_SRC_DIR}/CypherCommon/RenderSystem/Image"
)
target_link_libraries(CypherImageCodec
    PUBLIC
        Cypher::Image
    PRIVATE
        ${CYPHER_TEXTURE_TOOL_LINK_LIBS}
)

add_library(PicassoCore STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Core/PicassoChannel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Core/PicassoMaterialImport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Core/PicassoPaint.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Core/PicassoPaintMaterial.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Core/PicassoTextureDocument.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Core/PicassoTextureSet.cpp"
)
add_library(Cypher::PicassoCore ALIAS PicassoCore)
target_compile_features(PicassoCore PUBLIC cxx_std_20)
target_include_directories(PicassoCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Core"
)
target_link_libraries(PicassoCore
    PUBLIC
        Cypher::ImageCodec
        Cypher::RenderFormats
    PRIVATE
        Cypher::CommonTier2
)

option(CYPHERENGINE_BUILD_PICASSO "Build the Qt6 Picasso authoring tool" OFF)
if (CYPHERENGINE_BUILD_PICASSO)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Svg Widgets)

    add_library(PicassoGui STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/PicassoResources.qrc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Gui/PicassoCanvas.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Gui/PicassoConsole.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Gui/PicassoIcons.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Gui/PicassoMainWindow.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Gui/PicassoTheme.cpp"
    )
    add_library(Cypher::PicassoGui ALIAS PicassoGui)
    target_compile_features(PicassoGui PUBLIC cxx_std_20)
    target_include_directories(PicassoGui
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/Picasso"
            "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/Gui"
    )
    target_link_libraries(PicassoGui
        PUBLIC
            Cypher::PicassoCore
            Qt6::Core
            Qt6::Gui
            Qt6::Svg
            Qt6::Widgets
    )
    set_target_properties(PicassoGui PROPERTIES AUTORCC ON)

    add_executable(Picasso
        "${CMAKE_CURRENT_SOURCE_DIR}/Picasso/main.cpp"
    )
    add_executable(Cypher::Picasso ALIAS Picasso)
    target_compile_features(Picasso PRIVATE cxx_std_20)
    target_link_libraries(Picasso
        PRIVATE
            Cypher::PicassoGui
    )
endif()

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

add_library(CypherTextureCompiler STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTextureCompiler/CypherTextureCompiler.cpp"
)
add_library(Cypher::TextureCompiler ALIAS CypherTextureCompiler)
target_compile_features(CypherTextureCompiler PUBLIC cxx_std_20)
target_include_directories(CypherTextureCompiler
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTextureCompiler"
)
target_link_libraries(CypherTextureCompiler
    PUBLIC
        Cypher::RenderFormats
        Cypher::ToolFramework
        Cypher::VirtualFileSystem
        Cypher::ImageCodec
    PRIVATE
        ${CYPHER_TEXTURE_TOOL_LINK_LIBS}
)

add_library(CypherMaterialCompiler STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherMaterialCompiler/CypherMaterialCompiler.cpp"
)
add_library(Cypher::MaterialCompiler ALIAS CypherMaterialCompiler)
target_compile_features(CypherMaterialCompiler PUBLIC cxx_std_20)
target_include_directories(CypherMaterialCompiler
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherMaterialCompiler"
)
target_link_libraries(CypherMaterialCompiler
    PUBLIC
        Cypher::RenderFormats
        Cypher::ToolFramework
        Cypher::VirtualFileSystem
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
        Cypher::MaterialCompiler
        Cypher::ShaderCompiler
        Cypher::TextureCompiler
    PRIVATE
        Cypher::VfsDirectory
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
            Cypher::VfsDirectory
    )
    add_test(NAME cypher_shader_compiler_tests COMMAND cypher_shader_compiler_tests)

    add_executable(cypher_texture_compiler_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTextureCompiler/CypherTextureCompiler_Tests.cpp"
    )
    target_link_libraries(cypher_texture_compiler_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::TextureCompiler
            Cypher::ThirdPartyImageImport
            Cypher::VfsDirectory
    )
    add_test(NAME cypher_texture_compiler_tests COMMAND cypher_texture_compiler_tests)

    add_executable(cypher_common_image_codec_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherCommon/RenderSystem/Image/CypherCommon_ImageCodec_Tests.cpp"
    )
    target_link_libraries(cypher_common_image_codec_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::ImageCodec
            Cypher::ThirdPartyImageImport
    )
    add_test(NAME cypher_common_image_codec_tests COMMAND cypher_common_image_codec_tests)

    add_executable(cypher_picasso_texture_document_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/Picasso/PicassoTextureDocument_Tests.cpp"
    )
    target_link_libraries(cypher_picasso_texture_document_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::PicassoCore
    )
    add_test(
        NAME cypher_picasso_texture_document_tests
        COMMAND cypher_picasso_texture_document_tests
    )

    add_executable(cypher_picasso_authoring_core_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/Picasso/PicassoAuthoringCore_Tests.cpp"
    )
    target_link_libraries(cypher_picasso_authoring_core_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::PicassoCore
    )
    add_test(
        NAME cypher_picasso_authoring_core_tests
        COMMAND cypher_picasso_authoring_core_tests
    )

    if (CYPHERENGINE_BUILD_PICASSO)
        add_executable(cypher_picasso_console_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/Picasso/PicassoConsole_Tests.cpp"
        )
        target_link_libraries(cypher_picasso_console_tests
            PRIVATE
                Catch2::Catch2WithMain
                Cypher::PicassoGui
        )
        add_test(
            NAME cypher_picasso_console_tests
            COMMAND cypher_picasso_console_tests
        )
        set_tests_properties(cypher_picasso_console_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()

    add_executable(cypher_material_compiler_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler_Tests.cpp"
    )
    target_link_libraries(cypher_material_compiler_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::MaterialCompiler
            Cypher::VfsDirectory
    )
    add_test(NAME cypher_material_compiler_tests COMMAND cypher_material_compiler_tests)

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
        NAME cypher_resource_compiler_describe_texture
        COMMAND CypherResourceCompiler describe-compiler --color never cypher.texture
    )
    add_test(
        NAME cypher_resource_compiler_describe_material
        COMMAND CypherResourceCompiler describe-compiler --color never cypher.material
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
        NAME cypher_resource_compiler_output_contract
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCYPHER_RESOURCE_COMPILER=$<TARGET_FILE:CypherResourceCompiler>"
            -P "${CYPHERENGINE_ROOT_DIR}/cmake/CypherResourceCompilerOutputContract.cmake"
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
        cypher_resource_compiler_describe_texture
        cypher_resource_compiler_describe_material
        cypher_resource_compiler_list_formats_json
        cypher_resource_compiler_smoke
        cypher_resource_compiler_output_contract
        cypher_resource_compiler_shader_corpus
        PROPERTIES LABELS "CypherTools;Integration"
    )
endif()

set(CYPHER_REGISTERED_TOOL_PRODUCTS_VALUE
    "CypherShaderCompiler;CypherTextureCompiler;CypherMaterialCompiler;CypherResourceCompiler"
)
if (CYPHERENGINE_BUILD_PICASSO)
    list(APPEND CYPHER_REGISTERED_TOOL_PRODUCTS_VALUE "Picasso")
endif()
set(CYPHER_REGISTERED_TOOL_PRODUCTS
    "${CYPHER_REGISTERED_TOOL_PRODUCTS_VALUE}"
    CACHE INTERNAL
    "Cypher tool products currently registered with CMake"
    FORCE
)
