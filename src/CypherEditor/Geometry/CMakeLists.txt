##########################################################################
#
#  CypherEngine Source Code
#  Copyright (c) 2026 Karlo Siric. All rights reserved.
#
#  File: src/CypherEditor/Geometry/CMakeLists.txt
#  Purpose: Builds the shared Qt-free editor geometry runtime.
#  Details: Source identity and policy form the first compiled Core slice;
#           topology is added through focused vertical slices and tests.
#
##########################################################################

add_library(CypherEditorGeometry STATIC
    Core/CypherGeometry_Diagnostics.cpp
    Core/CypherGeometry_Diagnostics.h
    Core/CypherGeometry_IdAllocator.cpp
    Core/CypherGeometry_IdAllocator.h
    Core/CypherGeometry_Policy.cpp
    Core/CypherGeometry_Policy.h
    Core/CypherGeometry_Scratch.cpp
    Core/CypherGeometry_Scratch.h
    Core/CypherGeometry_Scratch.inl
    Core/CypherGeometry_SourceIdRegistry.cpp
    Core/CypherGeometry_SourceIdRegistry.h
    Core/CypherGeometry_Types.h
    Attributes/Schema/CypherGeometry_Attributes_BrushSideStore.cpp
    Attributes/Schema/CypherGeometry_Attributes_BrushSideStore.h
    Attributes/Schema/CypherGeometry_Attributes_Schema.cpp
    Attributes/Schema/CypherGeometry_Attributes_Schema.h
    Kernel/CypherGeometry_Kernel_Classification.cpp
    Kernel/CypherGeometry_Kernel_Classification.h
    Kernel/CypherGeometry_Kernel_CoordinateKey.cpp
    Kernel/CypherGeometry_Kernel_CoordinateKey.h
    Representations/Brush/CypherGeometry_BrushSolid.cpp
    Representations/Brush/CypherGeometry_BrushSolid.h
    Representations/Brush/CypherGeometry_BrushBoundary.cpp
    Representations/Brush/CypherGeometry_BrushBoundary.h
    Representations/Brush/CypherGeometry_BrushGenerator.cpp
    Representations/Brush/CypherGeometry_BrushGenerator.h
    Representations/Brush/CypherGeometry_BrushValidation.cpp
    Representations/Brush/CypherGeometry_BrushValidation.h
)
add_library(Cypher::EditorGeometry ALIAS CypherEditorGeometry)

target_compile_features(CypherEditorGeometry PUBLIC cxx_std_20)
target_include_directories(CypherEditorGeometry
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core"
        "${CMAKE_CURRENT_SOURCE_DIR}/Kernel"
        "${CMAKE_CURRENT_SOURCE_DIR}/Attributes/Schema"
        "${CMAKE_CURRENT_SOURCE_DIR}/Representations/Brush"
)
target_link_libraries(CypherEditorGeometry
    PUBLIC
        Cypher::CommonTier1
        Cypher::Math
)

if (CYPHERENGINE_BUILD_TESTS)
    add_executable(cypher_editor_geometry_contract_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Core_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Diagnostics_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Scratch_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_SourceIdRegistry_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Types_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Kernel/CypherGeometry_Kernel_Classification_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Kernel/CypherGeometry_Kernel_CoordinateKey_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Attributes/CypherGeometry_Attributes_Schema_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_BrushSolid_Tests.cpp"
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
        TIMEOUT 30
    )
endif()
