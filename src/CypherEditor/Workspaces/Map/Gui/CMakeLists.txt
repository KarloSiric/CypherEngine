##########################################################################
#
#  CypherEngine Source Code
#  Copyright (c) 2026 Karlo Siric. All rights reserved.
#
#  File: src/CypherEditor/Workspaces/Map/Gui/CMakeLists.txt
#  Purpose: Builds Cypher::EditorMapGui, the Qt half of the Map workspace:
#           the editing session, the 2D and 3D views, and the outliner and
#           properties panels (ADR 0008).
#  Details: Depends on its own Core and on the editor framework only; Mason
#           links it and registers its panels. Tests run on the offscreen
#           platform so they need no display.
#
##########################################################################

add_library(CypherEditorMapGui STATIC
    CypherMapGui_Check.cpp
    CypherMapGui_Check.h
    CypherMapGui_Dialogs.cpp
    CypherMapGui_Dialogs.h
    CypherMapGui_Edit.cpp
    CypherMapGui_GeometryCommands.cpp
    CypherMapGui_GeometryCommands.h
    CypherMapGui_EntityProperties.cpp
    CypherMapGui_EntityProperties.h
    CypherMapGui_Input.cpp
    CypherMapGui_Input.h
    CypherMapGui_Instances.cpp
    CypherMapGui_Instances.h
    CypherMapGui_Panels.cpp
    CypherMapGui_Panels.h
    CypherMapGui_PropertyEditors.cpp
    CypherMapGui_PropertyEditors.h
    CypherMapGui_ToolGestures.cpp
    CypherMapGui_EntityIo.cpp
    CypherMapGui_EntityIo.h
    CypherMapGui_EntityLook.cpp
    CypherMapGui_EntityLook.h
    CypherMapGui_ViewMenu.cpp
    CypherMapGui_ViewMenu.h
    CypherMapGui_Notice.cpp
    CypherMapGui_Notice.h
    CypherMapGui_ObjectTools.cpp
    CypherMapGui_ObjectTools.h
    CypherMapGui_Autosave.cpp
    CypherMapGui_Autosave.h
    CypherMapGui_Falloff.cpp
    CypherMapGui_Falloff.h
    CypherMapGui_Game.cpp
    CypherMapGui_Game.h
    CypherMapGui_FastTexture.cpp
    CypherMapGui_FastTexture.h
    CypherMapGui_Hotspots.cpp
    CypherMapGui_Hotspots.h
    CypherMapGui_Layers.cpp
    CypherMapGui_Layers.h
    CypherMapGui_FindEntities.cpp
    CypherMapGui_FindEntities.h
    CypherMapGui_FaceEdit.cpp
    CypherMapGui_FaceEdit.h
    CypherMapGui_ToolGestures.h
    CypherMapGui_ToolPanels.cpp
    CypherMapGui_ToolPanels.h
    CypherMapGui_Hierarchy.cpp
    CypherMapGui_Hierarchy.h
    CypherMapGui_UvEditor.cpp
    CypherMapGui_UvEditor.h
    CypherMapGui_Views.cpp
    CypherMapGui_Views.h
    CypherMapGui_Workspace.cpp
    CypherMapGui_Workspace.h
)
add_library(Cypher::EditorMapGui ALIAS CypherEditorMapGui)

target_compile_features(CypherEditorMapGui PUBLIC cxx_std_20)
target_include_directories(CypherEditorMapGui
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(CypherEditorMapGui
    PUBLIC
        Cypher::EditorMapCore
        Cypher::EditorGui
)

if (CYPHERENGINE_BUILD_TESTS)
    add_executable(cypher_editor_map_gui_contract_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_TestMain.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Workspace_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Views_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_OrbitWheel_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_OrbitSurface_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_TerrainCreate_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_PanWheel_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ViewContent_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ToolPanels_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Outliner_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Dialogs_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Check_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Input_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Edit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_EntityEdit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_EntityProperties_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_PropertyEditors_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ObjectTools_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Layers_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Instances_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_UvEditor_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Hierarchy_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_MeshEdit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Block_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Clipboard_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_QuadSlice_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_GeometryCommands_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ToolGestures_Tests.cpp"
    )
    target_link_libraries(cypher_editor_map_gui_contract_tests
        PRIVATE
            Catch2::Catch2
            Cypher::EditorMapGui
    )
    target_compile_definitions(cypher_editor_map_gui_contract_tests
        PRIVATE
            CYPHER_MAP_EXAMPLE_DIR="${CYPHERENGINE_ROOT_DIR}/docs/formats/examples/cymap"
    )
    add_test(
        NAME cypher_editor_map_gui_contract_tests
        COMMAND cypher_editor_map_gui_contract_tests
    )
    set_tests_properties(cypher_editor_map_gui_contract_tests PROPERTIES
        LABELS "CypherEditor;Map;Gui;Contract"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 300
    )
endif()
