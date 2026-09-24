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
    Core/CypherGeometry_OperationContext.cpp
    Core/CypherGeometry_OperationContext.h
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
    Kernel/CypherGeometry_Kernel_ConvexHull.cpp
    Kernel/CypherGeometry_Kernel_ConvexHull.h
    Representations/Brush/CypherGeometry_BrushSolid.cpp
    Representations/Brush/CypherGeometry_BrushSolid.h
    Representations/Brush/CypherGeometry_BrushBoundary.cpp
    Representations/Brush/CypherGeometry_BrushBoundary.h
    Representations/Brush/CypherGeometry_BrushGenerator.cpp
    Representations/Brush/CypherGeometry_BrushGenerator.h
    Representations/Brush/CypherGeometry_BrushValidation.cpp
    Representations/Brush/CypherGeometry_BrushValidation.h
    Document/CypherGeometry_Document.cpp
    Document/CypherGeometry_Document.h
    Document/CypherGeometry_Snapshot.cpp
    Document/CypherGeometry_Snapshot.h
    Transactions/CypherGeometry_Delta.cpp
    Transactions/CypherGeometry_Delta.h
    Transactions/CypherGeometry_ReplacementIdentity.cpp
    Transactions/CypherGeometry_ReplacementIdentity.h
    Transactions/CypherGeometry_Transaction.cpp
    Transactions/CypherGeometry_Transaction.h
    Tessellation/CypherGeometry_BrushTessellation.cpp
    Tessellation/CypherGeometry_BrushTessellation.h
    Queries/CypherGeometry_BrushQueries.cpp
    Queries/CypherGeometry_BrushQueries.h
    Queries/CypherGeometry_RaycastQueries.cpp
    Queries/CypherGeometry_RaycastQueries.h
    Spatial/CypherGeometry_SpatialIndex.cpp
    Spatial/CypherGeometry_SpatialIndex.h
    Selection/CypherGeometry_SelectionSet.cpp
    Selection/CypherGeometry_SelectionSet.h
    Constraints/CypherGeometry_Snap.cpp
    Constraints/CypherGeometry_Snap.h
    Operations/Transform/CypherGeometry_BrushTransform.cpp
    Operations/Transform/CypherGeometry_BrushTransform.h
    Operations/Cutting/CypherGeometry_BrushClip.cpp
    Operations/Cutting/CypherGeometry_BrushClip.h
    Operations/CypherGeometry_BrushEditPipeline.cpp
    Operations/CypherGeometry_BrushEditPipeline.h
    Operations/CSG/CypherGeometry_BrushCSG.cpp
    Operations/CSG/CypherGeometry_BrushCSG.h
    Csg/AttributeTransfer/CypherGeometry_BrushCsgAdoption.cpp
    Csg/AttributeTransfer/CypherGeometry_BrushCsgAdoption.h
    Operations/UvAlignment/CypherGeometry_UvAlignment.cpp
    Operations/UvAlignment/CypherGeometry_UvAlignment.h
    Operations/VertexManip/CypherGeometry_BrushVertexOps.cpp
    Operations/VertexManip/CypherGeometry_BrushVertexOps.h
    Serialization/CypherGeometry_BrushSerialization.cpp
    Serialization/CypherGeometry_BrushSerialization.h
    Cook/SourceMapping/CypherGeometry_CookSourceMapping.cpp
    Cook/SourceMapping/CypherGeometry_CookSourceMapping.h
    Representations/Mesh/CypherGeometry_EditableMesh.cpp
    Representations/Mesh/CypherGeometry_EditableMesh.h
    Representations/Mesh/CypherGeometry_MeshBuilder.cpp
    Representations/Mesh/CypherGeometry_MeshBuilder.h
    Representations/Mesh/CypherGeometry_MeshValidation.cpp
    Representations/Mesh/CypherGeometry_MeshValidation.h
    Representations/Mesh/CypherGeometry_MeshTopologyOps.cpp
    Representations/Mesh/CypherGeometry_MeshTopologyOps.h
    Representations/Mesh/CypherGeometry_MeshSubdivision.cpp
    Representations/Mesh/CypherGeometry_MeshSubdivision.h
    Representations/Mesh/CypherGeometry_MeshToBrush.cpp
    Representations/Mesh/CypherGeometry_MeshToBrush.h
    Representations/Mesh/CypherGeometry_MeshBooleans.cpp
    Representations/Mesh/CypherGeometry_MeshBooleans.h
    Representations/Mesh/CypherGeometry_MeshCleanup.cpp
    Representations/Mesh/CypherGeometry_MeshCleanup.h
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
        "${CMAKE_CURRENT_SOURCE_DIR}/Tessellation"
        "${CMAKE_CURRENT_SOURCE_DIR}/Queries"
        "${CMAKE_CURRENT_SOURCE_DIR}/Spatial"
        "${CMAKE_CURRENT_SOURCE_DIR}/Selection"
        "${CMAKE_CURRENT_SOURCE_DIR}/Constraints"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations/Transform"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations/Cutting"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations/CSG"
        "${CMAKE_CURRENT_SOURCE_DIR}/Csg/AttributeTransfer"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations/UvAlignment"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations/VertexManip"
        "${CMAKE_CURRENT_SOURCE_DIR}/Serialization"
        "${CMAKE_CURRENT_SOURCE_DIR}/Cook/SourceMapping"
        "${CMAKE_CURRENT_SOURCE_DIR}/Representations/Mesh"
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
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_OperationContext_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Scratch_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_SourceIdRegistry_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Types_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Kernel/CypherGeometry_Kernel_Classification_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Kernel/CypherGeometry_Kernel_CoordinateKey_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Attributes/CypherGeometry_Attributes_Schema_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_BrushSolid_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_BrushValidation_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Document/CypherGeometry_Document_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Transactions/CypherGeometry_Transaction_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Tessellation/CypherGeometry_BrushTessellation_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Queries/CypherGeometry_BrushQueries_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Queries/CypherGeometry_RaycastQueries_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Spatial/CypherGeometry_SpatialIndex_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Selection/CypherGeometry_SelectionSet_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Constraints/CypherGeometry_Snap_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_BrushTransform_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_BrushClip_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_BrushEditPipeline_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_BrushCSG_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_BrushCsgAdoption_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_IndoorWorkflow_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_BrushGenerator_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Serialization/CypherGeometry_BrushSerialization_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Cook/CypherGeometry_CookSourceMapping_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_EditableMesh_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_MeshTopologyOps_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_UvAlignment_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Kernel/CypherGeometry_Kernel_ConvexHull_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_BrushVertexOps_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_MeshSubdivision_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_MeshToBrush_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_MeshBooleans_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Representations/CypherGeometry_MeshInvariant_Tests.cpp"
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

# Modules added after the core slice (see file header).
include("${CMAKE_CURRENT_SOURCE_DIR}/CypherEditorGeometry_Modules.cmake")
