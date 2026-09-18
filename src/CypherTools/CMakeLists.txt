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

# CypherTileMapCore owns the authored map document shared by the standalone
# Qt editor, the future in-game ImGui editor, and the map compiler. Its public
# headers expose Tier1 allocator, vector, and unique-ID types, so CommonTier1
# must remain a PUBLIC dependency for downstream include and link propagation.
add_library(CypherTileMapCore STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileCamera.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileCamera.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileMapDocument.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileMapDocument.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileMapGeometry.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileMapGeometry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileMapMaterials.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileMapMaterials.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileMapSerialization.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileMapSerialization.cpp"
)
add_library(Cypher::TileMapCore ALIAS CypherTileMapCore)
target_compile_features(CypherTileMapCore PUBLIC cxx_std_20)
target_include_directories(CypherTileMapCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core"
)
target_link_libraries(CypherTileMapCore
    PUBLIC
        Cypher::CommonTier1
        Cypher::Math
    PRIVATE
        Cypher::CommonTier2
        Cypher::EditorGeometry
)

# Shared editor/runtime adapter for validated cooked material assets.
add_library(CypherTileMaterialPreview STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Core/CypherTileMaterialPreview.cpp")
add_library(Cypher::TileMaterialPreview ALIAS CypherTileMaterialPreview)
target_compile_features(CypherTileMaterialPreview PUBLIC cxx_std_20)
target_link_libraries(CypherTileMaterialPreview PUBLIC Cypher::TileMapCore Cypher::Render
    Cypher::RenderFormats Cypher::VfsDirectory)

option(CYPHERENGINE_BUILD_TILE_EDITOR "Build the Qt6 CypherTileEditor authoring tool" OFF)
if (CYPHERENGINE_BUILD_TILE_EDITOR)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui OpenGLWidgets Svg Widgets)

    add_library(CypherTileEditorGui STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileMapProperties.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorConfig.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTilePiecePalette.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileMaterialBrowser.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileOrthoMaterials.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileSelectionMaterialInspector.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileSelectionMaterialInspector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileHistoryPanel.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileHistoryPanel.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/CypherTileEditorResources.qrc"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorTheme.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorTheme.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorUserThemes.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorUserThemes.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorIcons.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorIcons.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorSettingsDialog.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorSettingsDialog.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileConsoleColors.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileConsole.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileConsole.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileShellRunner.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileShellRunner.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileDocumentBridge.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileDocumentBridge.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileOrthoCamera.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileViewportColors.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileCanvas.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileCanvas.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileGrid.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileOrthoView.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileOrthoView.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileViewWorkspace.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileViewWorkspace.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileRenderViewport.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileRenderViewport.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorMainWindow.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui/CypherTileEditorMainWindow.cpp"
    )
    add_library(Cypher::TileEditorGui ALIAS CypherTileEditorGui)
    target_compile_features(CypherTileEditorGui PUBLIC cxx_std_20)
    target_compile_definitions(CypherTileEditorGui PRIVATE
        CYPHER_TILE_EDITOR_ASSET_SOURCE_DIR="${CYPHERENGINE_ROOT_DIR}/assets")
    target_include_directories(CypherTileEditorGui
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor"
            "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/Gui"
    )
    target_link_libraries(CypherTileEditorGui
        PUBLIC
            Cypher::TileMapCore
            Cypher::TileMaterialPreview
            Cypher::Render
            Cypher::Math
            Qt6::Core
            Qt6::Gui
            Qt6::OpenGLWidgets
            Qt6::Svg
            Qt6::Widgets
    )
    set_target_properties(CypherTileEditorGui PROPERTIES
        AUTOMOC ON
        AUTORCC ON
    )

    add_executable(CypherTileEditor
        "${CMAKE_CURRENT_SOURCE_DIR}/CypherTileEditor/main.cpp"
    )
    add_executable(Cypher::TileEditor ALIAS CypherTileEditor)
    target_compile_features(CypherTileEditor PRIVATE cxx_std_20)
    target_link_libraries(CypherTileEditor PRIVATE Cypher::TileEditorGui)
    set_target_properties(CypherTileEditor PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "CypherTileEditor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.cypherengine.tileeditor"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        WIN32_EXECUTABLE TRUE
    )
endif()

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
    add_executable(cypher_tile_map_core_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMultiSelectionCore_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileCameraOptions_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMapProperties_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileCameraFraming_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileRegion_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileCamera_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMapCore_Tests.cpp"
    )
    target_link_libraries(cypher_tile_map_core_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::TileMapCore
    )
    add_test(NAME cypher_tile_map_core_tests COMMAND cypher_tile_map_core_tests)
    set_tests_properties(cypher_tile_map_core_tests PROPERTIES LABELS "CypherTools;TileMap;Unit")

    add_executable(cypher_tile_map_serialization_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMaterialBindings_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMapSerialization_Tests.cpp"
    )
    target_link_libraries(cypher_tile_map_serialization_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::TileMapCore
    )
    add_test(
        NAME cypher_tile_map_serialization_tests
        COMMAND cypher_tile_map_serialization_tests
    )
    set_tests_properties(
        cypher_tile_map_serialization_tests
        PROPERTIES LABELS "CypherTools;TileMap;Serialization"
    )

    if (CYPHERENGINE_BUILD_TILE_EDITOR)
        add_executable(cypher_tile_workspace_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMultiSelectionCanvas_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMultiSelectionViews_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMultiSelectionWorkspace_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileDuplicateViews_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileQuietDisplayPreferences_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileNavigationPreferences_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileConfigurationProfiles_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileCameraOptionsViewport_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileCameraNavigationWorkspace_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileOrthoMaterials_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileOrthoMaterialViews_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileOrthoStructureDisplay_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMaterialDisplayPreferences_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMapPropertiesWorkspace_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMapPropertiesWidget_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileEditorConfig_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileViewOrder_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTilePiecePalette_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileViewReadability_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMaterialBrowser_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileSelectionMaterialInspector_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileHistoryPanel_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileMaterialPreview_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileRegionWorkspace_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileSelection_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTilePicking_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileOrthoNavigation_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileStairs_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileCameraViewport_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileWorkspace_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileValidation_Tests.cpp"
        )
        target_link_libraries(cypher_tile_workspace_tests PRIVATE
            Catch2::Catch2WithMain Cypher::TileEditorGui)
        add_test(NAME cypher_tile_workspace_tests COMMAND cypher_tile_workspace_tests)
        set_tests_properties(cypher_tile_workspace_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "CypherTools;TileMap;Qt;Workspace;Validation"
            # This aggregate currently covers more than 220 Qt test cases and
            # takes about 35 seconds on the reference macOS development host.
            TIMEOUT 60
        )
        add_executable(cypher_tile_console_tests
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileConsole_Tests.cpp"
            "${CYPHERENGINE_ROOT_DIR}/tests/CypherTools/CypherTileEditor/CypherTileShellRunner_Tests.cpp"
        )
        target_link_libraries(cypher_tile_console_tests
            PRIVATE
                Catch2::Catch2WithMain
                Cypher::TileEditorGui
        )
        add_test(
            NAME cypher_tile_console_tests
            COMMAND cypher_tile_console_tests
        )
        set_tests_properties(cypher_tile_console_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "CypherTools;TileMap;Qt;Console"
        )

        add_test(
            NAME cypher_tile_editor_smoke
            COMMAND CypherTileEditor --smoke-test
                "${CYPHERENGINE_ROOT_DIR}/assets/maps/tile_editor_demo.cymap"
        )
        set_tests_properties(cypher_tile_editor_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "CypherTools;TileMap;Qt;Smoke"
            TIMEOUT 20
        )
    endif()

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
    "CypherShaderCompiler;CypherTextureCompiler;CypherMaterialCompiler;CypherResourceCompiler;CypherTileMapCore"
)
if (CYPHERENGINE_BUILD_TILE_EDITOR)
    list(APPEND CYPHER_REGISTERED_TOOL_PRODUCTS_VALUE "CypherTileEditor")
endif()
if (CYPHERENGINE_BUILD_PICASSO)
    list(APPEND CYPHER_REGISTERED_TOOL_PRODUCTS_VALUE "Picasso")
endif()
set(CYPHER_REGISTERED_TOOL_PRODUCTS
    "${CYPHER_REGISTERED_TOOL_PRODUCTS_VALUE}"
    CACHE INTERNAL
    "Cypher tool products currently registered with CMake"
    FORCE
)
