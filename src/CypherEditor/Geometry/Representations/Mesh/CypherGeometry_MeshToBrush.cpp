//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshToBrush.cpp
//  Purpose: Implements the mesh-to-brush round-trip conversion.
//  Details: Iterates each face of the input mesh, computes a plane from
//           the face's stored normal and one of its vertex positions,
//           and adds it as a brush side. The plane normal is taken
//           directly from the face record (computed by Newell's method
//           during mesh construction and maintained by topology ops).
//
//           The d constant is computed as -dot(normal, point) so that
//           the plane equation dot(normal, P) + d = 0 holds for any
//           vertex P on the face.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshToBrush.h"
#include "CypherGeometry_MeshValidation.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

bool BrushOutputIsCanonicalEmpty( const brush_solid_t &brush ) noexcept
{
    return brush.sides.pData == nullptr &&
           brush.sides.nCount == 0u &&
           brush.sides.nCapacity == 0u &&
           brush.sides.pAllocator == nullptr &&
           !GeometrySourceId_IsValid( brush.sourceId );
}

bool CoordinateIsWithinPolicy(
    math::vec3d_t point,
    const geometry_numerical_policy_t &policy ) noexcept
{
    return math::Vec3d_IsFinite( point ) &&
           std::fabs( point.x ) <= policy.fCoordinateMagnitudeLimit &&
           std::fabs( point.y ) <= policy.fCoordinateMagnitudeLimit &&
           std::fabs( point.z ) <= policy.fCoordinateMagnitudeLimit;
}

} // namespace

geometry_status_t MeshToBrush_TryConvert(
    const editable_mesh_t *pMeshIn,
    brush_solid_t *pBrushOut,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept
{
    if ( pMeshIn == nullptr || pBrushOut == nullptr ||
         pAllocator == nullptr || pIdAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrushOut->sides.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !BrushOutputIsCanonicalEmpty( *pBrushOut ) ||
         !Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMeshIn ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    const mesh_validation_result_t validation =
        MeshValidation_Validate( pMeshIn );
    if ( validation.status != geometry_status_t::OK ) {
        return validation.status;
    }
    if ( EditableMesh_ShellCount( pMeshIn ) != 1u ) {
        return geometry_status_t::UNSUPPORTED;
    }

    const usize cFaces = EditableMesh_FaceCount( pMeshIn );
    if ( cFaces < 4u ) {
        return geometry_status_t::DEGENERATE;
    }

    // --- Preflight. A brush is the intersection of its side half-spaces,
    // so converting a face set that is not planar-and-convex does not fail
    // naturally: it silently produces a *different*, convex shape. Every
    // check therefore runs before any source ID is allocated, keeping the
    // ID allocator sequence unchanged on failure (Gate 0 contract).
    //
    // Cost: O(F * V) plane tests. Bounded by the mesh limits and far below
    // the cost of the CSG that typically follows.
    const f64 planarTol = policy.numerical.fPlanarityTolerance;
    const f64 convexTol = policy.numerical.fCoplanarDistanceTolerance;
    geometry_status_t preflight = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &pMeshIn->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &vertex ) noexcept -> bool_t {
            if ( !math::Vec3d_IsFinite( vertex.position ) ) {
                preflight = geometry_status_t::NUMERIC_FAILURE;
                return false;
            }
            if ( !CoordinateIsWithinPolicy(
                     vertex.position, policy.numerical ) ) {
                preflight = geometry_status_t::LIMIT_EXCEEDED;
                return false;
            }
            return true;
        } );
    if ( preflight != geometry_status_t::OK ) {
        return preflight;
    }

    (void)GenerationPool_ForEach( &pMeshIn->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            if ( !math::Vec3d_IsFinite( face.normal ) ) {
                preflight = geometry_status_t::NUMERIC_FAILURE;
                return false;
            }
            const f64 normalLengthSquared =
                math::Vec3d_LengthSquared( face.normal );
            if ( !math::Scalar_IsFinite( normalLengthSquared ) ||
                 std::fabs( normalLengthSquared - 1.0 ) >
                     policy.numerical.fUnitNormalTolerance ) {
                preflight = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            const mesh_loop_record_t *pLoop =
                GenerationPool_Get( &pMeshIn->loops, face.hOuterLoop );
            const mesh_half_edge_record_t *pFirst = pLoop != nullptr
                ? GenerationPool_Get( &pMeshIn->halfEdges,
                                      pLoop->hFirstHalfEdge )
                : nullptr;
            const mesh_vertex_record_t *pAnchor = pFirst != nullptr
                ? GenerationPool_Get( &pMeshIn->vertices, pFirst->hOrigin )
                : nullptr;
            if ( pAnchor == nullptr ) {
                preflight = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            const f64 d = -math::Vec3d_Dot( face.normal, pAnchor->position );

            // Planarity: every corner of this face on its own plane.
            geometry_mesh_half_edge_handle_t hCur = pLoop->hFirstHalfEdge;
            for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
                const mesh_half_edge_record_t *pHE =
                    GenerationPool_Get( &pMeshIn->halfEdges, hCur );
                const mesh_vertex_record_t *pV = pHE != nullptr
                    ? GenerationPool_Get( &pMeshIn->vertices, pHE->hOrigin )
                    : nullptr;
                if ( pV == nullptr ) {
                    preflight = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                const f64 dist = math::Vec3d_Dot( face.normal, pV->position ) + d;
                if ( std::fabs( dist ) > planarTol ) {
                    preflight = geometry_status_t::NON_PLANAR;
                    return false;
                }
                hCur = pHE->hNext;
            }

            // Convexity: no mesh vertex strictly in front of any face plane.
            // For a closed outward-wound mesh this is equivalent to the
            // solid being convex.
            bool bFront = false;
            (void)GenerationPool_ForEach( &pMeshIn->vertices,
                [&]( geometry_mesh_vertex_handle_t,
                     const mesh_vertex_record_t &v ) noexcept -> bool_t {
                    if ( math::Vec3d_Dot( face.normal, v.position ) + d >
                         convexTol ) {
                        bFront = true;
                        return false;
                    }
                    return true;
                } );
            if ( bFront ) {
                // UNSUPPORTED rather than a topology error: the mesh may be
                // perfectly valid, it just has no single-brush equivalent.
                preflight = geometry_status_t::UNSUPPORTED;
                return false;
            }
            return true;
        } );
    if ( preflight != geometry_status_t::OK ) {
        return preflight;
    }

    // Allocate identities and construct into private state. Neither the
    // caller's allocator sequence nor its output object is published until
    // every fallible operation has succeeded.
    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    const geometry_source_id_result_t brushIdResult =
        GeometrySourceIdAllocator_Allocate( &stagedIds );
    if ( brushIdResult.status != geometry_status_t::OK ) {
        return brushIdResult.status;
    }

    brush_solid_t stagedBrush{};
    geometry_status_t s = BrushSolid_Init(
        &stagedBrush, pAllocator, brushIdResult.id );
    if ( s != geometry_status_t::OK ) { return s; }

    // A triangulated mesh can have many coplanar faces but only a few unique
    // brush planes. Reserve no more than the side policy allows; the staged
    // brush may still grow incrementally if the final unique count is larger.
    const usize cReserve = cFaces < static_cast<usize>(
        policy.limits.cBrushSidesPerBrushMax )
        ? cFaces
        : static_cast<usize>( policy.limits.cBrushSidesPerBrushMax );
    s = BrushSolid_TryReserve( &stagedBrush, policy.limits, cReserve );
    if ( s != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &stagedBrush );
        return s;
    }

    // For each face, compute a plane from its normal and a vertex position.
    bool failed = false;
    geometry_status_t failStatus = geometry_status_t::OK;

    (void)GenerationPool_ForEach( &pMeshIn->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            if ( failed ) { return false; }

            const mesh_loop_record_t *pLoop =
                GenerationPool_Get( &pMeshIn->loops, face.hOuterLoop );
            if ( pLoop == nullptr ) {
                failed = true;
                failStatus = geometry_status_t::CORRUPT_STATE;
                return false;
            }

            // Get any vertex on this face to compute d.
            const mesh_half_edge_record_t *pHE =
                GenerationPool_Get( &pMeshIn->halfEdges,
                                    pLoop->hFirstHalfEdge );
            if ( pHE == nullptr ) {
                failed = true;
                failStatus = geometry_status_t::CORRUPT_STATE;
                return false;
            }

            const mesh_vertex_record_t *pVert =
                GenerationPool_Get( &pMeshIn->vertices, pHE->hOrigin );
            if ( pVert == nullptr ) {
                failed = true;
                failStatus = geometry_status_t::CORRUPT_STATE;
                return false;
            }

            // Plane: dot(normal, point) + d = 0 → d = -dot(normal, point).
            const math::vec3d_t &n = face.normal;
            const f64 d = -math::Vec3d_Dot( n, pVert->position );

            // Coplanar faces (e.g. a triangulated quad) describe the same
            // half-space. Emitting both would give the brush duplicate
            // planes, which brush validation treats as redundant sides.
            const usize cExisting = BrushSolid_SideCount( &stagedBrush );
            for ( usize i = 0u; i < cExisting; ++i ) {
                brush_solid_side_t existing{};
                const geometry_status_t getStatus =
                    BrushSolid_TryGetSide( &stagedBrush, i, &existing );
                if ( getStatus != geometry_status_t::OK ) {
                    failed = true;
                    failStatus = getStatus;
                    return false;
                }
                if ( math::Vec3d_Dot( existing.plane.normal, n ) >
                         1.0 - 1.0e-9 &&
                     std::fabs( existing.plane.d - d ) <= convexTol ) {
                    return true;
                }
            }

            brush_solid_side_t side{};
            side.plane = math::Planed_Make( n, d );
            const geometry_source_id_result_t sideIdResult =
                GeometrySourceIdAllocator_Allocate( &stagedIds );
            if ( sideIdResult.status != geometry_status_t::OK ) {
                failed = true;
                failStatus = sideIdResult.status;
                return false;
            }
            side.sourceId = sideIdResult.id;
            side.iAttributeIndex = 0u;

            const geometry_status_t addStatus = BrushSolid_TryAddSide(
                &stagedBrush, policy.limits, side, nullptr );
            if ( addStatus != geometry_status_t::OK ) {
                failed = true;
                failStatus = addStatus;
                return false;
            }

            return true;
        } );

    if ( failed ) {
        BrushSolid_Shutdown( &stagedBrush );
        return failStatus;
    }

    if ( BrushSolid_SideCount( &stagedBrush ) < 4u ) {
        BrushSolid_Shutdown( &stagedBrush );
        return geometry_status_t::DEGENERATE;
    }

    // Publish is pointer-only and cannot fail.
    pBrushOut->sourceId = stagedBrush.sourceId;
    Vector_Move( &pBrushOut->sides, &stagedBrush.sides );
    stagedBrush.sourceId = GEOMETRY_SOURCE_ID_INVALID;
    *pIdAllocator = stagedIds;

    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
