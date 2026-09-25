//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSource.cpp
//  Purpose: Implements mesh source build/describe/clone, the identity
//           sidecars, and whole-source validation.
//  Details: Build maps description elements to mesh handles by walking each
//           built face loop from its first half-edge, which Sanitation
//           guarantees corresponds to the face's first corner. Every mapped
//           vertex is cross-checked (same description vertex -> same mesh
//           vertex, bit-identical position), so a change in Sanitation's
//           construction order surfaces as CORRUPT_STATE rather than as
//           silently shuffled identity.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSource.h"

#include "CypherGeometry_MeshValidation.h"
#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_Sanitation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

bool CoordinateOk( f64 v ) noexcept
{
    return std::isfinite( v ) && std::fabs( v ) <= kMeshSourceCoordinateMax;
}

bool PositionOk( math::vec3d_t p ) noexcept
{
    return CoordinateOk( p.x ) && CoordinateOk( p.y ) && CoordinateOk( p.z );
}

u64 PairKey( u32 a, u32 b ) noexcept
{
    const u32 lo = a < b ? a : b, hi = a < b ? b : a;
    return ( static_cast<u64>( lo ) << 32 ) | hi;
}

void Fault( mesh_source_validation_t *pOut, mesh_source_fault_t fault, geometry_source_id_t id = {} ) noexcept
{
    if ( pOut ) {
        pOut->fault = fault;
        pOut->sourceId = id;
    }
}

template <typename tag_t>
bool SidecarGet( const vector_t<mesh_identity_slot_t> &v, generation_handle_t<tag_t> h, geometry_source_id_t *pOut ) noexcept
{
    if ( h.nSlot >= v.nCount || h.nGeneration == 0u ) { return false; }
    const mesh_identity_slot_t &s = v.pData[h.nSlot];
    if ( s.nGeneration != h.nGeneration || !GeometrySourceId_IsValid( s.sourceId ) ) { return false; }
    *pOut = s.sourceId;
    return true;
}

// Grows a sidecar to cover `cSlots` pool slots. New slots are unwritten
// (generation 0), so growth never fabricates identity.
bool SidecarCover( vector_t<mesh_identity_slot_t> *pV, usize cSlots ) noexcept
{
    if ( pV->nCount >= cSlots ) { return true; }
    const usize old = pV->nCount;
    if ( !Vector_Resize( pV, cSlots ) ) { return false; }
    for ( usize i = old; i < cSlots; ++i ) { pV->pData[i] = mesh_identity_slot_t{}; }
    return true;
}

bool IdsUnique( const allocator_t *pAllocator, const u64 *pIds, usize cIds, u64 *pDuplicateOut, bool *pScratchFailed ) noexcept
{
    *pScratchFailed = false;
    vector_t<u64> sorted{};
    if ( !Vector_Init( &sorted, pAllocator ) || !Vector_Resize( &sorted, cIds ) ) {
        Vector_Shutdown( &sorted );
        *pScratchFailed = true;
        return false;
    }
    if ( cIds > 0u ) { std::memcpy( sorted.pData, pIds, sizeof( u64 ) * cIds ); }
    std::sort( sorted.pData, sorted.pData + cIds );
    bool bUnique = true;
    for ( usize i = 1u; i < cIds; ++i ) {
        if ( sorted.pData[i] == sorted.pData[i - 1u] ) {
            *pDuplicateOut = sorted.pData[i];
            bUnique = false;
            break;
        }
    }
    Vector_Shutdown( &sorted );
    return bUnique;
}

bool CornerAttributesEqual( const mesh_corner_attributes_t &a, const mesh_corner_attributes_t &b ) noexcept
{
    return a.uv0.x == b.uv0.x && a.uv0.y == b.uv0.y && a.uv1.x == b.uv1.x && a.uv1.y == b.uv1.y &&
           a.colorRgba == b.colorRgba;
}

bool FaceAttributesEqual( const mesh_face_attributes_t &a, const mesh_face_attributes_t &b ) noexcept
{
    return a.material.value == b.material.value && a.smoothingGroups == b.smoothingGroups;
}

// Checks everything about a description that does not need a built mesh.
geometry_status_t ValidateDescription(
    const mesh_source_description_t *pDesc,
    const allocator_t *pScratch,
    mesh_source_validation_t *pFaultOut ) noexcept
{
    if ( !GeometrySourceId_IsValid( pDesc->sourceId ) ) {
        Fault( pFaultOut, mesh_source_fault_t::INVALID_ROOT_ID );
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const usize cV = pDesc->vertices.nCount, cF = pDesc->faces.nCount, cC = pDesc->corners.nCount;
    const usize cE = pDesc->edges.nCount;
    if ( cV > kMeshSourceVerticesMax || cF > kMeshSourceFacesMax || cC > kMeshSourceCornersMax ||
         cE > kMeshSourceCornersMax || cE > cC ) {
        Fault( pFaultOut, mesh_source_fault_t::INVALID_TOPOLOGY );
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( cF == 0u ) {
        Fault( pFaultOut, mesh_source_fault_t::INVALID_TOPOLOGY );
        return geometry_status_t::DEGENERATE;
    }
    for ( usize v = 0u; v < cV; ++v ) {
        const mesh_source_vertex_t &vx = pDesc->vertices.pData[v];
        if ( !GeometrySourceId_IsValid( vx.sourceId ) ) {
            Fault( pFaultOut, mesh_source_fault_t::MISSING_VERTEX_ID );
            return geometry_status_t::INVALID_ARGUMENT;
        }
        if ( !math::Vec3d_IsFinite( vx.position ) ) {
            Fault( pFaultOut, mesh_source_fault_t::NON_FINITE, vx.sourceId );
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( !PositionOk( vx.position ) ) {
            Fault( pFaultOut, mesh_source_fault_t::COORDINATE_RANGE, vx.sourceId );
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }

    vector_t<u8> referenced{};
    if ( !Vector_Init( &referenced, pScratch ) || !Vector_Resize( &referenced, cV ) ) {
        Vector_Shutdown( &referenced );
        Fault( pFaultOut, mesh_source_fault_t::VALIDATION_INCOMPLETE );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize v = 0u; v < cV; ++v ) { referenced.pData[v] = 0u; }

    // Faces must partition the corner array in order: that is the canonical
    // form Describe produces, and it makes equality a plain array compare.
    u64 iExpected = 0u;
    geometry_status_t st = geometry_status_t::OK;
    for ( usize f = 0u; f < cF && st == geometry_status_t::OK; ++f ) {
        const mesh_source_face_t &fc = pDesc->faces.pData[f];
        if ( !GeometrySourceId_IsValid( fc.sourceId ) ) {
            Fault( pFaultOut, mesh_source_fault_t::MISSING_FACE_ID );
            st = geometry_status_t::INVALID_ARGUMENT;
            break;
        }
        if ( fc.cCorners < 3u || fc.cCorners > kMeshSourceCornersPerFaceMax || fc.iFirstCorner != iExpected ||
             static_cast<u64>( fc.iFirstCorner ) + fc.cCorners > cC ) {
            Fault( pFaultOut, mesh_source_fault_t::INVALID_TOPOLOGY, fc.sourceId );
            st = geometry_status_t::INVALID_TOPOLOGY;
            break;
        }
        iExpected += fc.cCorners;
        for ( u32 k = 0u; k < fc.cCorners && st == geometry_status_t::OK; ++k ) {
            const mesh_source_corner_t &corner = pDesc->corners.pData[fc.iFirstCorner + k];
            if ( !math::Vec2d_IsFinite( corner.attributes.uv0 ) ||
                 !math::Vec2d_IsFinite( corner.attributes.uv1 ) ) {
                Fault( pFaultOut, mesh_source_fault_t::INVALID_ATTRIBUTES, fc.sourceId );
                st = geometry_status_t::NUMERIC_FAILURE;
                break;
            }
            const u32 iv = corner.iVertex;
            if ( iv >= cV ) {
                Fault( pFaultOut, mesh_source_fault_t::INVALID_TOPOLOGY, fc.sourceId );
                st = geometry_status_t::INVALID_HANDLE;
                break;
            }
            // A face visiting a vertex twice has a zero-length or pinched
            // boundary; the half-edge model cannot represent it.
            for ( u32 m = 0u; m < k; ++m ) {
                if ( pDesc->corners.pData[fc.iFirstCorner + m].iVertex == iv ) {
                    Fault( pFaultOut, mesh_source_fault_t::INVALID_TOPOLOGY, fc.sourceId );
                    st = geometry_status_t::INVALID_TOPOLOGY;
                    break;
                }
            }
            if ( st == geometry_status_t::OK ) { referenced.pData[iv] = 1u; }
        }
    }
    if ( st == geometry_status_t::OK && iExpected != cC ) {
        Fault( pFaultOut, mesh_source_fault_t::INVALID_TOPOLOGY );
        st = geometry_status_t::INVALID_TOPOLOGY;
    }
    for ( usize v = 0u; v < cV && st == geometry_status_t::OK; ++v ) {
        if ( referenced.pData[v] == 0u ) {
            // Isolated vertices would be dropped by construction together
            // with their identity; refuse rather than lose data.
            Fault( pFaultOut, mesh_source_fault_t::INVALID_TOPOLOGY, pDesc->vertices.pData[v].sourceId );
            st = geometry_status_t::INVALID_TOPOLOGY;
        }
    }
    Vector_Shutdown( &referenced );
    if ( st != geometry_status_t::OK ) { return st; }

    // Identity uniqueness across root, vertices, faces.
    vector_t<u64> ids{};
    if ( !Vector_Init( &ids, pScratch ) || !Vector_Reserve( &ids, 1u + cV + cF ) ) {
        Vector_Shutdown( &ids );
        Fault( pFaultOut, mesh_source_fault_t::VALIDATION_INCOMPLETE );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    bool bPushed = Vector_PushBack( &ids, pDesc->sourceId.value );
    for ( usize v = 0u; v < cV; ++v ) { bPushed = bPushed && Vector_PushBack( &ids, pDesc->vertices.pData[v].sourceId.value ); }
    for ( usize f = 0u; f < cF; ++f ) { bPushed = bPushed && Vector_PushBack( &ids, pDesc->faces.pData[f].sourceId.value ); }
    u64 dup = 0u;
    bool bScratchFailed = false;
    const bool bUnique = bPushed && IdsUnique( pScratch, ids.pData, ids.nCount, &dup, &bScratchFailed );
    Vector_Shutdown( &ids );
    if ( !bPushed || bScratchFailed ) {
        Fault( pFaultOut, mesh_source_fault_t::VALIDATION_INCOMPLETE );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !bUnique ) {
        Fault( pFaultOut, mesh_source_fault_t::DUPLICATE_SOURCE_ID, geometry_source_id_t{ dup } );
        return geometry_status_t::IDENTITY_CONFLICT;
    }

    // Edge entries: valid pairs, sane crease, no repeats.
    vector_t<u64> keys{};
    if ( !Vector_Init( &keys, pScratch ) || !Vector_Resize( &keys, pDesc->edges.nCount ) ) {
        Vector_Shutdown( &keys );
        Fault( pFaultOut, mesh_source_fault_t::VALIDATION_INCOMPLETE );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize e = 0u; e < pDesc->edges.nCount; ++e ) {
        const mesh_source_edge_t &ed = pDesc->edges.pData[e];
        if ( ed.iVertexA >= ed.iVertexB || ed.iVertexB >= cV ) {
            Vector_Shutdown( &keys );
            Fault( pFaultOut, mesh_source_fault_t::INVALID_TOPOLOGY );
            return geometry_status_t::INVALID_HANDLE;
        }
        if ( !std::isfinite( ed.creaseWeight ) || ed.creaseWeight < 0.0 || ed.creaseWeight > 1.0 ) {
            Vector_Shutdown( &keys );
            Fault( pFaultOut, mesh_source_fault_t::INVALID_ATTRIBUTES );
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( ed.attributes.flags == 0u && ed.creaseWeight == 0.0 ) {
            Vector_Shutdown( &keys );
            Fault( pFaultOut, mesh_source_fault_t::INVALID_ATTRIBUTES );
            return geometry_status_t::INVALID_ARGUMENT;
        }
        keys.pData[e] = PairKey( ed.iVertexA, ed.iVertexB );
    }
    std::sort( keys.pData, keys.pData + keys.nCount );
    for ( usize e = 1u; e < keys.nCount; ++e ) {
        if ( keys.pData[e] == keys.pData[e - 1u] ) {
            Vector_Shutdown( &keys );
            Fault( pFaultOut, mesh_source_fault_t::INVALID_ATTRIBUTES );
            return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    Vector_Shutdown( &keys );
    return geometry_status_t::OK;
}

struct edge_key_entry_t {
    u64 key;
    geometry_mesh_edge_handle_t hEdge;
};

// Every twin that exists is reciprocal and runs the opposite way (starts
// where its partner ends). A missing twin marks a boundary half-edge.
bool TwinsConsistent( const editable_mesh_t *pMesh ) noexcept
{
    bool bOk = true;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            if ( !GeometryHandle_IsValid( he.hTwin ) ) { return true; }
            const mesh_half_edge_record_t *pTwin = GenerationPool_Get( &pMesh->halfEdges, he.hTwin );
            const mesh_half_edge_record_t *pNext = GenerationPool_Get( &pMesh->halfEdges, he.hNext );
            if ( pTwin == nullptr || pNext == nullptr || pTwin->hTwin.nSlot != h.nSlot ||
                 pTwin->hTwin.nGeneration != h.nGeneration || pTwin->hOrigin.nSlot != pNext->hOrigin.nSlot ||
                 pTwin->hOrigin.nGeneration != pNext->hOrigin.nGeneration ) {
                bOk = false;
                return false;
            }
            return true;
        } );
    return bOk;
}

struct vertex_fan_audit_t {
    u32 cOutgoing{ 0u };
    u32 cBoundaryStarts{ 0u };
    geometry_mesh_half_edge_handle_t hBoundaryStart{};
};

// MeshValidation intentionally owns a no-allocation diagnostic API. Mesh
// sources have a scratch allocator, so their stronger authoring contract can
// prove connected vertex fans, unique endpoint pairs, and actual shell
// connectivity in O(H + E log E) time instead of rescanning every half-edge
// for every vertex.
geometry_status_t ValidateSourceConnectivity(
    const editable_mesh_t *pMesh,
    const allocator_t *pScratch ) noexcept
{
    vector_t<vertex_fan_audit_t> fanAudit{};
    vector_t<u64> edgeKeys{};
    vector_t<u32> faceVisitStamp{};
    vector_t<geometry_mesh_face_handle_t> faceQueue{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &faceQueue );
        Vector_Shutdown( &faceVisitStamp );
        Vector_Shutdown( &edgeKeys );
        Vector_Shutdown( &fanAudit );
    };
    if ( !Allocator_IsValid( pScratch ) ||
         !Vector_Init( &fanAudit, pScratch ) ||
         !Vector_Resize( &fanAudit, pMesh->vertices.cSlots ) ||
         !Vector_Init( &edgeKeys, pScratch ) ||
         !Vector_Reserve( &edgeKeys, GenerationPool_Count( &pMesh->edges ) ) ||
         !Vector_Init( &faceVisitStamp, pScratch ) ||
         !Vector_Resize( &faceVisitStamp, pMesh->faces.cSlots ) ||
         !Vector_Init( &faceQueue, pScratch ) ||
         !Vector_Reserve( &faceQueue, GenerationPool_Count( &pMesh->faces ) ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < fanAudit.nCount; ++i ) { fanAudit.pData[i] = {}; }
    for ( usize i = 0u; i < faceVisitStamp.nCount; ++i ) { faceVisitStamp.pData[i] = 0u; }

    bool bValid = true;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t hHalfEdge,
             const mesh_half_edge_record_t &halfEdge ) noexcept -> bool_t {
            if ( halfEdge.hOrigin.nSlot >= fanAudit.nCount ||
                 GenerationPool_Get( &pMesh->vertices, halfEdge.hOrigin ) == nullptr ) {
                bValid = false;
                return false;
            }
            vertex_fan_audit_t &audit = fanAudit.pData[halfEdge.hOrigin.nSlot];
            ++audit.cOutgoing;
            const mesh_half_edge_record_t *pPrevious =
                GenerationPool_Get( &pMesh->halfEdges, halfEdge.hPrev );
            if ( pPrevious == nullptr ) {
                bValid = false;
                return false;
            }
            if ( !GenerationHandle_IsValid( pPrevious->hTwin ) ) {
                ++audit.cBoundaryStarts;
                audit.hBoundaryStart = hHalfEdge;
            }
            return true;
        } );

    if ( bValid ) {
        (void)GenerationPool_ForEach( &pMesh->vertices,
            [&]( geometry_mesh_vertex_handle_t hVertex,
                 const mesh_vertex_record_t &vertex ) noexcept -> bool_t {
                const vertex_fan_audit_t &audit = fanAudit.pData[hVertex.nSlot];
                if ( audit.cOutgoing == 0u || audit.cBoundaryStarts > 1u ) {
                    bValid = false;
                    return false;
                }
                const geometry_mesh_half_edge_handle_t hStart =
                    audit.cBoundaryStarts == 1u ? audit.hBoundaryStart : vertex.hOutHalfEdge;
                geometry_mesh_half_edge_handle_t hCurrent = hStart;
                u32 cVisited = 0u;
                bool bClosed = false;
                while ( true ) {
                    const mesh_half_edge_record_t *pCurrent =
                        GenerationPool_Get( &pMesh->halfEdges, hCurrent );
                    if ( pCurrent == nullptr ||
                         pCurrent->hOrigin.nSlot != hVertex.nSlot ||
                         pCurrent->hOrigin.nGeneration != hVertex.nGeneration ||
                         ++cVisited > audit.cOutgoing ) {
                        bValid = false;
                        break;
                    }
                    if ( !GenerationHandle_IsValid( pCurrent->hTwin ) ) { break; }
                    const mesh_half_edge_record_t *pTwin =
                        GenerationPool_Get( &pMesh->halfEdges, pCurrent->hTwin );
                    if ( pTwin == nullptr ) {
                        bValid = false;
                        break;
                    }
                    hCurrent = pTwin->hNext;
                    if ( hCurrent.nSlot == hStart.nSlot &&
                         hCurrent.nGeneration == hStart.nGeneration ) {
                        bClosed = true;
                        break;
                    }
                }
                if ( !bValid || cVisited != audit.cOutgoing ||
                     ( audit.cBoundaryStarts == 0u ) != bClosed ) {
                    bValid = false;
                    return false;
                }
                return true;
            } );
    }

    if ( bValid ) {
        (void)GenerationPool_ForEach( &pMesh->edges,
            [&]( geometry_mesh_edge_handle_t,
                 const mesh_edge_record_t &edge ) noexcept -> bool_t {
                const mesh_half_edge_record_t *pHalfEdge =
                    GenerationPool_Get( &pMesh->halfEdges, edge.hHalfEdge );
                const mesh_half_edge_record_t *pNext = pHalfEdge != nullptr
                    ? GenerationPool_Get( &pMesh->halfEdges, pHalfEdge->hNext )
                    : nullptr;
                if ( pHalfEdge == nullptr || pNext == nullptr ) {
                    bValid = false;
                    return false;
                }
                (void)Vector_PushBack(
                    &edgeKeys,
                    PairKey( pHalfEdge->hOrigin.nSlot, pNext->hOrigin.nSlot ) );
                return true;
            } );
        std::sort( edgeKeys.pData, edgeKeys.pData + edgeKeys.nCount );
        for ( usize i = 1u; i < edgeKeys.nCount; ++i ) {
            if ( edgeKeys.pData[i - 1u] == edgeKeys.pData[i] ) {
                bValid = false;
                break;
            }
        }
    }

    u32 nShellStamp = 0u;
    if ( bValid ) {
        (void)GenerationPool_ForEach( &pMesh->shells,
            [&]( geometry_mesh_shell_handle_t hShell,
                 const mesh_shell_record_t &shell ) noexcept -> bool_t {
                ++nShellStamp;
                Vector_Clear( &faceQueue );
                if ( shell.hAnyFace.nSlot >= faceVisitStamp.nCount ||
                     !Vector_PushBack( &faceQueue, shell.hAnyFace ) ) {
                    bValid = false;
                    return false;
                }
                faceVisitStamp.pData[shell.hAnyFace.nSlot] = nShellStamp;
                usize iQueue = 0u;
                u32 cReached = 0u;
                while ( iQueue < faceQueue.nCount && bValid ) {
                    const geometry_mesh_face_handle_t hFace = faceQueue.pData[iQueue++];
                    const mesh_face_record_t *pFace = GenerationPool_Get( &pMesh->faces, hFace );
                    const mesh_loop_record_t *pLoop = pFace != nullptr
                        ? GenerationPool_Get( &pMesh->loops, pFace->hOuterLoop )
                        : nullptr;
                    if ( pFace == nullptr || pLoop == nullptr ||
                         pFace->hShell.nSlot != hShell.nSlot ||
                         pFace->hShell.nGeneration != hShell.nGeneration ) {
                        bValid = false;
                        break;
                    }
                    ++cReached;
                    geometry_mesh_half_edge_handle_t hHalfEdge = pLoop->hFirstHalfEdge;
                    for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
                        const mesh_half_edge_record_t *pCurrent =
                            GenerationPool_Get( &pMesh->halfEdges, hHalfEdge );
                        if ( pCurrent == nullptr ) {
                            bValid = false;
                            break;
                        }
                        if ( GenerationHandle_IsValid( pCurrent->hTwin ) ) {
                            const mesh_half_edge_record_t *pTwin =
                                GenerationPool_Get( &pMesh->halfEdges, pCurrent->hTwin );
                            const mesh_loop_record_t *pTwinLoop = pTwin != nullptr
                                ? GenerationPool_Get( &pMesh->loops, pTwin->hLoop )
                                : nullptr;
                            const mesh_face_record_t *pNeighbor = pTwinLoop != nullptr
                                ? GenerationPool_Get( &pMesh->faces, pTwinLoop->hFace )
                                : nullptr;
                            if ( pNeighbor == nullptr ||
                                 pNeighbor->hShell.nSlot != hShell.nSlot ||
                                 pNeighbor->hShell.nGeneration != hShell.nGeneration ) {
                                bValid = false;
                                break;
                            }
                            const geometry_mesh_face_handle_t hNeighbor = pTwinLoop->hFace;
                            if ( faceVisitStamp.pData[hNeighbor.nSlot] != nShellStamp ) {
                                faceVisitStamp.pData[hNeighbor.nSlot] = nShellStamp;
                                (void)Vector_PushBack( &faceQueue, hNeighbor );
                            }
                        }
                        hHalfEdge = pCurrent->hNext;
                    }
                }
                if ( !bValid || cReached != shell.cFaces ) {
                    bValid = false;
                    return false;
                }
                return true;
            } );
    }

    cleanup();
    return bValid ? geometry_status_t::OK : geometry_status_t::INVALID_TOPOLOGY;
}

void ResetSource( mesh_source_t *pOut ) noexcept
{
    EditableMesh_Shutdown( &pOut->mesh );
    MeshAttributeStore_Shutdown( &pOut->attributes );
    Vector_Shutdown( &pOut->vertexIds );
    Vector_Shutdown( &pOut->faceIds );
    pOut->sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

} // namespace

// ---------------------------------------------------------------------------
// Description
// ---------------------------------------------------------------------------

geometry_status_t MeshSourceDescription_Init(
    mesh_source_description_t *pDesc,
    const allocator_t *pAllocator,
    geometry_source_id_t meshId ) noexcept
{
    if ( pDesc == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( MeshSourceDescription_IsInitialized( pDesc ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    if ( !Vector_Init( &pDesc->vertices, pAllocator ) || !Vector_Init( &pDesc->corners, pAllocator ) ||
         !Vector_Init( &pDesc->faces, pAllocator ) || !Vector_Init( &pDesc->edges, pAllocator ) ) {
        MeshSourceDescription_Shutdown( pDesc );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pDesc->sourceId = meshId;
    return geometry_status_t::OK;
}

void MeshSourceDescription_Shutdown( mesh_source_description_t *pDesc ) noexcept
{
    if ( pDesc == nullptr ) { return; }
    Vector_Shutdown( &pDesc->vertices );
    Vector_Shutdown( &pDesc->corners );
    Vector_Shutdown( &pDesc->faces );
    Vector_Shutdown( &pDesc->edges );
    pDesc->sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

bool MeshSourceDescription_IsInitialized( const mesh_source_description_t *pDesc ) noexcept
{
    return pDesc != nullptr && pDesc->vertices.pAllocator != nullptr;
}

void MeshSourceDescription_Clear( mesh_source_description_t *pDesc, geometry_source_id_t meshId ) noexcept
{
    if ( !MeshSourceDescription_IsInitialized( pDesc ) ) { return; }
    Vector_Clear( &pDesc->vertices );
    Vector_Clear( &pDesc->corners );
    Vector_Clear( &pDesc->faces );
    Vector_Clear( &pDesc->edges );
    pDesc->sourceId = meshId;
}

geometry_status_t MeshSourceDescription_TryAddVertex(
    mesh_source_description_t *pDesc,
    math::vec3d_t position,
    geometry_source_id_t vertexId,
    u32 *pIndexOut ) noexcept
{
    if ( !MeshSourceDescription_IsInitialized( pDesc ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySourceId_IsValid( vertexId ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !PositionOk( position ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    if ( pDesc->vertices.nCount >= kMeshSourceVerticesMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    if ( !Vector_PushBack( &pDesc->vertices, mesh_source_vertex_t{ position, vertexId } ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( pIndexOut ) { *pIndexOut = static_cast<u32>( pDesc->vertices.nCount - 1u ); }
    return geometry_status_t::OK;
}

geometry_status_t MeshSourceDescription_TryAddFace(
    mesh_source_description_t *pDesc,
    span_t<const u32> vertexIndices,
    geometry_source_id_t faceId,
    const mesh_face_attributes_t &attributes,
    u32 *pIndexOut ) noexcept
{
    if ( !MeshSourceDescription_IsInitialized( pDesc ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySourceId_IsValid( faceId ) || vertexIndices.pData == nullptr || vertexIndices.nCount < 3u ||
         vertexIndices.nCount > kMeshSourceCornersPerFaceMax ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    for ( usize k = 0u; k < vertexIndices.nCount; ++k ) {
        if ( vertexIndices.pData[k] >= pDesc->vertices.nCount ) { return geometry_status_t::INVALID_HANDLE; }
    }
    if ( pDesc->faces.nCount >= kMeshSourceFacesMax ||
         pDesc->corners.nCount + vertexIndices.nCount > kMeshSourceCornersMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    // Reserve first so the corner and face pushes cannot fail half-way.
    if ( !Vector_Reserve( &pDesc->corners, pDesc->corners.nCount + vertexIndices.nCount ) ||
         !Vector_Reserve( &pDesc->faces, pDesc->faces.nCount + 1u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    mesh_source_face_t face{};
    face.iFirstCorner = static_cast<u32>( pDesc->corners.nCount );
    face.cCorners = static_cast<u32>( vertexIndices.nCount );
    face.sourceId = faceId;
    face.attributes = attributes;
    for ( usize k = 0u; k < vertexIndices.nCount; ++k ) {
        (void)Vector_PushBack( &pDesc->corners, mesh_source_corner_t{ vertexIndices.pData[k], {} } );
    }
    (void)Vector_PushBack( &pDesc->faces, face );
    if ( pIndexOut ) { *pIndexOut = static_cast<u32>( pDesc->faces.nCount - 1u ); }
    return geometry_status_t::OK;
}

geometry_status_t MeshSourceDescription_TrySetEdge(
    mesh_source_description_t *pDesc,
    u32 iVertexA,
    u32 iVertexB,
    const mesh_edge_attributes_t &attributes,
    f64 creaseWeight ) noexcept
{
    if ( !MeshSourceDescription_IsInitialized( pDesc ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( iVertexA == iVertexB || !std::isfinite( creaseWeight ) || creaseWeight < 0.0 || creaseWeight > 1.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( iVertexA >= pDesc->vertices.nCount || iVertexB >= pDesc->vertices.nCount ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    const u32 a = std::min( iVertexA, iVertexB ), b = std::max( iVertexA, iVertexB );
    const bool bDefault = attributes.flags == 0u && creaseWeight == 0.0;
    for ( usize e = 0u; e < pDesc->edges.nCount; ++e ) {
        mesh_source_edge_t &ed = pDesc->edges.pData[e];
        if ( ed.iVertexA == a && ed.iVertexB == b ) {
            if ( bDefault ) {
                Vector_Erase( &pDesc->edges, e );
                return geometry_status_t::OK;
            }
            ed.attributes = attributes;
            ed.creaseWeight = creaseWeight;
            return geometry_status_t::OK;
        }
    }
    if ( bDefault ) { return geometry_status_t::OK; }
    if ( !Vector_PushBack( &pDesc->edges, mesh_source_edge_t{ a, b, attributes, creaseWeight } ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void MeshSourceDescription_Move( mesh_source_description_t *pDst, mesh_source_description_t *pSrc ) noexcept
{
    if ( pDst == nullptr || pSrc == nullptr || pDst == pSrc ) { return; }
    MeshSourceDescription_Shutdown( pDst );
    Vector_Move( &pDst->vertices, &pSrc->vertices );
    Vector_Move( &pDst->corners, &pSrc->corners );
    Vector_Move( &pDst->faces, &pSrc->faces );
    Vector_Move( &pDst->edges, &pSrc->edges );
    pDst->sourceId = pSrc->sourceId;
    MeshSourceDescription_Shutdown( pSrc );
}

geometry_status_t MeshSourceDescription_TryCopy( mesh_source_description_t *pDst, const mesh_source_description_t *pSrc ) noexcept
{
    if ( !MeshSourceDescription_IsInitialized( pDst ) || !MeshSourceDescription_IsInitialized( pSrc ) || pDst == pSrc ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    MeshSourceDescription_Clear( pDst, pSrc->sourceId );
    const bool bOk =
        Vector_Append( &pDst->vertices, span_t<const mesh_source_vertex_t>{ pSrc->vertices.pData, pSrc->vertices.nCount } ) &&
        Vector_Append( &pDst->corners, span_t<const mesh_source_corner_t>{ pSrc->corners.pData, pSrc->corners.nCount } ) &&
        Vector_Append( &pDst->faces, span_t<const mesh_source_face_t>{ pSrc->faces.pData, pSrc->faces.nCount } ) &&
        Vector_Append( &pDst->edges, span_t<const mesh_source_edge_t>{ pSrc->edges.pData, pSrc->edges.nCount } );
    if ( !bOk ) {
        MeshSourceDescription_Clear( pDst, pSrc->sourceId );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

bool MeshSourceDescription_Equal( const mesh_source_description_t *pA, const mesh_source_description_t *pB ) noexcept
{
    if ( !MeshSourceDescription_IsInitialized( pA ) || !MeshSourceDescription_IsInitialized( pB ) ) { return false; }
    if ( pA->sourceId.value != pB->sourceId.value || pA->vertices.nCount != pB->vertices.nCount ||
         pA->corners.nCount != pB->corners.nCount || pA->faces.nCount != pB->faces.nCount ||
         pA->edges.nCount != pB->edges.nCount ) {
        return false;
    }
    for ( usize i = 0u; i < pA->vertices.nCount; ++i ) {
        const mesh_source_vertex_t &a = pA->vertices.pData[i], &b = pB->vertices.pData[i];
        if ( !math::Vec3d_EqualsExact( a.position, b.position ) || a.sourceId.value != b.sourceId.value ) { return false; }
    }
    for ( usize i = 0u; i < pA->corners.nCount; ++i ) {
        const mesh_source_corner_t &a = pA->corners.pData[i], &b = pB->corners.pData[i];
        if ( a.iVertex != b.iVertex || !CornerAttributesEqual( a.attributes, b.attributes ) ) { return false; }
    }
    for ( usize i = 0u; i < pA->faces.nCount; ++i ) {
        const mesh_source_face_t &a = pA->faces.pData[i], &b = pB->faces.pData[i];
        if ( a.iFirstCorner != b.iFirstCorner || a.cCorners != b.cCorners || a.sourceId.value != b.sourceId.value ||
             !FaceAttributesEqual( a.attributes, b.attributes ) ) {
            return false;
        }
    }
    for ( usize i = 0u; i < pA->edges.nCount; ++i ) {
        const mesh_source_edge_t &a = pA->edges.pData[i], &b = pB->edges.pData[i];
        if ( a.iVertexA != b.iVertexA || a.iVertexB != b.iVertexB || a.attributes.flags != b.attributes.flags ||
             a.creaseWeight != b.creaseWeight ) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Mesh source
// ---------------------------------------------------------------------------

bool MeshSource_IsInitialized( const mesh_source_t *pSource ) noexcept
{
    return pSource != nullptr && EditableMesh_IsInitialized( &pSource->mesh ) &&
           MeshAttributeStore_IsInitialized( &pSource->attributes ) && pSource->vertexIds.pAllocator != nullptr &&
           pSource->faceIds.pAllocator != nullptr;
}

void MeshSource_Shutdown( mesh_source_t *pSource ) noexcept
{
    if ( pSource == nullptr ) { return; }
    ResetSource( pSource );
}

geometry_status_t MeshSource_TryBuild(
    const mesh_source_description_t *pDesc,
    const allocator_t *pAllocator,
    mesh_source_t *pOut,
    mesh_source_validation_t *pFaultOut ) noexcept
{
    Fault( pFaultOut, mesh_source_fault_t::NONE );
    if ( !MeshSourceDescription_IsInitialized( pDesc ) || !Allocator_IsValid( pAllocator ) || pOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( EditableMesh_IsInitialized( &pOut->mesh ) || pOut->attributes.faces.pAllocator != nullptr ||
         pOut->vertexIds.pAllocator != nullptr || pOut->faceIds.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    const geometry_status_t valid = ValidateDescription( pDesc, pAllocator, pFaultOut );
    if ( valid != geometry_status_t::OK ) { return valid; }

    // Description -> soup -> Sanitation: the one validated construction
    // path. No weld (identity is per description vertex), open allowed.
    polygon_soup_t soup{};
    if ( PolygonSoup_Init( &soup, pAllocator ) != geometry_status_t::OK ) { return geometry_status_t::ALLOCATION_FAILED; }
    geometry_status_t st = geometry_status_t::OK;
    for ( usize v = 0u; v < pDesc->vertices.nCount && st == geometry_status_t::OK; ++v ) {
        st = PolygonSoup_TryAddVertex( &soup, pDesc->vertices.pData[v].position, nullptr );
    }
    u32 cornerScratch[kMeshSourceCornersPerFaceMax];
    for ( usize f = 0u; f < pDesc->faces.nCount && st == geometry_status_t::OK; ++f ) {
        const mesh_source_face_t &fc = pDesc->faces.pData[f];
        for ( u32 k = 0u; k < fc.cCorners; ++k ) { cornerScratch[k] = pDesc->corners.pData[fc.iFirstCorner + k].iVertex; }
        st = PolygonSoup_TryAddFace( &soup, span_t<const u32>{ cornerScratch, fc.cCorners }, fc.sourceId,
                                     static_cast<u32>( f ), nullptr );
    }
    if ( st != geometry_status_t::OK ) {
        PolygonSoup_Shutdown( &soup );
        return st;
    }

    vector_t<geometry_mesh_face_handle_t> faceMap{};
    if ( !Vector_Init( &faceMap, pAllocator ) ) {
        PolygonSoup_Shutdown( &soup );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    sanitation_policy_t policy{};
    policy.bWeld = false;
    policy.bRequireClosed = false;
    policy.bDropDegenerateFaces = false;
    const sanitation_report_t report = Sanitation_TryPolygonSoupToMesh( &soup, policy, pAllocator, &pOut->mesh, &faceMap );
    PolygonSoup_Shutdown( &soup );
    if ( report.status != geometry_status_t::OK ) {
        Vector_Shutdown( &faceMap );
        // Degenerate, non-manifold, and inconsistently wound input all mean
        // the description is not a representable mesh.
        Fault( pFaultOut, mesh_source_fault_t::INVALID_TOPOLOGY,
               report.iFace < pDesc->faces.nCount ? pDesc->faces.pData[report.iFace].sourceId : geometry_source_id_t{} );
        return report.status;
    }
    if ( faceMap.nCount != pDesc->faces.nCount ) {
        Vector_Shutdown( &faceMap );
        EditableMesh_Shutdown( &pOut->mesh );
        return geometry_status_t::CORRUPT_STATE;
    }

    auto fail = [&]( geometry_status_t s ) noexcept {
        Vector_Shutdown( &faceMap );
        ResetSource( pOut );
        return s;
    };
    if ( MeshAttributeStore_Init( &pOut->attributes, pAllocator ) != geometry_status_t::OK ||
         !Vector_Init( &pOut->vertexIds, pAllocator ) || !Vector_Init( &pOut->faceIds, pAllocator ) ||
         !SidecarCover( &pOut->vertexIds, pOut->mesh.vertices.cSlots ) ||
         !SidecarCover( &pOut->faceIds, pOut->mesh.faces.cSlots ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }

    // Walk each built face from its first corner and bind identity,
    // attributes, and the vertex map.
    const usize cV = pDesc->vertices.nCount;
    vector_t<geometry_mesh_vertex_handle_t> vertexMap{};
    vector_t<edge_key_entry_t> edgeKeys{};
    if ( !Vector_Init( &vertexMap, pAllocator ) || !Vector_Resize( &vertexMap, cV ) ||
         !Vector_Init( &edgeKeys, pAllocator ) || !Vector_Reserve( &edgeKeys, pDesc->corners.nCount ) ) {
        Vector_Shutdown( &vertexMap );
        Vector_Shutdown( &edgeKeys );
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize v = 0u; v < cV; ++v ) { vertexMap.pData[v] = GEOMETRY_HANDLE_INVALID<geometry_mesh_vertex_tag_t>; }
    const usize cCornerSlots = pOut->mesh.halfEdges.cSlots;
    for ( usize f = 0u; f < pDesc->faces.nCount && st == geometry_status_t::OK; ++f ) {
        const mesh_source_face_t &fc = pDesc->faces.pData[f];
        const geometry_mesh_face_handle_t hFace = faceMap.pData[f];
        const mesh_face_record_t *pFace = GenerationPool_Get( &pOut->mesh.faces, hFace );
        const mesh_loop_record_t *pLoop = pFace ? GenerationPool_Get( &pOut->mesh.loops, pFace->hOuterLoop ) : nullptr;
        if ( pLoop == nullptr || pLoop->cHalfEdges != fc.cCorners ) {
            st = geometry_status_t::CORRUPT_STATE;
            break;
        }
        pOut->faceIds.pData[hFace.nSlot] = mesh_identity_slot_t{ hFace.nGeneration, fc.sourceId };
        st = MeshAttributeStore_TrySetFace( &pOut->attributes, hFace, fc.attributes, pOut->mesh.faces.cSlots );
        geometry_mesh_half_edge_handle_t h = pLoop->hFirstHalfEdge;
        for ( u32 k = 0u; k < fc.cCorners && st == geometry_status_t::OK; ++k ) {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pOut->mesh.halfEdges, h );
            if ( pH == nullptr ) {
                st = geometry_status_t::CORRUPT_STATE;
                break;
            }
            const mesh_source_corner_t &corner = pDesc->corners.pData[fc.iFirstCorner + k];
            const u32 iv = corner.iVertex;
            const mesh_vertex_record_t *pV = GenerationPool_Get( &pOut->mesh.vertices, pH->hOrigin );
            geometry_mesh_vertex_handle_t &mapped = vertexMap.pData[iv];
            if ( pV == nullptr || !math::Vec3d_EqualsExact( pV->position, pDesc->vertices.pData[iv].position ) ||
                 ( GeometryHandle_IsValid( mapped ) &&
                   ( mapped.nSlot != pH->hOrigin.nSlot || mapped.nGeneration != pH->hOrigin.nGeneration ) ) ) {
                st = geometry_status_t::CORRUPT_STATE;
                break;
            }
            mapped = pH->hOrigin;
            st = MeshAttributeStore_TrySetCorner( &pOut->attributes, h, corner.attributes, cCornerSlots );
            const u32 ivNext = pDesc->corners.pData[fc.iFirstCorner + ( k + 1u ) % fc.cCorners].iVertex;
            (void)Vector_PushBack( &edgeKeys, edge_key_entry_t{ PairKey( iv, ivNext ), pH->hEdge } );
            h = pH->hNext;
        }
    }
    for ( usize v = 0u; v < cV && st == geometry_status_t::OK; ++v ) {
        const geometry_mesh_vertex_handle_t hv = vertexMap.pData[v];
        if ( !GeometryHandle_IsValid( hv ) ) {
            st = geometry_status_t::CORRUPT_STATE;
            break;
        }
        pOut->vertexIds.pData[hv.nSlot] = mesh_identity_slot_t{ hv.nGeneration, pDesc->vertices.pData[v].sourceId };
    }

    // Edge attributes by vertex pair.
    if ( st == geometry_status_t::OK && pDesc->edges.nCount > 0u ) {
        std::sort( edgeKeys.pData, edgeKeys.pData + edgeKeys.nCount,
                   []( const edge_key_entry_t &a, const edge_key_entry_t &b ) { return a.key < b.key; } );
        for ( usize e = 0u; e < pDesc->edges.nCount && st == geometry_status_t::OK; ++e ) {
            const mesh_source_edge_t &ed = pDesc->edges.pData[e];
            const u64 key = PairKey( ed.iVertexA, ed.iVertexB );
            const edge_key_entry_t *pBegin = edgeKeys.pData;
            const edge_key_entry_t *pEnd = pBegin + edgeKeys.nCount;
            const edge_key_entry_t *pHit = std::lower_bound(
                pBegin, pEnd, key, []( const edge_key_entry_t &a, u64 k ) { return a.key < k; } );
            if ( pHit == pEnd || pHit->key != key ) {
                Fault( pFaultOut, mesh_source_fault_t::INVALID_ATTRIBUTES );
                st = geometry_status_t::INVALID_TOPOLOGY; // attributes for an edge that does not exist
                break;
            }
            mesh_edge_record_t *pEdge = GenerationPool_Get( &pOut->mesh.edges, pHit->hEdge );
            if ( pEdge == nullptr ) {
                st = geometry_status_t::CORRUPT_STATE;
                break;
            }
            pEdge->creaseWeight = ed.creaseWeight;
            st = MeshAttributeStore_TrySetEdge( &pOut->attributes, pHit->hEdge, ed.attributes, pOut->mesh.edges.cSlots );
        }
    }
    Vector_Shutdown( &vertexMap );
    Vector_Shutdown( &edgeKeys );
    if ( st != geometry_status_t::OK ) { return fail( st ); }
    Vector_Shutdown( &faceMap );
    pOut->sourceId = pDesc->sourceId;
    return geometry_status_t::OK;
}

geometry_status_t MeshSource_TryDescribe( const mesh_source_t *pSource, mesh_source_description_t *pDesc ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !MeshSourceDescription_IsInitialized( pDesc ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    MeshSourceDescription_Clear( pDesc, pSource->sourceId );
    const editable_mesh_t *pMesh = &pSource->mesh;
    const allocator_t *pAllocator = pDesc->vertices.pAllocator;

    struct vertex_entry_t {
        u64 id;
        geometry_mesh_vertex_handle_t h;
    };
    struct face_entry_t {
        u64 id;
        geometry_mesh_face_handle_t h;
    };
    vector_t<vertex_entry_t> vertexOrder{};
    vector_t<face_entry_t> faceOrder{};
    vector_t<u32> slotToIndex{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &vertexOrder );
        Vector_Shutdown( &faceOrder );
        Vector_Shutdown( &slotToIndex );
    };
    const usize cV = GenerationPool_Count( &pMesh->vertices ), cF = GenerationPool_Count( &pMesh->faces );
    if ( !Vector_Init( &vertexOrder, pAllocator ) || !Vector_Reserve( &vertexOrder, cV ) ||
         !Vector_Init( &faceOrder, pAllocator ) || !Vector_Reserve( &faceOrder, cF ) ||
         !Vector_Init( &slotToIndex, pAllocator ) || !Vector_Resize( &slotToIndex, pMesh->vertices.cSlots ) ||
         !Vector_Reserve( &pDesc->vertices, cV ) || !Vector_Reserve( &pDesc->faces, cF ) ||
         !Vector_Reserve( &pDesc->corners, GenerationPool_Count( &pMesh->halfEdges ) ) ) {
        cleanup();
        MeshSourceDescription_Clear( pDesc, pSource->sourceId );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < slotToIndex.nCount; ++i ) { slotToIndex.pData[i] = CY_INVALID_INDEX; }

    geometry_status_t st = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t hV, const mesh_vertex_record_t & ) noexcept -> bool_t {
            geometry_source_id_t id{};
            if ( !SidecarGet( pSource->vertexIds, hV, &id ) ) {
                st = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            (void)Vector_PushBack( &vertexOrder, vertex_entry_t{ id.value, hV } );
            return true;
        } );
    if ( st == geometry_status_t::OK ) {
        (void)GenerationPool_ForEach( &pMesh->faces,
            [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t & ) noexcept -> bool_t {
                geometry_source_id_t id{};
                if ( !SidecarGet( pSource->faceIds, hF, &id ) ) {
                    st = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                (void)Vector_PushBack( &faceOrder, face_entry_t{ id.value, hF } );
                return true;
            } );
    }
    if ( st != geometry_status_t::OK ) {
        cleanup();
        MeshSourceDescription_Clear( pDesc, pSource->sourceId );
        return st;
    }
    std::sort( vertexOrder.pData, vertexOrder.pData + vertexOrder.nCount,
               []( const vertex_entry_t &a, const vertex_entry_t &b ) { return a.id < b.id; } );
    std::sort( faceOrder.pData, faceOrder.pData + faceOrder.nCount,
               []( const face_entry_t &a, const face_entry_t &b ) { return a.id < b.id; } );

    for ( usize i = 0u; i < vertexOrder.nCount; ++i ) {
        const vertex_entry_t &e = vertexOrder.pData[i];
        slotToIndex.pData[e.h.nSlot] = static_cast<u32>( i );
        (void)Vector_PushBack( &pDesc->vertices,
                               mesh_source_vertex_t{ GenerationPool_Get( &pMesh->vertices, e.h )->position,
                                                     geometry_source_id_t{ e.id } } );
    }

    geometry_mesh_half_edge_handle_t ring[kMeshSourceCornersPerFaceMax];
    for ( usize i = 0u; i < faceOrder.nCount && st == geometry_status_t::OK; ++i ) {
        const face_entry_t &e = faceOrder.pData[i];
        const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, e.h );
        const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, pF->hOuterLoop );
        if ( pL == nullptr || pL->cHalfEdges < 3u || pL->cHalfEdges > kMeshSourceCornersPerFaceMax ) {
            st = geometry_status_t::CORRUPT_STATE;
            break;
        }
        // Gather the ring, then start it at the lowest-ID vertex so the
        // corner order does not depend on which half-edge the loop happens
        // to record as first.
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        u32 kStart = 0u;
        u32 minIndex = CY_INVALID_INDEX;
        for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
            if ( pH == nullptr || pH->hOrigin.nSlot >= slotToIndex.nCount ||
                 slotToIndex.pData[pH->hOrigin.nSlot] == CY_INVALID_INDEX ) {
                st = geometry_status_t::CORRUPT_STATE;
                break;
            }
            ring[k] = h;
            // Vertex indices follow ascending ID, so the lowest index is the
            // lowest ID.
            const u32 idx = slotToIndex.pData[pH->hOrigin.nSlot];
            if ( idx < minIndex ) {
                minIndex = idx;
                kStart = k;
            }
            h = pH->hNext;
        }
        if ( st != geometry_status_t::OK ) { break; }
        mesh_source_face_t face{};
        face.iFirstCorner = static_cast<u32>( pDesc->corners.nCount );
        face.cCorners = pL->cHalfEdges;
        face.sourceId = geometry_source_id_t{ e.id };
        face.attributes = MeshAttributeStore_GetFace( &pSource->attributes, e.h );
        for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
            const geometry_mesh_half_edge_handle_t hc = ring[( kStart + k ) % pL->cHalfEdges];
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, hc );
            if ( !Vector_PushBack( &pDesc->corners,
                                   mesh_source_corner_t{ slotToIndex.pData[pH->hOrigin.nSlot],
                                                         MeshAttributeStore_GetCorner( &pSource->attributes, hc ) } ) ) {
                st = geometry_status_t::ALLOCATION_FAILED;
                break;
            }
        }
        if ( st == geometry_status_t::OK ) { (void)Vector_PushBack( &pDesc->faces, face ); }
    }

    if ( st == geometry_status_t::OK ) {
        // Only non-default edges are recorded, sorted by vertex pair.
        (void)GenerationPool_ForEach( &pMesh->edges,
            [&]( geometry_mesh_edge_handle_t hE, const mesh_edge_record_t &e ) noexcept -> bool_t {
                const mesh_edge_attributes_t attr = MeshAttributeStore_GetEdge( &pSource->attributes, hE );
                if ( attr.flags == 0u && e.creaseWeight == 0.0 ) { return true; }
                const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, e.hHalfEdge );
                const mesh_half_edge_record_t *pN = pH ? GenerationPool_Get( &pMesh->halfEdges, pH->hNext ) : nullptr;
                if ( pN == nullptr || pH->hOrigin.nSlot >= slotToIndex.nCount ||
                     pN->hOrigin.nSlot >= slotToIndex.nCount ) {
                    st = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                const u32 a = slotToIndex.pData[pH->hOrigin.nSlot], b = slotToIndex.pData[pN->hOrigin.nSlot];
                if ( !Vector_PushBack( &pDesc->edges,
                                       mesh_source_edge_t{ std::min( a, b ), std::max( a, b ), attr, e.creaseWeight } ) ) {
                    st = geometry_status_t::ALLOCATION_FAILED;
                    return false;
                }
                return true;
            } );
        std::sort( pDesc->edges.pData, pDesc->edges.pData + pDesc->edges.nCount,
                   []( const mesh_source_edge_t &a, const mesh_source_edge_t &b ) {
                       return a.iVertexA != b.iVertexA ? a.iVertexA < b.iVertexA : a.iVertexB < b.iVertexB;
                   } );
    }
    cleanup();
    if ( st != geometry_status_t::OK ) { MeshSourceDescription_Clear( pDesc, pSource->sourceId ); }
    return st;
}

geometry_status_t MeshSource_TryClone( const mesh_source_t *pSource, const allocator_t *pAllocator, mesh_source_t *pOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( pOut == nullptr || pOut == pSource || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    mesh_source_description_t desc{};
    geometry_status_t st = MeshSourceDescription_Init( &desc, pAllocator, pSource->sourceId );
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryDescribe( pSource, &desc ); }
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryBuild( &desc, pAllocator, pOut ); }
    MeshSourceDescription_Shutdown( &desc );
    return st;
}

geometry_source_id_t MeshSource_VertexId( const mesh_source_t *pSource, geometry_mesh_vertex_handle_t hVertex ) noexcept
{
    geometry_source_id_t id{};
    if ( !MeshSource_IsInitialized( pSource ) || !GenerationPool_Contains( &pSource->mesh.vertices, hVertex ) ||
         !SidecarGet( pSource->vertexIds, hVertex, &id ) ) {
        return GEOMETRY_SOURCE_ID_INVALID;
    }
    return id;
}

geometry_source_id_t MeshSource_FaceId( const mesh_source_t *pSource, geometry_mesh_face_handle_t hFace ) noexcept
{
    geometry_source_id_t id{};
    if ( !MeshSource_IsInitialized( pSource ) || !GenerationPool_Contains( &pSource->mesh.faces, hFace ) ||
         !SidecarGet( pSource->faceIds, hFace, &id ) ) {
        return GEOMETRY_SOURCE_ID_INVALID;
    }
    return id;
}

bool MeshSource_TryFindVertex( const mesh_source_t *pSource, geometry_source_id_t id, geometry_mesh_vertex_handle_t *pOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) || pOut == nullptr || !GeometrySourceId_IsValid( id ) ) { return false; }
    bool bFound = false;
    (void)GenerationPool_ForEach( &pSource->mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t & ) noexcept -> bool_t {
            geometry_source_id_t have{};
            if ( SidecarGet( pSource->vertexIds, h, &have ) && have.value == id.value ) {
                *pOut = h;
                bFound = true;
                return false;
            }
            return true;
        } );
    return bFound;
}

bool MeshSource_TryFindFace( const mesh_source_t *pSource, geometry_source_id_t id, geometry_mesh_face_handle_t *pOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) || pOut == nullptr || !GeometrySourceId_IsValid( id ) ) { return false; }
    bool bFound = false;
    (void)GenerationPool_ForEach( &pSource->mesh.faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> bool_t {
            geometry_source_id_t have{};
            if ( SidecarGet( pSource->faceIds, h, &have ) && have.value == id.value ) {
                *pOut = h;
                bFound = true;
                return false;
            }
            return true;
        } );
    return bFound;
}

geometry_status_t MeshSource_TrySetVertexId(
    mesh_source_t *pSource, geometry_mesh_vertex_handle_t hVertex, geometry_source_id_t id ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySourceId_IsValid( id ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !GenerationPool_Contains( &pSource->mesh.vertices, hVertex ) ) { return geometry_status_t::STALE_HANDLE; }
    if ( !SidecarCover( &pSource->vertexIds, pSource->mesh.vertices.cSlots ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pSource->vertexIds.pData[hVertex.nSlot] = mesh_identity_slot_t{ hVertex.nGeneration, id };
    return geometry_status_t::OK;
}

geometry_status_t MeshSource_TrySetFaceId(
    mesh_source_t *pSource, geometry_mesh_face_handle_t hFace, geometry_source_id_t id ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySourceId_IsValid( id ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !GenerationPool_Contains( &pSource->mesh.faces, hFace ) ) { return geometry_status_t::STALE_HANDLE; }
    if ( !SidecarCover( &pSource->faceIds, pSource->mesh.faces.cSlots ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    pSource->faceIds.pData[hFace.nSlot] = mesh_identity_slot_t{ hFace.nGeneration, id };
    return geometry_status_t::OK;
}

geometry_status_t MeshSource_TryAssignMissingIds(
    mesh_source_t *pSource,
    geometry_source_id_allocator_t *pIdAllocator,
    u32 *pAssignedOut ) noexcept
{
    if ( pAssignedOut ) { *pAssignedOut = 0u; }
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( pIdAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    // Grow the sidecars first: the only fallible step, and harmless to keep
    // if a later step fails (new slots are unwritten).
    if ( !SidecarCover( &pSource->vertexIds, pSource->mesh.vertices.cSlots ) ||
         !SidecarCover( &pSource->faceIds, pSource->mesh.faces.cSlots ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    // Pass 1: allocate from a local copy so exhaustion changes nothing.
    geometry_source_id_allocator_t ids = *pIdAllocator;
    u32 cMissing = 0u;
    geometry_status_t st = geometry_status_t::OK;
    auto countMissing = [&]( const vector_t<mesh_identity_slot_t> &side, auto h ) noexcept {
        geometry_source_id_t have{};
        if ( !SidecarGet( side, h, &have ) ) {
            const geometry_source_id_result_t r = GeometrySourceIdAllocator_Allocate( &ids );
            if ( r.status != geometry_status_t::OK ) {
                st = r.status;
                return false;
            }
            ++cMissing;
        }
        return true;
    };
    (void)GenerationPool_ForEach( &pSource->mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t & ) noexcept -> bool_t {
            return countMissing( pSource->vertexIds, h );
        } );
    if ( st == geometry_status_t::OK ) {
        (void)GenerationPool_ForEach( &pSource->mesh.faces,
            [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> bool_t {
                return countMissing( pSource->faceIds, h );
            } );
    }
    if ( st != geometry_status_t::OK ) { return st; }

    // Pass 2: replay the same allocation sequence and write.
    geometry_source_id_allocator_t replay = *pIdAllocator;
    (void)GenerationPool_ForEach( &pSource->mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t & ) noexcept -> bool_t {
            geometry_source_id_t have{};
            if ( !SidecarGet( pSource->vertexIds, h, &have ) ) {
                pSource->vertexIds.pData[h.nSlot] =
                    mesh_identity_slot_t{ h.nGeneration, GeometrySourceIdAllocator_Allocate( &replay ).id };
            }
            return true;
        } );
    (void)GenerationPool_ForEach( &pSource->mesh.faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> bool_t {
            geometry_source_id_t have{};
            if ( !SidecarGet( pSource->faceIds, h, &have ) ) {
                pSource->faceIds.pData[h.nSlot] =
                    mesh_identity_slot_t{ h.nGeneration, GeometrySourceIdAllocator_Allocate( &replay ).id };
            }
            return true;
        } );
    *pIdAllocator = replay;
    if ( pAssignedOut ) { *pAssignedOut = cMissing; }
    return geometry_status_t::OK;
}

geometry_status_t MeshSource_TryCollectSourceIds( const mesh_source_t *pSource, vector_t<geometry_source_id_t> *pOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( pOut == nullptr || pOut->pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const usize cBefore = pOut->nCount;
    const usize cNew = 1u + GenerationPool_Count( &pSource->mesh.vertices ) + GenerationPool_Count( &pSource->mesh.faces );
    if ( !Vector_Reserve( pOut, cBefore + cNew ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    (void)Vector_PushBack( pOut, pSource->sourceId );
    geometry_status_t st = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &pSource->mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t & ) noexcept -> bool_t {
            geometry_source_id_t id{};
            if ( !SidecarGet( pSource->vertexIds, h, &id ) ) {
                st = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            (void)Vector_PushBack( pOut, id );
            return true;
        } );
    if ( st == geometry_status_t::OK ) {
        (void)GenerationPool_ForEach( &pSource->mesh.faces,
            [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> bool_t {
                geometry_source_id_t id{};
                if ( !SidecarGet( pSource->faceIds, h, &id ) ) {
                    st = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                (void)Vector_PushBack( pOut, id );
                return true;
            } );
    }
    if ( st != geometry_status_t::OK ) {
        pOut->nCount = cBefore;
        return st;
    }
    std::sort( pOut->pData + cBefore, pOut->pData + pOut->nCount,
               []( geometry_source_id_t a, geometry_source_id_t b ) { return a.value < b.value; } );
    return geometry_status_t::OK;
}

mesh_source_validation_t MeshSource_Validate( const mesh_source_t *pSource, const allocator_t *pScratchAllocator ) noexcept
{
    mesh_source_validation_t r{};
    if ( !MeshSource_IsInitialized( pSource ) ) {
        r.fault = mesh_source_fault_t::NOT_INITIALIZED;
        return r;
    }
    if ( !GeometrySourceId_IsValid( pSource->sourceId ) ) {
        r.fault = mesh_source_fault_t::INVALID_ROOT_ID;
        return r;
    }
    // MeshValidation's twin check assumes a closed mesh (every half-edge
    // twinned). Mesh sources may be open, so twins are checked here with
    // boundary half-edges allowed; the other structural checks apply as is.
    const mesh_validation_result_t mv = MeshValidation_Validate( &pSource->mesh );
    if ( !mv.bClosedLoops || !mv.bAllVerticesReferenced || !mv.bEdgeLinks || !mv.bShellLinks ||
         !mv.bConsistentWinding || !TwinsConsistent( &pSource->mesh ) ) {
        r.fault = mesh_source_fault_t::INVALID_TOPOLOGY;
        return r;
    }
    const geometry_status_t connectivity =
        ValidateSourceConnectivity( &pSource->mesh, pScratchAllocator );
    if ( connectivity != geometry_status_t::OK ) {
        r.fault = connectivity == geometry_status_t::ALLOCATION_FAILED
            ? mesh_source_fault_t::VALIDATION_INCOMPLETE
            : mesh_source_fault_t::INVALID_TOPOLOGY;
        return r;
    }
    bool bBad = false;
    (void)GenerationPool_ForEach( &pSource->mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t &v ) noexcept -> bool_t {
            geometry_source_id_t id{};
            if ( !SidecarGet( pSource->vertexIds, h, &id ) ) {
                r.fault = mesh_source_fault_t::MISSING_VERTEX_ID;
                bBad = true;
                return false;
            }
            if ( !math::Vec3d_IsFinite( v.position ) ) {
                r.fault = mesh_source_fault_t::NON_FINITE;
                r.sourceId = id;
                bBad = true;
                return false;
            }
            if ( !PositionOk( v.position ) ) {
                r.fault = mesh_source_fault_t::COORDINATE_RANGE;
                r.sourceId = id;
                bBad = true;
                return false;
            }
            return true;
        } );
    if ( bBad ) { return r; }
    (void)GenerationPool_ForEach( &pSource->mesh.faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> bool_t {
            geometry_source_id_t id{};
            if ( !SidecarGet( pSource->faceIds, h, &id ) ) {
                r.fault = mesh_source_fault_t::MISSING_FACE_ID;
                bBad = true;
                return false;
            }
            return true;
        } );
    if ( bBad ) { return r; }
    if ( MeshAttributeStore_Validate( &pSource->attributes, &pSource->mesh, nullptr ) != geometry_status_t::OK ) {
        r.fault = mesh_source_fault_t::INVALID_ATTRIBUTES;
        return r;
    }
    (void)GenerationPool_ForEach( &pSource->mesh.edges,
        [&]( geometry_mesh_edge_handle_t, const mesh_edge_record_t &edge ) noexcept -> bool_t {
            if ( !std::isfinite( edge.creaseWeight ) || edge.creaseWeight < 0.0 || edge.creaseWeight > 1.0 ) {
                r.fault = mesh_source_fault_t::INVALID_ATTRIBUTES;
                bBad = true;
                return false;
            }
            return true;
        } );
    if ( bBad ) { return r; }

    vector_t<geometry_source_id_t> ids{};
    if ( !Allocator_IsValid( pScratchAllocator ) || !Vector_Init( &ids, pScratchAllocator ) ||
         MeshSource_TryCollectSourceIds( pSource, &ids ) != geometry_status_t::OK ) {
        Vector_Shutdown( &ids );
        r.fault = mesh_source_fault_t::VALIDATION_INCOMPLETE;
        return r;
    }
    // Collect sorts vertices and faces separately behind the root; a full
    // sort finds collisions across all three groups.
    std::sort( ids.pData, ids.pData + ids.nCount,
               []( geometry_source_id_t a, geometry_source_id_t b ) { return a.value < b.value; } );
    for ( usize i = 1u; i < ids.nCount; ++i ) {
        if ( ids.pData[i].value == ids.pData[i - 1u].value ) {
            r.fault = mesh_source_fault_t::DUPLICATE_SOURCE_ID;
            r.sourceId = ids.pData[i];
            break;
        }
    }
    Vector_Shutdown( &ids );
    return r;
}

} // namespace cypher::editor::geometry
