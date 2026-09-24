//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSelection.cpp
//  Purpose: Implements mesh component selection, topological selection
//           tools, lineage conversion, and selection remap.
//  Details: Topological tools first build an ID-only view of the mesh
//           (faces with corner vertex IDs and edge neighbours, the sorted
//           edge set, and sorted vertex adjacency). Every tool is then a
//           pass over sorted arrays with binary searches, independent of
//           pool order, so results are deterministic.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSelection.h"

#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_MeshTopologyOps.h"

#include <algorithm>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

bool IdLess( geometry_source_id_t a, geometry_source_id_t b ) noexcept { return a.value < b.value; }

bool EdgeLess( const mesh_edge_ref_t &x, const mesh_edge_ref_t &y ) noexcept
{
    return x.a.value != y.a.value ? x.a.value < y.a.value : x.b.value < y.b.value;
}

bool EdgeEq( const mesh_edge_ref_t &x, const mesh_edge_ref_t &y ) noexcept
{
    return x.a.value == y.a.value && x.b.value == y.b.value;
}

template <typename t, typename less_t>
bool SortedContains( const vector_t<t> &v, const t &value, less_t less ) noexcept
{
    const t *pBegin = v.pData;
    const t *pEnd = pBegin + v.nCount;
    const t *p = std::lower_bound( pBegin, pEnd, value, less );
    return p != pEnd && !less( value, *p );
}

template <typename t, typename less_t>
geometry_status_t SortedInsert( vector_t<t> *pV, const t &value, less_t less ) noexcept
{
    t *pEnd = pV->pData + pV->nCount;
    t *p = std::lower_bound( pV->pData, pEnd, value, less );
    if ( p != pEnd && !less( value, *p ) ) { return geometry_status_t::OK; }
    return Vector_Insert( pV, static_cast<usize>( p - pV->pData ), value ) ? geometry_status_t::OK
                                                                           : geometry_status_t::ALLOCATION_FAILED;
}

template <typename t, typename less_t>
bool SortedRemove( vector_t<t> *pV, const t &value, less_t less ) noexcept
{
    t *pEnd = pV->pData + pV->nCount;
    t *p = std::lower_bound( pV->pData, pEnd, value, less );
    if ( p == pEnd || less( value, *p ) ) { return false; }
    Vector_Erase( pV, static_cast<usize>( p - pV->pData ) );
    return true;
}

// Sorts and removes duplicates in place.
template <typename t, typename less_t>
void Canonicalize( vector_t<t> *pV, less_t less ) noexcept
{
    std::sort( pV->pData, pV->pData + pV->nCount, less );
    usize w = 0u;
    for ( usize r = 0u; r < pV->nCount; ++r ) {
        if ( w == 0u || less( pV->pData[w - 1u], pV->pData[r] ) ) { pV->pData[w++] = pV->pData[r]; }
    }
    pV->nCount = w;
}

// ---------------------------------------------------------------------------
// ID-only mesh view
// ---------------------------------------------------------------------------

struct view_face_t {
    geometry_source_id_t id;
    u32 iFirst;
    u32 cCorners;
};

struct vertex_pair_t {
    geometry_source_id_t from;
    geometry_source_id_t to;
};

struct mesh_view_t {
    vector_t<view_face_t> faces{};               // sorted by id
    vector_t<geometry_source_id_t> cornerVertex{}; // per face corner
    vector_t<geometry_source_id_t> cornerNeighbour{}; // face across the corner's outgoing edge (0 = boundary)
    vector_t<mesh_edge_ref_t> edges{};           // sorted, unique
    vector_t<vertex_pair_t> adjacency{};         // sorted by (from, to), both directions
    vector_t<geometry_source_id_t> vertices{};   // sorted
};

void ShutdownView( mesh_view_t *pV ) noexcept
{
    Vector_Shutdown( &pV->faces );
    Vector_Shutdown( &pV->cornerVertex );
    Vector_Shutdown( &pV->cornerNeighbour );
    Vector_Shutdown( &pV->edges );
    Vector_Shutdown( &pV->adjacency );
    Vector_Shutdown( &pV->vertices );
}

geometry_status_t BuildView( const mesh_source_t *pSource, const allocator_t *pAllocator, mesh_view_t *pV ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const editable_mesh_t *pMesh = &pSource->mesh;
    if ( !Vector_Init( &pV->faces, pAllocator ) || !Vector_Init( &pV->cornerVertex, pAllocator ) ||
         !Vector_Init( &pV->cornerNeighbour, pAllocator ) || !Vector_Init( &pV->edges, pAllocator ) ||
         !Vector_Init( &pV->adjacency, pAllocator ) || !Vector_Init( &pV->vertices, pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t st = geometry_status_t::OK;
    bool bOk = true;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t & ) noexcept -> bool_t {
            const geometry_source_id_t id = MeshSource_VertexId( pSource, h );
            if ( !GeometrySourceId_IsValid( id ) ) {
                st = geometry_status_t::INVALID_ARGUMENT; // unidentified vertex: assign IDs first
                return false;
            }
            bOk = bOk && Vector_PushBack( &pV->vertices, id );
            return true;
        } );
    if ( st == geometry_status_t::OK ) {
        (void)GenerationPool_ForEach( &pMesh->faces,
            [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
                const geometry_source_id_t id = MeshSource_FaceId( pSource, hF );
                const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
                if ( !GeometrySourceId_IsValid( id ) || pL == nullptr ) {
                    st = GeometrySourceId_IsValid( id ) ? geometry_status_t::CORRUPT_STATE : geometry_status_t::INVALID_ARGUMENT;
                    return false;
                }
                bOk = bOk && Vector_PushBack( &pV->faces, view_face_t{ id, static_cast<u32>( pV->cornerVertex.nCount ),
                                                                       pL->cHalfEdges } );
                geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
                for ( u32 k = 0u; k < pL->cHalfEdges && bOk; ++k ) {
                    const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
                    const mesh_half_edge_record_t *pN = pH ? GenerationPool_Get( &pMesh->halfEdges, pH->hNext ) : nullptr;
                    if ( pN == nullptr ) {
                        st = geometry_status_t::CORRUPT_STATE;
                        return false;
                    }
                    const geometry_source_id_t va = MeshSource_VertexId( pSource, pH->hOrigin );
                    const geometry_source_id_t vb = MeshSource_VertexId( pSource, pN->hOrigin );
                    geometry_source_id_t across{};
                    const mesh_half_edge_record_t *pT = GenerationPool_Get( &pMesh->halfEdges, pH->hTwin );
                    const mesh_loop_record_t *pTL = pT ? GenerationPool_Get( &pMesh->loops, pT->hLoop ) : nullptr;
                    if ( pTL != nullptr ) { across = MeshSource_FaceId( pSource, pTL->hFace ); }
                    bOk = bOk && Vector_PushBack( &pV->cornerVertex, va ) &&
                          Vector_PushBack( &pV->cornerNeighbour, across ) &&
                          Vector_PushBack( &pV->edges, MeshEdgeRef_Make( va, vb ) ) &&
                          Vector_PushBack( &pV->adjacency, vertex_pair_t{ va, vb } ) &&
                          Vector_PushBack( &pV->adjacency, vertex_pair_t{ vb, va } );
                    h = pH->hNext;
                }
                return bOk;
            } );
    }
    if ( st != geometry_status_t::OK ) { return st; }
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    std::sort( pV->faces.pData, pV->faces.pData + pV->faces.nCount,
               []( const view_face_t &x, const view_face_t &y ) { return x.id.value < y.id.value; } );
    Canonicalize( &pV->edges, EdgeLess );
    Canonicalize( &pV->vertices, IdLess );
    Canonicalize( &pV->adjacency, []( const vertex_pair_t &x, const vertex_pair_t &y ) {
        return x.from.value != y.from.value ? x.from.value < y.from.value : x.to.value < y.to.value;
    } );
    return geometry_status_t::OK;
}

const view_face_t *FindViewFace( const mesh_view_t &v, geometry_source_id_t id ) noexcept
{
    const view_face_t *pBegin = v.faces.pData;
    const view_face_t *pEnd = pBegin + v.faces.nCount;
    const view_face_t *p = std::lower_bound( pBegin, pEnd, id.value,
                                             []( const view_face_t &f, u64 x ) { return f.id.value < x; } );
    return ( p != pEnd && p->id.value == id.value ) ? p : nullptr;
}

// [begin, end) range of adjacency entries leaving vertex id.
void AdjacencyRange( const mesh_view_t &v, geometry_source_id_t id, usize *pBegin, usize *pEnd ) noexcept
{
    const vertex_pair_t *pB = v.adjacency.pData, *pE = pB + v.adjacency.nCount;
    const vertex_pair_t *lo = std::lower_bound( pB, pE, id.value,
                                                []( const vertex_pair_t &p, u64 x ) { return p.from.value < x; } );
    const vertex_pair_t *hi = std::upper_bound( pB, pE, id.value,
                                                []( u64 x, const vertex_pair_t &p ) { return x < p.from.value; } );
    *pBegin = static_cast<usize>( lo - pB );
    *pEnd = static_cast<usize>( hi - pB );
}

// Scoped view + scratch vector bundle for the tool entry points.
struct tool_scope_t {
    mesh_view_t view{};
    vector_t<geometry_source_id_t> ids{};
    vector_t<mesh_edge_ref_t> edges{};
    ~tool_scope_t()
    {
        ShutdownView( &view );
        Vector_Shutdown( &ids );
        Vector_Shutdown( &edges );
    }
};

geometry_status_t Prepare( const mesh_selection_t *pSel, const mesh_source_t *pMesh, tool_scope_t *pScope ) noexcept
{
    if ( pSel == nullptr || pSel->faces.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !MeshSource_IsInitialized( pMesh ) || pMesh->sourceId.value != pSel->meshId.value ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const allocator_t *pAllocator = pSel->faces.pAllocator;
    if ( !Vector_Init( &pScope->ids, pAllocator ) || !Vector_Init( &pScope->edges, pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return BuildView( pMesh, pAllocator, &pScope->view );
}

// Appends src to dst and canonicalizes.
template <typename t, typename less_t>
geometry_status_t MergeInto( vector_t<t> *pDst, const vector_t<t> &src, less_t less ) noexcept
{
    if ( !Vector_Append( pDst, span_t<const t>{ src.pData, src.nCount } ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    Canonicalize( pDst, less );
    return geometry_status_t::OK;
}

geometry_source_id_t FollowMerges( const mesh_edit_lineage_t *pLineage, geometry_source_id_t id ) noexcept
{
    // Chains are short (weld cascades); the bound only guards against a
    // malformed cyclic lineage.
    for ( int step = 0; step < 64; ++step ) {
        bool bMoved = false;
        for ( usize i = 0u; i < pLineage->merges.nCount; ++i ) {
            if ( pLineage->merges.pData[i].removedId.value == id.value ) {
                id = pLineage->merges.pData[i].survivorId;
                bMoved = true;
                break;
            }
        }
        if ( !bMoved ) { break; }
    }
    return id;
}

} // namespace

mesh_edge_ref_t MeshEdgeRef_Make( geometry_source_id_t x, geometry_source_id_t y ) noexcept
{
    return x.value < y.value ? mesh_edge_ref_t{ x, y } : mesh_edge_ref_t{ y, x };
}

// ---------------------------------------------------------------------------
// Lineage
// ---------------------------------------------------------------------------

geometry_status_t MeshEditLineage_Init( mesh_edit_lineage_t *pLineage, const allocator_t *pAllocator ) noexcept
{
    if ( pLineage == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &pLineage->faces, pAllocator ) || !Vector_Init( &pLineage->edges, pAllocator ) ||
         !Vector_Init( &pLineage->merges, pAllocator ) ) {
        MeshEditLineage_Shutdown( pLineage );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void MeshEditLineage_Shutdown( mesh_edit_lineage_t *pLineage ) noexcept
{
    if ( pLineage == nullptr ) { return; }
    Vector_Shutdown( &pLineage->faces );
    Vector_Shutdown( &pLineage->edges );
    Vector_Shutdown( &pLineage->merges );
}

geometry_status_t MeshEditProvenance_TryToLineage(
    const mesh_edit_provenance_t *pProvenance,
    const mesh_source_t *pSource,
    mesh_edit_lineage_t *pLineage ) noexcept
{
    if ( pProvenance == nullptr || pLineage == nullptr || pLineage->faces.pAllocator == nullptr ||
         !MeshSource_IsInitialized( pSource ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    Vector_Clear( &pLineage->faces );
    Vector_Clear( &pLineage->edges );
    Vector_Clear( &pLineage->merges );
    for ( usize i = 0u; i < pProvenance->faces.nCount; ++i ) {
        const geometry_source_id_t child = MeshSource_FaceId( pSource, pProvenance->faces.pData[i].hFace );
        if ( !GeometrySourceId_IsValid( child ) ) { return geometry_status_t::INVALID_ARGUMENT; }
        if ( !Vector_PushBack( &pLineage->faces, mesh_lineage_face_t{ child, pProvenance->faces.pData[i].parentFaceId } ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    for ( usize i = 0u; i < pProvenance->edges.nCount; ++i ) {
        const mesh_edit_edge_origin_t &e = pProvenance->edges.pData[i];
        const geometry_source_id_t a = MeshSource_VertexId( pSource, e.hA ), b = MeshSource_VertexId( pSource, e.hB );
        if ( !GeometrySourceId_IsValid( a ) || !GeometrySourceId_IsValid( b ) ) { return geometry_status_t::INVALID_ARGUMENT; }
        if ( !Vector_PushBack( &pLineage->edges,
                               mesh_lineage_edge_t{ MeshEdgeRef_Make( a, b ), MeshEdgeRef_Make( e.parentA, e.parentB ) } ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    for ( usize i = 0u; i < pProvenance->merges.nCount; ++i ) {
        const geometry_source_id_t survivor = MeshSource_VertexId( pSource, pProvenance->merges.pData[i].hSurvivor );
        if ( !GeometrySourceId_IsValid( survivor ) ) { return geometry_status_t::INVALID_ARGUMENT; }
        if ( !Vector_PushBack( &pLineage->merges, mesh_lineage_vertex_t{ pProvenance->merges.pData[i].removedId, survivor } ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Sets
// ---------------------------------------------------------------------------

geometry_status_t MeshSelection_Init( mesh_selection_t *pSelection, const allocator_t *pAllocator, geometry_source_id_t meshId ) noexcept
{
    if ( pSelection == nullptr || !Allocator_IsValid( pAllocator ) || !GeometrySourceId_IsValid( meshId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pSelection->faces.pAllocator != nullptr ) { return geometry_status_t::ALREADY_INITIALIZED; }
    if ( !Vector_Init( &pSelection->vertices, pAllocator ) || !Vector_Init( &pSelection->edges, pAllocator ) ||
         !Vector_Init( &pSelection->faces, pAllocator ) ) {
        MeshSelection_Shutdown( pSelection );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pSelection->meshId = meshId;
    return geometry_status_t::OK;
}

void MeshSelection_Shutdown( mesh_selection_t *pSelection ) noexcept
{
    if ( pSelection == nullptr ) { return; }
    Vector_Shutdown( &pSelection->vertices );
    Vector_Shutdown( &pSelection->edges );
    Vector_Shutdown( &pSelection->faces );
    pSelection->meshId = GEOMETRY_SOURCE_ID_INVALID;
}

void MeshSelection_Clear( mesh_selection_t *pSelection ) noexcept
{
    if ( pSelection == nullptr ) { return; }
    Vector_Clear( &pSelection->vertices );
    Vector_Clear( &pSelection->edges );
    Vector_Clear( &pSelection->faces );
}

geometry_status_t MeshSelection_TryAddVertex( mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept
{
    if ( pSelection == nullptr || pSelection->faces.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySourceId_IsValid( id ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    return SortedInsert( &pSelection->vertices, id, IdLess );
}

geometry_status_t MeshSelection_TryAddFace( mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept
{
    if ( pSelection == nullptr || pSelection->faces.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySourceId_IsValid( id ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    return SortedInsert( &pSelection->faces, id, IdLess );
}

geometry_status_t MeshSelection_TryAddEdge( mesh_selection_t *pSelection, mesh_edge_ref_t edge ) noexcept
{
    if ( pSelection == nullptr || pSelection->faces.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    edge = MeshEdgeRef_Make( edge.a, edge.b );
    if ( !GeometrySourceId_IsValid( edge.a ) || edge.a.value == edge.b.value ) { return geometry_status_t::INVALID_ARGUMENT; }
    return SortedInsert( &pSelection->edges, edge, EdgeLess );
}

bool MeshSelection_RemoveVertex( mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept
{
    return pSelection != nullptr && SortedRemove( &pSelection->vertices, id, IdLess );
}

bool MeshSelection_RemoveFace( mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept
{
    return pSelection != nullptr && SortedRemove( &pSelection->faces, id, IdLess );
}

bool MeshSelection_RemoveEdge( mesh_selection_t *pSelection, mesh_edge_ref_t edge ) noexcept
{
    return pSelection != nullptr && SortedRemove( &pSelection->edges, MeshEdgeRef_Make( edge.a, edge.b ), EdgeLess );
}

bool MeshSelection_HasVertex( const mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept
{
    return pSelection != nullptr && SortedContains( pSelection->vertices, id, IdLess );
}

bool MeshSelection_HasFace( const mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept
{
    return pSelection != nullptr && SortedContains( pSelection->faces, id, IdLess );
}

bool MeshSelection_HasEdge( const mesh_selection_t *pSelection, mesh_edge_ref_t edge ) noexcept
{
    return pSelection != nullptr && SortedContains( pSelection->edges, MeshEdgeRef_Make( edge.a, edge.b ), EdgeLess );
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

geometry_status_t MeshSelection_TryGrow( mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_selection_mode_t mode ) noexcept
{
    tool_scope_t scope{};
    geometry_status_t st = Prepare( pSelection, pMesh, &scope );
    if ( st != geometry_status_t::OK ) { return st; }
    const mesh_view_t &v = scope.view;
    bool bOk = true;
    if ( mode == mesh_selection_mode_t::FACE ) {
        for ( usize i = 0u; i < pSelection->faces.nCount && bOk; ++i ) {
            const view_face_t *pF = FindViewFace( v, pSelection->faces.pData[i] );
            if ( pF == nullptr ) { continue; }
            for ( u32 k = 0u; k < pF->cCorners && bOk; ++k ) {
                const geometry_source_id_t n = v.cornerNeighbour.pData[pF->iFirst + k];
                if ( GeometrySourceId_IsValid( n ) ) { bOk = Vector_PushBack( &scope.ids, n ); }
            }
        }
        return bOk ? MergeInto( &pSelection->faces, scope.ids, IdLess ) : geometry_status_t::ALLOCATION_FAILED;
    }
    if ( mode == mesh_selection_mode_t::VERTEX ) {
        for ( usize i = 0u; i < pSelection->vertices.nCount && bOk; ++i ) {
            usize b = 0u, e = 0u;
            AdjacencyRange( v, pSelection->vertices.pData[i], &b, &e );
            for ( usize j = b; j < e && bOk; ++j ) { bOk = Vector_PushBack( &scope.ids, v.adjacency.pData[j].to ); }
        }
        return bOk ? MergeInto( &pSelection->vertices, scope.ids, IdLess ) : geometry_status_t::ALLOCATION_FAILED;
    }
    // EDGE: edges touching an endpoint of a selected edge.
    for ( usize i = 0u; i < pSelection->edges.nCount && bOk; ++i ) {
        bOk = Vector_PushBack( &scope.ids, pSelection->edges.pData[i].a ) &&
              Vector_PushBack( &scope.ids, pSelection->edges.pData[i].b );
    }
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    Canonicalize( &scope.ids, IdLess );
    for ( usize i = 0u; i < v.edges.nCount && bOk; ++i ) {
        const mesh_edge_ref_t &e = v.edges.pData[i];
        if ( SortedContains( scope.ids, e.a, IdLess ) || SortedContains( scope.ids, e.b, IdLess ) ) {
            bOk = Vector_PushBack( &scope.edges, e );
        }
    }
    return bOk ? MergeInto( &pSelection->edges, scope.edges, EdgeLess ) : geometry_status_t::ALLOCATION_FAILED;
}

geometry_status_t MeshSelection_TryShrink( mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_selection_mode_t mode ) noexcept
{
    tool_scope_t scope{};
    geometry_status_t st = Prepare( pSelection, pMesh, &scope );
    if ( st != geometry_status_t::OK ) { return st; }
    const mesh_view_t &v = scope.view;
    bool bOk = true;
    if ( mode == mesh_selection_mode_t::FACE ) {
        for ( usize i = 0u; i < pSelection->faces.nCount && bOk; ++i ) {
            const view_face_t *pF = FindViewFace( v, pSelection->faces.pData[i] );
            bool bInterior = pF != nullptr;
            for ( u32 k = 0u; bInterior && k < pF->cCorners; ++k ) {
                const geometry_source_id_t n = v.cornerNeighbour.pData[pF->iFirst + k];
                bInterior = GeometrySourceId_IsValid( n ) && SortedContains( pSelection->faces, n, IdLess );
            }
            if ( bInterior ) { bOk = Vector_PushBack( &scope.ids, pSelection->faces.pData[i] ); }
        }
        if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
        Vector_Clear( &pSelection->faces );
        return MergeInto( &pSelection->faces, scope.ids, IdLess );
    }
    if ( mode == mesh_selection_mode_t::VERTEX ) {
        for ( usize i = 0u; i < pSelection->vertices.nCount && bOk; ++i ) {
            usize b = 0u, e = 0u;
            AdjacencyRange( v, pSelection->vertices.pData[i], &b, &e );
            bool bInterior = b < e;
            for ( usize j = b; bInterior && j < e; ++j ) {
                bInterior = SortedContains( pSelection->vertices, v.adjacency.pData[j].to, IdLess );
            }
            if ( bInterior ) { bOk = Vector_PushBack( &scope.ids, pSelection->vertices.pData[i] ); }
        }
        if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
        Vector_Clear( &pSelection->vertices );
        return MergeInto( &pSelection->vertices, scope.ids, IdLess );
    }
    // EDGE: keep edges whose every touching edge is selected.
    for ( usize i = 0u; i < pSelection->edges.nCount && bOk; ++i ) {
        const mesh_edge_ref_t &sel = pSelection->edges.pData[i];
        bool bInterior = true;
        for ( usize j = 0u; bInterior && j < v.edges.nCount; ++j ) {
            const mesh_edge_ref_t &e = v.edges.pData[j];
            const bool bTouches = e.a.value == sel.a.value || e.a.value == sel.b.value || e.b.value == sel.a.value ||
                                  e.b.value == sel.b.value;
            if ( bTouches ) { bInterior = SortedContains( pSelection->edges, e, EdgeLess ); }
        }
        if ( bInterior ) { bOk = Vector_PushBack( &scope.edges, sel ); }
    }
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    Vector_Clear( &pSelection->edges );
    return MergeInto( &pSelection->edges, scope.edges, EdgeLess );
}

geometry_status_t MeshSelection_TrySelectConnected(
    mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_selection_mode_t mode ) noexcept
{
    tool_scope_t scope{};
    geometry_status_t st = Prepare( pSelection, pMesh, &scope );
    if ( st != geometry_status_t::OK ) { return st; }
    const mesh_view_t &v = scope.view;
    bool bOk = true;
    if ( mode == mesh_selection_mode_t::FACE ) {
        // Breadth-first over faces through shared edges; scope.ids is the
        // queue, the (sorted) selection the visited set.
        bOk = Vector_Append( &scope.ids, span_t<const geometry_source_id_t>{ pSelection->faces.pData, pSelection->faces.nCount } );
        for ( usize q = 0u; q < scope.ids.nCount && bOk; ++q ) {
            const view_face_t *pF = FindViewFace( v, scope.ids.pData[q] );
            if ( pF == nullptr ) { continue; }
            for ( u32 k = 0u; k < pF->cCorners && bOk; ++k ) {
                const geometry_source_id_t n = v.cornerNeighbour.pData[pF->iFirst + k];
                if ( GeometrySourceId_IsValid( n ) && !SortedContains( pSelection->faces, n, IdLess ) ) {
                    bOk = SortedInsert( &pSelection->faces, n, IdLess ) == geometry_status_t::OK &&
                          Vector_PushBack( &scope.ids, n );
                }
            }
        }
        return bOk ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
    }
    // Vertices (and, for edges, the endpoints) flood through adjacency.
    vector_t<geometry_source_id_t> &visited = scope.ids;
    if ( mode == mesh_selection_mode_t::VERTEX ) {
        bOk = Vector_Append( &visited, span_t<const geometry_source_id_t>{ pSelection->vertices.pData, pSelection->vertices.nCount } );
    } else {
        for ( usize i = 0u; i < pSelection->edges.nCount && bOk; ++i ) {
            bOk = Vector_PushBack( &visited, pSelection->edges.pData[i].a ) &&
                  Vector_PushBack( &visited, pSelection->edges.pData[i].b );
        }
    }
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    Canonicalize( &visited, IdLess );
    vector_t<geometry_source_id_t> queue{};
    if ( !Vector_Init( &queue, pSelection->faces.pAllocator ) ||
         !Vector_Append( &queue, span_t<const geometry_source_id_t>{ visited.pData, visited.nCount } ) ) {
        Vector_Shutdown( &queue );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize q = 0u; q < queue.nCount && bOk; ++q ) {
        usize b = 0u, e = 0u;
        AdjacencyRange( v, queue.pData[q], &b, &e );
        for ( usize j = b; j < e && bOk; ++j ) {
            const geometry_source_id_t n = v.adjacency.pData[j].to;
            if ( !SortedContains( visited, n, IdLess ) ) {
                bOk = SortedInsert( &visited, n, IdLess ) == geometry_status_t::OK && Vector_PushBack( &queue, n );
            }
        }
    }
    Vector_Shutdown( &queue );
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    if ( mode == mesh_selection_mode_t::VERTEX ) { return MergeInto( &pSelection->vertices, visited, IdLess ); }
    for ( usize i = 0u; i < v.edges.nCount && bOk; ++i ) {
        if ( SortedContains( visited, v.edges.pData[i].a, IdLess ) ) { bOk = Vector_PushBack( &scope.edges, v.edges.pData[i] ); }
    }
    return bOk ? MergeInto( &pSelection->edges, scope.edges, EdgeLess ) : geometry_status_t::ALLOCATION_FAILED;
}

namespace
{

geometry_status_t AddOpEdges( mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_edge_ref_t edge, bool bLoop ) noexcept
{
    if ( pSelection == nullptr || pSelection->faces.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !MeshSource_IsInitialized( pMesh ) || pMesh->sourceId.value != pSelection->meshId.value ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_mesh_edge_handle_t hEdge{};
    if ( !MeshSourceEdit_TryFindEdge( pMesh, edge.a, edge.b, &hEdge ) ) { return geometry_status_t::INVALID_HANDLE; }
    const mesh_edge_selection_result_t r =
        bLoop ? MeshOps_SelectEdgeLoop( &pMesh->mesh, hEdge ) : MeshOps_SelectEdgeRing( &pMesh->mesh, hEdge );
    if ( r.status != geometry_status_t::OK ) { return r.status; }
    for ( u32 i = 0u; i < r.cEdges; ++i ) {
        const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->mesh.edges, r.edges[i] );
        const mesh_half_edge_record_t *pH = pE ? GenerationPool_Get( &pMesh->mesh.halfEdges, pE->hHalfEdge ) : nullptr;
        const mesh_half_edge_record_t *pN = pH ? GenerationPool_Get( &pMesh->mesh.halfEdges, pH->hNext ) : nullptr;
        if ( pN == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        const geometry_status_t st = MeshSelection_TryAddEdge(
            pSelection,
            MeshEdgeRef_Make( MeshSource_VertexId( pMesh, pH->hOrigin ), MeshSource_VertexId( pMesh, pN->hOrigin ) ) );
        if ( st != geometry_status_t::OK ) { return st; }
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t MeshSelection_TrySelectEdgeLoop( mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_edge_ref_t edge ) noexcept
{
    return AddOpEdges( pSelection, pMesh, edge, true );
}

geometry_status_t MeshSelection_TrySelectEdgeRing( mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_edge_ref_t edge ) noexcept
{
    return AddOpEdges( pSelection, pMesh, edge, false );
}

geometry_status_t MeshSelection_TryConvert(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    mesh_selection_mode_t from,
    mesh_selection_mode_t to ) noexcept
{
    tool_scope_t scope{};
    geometry_status_t st = Prepare( pSelection, pMesh, &scope );
    if ( st != geometry_status_t::OK ) { return st; }
    if ( from == to ) { return geometry_status_t::OK; }
    const mesh_view_t &v = scope.view;
    bool bOk = true;
    auto faceCornerEdge = [&]( const view_face_t &f, u32 k ) noexcept {
        return MeshEdgeRef_Make( v.cornerVertex.pData[f.iFirst + k], v.cornerVertex.pData[f.iFirst + ( k + 1u ) % f.cCorners] );
    };
    if ( from == mesh_selection_mode_t::FACE ) {
        for ( usize i = 0u; i < pSelection->faces.nCount && bOk; ++i ) {
            const view_face_t *pF = FindViewFace( v, pSelection->faces.pData[i] );
            for ( u32 k = 0u; pF != nullptr && k < pF->cCorners && bOk; ++k ) {
                bOk = to == mesh_selection_mode_t::VERTEX ? Vector_PushBack( &scope.ids, v.cornerVertex.pData[pF->iFirst + k] )
                                                          : Vector_PushBack( &scope.edges, faceCornerEdge( *pF, k ) );
            }
        }
    } else if ( from == mesh_selection_mode_t::EDGE && to == mesh_selection_mode_t::VERTEX ) {
        for ( usize i = 0u; i < pSelection->edges.nCount && bOk; ++i ) {
            bOk = Vector_PushBack( &scope.ids, pSelection->edges.pData[i].a ) &&
                  Vector_PushBack( &scope.ids, pSelection->edges.pData[i].b );
        }
    } else if ( from == mesh_selection_mode_t::VERTEX && to == mesh_selection_mode_t::EDGE ) {
        for ( usize i = 0u; i < v.edges.nCount && bOk; ++i ) {
            const mesh_edge_ref_t &e = v.edges.pData[i];
            if ( SortedContains( pSelection->vertices, e.a, IdLess ) && SortedContains( pSelection->vertices, e.b, IdLess ) ) {
                bOk = Vector_PushBack( &scope.edges, e );
            }
        }
    } else {
        // VERTEX or EDGE -> FACE: faces fully covered.
        for ( usize i = 0u; i < v.faces.nCount && bOk; ++i ) {
            const view_face_t &f = v.faces.pData[i];
            bool bCovered = true;
            for ( u32 k = 0u; bCovered && k < f.cCorners; ++k ) {
                bCovered = from == mesh_selection_mode_t::VERTEX
                               ? SortedContains( pSelection->vertices, v.cornerVertex.pData[f.iFirst + k], IdLess )
                               : SortedContains( pSelection->edges, faceCornerEdge( f, k ), EdgeLess );
            }
            if ( bCovered ) { bOk = Vector_PushBack( &scope.ids, f.id ); }
        }
    }
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    switch ( to ) {
    case mesh_selection_mode_t::VERTEX:
        Vector_Clear( &pSelection->vertices );
        return MergeInto( &pSelection->vertices, scope.ids, IdLess );
    case mesh_selection_mode_t::EDGE:
        Vector_Clear( &pSelection->edges );
        return MergeInto( &pSelection->edges, scope.edges, EdgeLess );
    default:
        Vector_Clear( &pSelection->faces );
        return MergeInto( &pSelection->faces, scope.ids, IdLess );
    }
}

geometry_status_t MeshSelection_TryPrune( mesh_selection_t *pSelection, const mesh_source_t *pMesh, u32 *pRemovedOut ) noexcept
{
    if ( pRemovedOut ) { *pRemovedOut = 0u; }
    tool_scope_t scope{};
    geometry_status_t st = Prepare( pSelection, pMesh, &scope );
    if ( st != geometry_status_t::OK ) { return st; }
    const mesh_view_t &v = scope.view;
    u32 cRemoved = 0u;
    auto pruneIds = [&]( vector_t<geometry_source_id_t> *pSet, bool bFaces ) noexcept {
        usize w = 0u;
        for ( usize r = 0u; r < pSet->nCount; ++r ) {
            const geometry_source_id_t id = pSet->pData[r];
            const bool bLive = bFaces ? FindViewFace( v, id ) != nullptr : SortedContains( v.vertices, id, IdLess );
            if ( bLive ) {
                pSet->pData[w++] = id;
            } else {
                ++cRemoved;
            }
        }
        pSet->nCount = w;
    };
    pruneIds( &pSelection->vertices, false );
    pruneIds( &pSelection->faces, true );
    usize w = 0u;
    for ( usize r = 0u; r < pSelection->edges.nCount; ++r ) {
        if ( SortedContains( v.edges, pSelection->edges.pData[r], EdgeLess ) ) {
            pSelection->edges.pData[w++] = pSelection->edges.pData[r];
        } else {
            ++cRemoved;
        }
    }
    pSelection->edges.nCount = w;
    if ( pRemovedOut ) { *pRemovedOut = cRemoved; }
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Remap
// ---------------------------------------------------------------------------

geometry_status_t MeshSelectionRemapReport_Init( mesh_selection_remap_report_t *pReport, const allocator_t *pAllocator ) noexcept
{
    if ( pReport == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    pReport->cKept = pReport->cSplit = pReport->cMerged = pReport->cLost = 0u;
    return Vector_Init( &pReport->entries, pAllocator ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
}

void MeshSelectionRemapReport_Shutdown( mesh_selection_remap_report_t *pReport ) noexcept
{
    if ( pReport == nullptr ) { return; }
    Vector_Shutdown( &pReport->entries );
}

bool MeshSelectionRemapReport_IsAmbiguous( const mesh_selection_remap_report_t *pReport ) noexcept
{
    return pReport != nullptr && ( pReport->cSplit + pReport->cMerged + pReport->cLost ) > 0u;
}

geometry_status_t MeshSelection_TryRemap(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    const mesh_edit_lineage_t *pLineage,
    mesh_selection_remap_report_t *pReportOut ) noexcept
{
    if ( pLineage == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    tool_scope_t scope{};
    geometry_status_t st = Prepare( pSelection, pMesh, &scope );
    if ( st != geometry_status_t::OK ) { return st; }
    const mesh_view_t &v = scope.view;
    mesh_selection_remap_report_t local{};
    mesh_selection_remap_report_t &report = pReportOut != nullptr ? *pReportOut : local;
    if ( pReportOut != nullptr ) {
        Vector_Clear( &report.entries );
        report.cKept = report.cSplit = report.cMerged = report.cLost = 0u;
    }
    bool bOk = true;
    auto note = [&]( mesh_selection_outcome_t outcome, mesh_selection_mode_t mode, geometry_source_id_t a,
                     geometry_source_id_t b ) noexcept {
        switch ( outcome ) {
        case mesh_selection_outcome_t::SPLIT: ++report.cSplit; break;
        case mesh_selection_outcome_t::MERGED: ++report.cMerged; break;
        default: ++report.cLost; break;
        }
        if ( pReportOut != nullptr ) {
            bOk = bOk && Vector_PushBack( &report.entries, mesh_selection_remap_entry_t{ outcome, mode, a, b } );
        }
    };

    // Faces.
    vector_t<geometry_source_id_t> &faces = scope.ids;
    for ( usize i = 0u; i < pSelection->faces.nCount && bOk; ++i ) {
        const geometry_source_id_t f = pSelection->faces.pData[i];
        const bool bLive = FindViewFace( v, f ) != nullptr;
        bool bChildren = false;
        for ( usize j = 0u; j < pLineage->faces.nCount && bOk; ++j ) {
            const mesh_lineage_face_t &l = pLineage->faces.pData[j];
            if ( l.parentId.value == f.value && FindViewFace( v, l.childId ) != nullptr ) {
                bOk = Vector_PushBack( &faces, l.childId );
                bChildren = true;
            }
        }
        if ( bLive ) { bOk = bOk && Vector_PushBack( &faces, f ); }
        if ( bChildren ) {
            note( mesh_selection_outcome_t::SPLIT, mesh_selection_mode_t::FACE, f, {} );
        } else if ( bLive ) {
            ++report.cKept;
        } else {
            note( mesh_selection_outcome_t::LOST, mesh_selection_mode_t::FACE, f, {} );
        }
    }

    // Vertices.
    vector_t<geometry_source_id_t> vertices{};
    vector_t<mesh_edge_ref_t> &edges = scope.edges;
    if ( !Vector_Init( &vertices, pSelection->faces.pAllocator ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i < pSelection->vertices.nCount && bOk; ++i ) {
        const geometry_source_id_t id = pSelection->vertices.pData[i];
        if ( SortedContains( v.vertices, id, IdLess ) ) {
            bOk = Vector_PushBack( &vertices, id );
            ++report.cKept;
            continue;
        }
        const geometry_source_id_t to = FollowMerges( pLineage, id );
        if ( to.value != id.value && SortedContains( v.vertices, to, IdLess ) ) {
            bOk = Vector_PushBack( &vertices, to );
            note( mesh_selection_outcome_t::MERGED, mesh_selection_mode_t::VERTEX, id, to );
        } else {
            note( mesh_selection_outcome_t::LOST, mesh_selection_mode_t::VERTEX, id, {} );
        }
    }

    // Edges.
    for ( usize i = 0u; i < pSelection->edges.nCount && bOk; ++i ) {
        const mesh_edge_ref_t e = pSelection->edges.pData[i];
        if ( SortedContains( v.edges, e, EdgeLess ) ) {
            bOk = Vector_PushBack( &edges, e );
            ++report.cKept;
            continue;
        }
        bool bChildren = false;
        for ( usize j = 0u; j < pLineage->edges.nCount && bOk; ++j ) {
            const mesh_lineage_edge_t &l = pLineage->edges.pData[j];
            if ( EdgeEq( l.parent, e ) && SortedContains( v.edges, l.child, EdgeLess ) ) {
                bOk = Vector_PushBack( &edges, l.child );
                bChildren = true;
            }
        }
        if ( bChildren ) {
            note( mesh_selection_outcome_t::SPLIT, mesh_selection_mode_t::EDGE, e.a, e.b );
            continue;
        }
        const geometry_source_id_t a = FollowMerges( pLineage, e.a ), b = FollowMerges( pLineage, e.b );
        const mesh_edge_ref_t merged = MeshEdgeRef_Make( a, b );
        if ( a.value != b.value && SortedContains( v.edges, merged, EdgeLess ) ) {
            bOk = Vector_PushBack( &edges, merged );
            note( mesh_selection_outcome_t::MERGED, mesh_selection_mode_t::EDGE, e.a, e.b );
        } else {
            note( mesh_selection_outcome_t::LOST, mesh_selection_mode_t::EDGE, e.a, e.b );
        }
    }
    if ( !bOk ) {
        Vector_Shutdown( &vertices );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    Vector_Clear( &pSelection->faces );
    Vector_Clear( &pSelection->vertices );
    Vector_Clear( &pSelection->edges );
    st = MergeInto( &pSelection->faces, faces, IdLess );
    if ( st == geometry_status_t::OK ) { st = MergeInto( &pSelection->vertices, vertices, IdLess ); }
    if ( st == geometry_status_t::OK ) { st = MergeInto( &pSelection->edges, edges, EdgeLess ); }
    Vector_Shutdown( &vertices );
    return st;
}

} // namespace cypher::editor::geometry
