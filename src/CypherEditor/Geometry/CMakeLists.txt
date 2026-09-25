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
    Brush/CypherGeometry_BrushBoundary.cpp
    Brush/CypherGeometry_BrushBoundary.h
    Brush/CypherGeometry_BrushClip.cpp
    Brush/CypherGeometry_BrushClip.h
    Brush/CypherGeometry_BrushCSG.cpp
    Brush/CypherGeometry_BrushCSG.h
    Brush/CypherGeometry_BrushEditPipeline.cpp
    Brush/CypherGeometry_BrushEditPipeline.h
    Brush/CypherGeometry_BrushGenerator.cpp
    Brush/CypherGeometry_BrushGenerator.h
    Brush/CypherGeometry_BrushSolid.cpp
    Brush/CypherGeometry_BrushSolid.h
    Brush/CypherGeometry_BrushSource.cpp
    Brush/CypherGeometry_BrushSource.h
    Brush/CypherGeometry_BrushTransform.cpp
    Brush/CypherGeometry_BrushTransform.h
    Brush/CypherGeometry_BrushValidation.cpp
    Brush/CypherGeometry_BrushValidation.h
    Brush/CypherGeometry_BrushVertexOps.cpp
    Brush/CypherGeometry_BrushVertexOps.h
    Cook/CypherGeometry_BrushTessellation.cpp
    Cook/CypherGeometry_BrushTessellation.h
    Cook/CypherGeometry_CookSourceMapping.cpp
    Cook/CypherGeometry_CookSourceMapping.h
    Core/CypherGeometry_Attributes_BrushSideStore.cpp
    Core/CypherGeometry_Attributes_BrushSideStore.h
    Core/CypherGeometry_Attributes_Schema.cpp
    Core/CypherGeometry_Attributes_Schema.h
    Core/CypherGeometry_Diagnostics.cpp
    Core/CypherGeometry_Diagnostics.h
    Core/CypherGeometry_IdAllocator.cpp
    Core/CypherGeometry_IdAllocator.h
    Core/CypherGeometry_Kernel_Classification.cpp
    Core/CypherGeometry_Kernel_Classification.h
    Core/CypherGeometry_Kernel_ConvexHull.cpp
    Core/CypherGeometry_Kernel_ConvexHull.h
    Core/CypherGeometry_Kernel_CoordinateKey.cpp
    Core/CypherGeometry_Kernel_CoordinateKey.h
    Core/CypherGeometry_OperationContext.cpp
    Core/CypherGeometry_OperationContext.h
    Core/CypherGeometry_Policy.cpp
    Core/CypherGeometry_Policy.h
    Core/CypherGeometry_Scratch.cpp
    Core/CypherGeometry_Scratch.h
    Core/CypherGeometry_Scratch.inl
    Core/CypherGeometry_SourceIdRegistry.cpp
    Core/CypherGeometry_SourceIdRegistry.h
    Core/CypherGeometry_SpatialIndex.cpp
    Core/CypherGeometry_SpatialIndex.h
    Core/CypherGeometry_Types.h
    Csg/CypherGeometry_BrushCsgAdoption.cpp
    Csg/CypherGeometry_BrushCsgAdoption.h
    Document/CypherGeometry_BrushSerialization.cpp
    Document/CypherGeometry_BrushSerialization.h
    Document/CypherGeometry_Delta.cpp
    Document/CypherGeometry_Delta.h
    Document/CypherGeometry_Document.cpp
    Document/CypherGeometry_Document.h
    Document/CypherGeometry_ReplacementIdentity.cpp
    Document/CypherGeometry_ReplacementIdentity.h
    Document/CypherGeometry_Snapshot.cpp
    Document/CypherGeometry_Snapshot.h
    Document/CypherGeometry_Transaction.cpp
    Document/CypherGeometry_Transaction.h
    Mesh/CypherGeometry_EditableMesh.cpp
    Mesh/CypherGeometry_EditableMesh.h
    Mesh/CypherGeometry_MeshBooleans.cpp
    Mesh/CypherGeometry_MeshBooleans.h
    Mesh/CypherGeometry_MeshBuilder.cpp
    Mesh/CypherGeometry_MeshBuilder.h
    Mesh/CypherGeometry_MeshCleanup.cpp
    Mesh/CypherGeometry_MeshCleanup.h
    Mesh/CypherGeometry_MeshSubdivision.cpp
    Mesh/CypherGeometry_MeshSubdivision.h
    Mesh/CypherGeometry_MeshToBrush.cpp
    Mesh/CypherGeometry_MeshToBrush.h
    Mesh/CypherGeometry_MeshTopologyOps.cpp
    Mesh/CypherGeometry_MeshTopologyOps.h
    Mesh/CypherGeometry_MeshValidation.cpp
    Mesh/CypherGeometry_MeshValidation.h
    Operations/CypherGeometry_Snap.cpp
    Operations/CypherGeometry_Snap.h
    Operations/CypherGeometry_UvAlignment.cpp
    Operations/CypherGeometry_UvAlignment.h
    Queries/CypherGeometry_BrushQueries.cpp
    Queries/CypherGeometry_BrushQueries.h
    Queries/CypherGeometry_RaycastQueries.cpp
    Queries/CypherGeometry_RaycastQueries.h
    Queries/CypherGeometry_SelectionSet.cpp
    Queries/CypherGeometry_SelectionSet.h
)
add_library(Cypher::EditorGeometry ALIAS CypherEditorGeometry)

target_compile_features(CypherEditorGeometry PUBLIC cxx_std_20)
target_include_directories(CypherEditorGeometry
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core"
        "${CMAKE_CURRENT_SOURCE_DIR}/Brush"
        "${CMAKE_CURRENT_SOURCE_DIR}/Mesh"
        "${CMAKE_CURRENT_SOURCE_DIR}/Surfaces"
        "${CMAKE_CURRENT_SOURCE_DIR}/Operations"
        "${CMAKE_CURRENT_SOURCE_DIR}/Csg"
        "${CMAKE_CURRENT_SOURCE_DIR}/Document"
        "${CMAKE_CURRENT_SOURCE_DIR}/Cook"
        "${CMAKE_CURRENT_SOURCE_DIR}/Validation"
        "${CMAKE_CURRENT_SOURCE_DIR}/Queries"
)
target_link_libraries(CypherEditorGeometry
    PUBLIC
        Cypher::CommonTier1
        Cypher::Math
)

if (CYPHERENGINE_BUILD_TESTS)
    add_executable(cypher_editor_geometry_contract_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Brush/CypherGeometry_BrushClip_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Brush/CypherGeometry_BrushCSG_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Brush/CypherGeometry_BrushEditPipeline_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Brush/CypherGeometry_BrushGenerator_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Brush/CypherGeometry_BrushSolid_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Brush/CypherGeometry_BrushSource_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Brush/CypherGeometry_BrushTransform_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Brush/CypherGeometry_BrushValidation_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Brush/CypherGeometry_BrushVertexOps_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Cook/CypherGeometry_BrushTessellation_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Cook/CypherGeometry_CookSourceMapping_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Attributes_Schema_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Core_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Diagnostics_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Kernel_Classification_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Kernel_ConvexHull_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Kernel_CoordinateKey_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_OperationContext_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Scratch_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_SourceIdRegistry_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_SpatialIndex_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Core/CypherGeometry_Types_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Csg/CypherGeometry_BrushCsgAdoption_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Document/CypherGeometry_BrushSerialization_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Document/CypherGeometry_Document_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Document/CypherGeometry_Transaction_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Mesh/CypherGeometry_EditableMesh_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Mesh/CypherGeometry_MeshBooleans_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Mesh/CypherGeometry_MeshInvariant_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Mesh/CypherGeometry_MeshSubdivision_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Mesh/CypherGeometry_MeshToBrush_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Mesh/CypherGeometry_MeshTopologyOps_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_IndoorWorkflow_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_Snap_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Operations/CypherGeometry_UvAlignment_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Queries/CypherGeometry_BrushQueries_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Queries/CypherGeometry_RaycastQueries_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Geometry/Queries/CypherGeometry_SelectionSet_Tests.cpp"
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
    # The timeout is a hang detector, not a performance budget. Every geometry
    # contract case runs in this one executable, so its cost grows with each
    # feature, and the build type dominates it: on CI (2026-09-24) the suite
    # took ~1 s in Release, 8-13 s in Debug, and passed 30 s in the Debug
    # ASan+UBSan job. 300 s leaves room for that slowest configuration to keep
    # growing while a genuine hang still fails well inside the job limit.
    set_tests_properties(cypher_editor_geometry_contract_tests PROPERTIES
        LABELS "CypherEditor;Geometry;Contract"
        TIMEOUT 300
    )
endif()

# Modules added after the core slice (see file header).
include("${CMAKE_CURRENT_SOURCE_DIR}/CypherEditorGeometry_Modules.cmake")
