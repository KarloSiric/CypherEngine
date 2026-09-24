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
    Constraints/CypherGeometry_Constraints.cpp
    Constraints/CypherGeometry_Constraints.h
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
    Attributes/Propagation/CypherGeometry_Attributes_Propagation.cpp
    Attributes/Propagation/CypherGeometry_Attributes_Propagation.h
    Attributes/Schema/CypherGeometry_Attributes_BrushSideStore.cpp
    Attributes/Schema/CypherGeometry_Attributes_BrushSideStore.h
    Attributes/Schema/CypherGeometry_Attributes_Schema.cpp
    Attributes/Schema/CypherGeometry_Attributes_Schema.h
    Csg/Brush/CypherGeometry_BrushCsg.cpp
    Csg/Brush/CypherGeometry_BrushCsg.h
    Csg/Operations/CypherGeometry_BrushCsgCommands.cpp
    Csg/Operations/CypherGeometry_BrushCsgCommands.h
    Document/CypherGeometry_BrushValue.cpp
    Document/CypherGeometry_BrushValue.h
    Document/CypherGeometry_Document.cpp
    Document/CypherGeometry_Document.h
    Document/CypherGeometry_Snapshot.cpp
    Document/CypherGeometry_Snapshot.h
    Intermediates/BrushPiece/CypherGeometry_BrushPiece.cpp
    Intermediates/BrushPiece/CypherGeometry_BrushPiece.h
    Kernel/CypherGeometry_Kernel_Classification.cpp
    Kernel/CypherGeometry_Kernel_Classification.h
    Kernel/CypherGeometry_Kernel_CoordinateKey.cpp
    Kernel/CypherGeometry_Kernel_CoordinateKey.h
    Representations/Brush/CypherGeometry_BrushSolid.cpp
    Representations/Brush/CypherGeometry_BrushSolid.h
    Operations/Conversion/CypherGeometry_PieceMaterialize.cpp
    Operations/Conversion/CypherGeometry_PieceMaterialize.h
    Operations/Cutting/CypherGeometry_BrushCutting.cpp
    Operations/Cutting/CypherGeometry_BrushCutting.h
    Operations/Modeling/CypherGeometry_BrushCreate.cpp
    Operations/Modeling/CypherGeometry_BrushCreate.h
    Operations/Transform/CypherGeometry_BrushTransform.cpp
    Operations/Transform/CypherGeometry_BrushTransform.h
    Primitives/CypherGeometry_BrushPrimitives.cpp
    Primitives/CypherGeometry_BrushPrimitives.h
    Queries/CypherGeometry_BrushQueries.cpp
    Queries/CypherGeometry_BrushQueries.h
    Representations/Brush/CypherGeometry_BrushBoundary.cpp
    Representations/Brush/CypherGeometry_BrushBoundary.h
    Representations/Brush/CypherGeometry_BrushGenerator.cpp
    Representations/Brush/CypherGeometry_BrushGenerator.h
    Representations/Brush/CypherGeometry_BrushValidation.cpp
    Representations/Brush/CypherGeometry_BrushValidation.h
    Selection/CypherGeometry_Selection.cpp
    Selection/CypherGeometry_Selection.h
    Spatial/CypherGeometry_SpatialIndex.cpp
    Spatial/CypherGeometry_SpatialIndex.h
    Tessellation/CypherGeometry_BrushTessellation.cpp
    Tessellation/CypherGeometry_BrushTessellation.h
    Transactions/CypherGeometry_ChangeRecord.cpp
    Transactions/CypherGeometry_ChangeRecord.h
    Transactions/CypherGeometry_History.cpp
    Transactions/CypherGeometry_History.h
    Transactions/CypherGeometry_PreviewPlan.cpp
    Transactions/CypherGeometry_PreviewPlan.h
    Transactions/CypherGeometry_Transaction.cpp
    Transactions/CypherGeometry_Transaction.h
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
        "${CMAKE_CURRENT_SOURCE_DIR}/Document"
        "${CMAKE_CURRENT_SOURCE_DIR}/Transactions"
        "${CMAKE_CURRENT_SOURCE_DIR}/Queries"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tessellation"
        "${CMAKE_CURRENT_SOURCE_DIR}/Spatial"
        "${CMAKE_CURRENT_SOURCE_DIR}/Intermediates/BrushPiece"
        "${CMAKE_CURRENT_SOURCE_DIR}/Csg/Brush"
        "${CMAKE_CURRENT_SOURCE_DIR}/Csg/Operations"
        "${CMAKE_CURRENT_SOURCE_DIR}/Attributes/Propagation"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations/Conversion"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations/Cutting"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations/Transform"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations/Modeling"
        "${CMAKE_CURRENT_SOURCE_DIR}/Primitives"
        "${CMAKE_CURRENT_SOURCE_DIR}/Selection"
        "${CMAKE_CURRENT_SOURCE_DIR}/Constraints"
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
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Document/CypherGeometry_Document_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Kernel/CypherGeometry_Kernel_Classification_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Kernel/CypherGeometry_Kernel_CoordinateKey_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Attributes/CypherGeometry_Attributes_Schema_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Queries/CypherGeometry_BrushQueries_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_BrushCanonical_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_BrushSolid_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Tessellation/CypherGeometry_BrushTessellation_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Transactions/CypherGeometry_Transaction_Tests.cpp"
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
