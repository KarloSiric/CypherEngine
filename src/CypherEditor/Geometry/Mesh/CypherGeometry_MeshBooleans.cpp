//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBooleans.cpp
//  Purpose: Implements mesh-level boolean operations (union, subtract,
//           intersect) on closed editable half-edge meshes.
//  Details: The implementation leverages the existing brush CSG pipeline
//           rather than reimplementing polygon clipping from scratch.
//           The pipeline is:
//
//             mesh A → brush A  (MeshToBrush_TryConvert)
//             mesh B → brush B  (MeshToBrush_TryConvert)
//             brush A ⊕ brush B → result brush(es)  (BrushCSG)
//             result brush → boundary → result mesh  (MeshBuilder)
//
//           For intersection and union (when the result is convex), this
//           produces a single result mesh directly. A subtraction result
//           containing one convex fragment is also converted directly.
//           Multi-fragment subtraction currently reports UNSUPPORTED so
//           that no valid fragment is silently discarded.
//
//           This approach reuses all the validated brush CSG infrastructure
//           (half-space classification, iterative plane clipping, boundary
//           reconstruction) rather than duplicating that logic at the mesh
//           level.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBooleans.h"
#include "CypherGeometry_MeshToBrush.h"
#include "CypherGeometry_MeshBuilder.h"
#include "CypherGeometry_BrushCSG.h"
#include "CypherGeometry_BrushBoundary.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

enum class mesh_bool_op_t : u8 {
    UNION     = 0u,
    INTERSECT = 1u
};

// Converts a mesh to a brush solid, performs a brush-level operation,
// then converts the single-brush result back to a mesh. Used for
// intersection and union, which both produce a single convex result
// when the operands are convex.
geometry_status_t ExecuteSingleResultOp(
    const editable_mesh_t *pMeshA,
    const editable_mesh_t *pMeshB,
    const allocator_t *pAllocator,
    f64 tolerance,
    mesh_bool_op_t op,
    editable_mesh_t *pMeshOut ) noexcept
{
    geometry_policy_t policy{};
    policy.numerical.fCoplanarDistanceTolerance = tolerance;
    geometry_source_id_allocator_t idAlloc{};

    // Phase 1: Convert both meshes to brush solids.
    brush_solid_t brushA{};
    brush_solid_t brushB{};

    geometry_status_t s = MeshToBrush_TryConvert(
        pMeshA, &brushA, pAllocator, policy, &idAlloc );
    if ( s != geometry_status_t::OK ) { return s; }

    s = MeshToBrush_TryConvert(
        pMeshB, &brushB, pAllocator, policy, &idAlloc );
    if ( s != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &brushA );
        return s;
    }

    // Phase 2: Perform the brush-level boolean operation.
    brush_solid_t resultBrush{};

    if ( op == mesh_bool_op_t::INTERSECT ) {
        s = BrushCSG_TryIntersect(
            &brushA, &brushB, pAllocator, &idAlloc, policy,
            &resultBrush );
    }
    else {
        // Union: classify, then merge via convex hull.
        brush_boundary_t boundaryA{};
        brush_boundary_t boundaryB{};

        s = BrushBoundary_Init( &boundaryA, pAllocator );
        if ( s != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &brushA );
            BrushSolid_Shutdown( &brushB );
            return s;
        }
        s = BrushBoundary_Init( &boundaryB, pAllocator );
        if ( s != geometry_status_t::OK ) {
            BrushBoundary_Shutdown( &boundaryA );
            BrushSolid_Shutdown( &brushA );
            BrushSolid_Shutdown( &brushB );
            return s;
        }

        s = BrushBoundary_TryReconstruct( &boundaryA, &brushA, policy );
        if ( s == geometry_status_t::OK ) {
            s = BrushBoundary_TryReconstruct(
                &boundaryB, &brushB, policy );
        }
        if ( s == geometry_status_t::OK ) {
            s = BrushCSG_TryMerge(
                &brushA, &boundaryA, &brushB, &boundaryB,
                pAllocator, &idAlloc, policy, &resultBrush );
        }

        BrushBoundary_Shutdown( &boundaryA );
        BrushBoundary_Shutdown( &boundaryB );
    }

    BrushSolid_Shutdown( &brushA );
    BrushSolid_Shutdown( &brushB );

    if ( s != geometry_status_t::OK ) { return s; }

    // Phase 3: Reconstruct the boundary of the result brush.
    brush_boundary_t resultBoundary{};
    s = BrushBoundary_Init( &resultBoundary, pAllocator );
    if ( s != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &resultBrush );
        return s;
    }

    s = BrushBoundary_TryReconstruct(
        &resultBoundary, &resultBrush, policy );
    if ( s != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( &resultBoundary );
        BrushSolid_Shutdown( &resultBrush );
        return s;
    }

    // Phase 4: Build the result mesh from the boundary.
    s = EditableMesh_Init( pMeshOut, pAllocator );
    if ( s != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( &resultBoundary );
        BrushSolid_Shutdown( &resultBrush );
        return s;
    }

    s = MeshBuilder_TryBuildFromBoundary( pMeshOut, &resultBoundary );

    BrushBoundary_Shutdown( &resultBoundary );
    BrushSolid_Shutdown( &resultBrush );

    if ( s != geometry_status_t::OK ) {
        EditableMesh_Shutdown( pMeshOut );
    }
    return s;
}

// Subtraction may produce multiple convex fragments. The current mesh
// builder accepts one closed boundary, so only the single-fragment case can
// be published without a topology-handle remap and multi-shell merge.
geometry_status_t ExecuteSubtract(
    const editable_mesh_t *pMeshA,
    const editable_mesh_t *pMeshB,
    const allocator_t *pAllocator,
    f64 tolerance,
    editable_mesh_t *pMeshOut ) noexcept
{
    geometry_policy_t policy{};
    policy.numerical.fCoplanarDistanceTolerance = tolerance;
    geometry_source_id_allocator_t idAlloc{};

    // Phase 1: Convert both meshes to brush solids.
    brush_solid_t brushA{};
    brush_solid_t brushB{};

    geometry_status_t s = MeshToBrush_TryConvert(
        pMeshA, &brushA, pAllocator, policy, &idAlloc );
    if ( s != geometry_status_t::OK ) { return s; }

    s = MeshToBrush_TryConvert(
        pMeshB, &brushB, pAllocator, policy, &idAlloc );
    if ( s != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &brushA );
        return s;
    }

    // Phase 2: Perform brush-level subtraction.
    brush_csg_subtract_result_t subResult{};

    s = BrushCSG_TrySubtract(
        &brushA, &brushB, pAllocator, &idAlloc, policy, &subResult );

    BrushSolid_Shutdown( &brushA );
    BrushSolid_Shutdown( &brushB );

    if ( s != geometry_status_t::OK ) { return s; }

    if ( subResult.cFragments == 0u ) {
        BrushCSGSubtractResult_Shutdown( &subResult );
        return geometry_status_t::DEGENERATE;
    }

    // If there's exactly one fragment, build the mesh directly from it.
    if ( subResult.cFragments == 1u ) {
        brush_boundary_t boundary{};
        s = BrushBoundary_Init( &boundary, pAllocator );
        if ( s == geometry_status_t::OK ) {
            s = BrushBoundary_TryReconstruct(
                &boundary, &subResult.fragments[0], policy );
        }
        if ( s == geometry_status_t::OK ) {
            s = EditableMesh_Init( pMeshOut, pAllocator );
        }
        if ( s == geometry_status_t::OK ) {
            s = MeshBuilder_TryBuildFromBoundary( pMeshOut, &boundary );
            if ( s != geometry_status_t::OK ) {
                EditableMesh_Shutdown( pMeshOut );
            }
        }
        BrushBoundary_Shutdown( &boundary );
        BrushCSGSubtractResult_Shutdown( &subResult );
        return s;
    }

    // A multi-fragment brush result cannot yet be represented by the
    // single-boundary MeshBuilder without explicitly remapping and merging
    // all six topology pools. Returning the first fragment would silently
    // discard valid volume, so fail honestly and leave the output untouched.
    BrushCSGSubtractResult_Shutdown( &subResult );
    return geometry_status_t::UNSUPPORTED;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

geometry_status_t MeshBool_TryUnion(
    const editable_mesh_t *pMeshA,
    const editable_mesh_t *pMeshB,
    const allocator_t *pAllocator,
    f64 tolerance,
    editable_mesh_t *pMeshOut ) noexcept
{
    if ( pMeshA == nullptr || pMeshB == nullptr ||
         pAllocator == nullptr || pMeshOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMeshA ) ||
         !EditableMesh_IsInitialized( pMeshB ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !std::isfinite( tolerance ) || tolerance <= 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( EditableMesh_FaceCount( pMeshA ) > kMeshBoolMaxFaces ||
         EditableMesh_FaceCount( pMeshB ) > kMeshBoolMaxFaces ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    return ExecuteSingleResultOp(
        pMeshA, pMeshB, pAllocator, tolerance,
        mesh_bool_op_t::UNION, pMeshOut );
}

geometry_status_t MeshBool_TrySubtract(
    const editable_mesh_t *pMeshA,
    const editable_mesh_t *pMeshB,
    const allocator_t *pAllocator,
    f64 tolerance,
    editable_mesh_t *pMeshOut ) noexcept
{
    if ( pMeshA == nullptr || pMeshB == nullptr ||
         pAllocator == nullptr || pMeshOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMeshA ) ||
         !EditableMesh_IsInitialized( pMeshB ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !std::isfinite( tolerance ) || tolerance <= 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( EditableMesh_FaceCount( pMeshA ) > kMeshBoolMaxFaces ||
         EditableMesh_FaceCount( pMeshB ) > kMeshBoolMaxFaces ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    return ExecuteSubtract(
        pMeshA, pMeshB, pAllocator, tolerance, pMeshOut );
}

geometry_status_t MeshBool_TryIntersect(
    const editable_mesh_t *pMeshA,
    const editable_mesh_t *pMeshB,
    const allocator_t *pAllocator,
    f64 tolerance,
    editable_mesh_t *pMeshOut ) noexcept
{
    if ( pMeshA == nullptr || pMeshB == nullptr ||
         pAllocator == nullptr || pMeshOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMeshA ) ||
         !EditableMesh_IsInitialized( pMeshB ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !std::isfinite( tolerance ) || tolerance <= 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( EditableMesh_FaceCount( pMeshA ) > kMeshBoolMaxFaces ||
         EditableMesh_FaceCount( pMeshB ) > kMeshBoolMaxFaces ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    return ExecuteSingleResultOp(
        pMeshA, pMeshB, pAllocator, tolerance,
        mesh_bool_op_t::INTERSECT, pMeshOut );
}

} // namespace cypher::editor::geometry
