//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Sanitation.cpp
//  Purpose: Implements PolygonSoup <-> EditableMesh conversion.
//  Details: Everything is decided on plain index arrays first; the mesh
//           pools are touched only after every check has passed, so the
//           only failure modes after that point are allocation/limits.
//
//           Half-edge h is corner k of face f, running c[k] -> c[k+1].
//           twin(h) is found by sorting half-edges on their undirected key.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Sanitation.h"
#include "CypherGeometry_PointWeld.h"
#include "CypherGeometry_MeshValidation.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

template <typename type_t>
struct buf_t {
    vector_t<type_t> v{};
    bool ok{ true };
    bool Init( const allocator_t *a, usize cap ) noexcept { return ok = Vector_Init( &v, a, cap ); }
    ~buf_t() { Vector_Shutdown( &v ); }
    void Push( const type_t &x ) noexcept { if ( ok && !Vector_PushBack( &v, x ) ) { ok = false; } }
    bool Resize( usize n ) noexcept { if ( ok && !Vector_Resize( &v, n ) ) { ok = false; } return ok; }
    usize Size() const noexcept { return v.nCount; }
    type_t &operator[]( usize i ) noexcept { return v.pData[i]; }
    type_t *Data() noexcept { return v.pData; }
};

struct face_t {
    u32 iFirst{ 0u };  // into corner buffer (welded, compacted indices)
    u32 cCorners{ 0u };
    u32 iInput{ 0u };  // input face index
};

struct half_t {
    u32 from{ 0u };
    u32 to{ 0u };
    u32 face{ 0u };
    u32 twin{ CY_INVALID_INDEX };
};

u32 Root( u32 *parent, u32 x ) noexcept
{
    while ( parent[x] != x ) { parent[x] = parent[parent[x]]; x = parent[x]; }
    return x;
}

sanitation_report_t Fail( sanitation_report_t r, geometry_status_t s, sanitation_fault_t f ) noexcept
{
    r.status = s;
    r.fault = f;
    return r;
}

math::vec3d_t Newell( const math::vec3d_t *pos, const u32 *c, u32 n ) noexcept
{
    math::vec3d_t nrm = math::Vec3d_Make( 0.0, 0.0, 0.0 );
    for ( u32 i = 0u; i < n; ++i ) {
        const math::vec3d_t a = pos[c[i]];
        const math::vec3d_t b = pos[c[( i + 1u ) % n]];
        nrm.x += ( a.y - b.y ) * ( a.z + b.z );
        nrm.y += ( a.z - b.z ) * ( a.x + b.x );
        nrm.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    return nrm;
}

} // namespace

const char *SanitationFault_Name( sanitation_fault_t fault ) noexcept
{
    switch ( fault ) {
        case sanitation_fault_t::NONE: return "none";
        case sanitation_fault_t::INVALID_INPUT: return "invalid_input";
        case sanitation_fault_t::DEGENERATE_FACE: return "degenerate_face";
        case sanitation_fault_t::NON_MANIFOLD_EDGE: return "non_manifold_edge";
        case sanitation_fault_t::INCONSISTENT_ORIENTATION: return "inconsistent_orientation";
        case sanitation_fault_t::OPEN_BOUNDARY: return "open_boundary";
        case sanitation_fault_t::NON_MANIFOLD_VERTEX: return "non_manifold_vertex";
        case sanitation_fault_t::EMPTY: return "empty";
    }
    return "unknown";
}

sanitation_report_t Sanitation_TryPolygonSoupToMesh(
    const polygon_soup_t *pSoup,
    const sanitation_policy_t &policy,
    const allocator_t *pAllocator,
    editable_mesh_t *pMeshOut,
    vector_t<geometry_mesh_face_handle_t> *pFaceMapOut ) noexcept
{
    sanitation_report_t r{};
    if ( pSoup == nullptr || pMeshOut == nullptr || !Allocator_IsValid( pAllocator ) ||
         !( policy.fWeldDistance >= 0.0 ) || !( policy.fMinimumFaceArea >= 0.0 ) ||
         ( pFaceMapOut != nullptr && pFaceMapOut->pAllocator == nullptr ) ) {
        return r;
    }
    if ( !PolygonSoup_IsInitialized( pSoup ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    if ( EditableMesh_IsInitialized( pMeshOut ) ) {
        r.status = geometry_status_t::ALREADY_INITIALIZED;
        return r;
    }
    r.cInputFaces = static_cast<u32>( pSoup->faces.nCount );

    // ---- 1. Structural input checks (never repaired) --------------------------
    {
        const polygon_soup_validation_t v = PolygonSoup_Validate( pSoup, 0.0 );
        if ( v.fault == polygon_soup_fault_t::NON_FINITE ||
             v.fault == polygon_soup_fault_t::INDEX_OUT_OF_RANGE ) {
            r.iFace = v.iFace;
            r.iVertexA = v.iVertex;
            return Fail( r, v.status, sanitation_fault_t::INVALID_INPUT );
        }
    }

    // ---- 2. Explicit weld ------------------------------------------------------
    const usize cVin = pSoup->positions.nCount;
    buf_t<u32> remap, reps;
    if ( !remap.Init( pAllocator, cVin ) || !reps.Init( pAllocator, cVin ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    point_weld_result_t w{};
    if ( policy.bWeld ) {
        w = PointWeld_BuildRemap( span_t<const math::vec3d_t>{ pSoup->positions.pData, cVin },
                                  policy.fWeldDistance, pAllocator, &remap.v, &reps.v );
        if ( w.status != geometry_status_t::OK ) {
            return Fail( r, w.status, sanitation_fault_t::NONE );
        }
    } else {
        // Identity: every soup vertex is its own cluster.
        if ( !remap.Resize( cVin ) || !reps.Resize( cVin ) ) {
            return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
        }
        for ( usize i = 0u; i < cVin; ++i ) {
            remap[i] = static_cast<u32>( i );
            reps[i] = static_cast<u32>( i );
        }
    }
    r.cWeldedVertices = w.cMerged;
    r.fMaxWeldDisplacement = w.fMaxDisplacement;
    const math::vec3d_t *inPos = pSoup->positions.pData;

    // ---- 3. Faces on welded indices; degenerate handling ------------------------
    // Clusters are addressed by weld cluster id; positions are the cluster
    // representatives' input positions.
    const usize cClusters = reps.Size();
    buf_t<math::vec3d_t> clusterPos;
    if ( !clusterPos.Init( pAllocator, cClusters ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    for ( usize c = 0u; c < cClusters; ++c ) { clusterPos.Push( inPos[reps[c]] ); }

    buf_t<u32> corners;
    buf_t<face_t> faces;
    if ( !corners.Init( pAllocator, pSoup->corners.nCount ) ||
         !faces.Init( pAllocator, pSoup->faces.nCount ) || !clusterPos.ok ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    u32 scratch[kPolygonSoupFaceCornersMax];
    for ( usize fi = 0u; fi < pSoup->faces.nCount; ++fi ) {
        const span_t<const u32> in = PolygonSoup_FaceCorners( pSoup, fi );
        u32 n = 0u;
        for ( usize k = 0u; k < in.nCount; ++k ) {
            const u32 c = remap[in.pData[k]];
            if ( n > 0u && scratch[n - 1u] == c ) { continue; } // welded consecutive duplicate
            scratch[n++] = c;
        }
        while ( n > 1u && scratch[n - 1u] == scratch[0] ) { --n; } // wrap-around duplicate
        bool degenerate = n < 3u;
        for ( u32 a = 0u; a < n && !degenerate; ++a ) {
            for ( u32 b = a + 1u; b < n; ++b ) {
                if ( scratch[a] == scratch[b] ) { degenerate = true; break; } // pinched face
            }
        }
        if ( !degenerate ) {
            const f64 area = 0.5 * std::sqrt( math::Vec3d_LengthSquared(
                                       Newell( clusterPos.Data(), scratch, n ) ) );
            degenerate = !( area > policy.fMinimumFaceArea );
        }
        if ( degenerate ) {
            if ( !policy.bDropDegenerateFaces ) {
                r.iFace = static_cast<u32>( fi );
                return Fail( r, geometry_status_t::DEGENERATE, sanitation_fault_t::DEGENERATE_FACE );
            }
            ++r.cDroppedFaces;
            continue;
        }
        faces.Push( face_t{ static_cast<u32>( corners.Size() ), n, static_cast<u32>( fi ) } );
        for ( u32 k = 0u; k < n; ++k ) { corners.Push( scratch[k] ); }
    }
    if ( !corners.ok || !faces.ok ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    if ( faces.Size() == 0u ) {
        return Fail( r, geometry_status_t::DEGENERATE, sanitation_fault_t::EMPTY );
    }

    // Compact to referenced clusters only (dropped faces may orphan some).
    buf_t<u32> clusterToVertex;
    if ( !clusterToVertex.Init( pAllocator, cClusters ) || !clusterToVertex.Resize( cClusters ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    for ( usize c = 0u; c < cClusters; ++c ) { clusterToVertex[c] = CY_INVALID_INDEX; }
    u32 cV = 0u;
    for ( usize k = 0u; k < corners.Size(); ++k ) {
        u32 &slot = clusterToVertex[corners[k]];
        if ( slot == CY_INVALID_INDEX ) { slot = cV++; }
    }
    r.cUnreferencedVertices = static_cast<u32>( cClusters ) - cV;
    // Input vertex index of each compact vertex, for fault reporting.
    buf_t<u32> vertexToInput;
    buf_t<math::vec3d_t> vpos;
    if ( !vertexToInput.Init( pAllocator, cV ) || !vertexToInput.Resize( cV ) ||
         !vpos.Init( pAllocator, cV ) || !vpos.Resize( cV ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    for ( usize c = 0u; c < cClusters; ++c ) {
        const u32 v = clusterToVertex[c];
        if ( v != CY_INVALID_INDEX ) {
            vertexToInput[v] = reps[c];
            vpos[v] = clusterPos[c];
        }
    }
    for ( usize k = 0u; k < corners.Size(); ++k ) { corners[k] = clusterToVertex[corners[k]]; }

    // ---- 4. Half-edges and twins ---------------------------------------------------
    const usize cH = corners.Size();
    buf_t<half_t> halves;
    buf_t<u32> order;
    if ( !halves.Init( pAllocator, cH ) || !order.Init( pAllocator, cH ) || !order.Resize( cH ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    for ( usize f = 0u; f < faces.Size(); ++f ) {
        const face_t &fc = faces[f];
        for ( u32 k = 0u; k < fc.cCorners; ++k ) {
            halves.Push( half_t{ corners[fc.iFirst + k],
                                 corners[fc.iFirst + ( k + 1u ) % fc.cCorners],
                                 static_cast<u32>( f ), CY_INVALID_INDEX } );
        }
    }
    if ( !halves.ok ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE ); }
    for ( usize h = 0u; h < cH; ++h ) { order[h] = static_cast<u32>( h ); }
    half_t *hp = halves.Data();
    std::sort( order.Data(), order.Data() + cH, [hp]( u32 a, u32 b ) {
        const u32 alo = std::min( hp[a].from, hp[a].to ), ahi = std::max( hp[a].from, hp[a].to );
        const u32 blo = std::min( hp[b].from, hp[b].to ), bhi = std::max( hp[b].from, hp[b].to );
        if ( alo != blo ) { return alo < blo; }
        if ( ahi != bhi ) { return ahi < bhi; }
        return a < b;
    } );
    usize cEdges = 0u;
    for ( usize i = 0u; i < cH; ) {
        const half_t &a = hp[order[i]];
        const u32 lo = std::min( a.from, a.to ), hi = std::max( a.from, a.to );
        usize j = i + 1u;
        while ( j < cH && std::min( hp[order[j]].from, hp[order[j]].to ) == lo &&
                std::max( hp[order[j]].from, hp[order[j]].to ) == hi ) {
            ++j;
        }
        const usize count = j - i;
        r.iVertexA = vertexToInput[lo];
        r.iVertexB = vertexToInput[hi];
        if ( count > 2u ) {
            return Fail( r, geometry_status_t::NON_MANIFOLD, sanitation_fault_t::NON_MANIFOLD_EDGE );
        }
        if ( count == 2u ) {
            half_t &x = hp[order[i]];
            half_t &y = hp[order[i + 1u]];
            if ( x.from == y.from ) {
                return Fail( r, geometry_status_t::INVALID_TOPOLOGY,
                             sanitation_fault_t::INCONSISTENT_ORIENTATION );
            }
            x.twin = order[i + 1u];
            y.twin = order[i];
        } else {
            ++r.cBoundaryEdges;
            if ( policy.bRequireClosed ) {
                return Fail( r, geometry_status_t::OPEN_VOLUME, sanitation_fault_t::OPEN_BOUNDARY );
            }
        }
        ++cEdges;
        i = j;
    }
    r.iVertexA = CY_INVALID_INDEX;
    r.iVertexB = CY_INVALID_INDEX;

    // next/prev within faces (half-edges of a face are contiguous).
    buf_t<u32> heNext, hePrev, faceOfHalfFirst;
    if ( !heNext.Init( pAllocator, cH ) || !heNext.Resize( cH ) ||
         !hePrev.Init( pAllocator, cH ) || !hePrev.Resize( cH ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    for ( usize f = 0u; f < faces.Size(); ++f ) {
        const face_t &fc = faces[f];
        for ( u32 k = 0u; k < fc.cCorners; ++k ) {
            const u32 h = fc.iFirst + k;
            heNext[h] = fc.iFirst + ( k + 1u ) % fc.cCorners;
            hePrev[h] = fc.iFirst + ( k + fc.cCorners - 1u ) % fc.cCorners;
        }
    }

    // ---- 5. Vertex manifoldness: one fan per vertex --------------------------------
    buf_t<u32> outCount, anyOut, boundaryOut;
    if ( !outCount.Init( pAllocator, cV ) || !outCount.Resize( cV ) ||
         !anyOut.Init( pAllocator, cV ) || !anyOut.Resize( cV ) ||
         !boundaryOut.Init( pAllocator, cV ) || !boundaryOut.Resize( cV ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    for ( u32 v = 0u; v < cV; ++v ) {
        outCount[v] = 0u;
        anyOut[v] = CY_INVALID_INDEX;
        boundaryOut[v] = CY_INVALID_INDEX;
    }
    for ( usize h = 0u; h < cH; ++h ) {
        const u32 v = hp[h].from;
        ++outCount[v];
        if ( anyOut[v] == CY_INVALID_INDEX ) { anyOut[v] = static_cast<u32>( h ); }
        // Outgoing half-edge whose *incoming* neighbour side is open: the
        // start of a boundary fan. prev(h) arrives at v; if it has no twin,
        // rotating clockwise from h ends here.
        if ( hp[hePrev[h]].twin == CY_INVALID_INDEX && boundaryOut[v] == CY_INVALID_INDEX ) {
            boundaryOut[v] = static_cast<u32>( h );
        }
    }
    for ( u32 v = 0u; v < cV; ++v ) {
        // Rotate counter-clockwise: h -> next(twin(h)) stays outgoing at v.
        const u32 start = boundaryOut[v] != CY_INVALID_INDEX ? boundaryOut[v] : anyOut[v];
        u32 visited = 0u;
        u32 h = start;
        do {
            ++visited;
            const u32 tw = hp[h].twin;
            if ( tw == CY_INVALID_INDEX ) { break; } // reached the other boundary side
            h = heNext[tw];
            if ( visited > outCount[v] ) { break; }
        } while ( h != start );
        if ( visited != outCount[v] ) {
            r.iVertexA = vertexToInput[v];
            return Fail( r, geometry_status_t::NON_MANIFOLD, sanitation_fault_t::NON_MANIFOLD_VERTEX );
        }
    }

    // ---- 6. Shells by face connectivity ------------------------------------------------
    const usize cF = faces.Size();
    buf_t<u32> parent, faceShell;
    if ( !parent.Init( pAllocator, cF ) || !parent.Resize( cF ) ||
         !faceShell.Init( pAllocator, cF ) || !faceShell.Resize( cF ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    for ( usize f = 0u; f < cF; ++f ) { parent[f] = static_cast<u32>( f ); }
    for ( usize h = 0u; h < cH; ++h ) {
        if ( hp[h].twin == CY_INVALID_INDEX ) { continue; }
        const u32 a = Root( parent.Data(), hp[h].face );
        const u32 b = Root( parent.Data(), hp[hp[h].twin].face );
        if ( a != b ) { parent[a < b ? b : a] = a < b ? a : b; }
    }
    u32 cShells = 0u;
    buf_t<u32> rootShell;
    if ( !rootShell.Init( pAllocator, cF ) || !rootShell.Resize( cF ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    for ( usize f = 0u; f < cF; ++f ) { rootShell[f] = CY_INVALID_INDEX; }
    for ( usize f = 0u; f < cF; ++f ) {
        const u32 root = Root( parent.Data(), static_cast<u32>( f ) );
        if ( rootShell[root] == CY_INVALID_INDEX ) { rootShell[root] = cShells++; }
        faceShell[f] = rootShell[root];
    }

    // ---- 7. Publish into the mesh ------------------------------------------------------
    editable_mesh_limits_t limits{};
    limits.cVertexMax = std::max<usize>( limits.cVertexMax, cV );
    limits.cHalfEdgeMax = std::max<usize>( limits.cHalfEdgeMax, cH );
    limits.cEdgeMax = std::max<usize>( limits.cEdgeMax, cEdges );
    limits.cLoopMax = std::max<usize>( limits.cLoopMax, cF );
    limits.cFaceMax = std::max<usize>( limits.cFaceMax, cF );
    limits.cShellMax = std::max<usize>( limits.cShellMax, cShells );
    geometry_status_t s = EditableMesh_Init( pMeshOut, pAllocator, limits );
    if ( s != geometry_status_t::OK ) { return Fail( r, s, sanitation_fault_t::NONE ); }

    buf_t<geometry_mesh_vertex_handle_t> vh;
    buf_t<geometry_mesh_half_edge_handle_t> hh;
    buf_t<geometry_mesh_edge_handle_t> eOfHalf;
    buf_t<geometry_mesh_loop_handle_t> lh;
    buf_t<geometry_mesh_face_handle_t> fh;
    buf_t<geometry_mesh_shell_handle_t> sh;
    if ( !vh.Init( pAllocator, cV ) || !hh.Init( pAllocator, cH ) ||
         !eOfHalf.Init( pAllocator, cH ) || !eOfHalf.Resize( cH ) ||
         !lh.Init( pAllocator, cF ) || !fh.Init( pAllocator, cF ) || !sh.Init( pAllocator, cShells ) ) {
        EditableMesh_Shutdown( pMeshOut );
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }
    auto bail = [&]( generation_pool_status_t ps ) noexcept {
        EditableMesh_Shutdown( pMeshOut );
        return Fail( r, GeometryStatus_FromGenerationPoolStatus( ps ), sanitation_fault_t::NONE );
    };

    for ( u32 v = 0u; v < cV; ++v ) {
        mesh_vertex_record_t rec{};
        rec.position = vpos[v];
        const auto ins = GenerationPool_Insert( &pMeshOut->vertices, rec );
        if ( ins.status != generation_pool_status_t::OK ) { return bail( ins.status ); }
        vh.Push( ins.handle );
    }
    for ( usize h = 0u; h < cH; ++h ) {
        const auto ins = GenerationPool_Insert( &pMeshOut->halfEdges, mesh_half_edge_record_t{} );
        if ( ins.status != generation_pool_status_t::OK ) { return bail( ins.status ); }
        hh.Push( ins.handle );
    }
    for ( u32 si = 0u; si < cShells; ++si ) {
        const auto ins = GenerationPool_Insert( &pMeshOut->shells, mesh_shell_record_t{} );
        if ( ins.status != generation_pool_status_t::OK ) { return bail( ins.status ); }
        sh.Push( ins.handle );
    }
    for ( usize f = 0u; f < cF; ++f ) {
        const auto li = GenerationPool_Insert( &pMeshOut->loops, mesh_loop_record_t{} );
        if ( li.status != generation_pool_status_t::OK ) { return bail( li.status ); }
        const auto fi = GenerationPool_Insert( &pMeshOut->faces, mesh_face_record_t{} );
        if ( fi.status != generation_pool_status_t::OK ) { return bail( fi.status ); }
        lh.Push( li.handle );
        fh.Push( fi.handle );
    }
    // One edge per undirected pair; the representative is the lower-index half.
    for ( usize h = 0u; h < cH; ++h ) {
        const u32 tw = hp[h].twin;
        if ( tw != CY_INVALID_INDEX && tw < h ) { eOfHalf[h] = eOfHalf[tw]; continue; }
        mesh_edge_record_t er{};
        er.hHalfEdge = hh[h];
        const auto ins = GenerationPool_Insert( &pMeshOut->edges, er );
        if ( ins.status != generation_pool_status_t::OK ) { return bail( ins.status ); }
        eOfHalf[h] = ins.handle;
    }
    if ( !vh.ok || !hh.ok || !lh.ok || !fh.ok || !sh.ok ) {
        EditableMesh_Shutdown( pMeshOut );
        return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
    }

    // Patch records. All pools are fully populated, so the pointers from
    // Get stay valid for the rest of this function.
    for ( usize h = 0u; h < cH; ++h ) {
        mesh_half_edge_record_t *pH = GenerationPool_Get( &pMeshOut->halfEdges, hh[h] );
        pH->hOrigin = vh[hp[h].from];
        pH->hTwin = hp[h].twin != CY_INVALID_INDEX ? hh[hp[h].twin]
                                                   : GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
        pH->hNext = hh[heNext[h]];
        pH->hPrev = hh[hePrev[h]];
        pH->hEdge = eOfHalf[h];
        pH->hLoop = lh[hp[h].face];
    }
    for ( u32 v = 0u; v < cV; ++v ) {
        mesh_vertex_record_t *pV = GenerationPool_Get( &pMeshOut->vertices, vh[v] );
        pV->hOutHalfEdge = hh[boundaryOut[v] != CY_INVALID_INDEX ? boundaryOut[v] : anyOut[v]];
    }
    for ( usize f = 0u; f < cF; ++f ) {
        const face_t &fc = faces[f];
        mesh_loop_record_t *pL = GenerationPool_Get( &pMeshOut->loops, lh[f] );
        pL->hFirstHalfEdge = hh[fc.iFirst];
        pL->hFace = fh[f];
        pL->cHalfEdges = fc.cCorners;
        mesh_face_record_t *pF = GenerationPool_Get( &pMeshOut->faces, fh[f] );
        pF->hOuterLoop = lh[f];
        pF->hShell = sh[faceShell[f]];
        math::vec3d_t n{};
        (void)math::Vec3d_TryNormalize( Newell( vpos.Data(), corners.Data() + fc.iFirst, fc.cCorners ),
                                        1.0e-300, &n, nullptr );
        pF->normal = n;
        pF->iSourceSide = CY_INVALID_INDEX;
        mesh_shell_record_t *pS = GenerationPool_Get( &pMeshOut->shells, sh[faceShell[f]] );
        if ( pS->cFaces == 0u ) { pS->hAnyFace = fh[f]; }
        ++pS->cFaces;
    }

    // Closed output must satisfy full structural validation. Anything else
    // is a bug in this function, not a property of the input.
    if ( r.cBoundaryEdges == 0u ) {
        const mesh_validation_result_t mv = MeshValidation_Validate( pMeshOut );
        if ( !mv.bReciprocalTwins || !mv.bClosedLoops || !mv.bAllVerticesReferenced ) {
            EditableMesh_Shutdown( pMeshOut );
            return Fail( r, geometry_status_t::CORRUPT_STATE, sanitation_fault_t::NONE );
        }
    }

    if ( pFaceMapOut != nullptr ) {
        if ( !Vector_Resize( pFaceMapOut, pSoup->faces.nCount ) ) {
            EditableMesh_Shutdown( pMeshOut );
            return Fail( r, geometry_status_t::ALLOCATION_FAILED, sanitation_fault_t::NONE );
        }
        for ( usize i = 0u; i < pSoup->faces.nCount; ++i ) {
            pFaceMapOut->pData[i] = GEOMETRY_HANDLE_INVALID<geometry_mesh_face_tag_t>;
        }
        for ( usize f = 0u; f < cF; ++f ) { pFaceMapOut->pData[faces[f].iInput] = fh[f]; }
    }

    r.cOutputFaces = static_cast<u32>( cF );
    r.cShells = cShells;
    r.status = geometry_status_t::OK;
    return r;
}

geometry_status_t Sanitation_TryMeshToPolygonSoup(
    const editable_mesh_t *pMesh,
    polygon_soup_t *pSoupOut ) noexcept
{
    if ( pMesh == nullptr || pSoupOut == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !EditableMesh_IsInitialized( pMesh ) || !PolygonSoup_IsInitialized( pSoupOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( PolygonSoup_VertexCount( pSoupOut ) != 0u || PolygonSoup_FaceCount( pSoupOut ) != 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const allocator_t *pAlloc = pSoupOut->positions.pAllocator;
    const usize cSlots = GenerationPool_Capacity( &pMesh->vertices );
    buf_t<u32> slotToIndex;
    if ( !slotToIndex.Init( pAlloc, cSlots ) || !slotToIndex.Resize( cSlots ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t s = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t hV, const mesh_vertex_record_t &v ) noexcept -> bool_t {
            u32 idx = 0u;
            s = PolygonSoup_TryAddVertex( pSoupOut, v.position, &idx );
            slotToIndex[hV.nSlot] = idx;
            return s == geometry_status_t::OK;
        } );
    u32 idx[kPolygonSoupFaceCornersMax];
    if ( s == geometry_status_t::OK ) {
        (void)GenerationPool_ForEach( &pMesh->faces,
            [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) noexcept -> bool_t {
                const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
                if ( pL == nullptr || pL->cHalfEdges > kPolygonSoupFaceCornersMax ) {
                    s = pL == nullptr ? geometry_status_t::CORRUPT_STATE
                                      : geometry_status_t::LIMIT_EXCEEDED;
                    return false;
                }
                geometry_mesh_half_edge_handle_t hCur = pL->hFirstHalfEdge;
                for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
                    const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, hCur );
                    if ( pH == nullptr ) { s = geometry_status_t::CORRUPT_STATE; return false; }
                    idx[k] = slotToIndex[pH->hOrigin.nSlot];
                    hCur = pH->hNext;
                }
                s = PolygonSoup_TryAddFace( pSoupOut, span_t<const u32>{ idx, pL->cHalfEdges },
                                            GEOMETRY_SOURCE_ID_INVALID, 0u, nullptr );
                return s == geometry_status_t::OK;
            } );
    }
    if ( s != geometry_status_t::OK ) { PolygonSoup_Clear( pSoupOut ); }
    return s;
}

} // namespace cypher::editor::geometry
