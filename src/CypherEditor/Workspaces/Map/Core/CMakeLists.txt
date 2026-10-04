##########################################################################
#
#  CypherEngine Source Code
#  Copyright (c) 2026 Karlo Siric. All rights reserved.
#
#  File: src/CypherEditor/Workspaces/Map/Core/CMakeLists.txt
#  Purpose: Builds Cypher::EditorMapCore, the Qt-free half of the Map
#           workspace: the `.cymap` / `.cymapchunk` document (ADR 0011).
#  Details: The Map workspace's Qt half (Workspaces/Map/Gui) builds on top.
#           The document does no file I/O - it takes texts and hands texts to
#           a sink - so the scene compiler and tests share it; CypherMap_Files
#           is the one disk adapter (root plus chunk folder, staged saves).
#
##########################################################################

add_library(CypherEditorMapCore STATIC
    CypherMap_BrushEdit.cpp
    CypherMap_BrushEdit.h
    CypherMap_Cell.cpp
    CypherMap_Cell.h
    CypherMap_Clipboard.cpp
    CypherMap_Clipboard.h
    CypherMap_Document.cpp
    CypherMap_Document.h
    CypherMap_Edit.cpp
    CypherMap_Edit.h
    CypherMap_EntityEdit.cpp
    CypherMap_EntityEdit.h
    CypherMap_FaceEdit.cpp
    CypherMap_FaceEdit.h
    CypherMap_Files.cpp
    CypherMap_Files.h
    CypherMap_Geometry.cpp
    CypherMap_Geometry.h
    CypherMap_MeshEdit.cpp
    CypherMap_MeshEdit.h
    CypherMap_Wireframe.cpp
    CypherMap_Wireframe.h
)
add_library(Cypher::EditorMapCore ALIAS CypherEditorMapCore)

target_compile_features(CypherEditorMapCore PUBLIC cxx_std_20)
target_include_directories(CypherEditorMapCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(CypherEditorMapCore
    PUBLIC
        Cypher::EditorCore
        Cypher::EditorGeometry
    PRIVATE
        Cypher::VfsDirectory # Chunk folder scan (CypherMap_Files).
)

if (CYPHERENGINE_BUILD_TESTS)
    add_executable(cypher_editor_map_core_contract_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Core/CypherMap_BrushEdit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Core/CypherMap_Cell_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Core/CypherMap_Clipboard_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Core/CypherMap_Document_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Core/CypherMap_Edit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Core/CypherMap_EntityEdit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Core/CypherMap_FaceEdit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Core/CypherMap_Files_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Core/CypherMap_MeshEdit_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Workspaces/Map/Core/CypherMap_Wireframe_Tests.cpp"
    )
    target_link_libraries(cypher_editor_map_core_contract_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::EditorMapCore
    )
    # The documented example map is loaded by the tests so CYMAP.md's
    # examples can never drift from what the code writes.
    target_compile_definitions(cypher_editor_map_core_contract_tests
        PRIVATE
            CYPHER_MAP_EXAMPLE_DIR="${CYPHERENGINE_ROOT_DIR}/docs/formats/examples/cymap"
    )
    add_test(
        NAME cypher_editor_map_core_contract_tests
        COMMAND cypher_editor_map_core_contract_tests
    )
    set_tests_properties(cypher_editor_map_core_contract_tests PROPERTIES
        LABELS "CypherEditor;Map;Contract"
        TIMEOUT 120
    )
endif()
