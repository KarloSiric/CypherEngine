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
    CypherMapGui_EntityProperties.cpp
    CypherMapGui_EntityProperties.h
    CypherMapGui_Input.cpp
    CypherMapGui_Input.h
    CypherMapGui_Panels.cpp
    CypherMapGui_Panels.h
    CypherMapGui_ToolPanels.cpp
    CypherMapGui_ToolPanels.h
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
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ViewContent_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ToolPanels_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Outliner_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Dialogs_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Check_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Input_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Edit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_EntityEdit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_EntityProperties_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_MeshEdit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Block_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Clipboard_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Gui/CypherMapGui_QuadSlice_Tests.cpp"
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
        TIMEOUT 120
    )
endif()
