##########################################################################
#
#  CypherEngine Source Code
#  Copyright (c) 2026 Karlo Siric. All rights reserved.
#
#  File: src/CypherEditor/Geometry/CMakeLists.txt
#  Purpose: Registers the shared Qt-free editor geometry target.
#  Details: The initial interface target establishes ownership while topology
#           implementation is developed file by file with focused tests.
#
##########################################################################

add_library(CypherEditorGeometry INTERFACE)
add_library(Cypher::EditorGeometry ALIAS CypherEditorGeometry)

target_compile_features(CypherEditorGeometry INTERFACE cxx_std_20)
target_include_directories(CypherEditorGeometry
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core"
)
target_link_libraries(CypherEditorGeometry
    INTERFACE
        Cypher::CommonTier1
        Cypher::Math
)

if (CYPHERENGINE_BUILD_TESTS)
    add_executable(cypher_editor_geometry_contract_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/CypherGeometry_Types_Tests.cpp"
    )
    target_link_libraries(cypher_editor_geometry_contract_tests
        PRIVATE
            Catch2::Catch2WithMain
            Cypher::EditorGeometry
    )
    add_test(
        NAME cypher_editor_geometry_contract_tests
        COMMAND cypher_editor_geometry_contract_tests
    )
    set_tests_properties(cypher_editor_geometry_contract_tests PROPERTIES
        LABELS "CypherEditor;Geometry;Contract"
    )
endif()

