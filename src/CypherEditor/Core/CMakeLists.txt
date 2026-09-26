##########################################################################
#
#  CypherEngine Source Code
#  Copyright (c) 2026 Karlo Siric. All rights reserved.
#
#  File: src/CypherEditor/Core/CMakeLists.txt
#  Purpose: Builds Cypher::EditorCore, the Qt-free editor framework (ADR 0008).
#  Details: Headers are included by basename, like Geometry. Nothing here may
#           link Qt; the Qt framework (Cypher::EditorGui) builds on top.
#
##########################################################################

add_library(CypherEditorCore STATIC
    Commands/CypherEditor_Commands.cpp
    Commands/CypherEditor_Commands.h
    Commands/CypherEditor_Keymap.cpp
    Commands/CypherEditor_Keymap.h
    Layouts/CypherEditor_Layout.cpp
    Layouts/CypherEditor_Layout.h
    Project/CypherEditor_Workspace.cpp
    Project/CypherEditor_Workspace.h
    Settings/CypherEditor_SettingsFile.cpp
    Settings/CypherEditor_SettingsFile.h
    Themes/CypherEditor_Theme.cpp
    Themes/CypherEditor_Theme.h
)
add_library(Cypher::EditorCore ALIAS CypherEditorCore)

target_compile_features(CypherEditorCore PUBLIC cxx_std_20)
target_include_directories(CypherEditorCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Commands"
        "${CMAKE_CURRENT_SOURCE_DIR}/Layouts"
        "${CMAKE_CURRENT_SOURCE_DIR}/Project"
        "${CMAKE_CURRENT_SOURCE_DIR}/Settings"
        "${CMAKE_CURRENT_SOURCE_DIR}/Themes"
)
target_link_libraries(CypherEditorCore
    PUBLIC
        Cypher::CommonTier2
)

if (CYPHERENGINE_BUILD_TESTS)
    add_executable(cypher_editor_core_contract_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Core/Commands/CypherEditor_Commands_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Core/Commands/CypherEditor_Keymap_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Core/Layouts/CypherEditor_Layout_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Core/Project/CypherEditor_Workspace_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Core/Settings/CypherEditor_SettingsFile_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Core/Themes/CypherEditor_Theme_Tests.cpp"
    )
    target_link_libraries(cypher_editor_core_contract_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::EditorCore
    )
    add_test(
        NAME cypher_editor_core_contract_tests
        COMMAND cypher_editor_core_contract_tests
    )
    set_tests_properties(cypher_editor_core_contract_tests PROPERTIES
        LABELS "CypherEditor;Core;Contract"
        TIMEOUT 120
    )
endif()
