//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshCleanup.cpp
//  Purpose: Implements mesh cleanup primitives, deep clone, and per-corner
//           auto-smooth normal evaluation.
//  Details: Cleanup passes snapshot handles before mutating because the
//           generation pool forbids structural mutation during ForEach.
//           Snapshots are heap-sized from the live counts so no pass can
//           silently truncate on large meshes.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//  - 2026-09-23: clone handle remapping, heap-sized snapshots, honest
//    normal recalculation, convex decomposition removed.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshCleanup.h"
#include "CypherGeometry_MeshTopologyOps.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr f64 kPi = 3.141592653589793238462643383279502884;

// Heap array of handles sized to a live element count. Frees on scope exit
// so every early return in the passes below stays leak-free.
template <typename handle_t>
struct handle_list_t {
    const allocator_t *pAllocator{ nullptr };
    handle_t *pData{ nullptr };
    usize cCapacity{ 0u };
    usize cCount{ 0u };

    handle_list_t( const allocator_t *pAlloc, usize cMax ) noexcept
        : pAllocator( pAlloc ), cCapacity( cMax ) {
        if ( cMax > 0u ) {
            pData = static_cast<handle_t *>( Allocator_Allocate(
                pAllocator, cMax * sizeof( handle_t ), alignof( handle_t ) ) );
        }
    }
    ~handle_list_t() {
        if ( pData != nullptr ) {
            Allocator_Free( pAllocator, pData, cCapacity * sizeof( handle_t ),
                            alignof( handle_t ) );
        }
    }
    handle_list_t( const handle_list_t & ) = delete;
    handle_list_t &operator=( const handle_list_t & ) = delete;

    bool Ok() const noexcept { return cCapacity == 0u || pData != nullptr; }
    void Push( handle_t h ) noexcept {
        if ( cCount < cCapacity ) { pData[cCount++] = h; }
    }
};

// Area of one face by fan-triangulating its loop. Fan area is exact for
// planar simple polygons (convex or concave) because the signed triangle
// areas cancel correctly; it is only an approximation for non-planar faces.
f64 ComputeFaceArea(
    const editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace ) noexcept
{
    const mesh_face_record_t *pFace =
        GenerationPool_Get( &pMesh->faces, hFace );
    if ( pFace == nullptr ) { return 0.0; }
    const mesh_loop_record_t *pLoop =
        GenerationPool_Get( &pMesh->loops, pFace->hOuterLoop );
    if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ) { return 0.0; }

    // Sum of cross products (Newell form) gives twice the vector area.
    math::vec3d_t twiceArea = math::Vec3d_Make( 0.0, 0.0, 0.0 );
    geometry_mesh_half_edge_handle_t hCur = pLoop->hFirstHalfEdge;
    for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
        const mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, hCur );
        if ( pHE == nullptr ) { return 0.0; }
        const mesh_half_edge_record_t *pNext =
            GenerationPool_Get( &pMesh->halfEdges, pHE->hNext );
        if ( pNext == nullptr ) { return 0.0; }
        const mesh_vertex_record_t *pA =
            GenerationPool_Get( &pMesh->vertices, pHE->hOrigin );
        const mesh_vertex_record_t *pB =
            GenerationPool_Get( &pMesh->vertices, pNext->hOrigin );
        if ( pA == nullptr || pB == nullptr ) { return 0.0; }
        twiceArea = math::Vec3d_Add(
            twiceArea, math::Vec3d_Cross( pA->position, pB->position ) );
        hCur = pHE->hNext;
    }
    return 0.5 * std::sqrt( math::Vec3d_LengthSquared( twiceArea ) );
}

// Newell normal of a face loop (unnormalized). Robust to collinear corners
// and concave loops, unlike a three-corner cross product.
math::vec3d_t ComputeNewellNormal(
    const editable_mesh_t *pMesh,
    const mesh_loop_record_t *pLoop ) noexcept
{
    math::vec3d_t normal = math::Vec3d_Make( 0.0, 0.0, 0.0 );
    geometry_mesh_half_edge_handle_t hCur = pLoop->hFirstHalfEdge;
    for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
        const mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, hCur );
        if ( pHE == nullptr ) { break; }
        const mesh_half_edge_record_t *pNext =
            GenerationPool_Get( &pMesh->halfEdges, pHE->hNext );
        if ( pNext == nullptr ) { break; }
        const mesh_vertex_record_t *pVA =
            GenerationPool_Get( &pMesh->vertices, pHE->hOrigin );
        const mesh_vertex_record_t *pVB =
            GenerationPool_Get( &pMesh->vertices, pNext->hOrigin );
        if ( pVA == nullptr || pVB == nullptr ) { break; }
        const math::vec3d_t a = pVA->position;
        const math::vec3d_t b = pVB->position;
        normal.x += ( a.y - b.y ) * ( a.z + b.z );
        normal.y += ( a.z - b.z ) * ( a.x + b.x );
        normal.z += ( a.x - b.x ) * ( a.y + b.y );
        hCur = pHE->hNext;
    }
    return normal;
}

// Source-slot -> destination-handle table for one pool, used by clone.
template <typename tag_t>
struct slot_map_t {
    const allocator_t *pAllocator{ nullptr };
    geometry_handle_t<tag_t> *pMap{ nullptr };
    usize cSlots{ 0u };

    slot_map_t( const allocator_t *pAlloc, usize c ) noexcept
        : pAllocator( pAlloc ), cSlots( c ) {
        if ( c > 0u ) {
            pMap = static_cast<geometry_handle_t<tag_t> *>( Allocator_Allocate(
                pAllocator, c * sizeof( geometry_handle_t<tag_t> ),
                alignof( geometry_handle_t<tag_t> ) ) );
            if ( pMap != nullptr ) {
                for ( usize i = 0u; i < c; ++i ) {
                    pMap[i] = GEOMETRY_HANDLE_INVALID<tag_t>;
                }
            }
        }
    }
    ~slot_map_t() {
        if ( pMap != nullptr ) {
            Allocator_Free( pAllocator, pMap,
                            cSlots * sizeof( geometry_handle_t<tag_t> ),
                            alignof( geometry_handle_t<tag_t> ) );
        }
    }
    slot_map_t( const slot_map_t & ) = delete;
    slot_map_t &operator=( const slot_map_t & ) = delete;

    bool Ok() const noexcept { return cSlots == 0u || pMap != nullptr; }
};

// Rewrites one stored source handle into its destination handle. Invalid
// handles stay invalid (legitimate for e.g. open-boundary twins). A handle
// that looks valid but does not resolve in the source pool is stale, which
// means the source was already corrupt; clone refuses to copy that.
template <typename record_t, typename tag_t>
bool RemapHandle(
    const generation_pool_t<record_t, tag_t> *pSrcPool,
    const slot_map_t<tag_t> &map,
    geometry_handle_t<tag_t> *pHandle ) noexcept
{
    if ( !GenerationHandle_IsValid( *pHandle ) ) {
        *pHandle = GEOMETRY_HANDLE_INVALID<tag_t>;
        return true;
    }
    if ( !GenerationPool_Contains( pSrcPool, *pHandle ) ||
         pHandle->nSlot >= map.cSlots ) {
        return false;
    }
    *pHandle = map.pMap[pHandle->nSlot];
    return GenerationHandle_IsValid( *pHandle );
}

// Copies every live record of one pool into the destination and records the
// slot mapping. Records still contain *source* handles after this step.
template <typename record_t, typename tag_t>
geometry_status_t CopyPool(
    const generation_pool_t<record_t, tag_t> *pSrc,
    generation_pool_t<record_t, tag_t> *pDst,
    slot_map_t<tag_t> *pMap ) noexcept
{
    geometry_status_t status = geometry_status_t::OK;
    const usize cVisited = GenerationPool_ForEach( pSrc,
        [&]( geometry_handle_t<tag_t> hSrc,
             const record_t &rec ) noexcept -> bool_t {
            const auto ins = GenerationPool_Insert( pDst, rec );
            if ( ins.status != generation_pool_status_t::OK ) {
                status = GeometryStatus_FromGenerationPoolStatus( ins.status );
                return false;
            }
            if ( hSrc.nSlot >= pMap->cSlots ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            pMap->pMap[hSrc.nSlot] = ins.handle;
            return true;
        } );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    return cVisited == GenerationPool_Count( pSrc )
        ? geometry_status_t::OK
        : geometry_status_t::CORRUPT_STATE;
}

bool CloneSourcePoolsAreValid( const editable_mesh_t *pMesh ) noexcept
{
    return GenerationPool_IsValid( &pMesh->vertices ) &&
           GenerationPool_IsValid( &pMesh->halfEdges ) &&
           GenerationPool_IsValid( &pMesh->edges ) &&
           GenerationPool_IsValid( &pMesh->loops ) &&
           GenerationPool_IsValid( &pMesh->faces ) &&
           GenerationPool_IsValid( &pMesh->shells );
}

} // namespace

// ---------------------------------------------------------------------------
// Remove degenerate faces
// ---------------------------------------------------------------------------

mesh_remove_degenerate_result_t
MeshCleanup_RemoveDegenerateFaces(
    editable_mesh_t *pMesh,
    f64 fAreaTolerance ) noexcept
{
    mesh_remove_degenerate_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !std::isfinite( fAreaTolerance ) || fAreaTolerance < 0.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    handle_list_t<geometry_mesh_face_handle_t> degenerate(
        pMesh->pAllocator, EditableMesh_FaceCount( pMesh ) );
    if ( !degenerate.Ok() ) {
        result.status = geometry_status_t::ALLOCATION_FAILED;
        return result;
    }

    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hFace,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                GenerationPool_Get( &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr ) { return true; }
            if ( pLoop->cHalfEdges < 3u ||
                 ( fAreaTolerance > 0.0 &&
                   ComputeFaceArea( pMesh, hFace ) < fAreaTolerance ) ) {
                degenerate.Push( hFace );
            }
            return true;
        } );

    for ( usize i = 0u; i < degenerate.cCount; ++i ) {
        // An earlier dissolve may already have merged this face away.
        const mesh_face_record_t *pFace =
            GenerationPool_Get( &pMesh->faces, degenerate.pData[i] );
        if ( pFace == nullptr ) { continue; }
        const mesh_loop_record_t *pLoop =
            GenerationPool_Get( &pMesh->loops, pFace->hOuterLoop );
        if ( pLoop == nullptr ) { continue; }
        const mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, pLoop->hFirstHalfEdge );
        if ( pHE == nullptr ) { continue; }

        if ( MeshOps_DissolveEdge( pMesh, pHE->hEdge ) ==
             geometry_status_t::OK ) {
            ++result.cFacesRemoved;
        }
    }

    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// Remove isolated vertices
// ---------------------------------------------------------------------------

mesh_remove_isolated_result_t
MeshCleanup_RemoveIsolatedVertices(
    editable_mesh_t *pMesh ) noexcept
{
    mesh_remove_isolated_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }

    handle_list_t<geometry_mesh_vertex_handle_t> isolated(
        pMesh->pAllocator, EditableMesh_VertexCount( pMesh ) );
    if ( !isolated.Ok() ) {
        result.status = geometry_status_t::ALLOCATION_FAILED;
        return result;
    }

    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t hVert,
             const mesh_vertex_record_t &vert ) noexcept -> bool_t {
            if ( !GenerationHandle_IsValid( vert.hOutHalfEdge ) ) {
                isolated.Push( hVert );
            }
            return true;
        } );

    for ( usize i = 0u; i < isolated.cCount; ++i ) {
        if ( GenerationPool_Remove( &pMesh->vertices, isolated.pData[i] ) ==
             generation_pool_status_t::OK ) {
            ++result.cVerticesRemoved;
        }
    }

    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// Recalculate normals
// ---------------------------------------------------------------------------

mesh_recalc_normals_result_t
MeshCleanup_RecalculateNormals(
    editable_mesh_t *pMesh ) noexcept
{
    mesh_recalc_normals_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }

    // cos(~0.06 deg): below this the old and new normals are the same
    // direction up to rounding, so the face is not reported as changed.
    static constexpr f64 kSameDirectionCos = 0.9999995;

    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t,
             mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                GenerationPool_Get( &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ) { return true; }

            const math::vec3d_t n = ComputeNewellNormal( pMesh, pLoop );
            const f64 lenSq = math::Vec3d_LengthSquared( n );
            // Zero-area faces keep their previous normal; they are a
            // degeneracy for RemoveDegenerateFaces/Validation, not a
            // direction this pass can invent.
            if ( !( lenSq > 1.0e-30 ) ) { return true; }

            const math::vec3d_t unit =
                math::Vec3d_Scale( n, 1.0 / std::sqrt( lenSq ) );
            if ( math::Vec3d_Dot( unit, face.normal ) < kSameDirectionCos ) {
                ++result.cNormalsChanged;
            }
            face.normal = unit;
            return true;
        } );

    // SignedVolume integrates over loop winding, not stored normals, so it
    // is the right orientation witness for a closed shell.
    result.bInwardWinding = EditableMesh_SignedVolume( pMesh ) < 0.0;
    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// Full cleanup
// ---------------------------------------------------------------------------

mesh_cleanup_result_t MeshCleanup_Full(
    editable_mesh_t *pMesh,
    f64 fAreaTolerance ) noexcept
{
    mesh_cleanup_result_t result{};

    const auto degen = MeshCleanup_RemoveDegenerateFaces( pMesh, fAreaTolerance );
    if ( degen.status != geometry_status_t::OK ) {
        result.status = degen.status;
        return result;
    }
    result.cDegenerateFacesRemoved = degen.cFacesRemoved;

    const auto iso = MeshCleanup_RemoveIsolatedVertices( pMesh );
    if ( iso.status != geometry_status_t::OK ) {
        result.status = iso.status;
        return result;
    }
    result.cIsolatedVerticesRemoved = iso.cVerticesRemoved;

    const auto norm = MeshCleanup_RecalculateNormals( pMesh );
    if ( norm.status != geometry_status_t::OK ) {
        result.status = norm.status;
        return result;
    }
    result.cNormalsChanged = norm.cNormalsChanged;
    result.bInwardWinding = norm.bInwardWinding;

    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// Mesh clone
// ---------------------------------------------------------------------------

geometry_status_t EditableMesh_TryClone(
    const editable_mesh_t *pMeshSrc,
    const allocator_t *pAllocator,
    editable_mesh_t *pMeshOut ) noexcept
{
    if ( pMeshSrc == nullptr || pAllocator == nullptr ||
         pMeshOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMeshSrc ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !CloneSourcePoolsAreValid( pMeshSrc ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    // The clone inherits the source's limits so an edit that fits the
    // source also fits the copy.
    geometry_status_t s =
        EditableMesh_Init( pMeshOut, pAllocator, pMeshSrc->limits );
    if ( s != geometry_status_t::OK ) { return s; }

    // Why remap at all: a fresh pool hands out slots densely from 0 with
    // first-generation handles, while the source may have holes and bumped
    // generations from deletions. Stored handles copied verbatim would then
    // point at the wrong records (or none).
    slot_map_t<geometry_mesh_vertex_tag_t> vMap(
        pAllocator, GenerationPool_Capacity( &pMeshSrc->vertices ) );
    slot_map_t<geometry_mesh_half_edge_tag_t> hMap(
        pAllocator, GenerationPool_Capacity( &pMeshSrc->halfEdges ) );
    slot_map_t<geometry_mesh_edge_tag_t> eMap(
        pAllocator, GenerationPool_Capacity( &pMeshSrc->edges ) );
    slot_map_t<geometry_mesh_loop_tag_t> lMap(
        pAllocator, GenerationPool_Capacity( &pMeshSrc->loops ) );
    slot_map_t<geometry_mesh_face_tag_t> fMap(
        pAllocator, GenerationPool_Capacity( &pMeshSrc->faces ) );
    slot_map_t<geometry_mesh_shell_tag_t> sMap(
        pAllocator, GenerationPool_Capacity( &pMeshSrc->shells ) );
    if ( !vMap.Ok() || !hMap.Ok() || !eMap.Ok() || !lMap.Ok() ||
         !fMap.Ok() || !sMap.Ok() ) {
        EditableMesh_Shutdown( pMeshOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // Pass 1: copy records and build the slot tables. Preserve the exact
    // generation-pool failure status rather than collapsing allocation and
    // corruption failures into LIMIT_EXCEEDED.
    s = CopyPool( &pMeshSrc->vertices, &pMeshOut->vertices, &vMap );
    if ( s == geometry_status_t::OK ) {
        s = CopyPool( &pMeshSrc->halfEdges, &pMeshOut->halfEdges, &hMap );
    }
    if ( s == geometry_status_t::OK ) {
        s = CopyPool( &pMeshSrc->edges, &pMeshOut->edges, &eMap );
    }
    if ( s == geometry_status_t::OK ) {
        s = CopyPool( &pMeshSrc->loops, &pMeshOut->loops, &lMap );
    }
    if ( s == geometry_status_t::OK ) {
        s = CopyPool( &pMeshSrc->faces, &pMeshOut->faces, &fMap );
    }
    if ( s == geometry_status_t::OK ) {
        s = CopyPool( &pMeshSrc->shells, &pMeshOut->shells, &sMap );
    }
    if ( s != geometry_status_t::OK ) {
        EditableMesh_Shutdown( pMeshOut );
        return s;
    }

    // Pass 2: rewrite every stored handle from source to destination space.
    bool ok = true;
    (void)GenerationPool_ForEach( &pMeshOut->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             mesh_vertex_record_t &v ) noexcept -> bool_t {
            ok = RemapHandle( &pMeshSrc->halfEdges, hMap, &v.hOutHalfEdge );
            return ok;
        } );
    if ( ok ) {
        (void)GenerationPool_ForEach( &pMeshOut->halfEdges,
            [&]( geometry_mesh_half_edge_handle_t,
                 mesh_half_edge_record_t &he ) noexcept -> bool_t {
                ok = RemapHandle( &pMeshSrc->vertices, vMap, &he.hOrigin ) &&
                     RemapHandle( &pMeshSrc->halfEdges, hMap, &he.hTwin ) &&
                     RemapHandle( &pMeshSrc->halfEdges, hMap, &he.hNext ) &&
                     RemapHandle( &pMeshSrc->halfEdges, hMap, &he.hPrev ) &&
                     RemapHandle( &pMeshSrc->edges, eMap, &he.hEdge ) &&
                     RemapHandle( &pMeshSrc->loops, lMap, &he.hLoop );
                return ok;
            } );
    }
    if ( ok ) {
        (void)GenerationPool_ForEach( &pMeshOut->edges,
            [&]( geometry_mesh_edge_handle_t,
                 mesh_edge_record_t &e ) noexcept -> bool_t {
                ok = RemapHandle( &pMeshSrc->halfEdges, hMap, &e.hHalfEdge );
                return ok;
            } );
    }
    if ( ok ) {
        (void)GenerationPool_ForEach( &pMeshOut->loops,
            [&]( geometry_mesh_loop_handle_t,
                 mesh_loop_record_t &l ) noexcept -> bool_t {
                ok = RemapHandle( &pMeshSrc->halfEdges, hMap,
                                  &l.hFirstHalfEdge ) &&
                     RemapHandle( &pMeshSrc->faces, fMap, &l.hFace );
                return ok;
            } );
    }
    if ( ok ) {
        (void)GenerationPool_ForEach( &pMeshOut->faces,
            [&]( geometry_mesh_face_handle_t,
                 mesh_face_record_t &f ) noexcept -> bool_t {
                ok = RemapHandle( &pMeshSrc->loops, lMap, &f.hOuterLoop ) &&
                     RemapHandle( &pMeshSrc->shells, sMap, &f.hShell );
                return ok;
            } );
    }
    if ( ok ) {
        (void)GenerationPool_ForEach( &pMeshOut->shells,
            [&]( geometry_mesh_shell_handle_t,
                 mesh_shell_record_t &sh ) noexcept -> bool_t {
                ok = RemapHandle( &pMeshSrc->faces, fMap, &sh.hAnyFace );
                return ok;
            } );
    }
    if ( !ok ) {
        EditableMesh_Shutdown( pMeshOut );
        return geometry_status_t::CORRUPT_STATE;
    }

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Auto-smooth normals
// ---------------------------------------------------------------------------

mesh_auto_smooth_result_t
MeshCleanup_ComputeAutoSmoothNormals(
    const editable_mesh_t *pMesh,
    const allocator_t *pAllocator,
    f64 fAngleThresholdRadians ) noexcept
{
    mesh_auto_smooth_result_t result{};

    if ( pMesh == nullptr || pAllocator == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !std::isfinite( fAngleThresholdRadians ) ||
         fAngleThresholdRadians < 0.0 ||
         fAngleThresholdRadians > kPi ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    const usize cHalfEdges = EditableMesh_HalfEdgeCount( pMesh );
    if ( cHalfEdges == 0u ) {
        result.status = geometry_status_t::OK;
        return result;
    }

    const usize cBytes = cHalfEdges * sizeof( mesh_corner_normal_t );
    auto *pNormals = static_cast<mesh_corner_normal_t *>(
        Allocator_Allocate( pAllocator, cBytes,
                            alignof( mesh_corner_normal_t ) ) );
    if ( pNormals == nullptr ) {
        result.status = geometry_status_t::ALLOCATION_FAILED;
        return result;
    }

    const f64 cosThreshold = std::cos( fAngleThresholdRadians );

    auto faceNormalOf = [&]( const mesh_half_edge_record_t &he,
                             math::vec3d_t *pOut ) noexcept {
        const mesh_loop_record_t *pLoop =
            GenerationPool_Get( &pMesh->loops, he.hLoop );
        const mesh_face_record_t *pFace = pLoop != nullptr
            ? GenerationPool_Get( &pMesh->faces, pLoop->hFace )
            : nullptr;
        if ( pFace == nullptr ) { return false; }
        *pOut = pFace->normal;
        return true;
    };

    usize outIdx = 0u;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t hHE,
             const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            math::vec3d_t ownNormal{};
            if ( !faceNormalOf( he, &ownNormal ) ) {
                pNormals[outIdx++] = { hHE, math::Vec3d_Make( 0.0, 0.0, 1.0 ) };
                return true;
            }

            // Rotate around the corner's origin vertex through the other
            // outgoing half-edges: next(twin(h)) is the neighbouring
            // outgoing half-edge. Each face in the fan is visited once and
            // the corner's own face is counted exactly once (as the seed).
            math::vec3d_t sum = ownNormal;
            geometry_mesh_half_edge_handle_t hOut = hHE;
            for ( u32 guard = 0u; guard < 256u; ++guard ) {
                const mesh_half_edge_record_t *pOut =
                    GenerationPool_Get( &pMesh->halfEdges, hOut );
                const mesh_half_edge_record_t *pTwin = pOut != nullptr
                    ? GenerationPool_Get( &pMesh->halfEdges, pOut->hTwin )
                    : nullptr;
                if ( pTwin == nullptr ) { break; }  // open boundary
                hOut = pTwin->hNext;
                if ( hOut.nSlot == hHE.nSlot &&
                     hOut.nGeneration == hHE.nGeneration ) {
                    break;
                }
                const mesh_half_edge_record_t *pNextOut =
                    GenerationPool_Get( &pMesh->halfEdges, hOut );
                math::vec3d_t fanNormal{};
                if ( pNextOut == nullptr ||
                     !faceNormalOf( *pNextOut, &fanNormal ) ) {
                    break;
                }
                if ( math::Vec3d_Dot( ownNormal, fanNormal ) >= cosThreshold ) {
                    sum = math::Vec3d_Add( sum, fanNormal );
                }
            }

            const f64 lenSq = math::Vec3d_LengthSquared( sum );
            pNormals[outIdx++] = {
                hHE, lenSq > 1.0e-30
                         ? math::Vec3d_Scale( sum, 1.0 / std::sqrt( lenSq ) )
                         : ownNormal };
            return true;
        } );

    result.pNormals = pNormals;
    result.cNormals = outIdx;
    result.status = geometry_status_t::OK;
    return result;
}

} // namespace cypher::editor::geometry
