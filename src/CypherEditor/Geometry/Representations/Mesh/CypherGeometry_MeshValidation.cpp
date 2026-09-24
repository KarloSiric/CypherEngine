//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshValidation.cpp
//  Purpose: Implements structural validation for half-edge meshes.
//  Details: Each check runs independently and records its result in the
//           output struct. The overall status is set to the first failure
//           encountered, but all checks complete regardless so the caller
//           can see everything that's wrong in one pass.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshValidation.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// Check 1: every half-edge has a twin, and twin→twin is the original.
bool ValidateReciprocalTwins(
    const editable_mesh_t *pMesh ) noexcept
{
    bool valid = true;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t hHe,
             const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            if ( !GenerationHandle_IsValid( he.hTwin ) ) {
                valid = false;
                return false;
            }

            const mesh_half_edge_record_t *pTwin =
                GenerationPool_Get( &pMesh->halfEdges, he.hTwin );
            if ( pTwin == nullptr ) {
                valid = false;
                return false;
            }

            // Twin's twin must point back to this half-edge.
            if ( pTwin->hTwin.nSlot != hHe.nSlot ||
                 pTwin->hTwin.nGeneration != hHe.nGeneration ) {
                valid = false;
                return false;
            }

            // Reciprocal pointers alone accept a twin that runs the same
            // direction (a sign of a botched splice). The twin must start
            // where this half-edge ends, i.e. at next(he).origin.
            const mesh_half_edge_record_t *pNext =
                GenerationPool_Get( &pMesh->halfEdges, he.hNext );
            if ( pNext == nullptr ||
                 pTwin->hOrigin.nSlot != pNext->hOrigin.nSlot ||
                 pTwin->hOrigin.nGeneration != pNext->hOrigin.nGeneration ) {
                valid = false;
                return false;
            }
            return true;
        } );
    return valid;
}

// Check 2: every loop's half-edge ring is closed and has the stated count.
bool ValidateClosedLoops(
    const editable_mesh_t *pMesh ) noexcept
{
    bool valid = true;
    usize cCoveredHalfEdges = 0u;
    (void)GenerationPool_ForEach( &pMesh->loops,
        [&]( geometry_mesh_loop_handle_t hLoop,
             const mesh_loop_record_t &loop ) noexcept -> bool_t {
            const mesh_face_record_t *pFace =
                GenerationPool_Get( &pMesh->faces, loop.hFace );
            if ( pFace == nullptr ||
                 pFace->hOuterLoop.nSlot != hLoop.nSlot ||
                 pFace->hOuterLoop.nGeneration != hLoop.nGeneration ) {
                valid = false;
                return false;
            }
            if ( loop.cHalfEdges < 3u ) {
                valid = false;
                return false;
            }

            // Walk next pointers, counting steps. Every ring member must
            // name this loop as its owner and agree with its successor's
            // prev pointer; a count-only walk accepts rings whose prev
            // links or loop ownership were left stale by a splice.
            u32 count = 0u;
            geometry_mesh_half_edge_handle_t hCurr = loop.hFirstHalfEdge;
            do {
                const mesh_half_edge_record_t *pCurr =
                    GenerationPool_Get( &pMesh->halfEdges, hCurr );
                if ( pCurr == nullptr ||
                     GenerationPool_Get( &pMesh->vertices, pCurr->hOrigin ) == nullptr ||
                     pCurr->hLoop.nSlot != hLoop.nSlot ||
                     pCurr->hLoop.nGeneration != hLoop.nGeneration ) {
                    valid = false;
                    return false;
                }
                const mesh_half_edge_record_t *pNext =
                    GenerationPool_Get( &pMesh->halfEdges, pCurr->hNext );
                if ( pNext == nullptr ||
                     pNext->hPrev.nSlot != hCurr.nSlot ||
                     pNext->hPrev.nGeneration != hCurr.nGeneration ) {
                    valid = false;
                    return false;
                }
                hCurr = pCurr->hNext;
                ++count;
            } while ( count <= loop.cHalfEdges &&
                      ( hCurr.nSlot != loop.hFirstHalfEdge.nSlot ||
                        hCurr.nGeneration !=
                            loop.hFirstHalfEdge.nGeneration ) );

            if ( count != loop.cHalfEdges ) {
                valid = false;
                return false;
            }
            cCoveredHalfEdges += count;
            return true;
        } );
    if ( !valid ) { return false; }
    if ( cCoveredHalfEdges != GenerationPool_Count( &pMesh->halfEdges ) ) {
        return false;
    }

    // The loop walk above proves loop -> face -> loop. Check the other
    // direction as well so two faces cannot silently share one outer loop.
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hFace,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                GenerationPool_Get( &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr ||
                 pLoop->hFace.nSlot != hFace.nSlot ||
                 pLoop->hFace.nGeneration != hFace.nGeneration ) {
                valid = false;
                return false;
            }
            return true;
        } );
    return valid;
}

// Check 3: every vertex has at least one valid outgoing half-edge.
bool ValidateAllVerticesReferenced(
    const editable_mesh_t *pMesh ) noexcept
{
    bool valid = true;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t hV,
             const mesh_vertex_record_t &vert ) noexcept -> bool_t {
            if ( !GenerationHandle_IsValid( vert.hOutHalfEdge ) ) {
                valid = false;
                return false;
            }
            const mesh_half_edge_record_t *pHe =
                GenerationPool_Get( &pMesh->halfEdges,
                                    vert.hOutHalfEdge );
            // A live handle is not enough: fan walks start from this
            // pointer, so it must actually leave this vertex.
            if ( pHe == nullptr ||
                 pHe->hOrigin.nSlot != hV.nSlot ||
                 pHe->hOrigin.nGeneration != hV.nGeneration ) {
                valid = false;
                return false;
            }
            return true;
        } );
    return valid;
}

// Check 3b: half-edge -> edge -> half-edge links agree. Each half-edge
// names a live edge, and that edge's representative half-edge is either
// this half-edge or its twin. Collapse/dissolve splices that forget to
// re-home a surviving twin leave it naming a removed edge.
bool ValidateEdgeLinks(
    const editable_mesh_t *pMesh ) noexcept
{
    bool valid = true;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t hHe,
             const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            const mesh_edge_record_t *pEdge =
                GenerationPool_Get( &pMesh->edges, he.hEdge );
            if ( pEdge == nullptr ) {
                valid = false;
                return false;
            }
            const bool bSelf =
                pEdge->hHalfEdge.nSlot == hHe.nSlot &&
                pEdge->hHalfEdge.nGeneration == hHe.nGeneration;
            const bool bTwin =
                pEdge->hHalfEdge.nSlot == he.hTwin.nSlot &&
                pEdge->hHalfEdge.nGeneration == he.hTwin.nGeneration;
            if ( !bSelf && !bTwin ) {
                valid = false;
                return false;
            }
            if ( GenerationHandle_IsValid( he.hTwin ) ) {
                const mesh_half_edge_record_t *pTwinRecord =
                    GenerationPool_Get( &pMesh->halfEdges, he.hTwin );
                if ( pTwinRecord == nullptr ||
                     pTwinRecord->hEdge.nSlot != he.hEdge.nSlot ||
                     pTwinRecord->hEdge.nGeneration != he.hEdge.nGeneration ) {
                    valid = false;
                    return false;
                }
            }
            return true;
        } );
    if ( !valid ) { return false; }

    // Orphaned edge records inflate E and corrupt the Euler count even
    // when every half-edge is individually well formed.
    (void)GenerationPool_ForEach( &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t hEdge,
             const mesh_edge_record_t &edge ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pHe =
                GenerationPool_Get( &pMesh->halfEdges, edge.hHalfEdge );
            if ( pHe == nullptr ||
                 pHe->hEdge.nSlot != hEdge.nSlot ||
                 pHe->hEdge.nGeneration != hEdge.nGeneration ) {
                valid = false;
                return false;
            }
            return true;
        } );
    return valid;
}

// Check 3c: face <-> shell ownership is reciprocal and cached shell face
// counts are exact. The per-shell scan is bounded by the editable mesh's
// deliberately small shell limit and avoids adding a fallible scratch
// allocation to this diagnostic API.
bool ValidateShellLinks(
    const editable_mesh_t *pMesh ) noexcept
{
    bool valid = true;
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            if ( GenerationPool_Get( &pMesh->shells, face.hShell ) == nullptr ) {
                valid = false;
                return false;
            }
            return true;
        } );
    if ( !valid ) { return false; }

    (void)GenerationPool_ForEach( &pMesh->shells,
        [&]( geometry_mesh_shell_handle_t hShell,
             const mesh_shell_record_t &shell ) noexcept -> bool_t {
            const mesh_face_record_t *pRepresentative =
                GenerationPool_Get( &pMesh->faces, shell.hAnyFace );
            if ( pRepresentative == nullptr ||
                 pRepresentative->hShell.nSlot != hShell.nSlot ||
                 pRepresentative->hShell.nGeneration != hShell.nGeneration ) {
                valid = false;
                return false;
            }

            u32 cFaces = 0u;
            (void)GenerationPool_ForEach( &pMesh->faces,
                [&]( geometry_mesh_face_handle_t,
                     const mesh_face_record_t &face ) noexcept -> bool_t {
                    if ( face.hShell.nSlot == hShell.nSlot &&
                         face.hShell.nGeneration == hShell.nGeneration ) {
                        ++cFaces;
                    }
                    return true;
                } );
            if ( cFaces == 0u || cFaces != shell.cFaces ) {
                valid = false;
                return false;
            }
            return true;
        } );
    return valid;
}

// Check 4: Euler characteristic matches 2 × shell count.
bool ValidateEuler(
    const editable_mesh_t *pMesh,
    i32 chi ) noexcept
{
    const usize cShells = EditableMesh_ShellCount( pMesh );
    // For a closed orientable surface: V - E + F = 2 per shell.
    return chi == static_cast<i32>( 2u * cShells );
}

// Check 5: face normals agree with CCW winding from vertex positions.
bool ValidateConsistentWinding(
    const editable_mesh_t *pMesh ) noexcept
{
    bool valid = true;
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t /*hFace*/,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                GenerationPool_Get( &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ) {
                valid = false;
                return false;
            }

            // Winding normal via Newell's method over every corner. Using
            // only the first three corners misreports any face whose first
            // corners are collinear (every face touched by SplitEdge), and
            // misreports concave faces whose first corner is reflex.
            math::vec3d_t windingNormal = math::Vec3d_Make( 0.0, 0.0, 0.0 );
            geometry_mesh_half_edge_handle_t hCur = pLoop->hFirstHalfEdge;
            for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
                const mesh_half_edge_record_t *pHe =
                    GenerationPool_Get( &pMesh->halfEdges, hCur );
                if ( pHe == nullptr ) { valid = false; return false; }
                const mesh_half_edge_record_t *pNext =
                    GenerationPool_Get( &pMesh->halfEdges, pHe->hNext );
                if ( pNext == nullptr ) { valid = false; return false; }
                const mesh_vertex_record_t *pA =
                    GenerationPool_Get( &pMesh->vertices, pHe->hOrigin );
                const mesh_vertex_record_t *pB =
                    GenerationPool_Get( &pMesh->vertices, pNext->hOrigin );
                if ( pA == nullptr || pB == nullptr ) {
                    valid = false;
                    return false;
                }
                const math::vec3d_t a = pA->position;
                const math::vec3d_t b = pB->position;
                windingNormal.x += ( a.y - b.y ) * ( a.z + b.z );
                windingNormal.y += ( a.z - b.z ) * ( a.x + b.x );
                windingNormal.z += ( a.x - b.x ) * ( a.y + b.y );
                hCur = pHe->hNext;
            }

            // The winding normal should agree with the stored face normal.
            const f64 dot = math::Vec3d_Dot( windingNormal, face.normal );
            if ( !std::isfinite( dot ) || dot <= 0.0 ) {
                valid = false;
                return false;
            }
            return true;
        } );
    return valid;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

mesh_validation_result_t MeshValidation_Validate(
    const editable_mesh_t *pMesh ) noexcept
{
    mesh_validation_result_t result{};

    if ( pMesh == nullptr || !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    // Run all checks unconditionally.
    result.bReciprocalTwins = ValidateReciprocalTwins( pMesh );
    result.bClosedLoops = ValidateClosedLoops( pMesh );
    result.bAllVerticesReferenced = ValidateAllVerticesReferenced( pMesh );
    result.bEdgeLinks = ValidateEdgeLinks( pMesh );
    result.bShellLinks = ValidateShellLinks( pMesh );

    result.nEulerCharacteristic =
        EditableMesh_EulerCharacteristic( pMesh );
    result.bEulerValid = ValidateEuler(
        pMesh, result.nEulerCharacteristic );

    result.bConsistentWinding = ValidateConsistentWinding( pMesh );

    result.fSignedVolume = EditableMesh_SignedVolume( pMesh );
    result.bPositiveVolume = result.fSignedVolume > 0.0;

    // Set overall status to the first failure.
    if ( !result.bReciprocalTwins ) {
        result.status = geometry_status_t::INVALID_TOPOLOGY;
    } else if ( !result.bClosedLoops ) {
        result.status = geometry_status_t::INVALID_TOPOLOGY;
    } else if ( !result.bAllVerticesReferenced ) {
        result.status = geometry_status_t::INVALID_TOPOLOGY;
    } else if ( !result.bEdgeLinks ) {
        result.status = geometry_status_t::INVALID_TOPOLOGY;
    } else if ( !result.bShellLinks ) {
        result.status = geometry_status_t::INVALID_TOPOLOGY;
    } else if ( !result.bEulerValid ) {
        result.status = geometry_status_t::NON_MANIFOLD;
    } else if ( !result.bConsistentWinding ) {
        result.status = geometry_status_t::INVALID_TOPOLOGY;
    } else if ( !result.bPositiveVolume ) {
        result.status = geometry_status_t::OPEN_VOLUME;
    }

    return result;
}

} // namespace cypher::editor::geometry
