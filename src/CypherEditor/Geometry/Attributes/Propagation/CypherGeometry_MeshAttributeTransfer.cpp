//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshAttributeTransfer.cpp
//  Purpose: Implements attribute capture before, and resolution after, a
//           topology edit on a mesh source.
//  Details: Handles are compared by key = (slot << 32) | generation, so a
//           slot reused by the op after a deletion never matches the old
//           record. All lookups are binary searches over vectors sorted at
//           capture time; resolution is O((F + C + E) log C).
//
//           Affine UV fit: in the parent face's plane frame (origin at the
//           corner centroid, orthonormal e1/e2), solve the 3x3 normal
//           equations of uv = a x + b y + c by Cramer's rule, once per
//           parent and output channel. Centering keeps the system well
//           conditioned; a (near-)singular system (collinear corners) falls
//           back to copying the nearest corner.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshAttributeTransfer.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

template <typename tag_t>
u64 Key( generation_handle_t<tag_t> h ) noexcept
{
    return ( static_cast<u64>( h.nSlot ) << 32 ) | h.nGeneration;
}

constexpr u32 kNone = CY_INVALID_INDEX;

// Index of the captured face with handle h, or kNone.
u32 FindFace( const mesh_edit_capture_t *pCap, geometry_mesh_face_handle_t h ) noexcept
{
    const u64 key = Key( h );
    const mesh_edit_face_record_t *pBegin = pCap->faces.pData;
    const mesh_edit_face_record_t *pEnd = pBegin + pCap->faces.nCount;
    const mesh_edit_face_record_t *p = std::lower_bound(
        pBegin, pEnd, key, []( const mesh_edit_face_record_t &r, u64 k ) { return Key( r.hFace ) < k; } );
    return ( p != pEnd && Key( p->hFace ) == key ) ? static_cast<u32>( p - pBegin ) : kNone;
}

// Index of the captured corner (face, vertex), or kNone.
u32 FindCorner( const mesh_edit_capture_t *pCap, geometry_mesh_face_handle_t hFace, geometry_mesh_vertex_handle_t hVertex ) noexcept
{
    const u64 fk = Key( hFace ), vk = Key( hVertex );
    const u32 *pBegin = pCap->cornerOrder.pData;
    const u32 *pEnd = pBegin + pCap->cornerOrder.nCount;
    const mesh_edit_corner_record_t *pC = pCap->corners.pData;
    const u32 *p = std::lower_bound( pBegin, pEnd, 0u, [&]( u32 i, u32 ) {
        const u64 a = Key( pC[i].hFace ), b = Key( pC[i].hVertex );
        return a != fk ? a < fk : b < vk;
    } );
    if ( p == pEnd ) { return kNone; }
    return ( Key( pC[*p].hFace ) == fk && Key( pC[*p].hVertex ) == vk ) ? *p : kNone;
}

bool VertexExistedBefore( const mesh_edit_capture_t *pCap, geometry_mesh_vertex_handle_t hVertex ) noexcept
{
    const u64 vk = Key( hVertex );
    const u32 *pBegin = pCap->vertexOrder.pData;
    const u32 *pEnd = pBegin + pCap->vertexOrder.nCount;
    const mesh_edit_corner_record_t *pC = pCap->corners.pData;
    const u32 *p = std::lower_bound( pBegin, pEnd, 0u, [&]( u32 i, u32 ) { return Key( pC[i].hVertex ) < vk; } );
    return p != pEnd && Key( pC[*p].hVertex ) == vk;
}

void OrderPair( geometry_mesh_vertex_handle_t *pA, geometry_mesh_vertex_handle_t *pB ) noexcept
{
    if ( Key( *pB ) < Key( *pA ) ) { std::swap( *pA, *pB ); }
}

u32 FindEdge( const mesh_edit_capture_t *pCap, geometry_mesh_vertex_handle_t a, geometry_mesh_vertex_handle_t b ) noexcept
{
    OrderPair( &a, &b );
    const u64 ka = Key( a ), kb = Key( b );
    const mesh_edit_edge_record_t *pBegin = pCap->edges.pData;
    const mesh_edit_edge_record_t *pEnd = pBegin + pCap->edges.nCount;
    const mesh_edit_edge_record_t *p = std::lower_bound( pBegin, pEnd, 0u, [&]( const mesh_edit_edge_record_t &e, u32 ) {
        const u64 x = Key( e.hA ), y = Key( e.hB );
        return x != ka ? x < ka : y < kb;
    } );
    return ( p != pEnd && Key( p->hA ) == ka && Key( p->hB ) == kb ) ? static_cast<u32>( p - pBegin ) : kNone;
}

// True when q lies strictly inside segment [p0, p1] (collinear within a
// relative 1e-9 of the segment length).
bool OnSegmentInterior( math::vec3d_t p0, math::vec3d_t p1, math::vec3d_t q ) noexcept
{
    const math::vec3d_t d = math::Vec3d_Subtract( p1, p0 );
    const f64 len2 = math::Vec3d_LengthSquared( d );
    if ( !( len2 > 0.0 ) ) { return false; }
    const f64 t = math::Vec3d_Dot( math::Vec3d_Subtract( q, p0 ), d ) / len2;
    if ( !( t > 0.0 && t < 1.0 ) ) { return false; }
    const math::vec3d_t off = math::Vec3d_Subtract( q, math::Vec3d_Add( p0, math::Vec3d_Scale( d, t ) ) );
    return math::Vec3d_LengthSquared( off ) <= 1e-18 * len2;
}

struct affine_fit_t {
    bool bComputed{ false };
    bool bValid{ false };
    math::vec3d_t origin{};
    math::vec3d_t e1{};
    math::vec3d_t e2{};
    f64 coef[4][3]{}; // uv0.x, uv0.y, uv1.x, uv1.y : a, b, c
};

f64 Det3( const f64 m[3][3] ) noexcept
{
    return m[0][0] * ( m[1][1] * m[2][2] - m[1][2] * m[2][1] ) - m[0][1] * ( m[1][0] * m[2][2] - m[1][2] * m[2][0] ) +
           m[0][2] * ( m[1][0] * m[2][1] - m[1][1] * m[2][0] );
}

void ComputeFit( const mesh_edit_capture_t *pCap, u32 iFace, affine_fit_t *pFit ) noexcept
{
    pFit->bComputed = true;
    pFit->bValid = false;
    const mesh_edit_face_record_t &face = pCap->faces.pData[iFace];
    if ( face.cCorners < 3u ) { return; }
    const mesh_edit_corner_record_t *pC = pCap->corners.pData + face.iFirstCorner;
    math::vec3d_t n{};
    if ( !math::Vec3d_TryNormalize( face.normal, 1e-12, &n, nullptr ) ) { return; }
    pFit->origin = face.centroid;
    math::Vec3d_BuildOrthonormalBasis( n, &pFit->e1, &pFit->e2 );

    f64 m[3][3] = {};
    f64 rhs[4][3] = {};
    for ( u32 k = 0u; k < face.cCorners; ++k ) {
        const math::vec3d_t r = math::Vec3d_Subtract( pC[k].position, pFit->origin );
        const f64 row[3] = { math::Vec3d_Dot( r, pFit->e1 ), math::Vec3d_Dot( r, pFit->e2 ), 1.0 };
        const f64 values[4] = { pC[k].attributes.uv0.x, pC[k].attributes.uv0.y, pC[k].attributes.uv1.x,
                                pC[k].attributes.uv1.y };
        for ( int i = 0; i < 3; ++i ) {
            for ( int j = 0; j < 3; ++j ) { m[i][j] += row[i] * row[j]; }
            for ( int c = 0; c < 4; ++c ) { rhs[c][i] += row[i] * values[c]; }
        }
    }
    const f64 det = Det3( m );
    // Relative singularity test: det scales like (sum x^2)(sum y^2) n.
    const f64 scale = m[0][0] * m[1][1] * m[2][2];
    if ( !std::isfinite( det ) || !( scale > 0.0 ) || std::fabs( det ) <= 1e-12 * scale ) { return; }
    for ( int c = 0; c < 4; ++c ) {
        for ( int col = 0; col < 3; ++col ) {
            f64 mc[3][3];
            for ( int i = 0; i < 3; ++i ) {
                for ( int j = 0; j < 3; ++j ) { mc[i][j] = j == col ? rhs[c][i] : m[i][j]; }
            }
            pFit->coef[c][col] = Det3( mc ) / det;
        }
    }
    pFit->bValid = true;
}

u32 NearestCorner( const mesh_edit_capture_t *pCap, u32 iFace, math::vec3d_t p ) noexcept
{
    const mesh_edit_face_record_t &face = pCap->faces.pData[iFace];
    u32 best = kNone;
    f64 bestD = 0.0;
    for ( u32 k = 0u; k < face.cCorners; ++k ) {
        const f64 d = math::Vec3d_DistanceSquared( pCap->corners.pData[face.iFirstCorner + k].position, p );
        if ( best == kNone || d < bestD ) {
            best = face.iFirstCorner + k;
            bestD = d;
        }
    }
    return best;
}

// Generic parent: the captured face sharing the most (pre-op) vertices with
// the new face; ties by distance from the new face's centroid to the
// candidate's plane, then by lowest source ID.
geometry_status_t FindParentBySharedVertices(
    const mesh_edit_capture_t *pCap,
    const mesh_source_t *pSource,
    geometry_mesh_face_handle_t hNew,
    vector_t<u32> *pScratch,
    u32 *pParentOut ) noexcept
{
    if ( pParentOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pParentOut = kNone;
    const editable_mesh_t *pMesh = &pSource->mesh;
    const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, hNew );
    const mesh_loop_record_t *pL = pF ? GenerationPool_Get( &pMesh->loops, pF->hOuterLoop ) : nullptr;
    if ( pL == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
    Vector_Clear( pScratch );
    math::vec3d_t centroid{};
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
        const mesh_vertex_record_t *pVertex = pH != nullptr
            ? GenerationPool_Get( &pMesh->vertices, pH->hOrigin )
            : nullptr;
        if ( pH == nullptr || pVertex == nullptr ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        centroid = math::Vec3d_Add(
            centroid, pVertex->position );
        // Every captured face this vertex belonged to is a candidate.
        const u64 vk = Key( pH->hOrigin );
        const u32 *pBegin = pCap->vertexOrder.pData;
        const u32 *pEnd = pBegin + pCap->vertexOrder.nCount;
        const mesh_edit_corner_record_t *pC = pCap->corners.pData;
        const u32 *p = std::lower_bound( pBegin, pEnd, 0u, [&]( u32 i, u32 ) { return Key( pC[i].hVertex ) < vk; } );
        for ( ; p != pEnd && Key( pC[*p].hVertex ) == vk; ++p ) {
            const u32 iFace = FindFace( pCap, pC[*p].hFace );
            if ( iFace != kNone &&
                 !Vector_PushBack( pScratch, iFace ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
        h = pH->hNext;
    }
    if ( pScratch->nCount == 0u || pL->cHalfEdges == 0u ) {
        return geometry_status_t::OK;
    }
    centroid = math::Vec3d_Scale( centroid, 1.0 / static_cast<f64>( pL->cHalfEdges ) );
    std::sort( pScratch->pData, pScratch->pData + pScratch->nCount );

    u32 best = kNone, bestShared = 0u;
    f64 bestDist = 0.0;
    for ( usize i = 0u; i < pScratch->nCount; ) {
        const u32 iFace = pScratch->pData[i];
        u32 shared = 0u;
        while ( i < pScratch->nCount && pScratch->pData[i] == iFace ) {
            ++shared;
            ++i;
        }
        const mesh_edit_face_record_t &cand = pCap->faces.pData[iFace];
        const f64 dist = std::fabs( math::Vec3d_Dot( cand.normal, math::Vec3d_Subtract( centroid, cand.centroid ) ) );
        const bool bBetter = best == kNone || shared > bestShared ||
                             ( shared == bestShared &&
                               ( dist < bestDist ||
                                 ( dist == bestDist &&
                                   cand.sourceId.value < pCap->faces.pData[best].sourceId.value ) ) );
        if ( bBetter ) {
            best = iFace;
            bestShared = shared;
            bestDist = dist;
        }
    }
    *pParentOut = best;
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t MeshEditProvenance_Init( mesh_edit_provenance_t *pProvenance, const allocator_t *pAllocator ) noexcept
{
    if ( pProvenance == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &pProvenance->faces, pAllocator ) || !Vector_Init( &pProvenance->edges, pAllocator ) ||
         !Vector_Init( &pProvenance->merges, pAllocator ) ) {
        MeshEditProvenance_Shutdown( pProvenance );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void MeshEditProvenance_Shutdown( mesh_edit_provenance_t *pProvenance ) noexcept
{
    if ( pProvenance == nullptr ) { return; }
    Vector_Shutdown( &pProvenance->faces );
    Vector_Shutdown( &pProvenance->edges );
    Vector_Shutdown( &pProvenance->merges );
}

void MeshEditProvenance_Clear( mesh_edit_provenance_t *pProvenance ) noexcept
{
    if ( pProvenance == nullptr ) { return; }
    Vector_Clear( &pProvenance->faces );
    Vector_Clear( &pProvenance->edges );
    Vector_Clear( &pProvenance->merges );
}

geometry_status_t MeshEditCapture_Init( mesh_edit_capture_t *pCapture, const allocator_t *pAllocator ) noexcept
{
    if ( pCapture == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( pCapture->faces.pAllocator != nullptr ) { return geometry_status_t::ALREADY_INITIALIZED; }
    if ( !Vector_Init( &pCapture->faces, pAllocator ) || !Vector_Init( &pCapture->corners, pAllocator ) ||
         !Vector_Init( &pCapture->cornerOrder, pAllocator ) || !Vector_Init( &pCapture->vertexOrder, pAllocator ) ||
         !Vector_Init( &pCapture->edges, pAllocator ) || !Vector_Init( &pCapture->edgeEnds, pAllocator ) ) {
        MeshEditCapture_Shutdown( pCapture );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pCapture->bCaptured = false;
    return geometry_status_t::OK;
}

void MeshEditCapture_Shutdown( mesh_edit_capture_t *pCapture ) noexcept
{
    if ( pCapture == nullptr ) { return; }
    Vector_Shutdown( &pCapture->faces );
    Vector_Shutdown( &pCapture->corners );
    Vector_Shutdown( &pCapture->cornerOrder );
    Vector_Shutdown( &pCapture->vertexOrder );
    Vector_Shutdown( &pCapture->edges );
    Vector_Shutdown( &pCapture->edgeEnds );
    pCapture->bCaptured = false;
}

geometry_status_t MeshEditCapture_TryCapture( mesh_edit_capture_t *pCapture, const mesh_source_t *pSource ) noexcept
{
    if ( pCapture == nullptr || pCapture->faces.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    Vector_Clear( &pCapture->faces );
    Vector_Clear( &pCapture->corners );
    Vector_Clear( &pCapture->cornerOrder );
    Vector_Clear( &pCapture->vertexOrder );
    Vector_Clear( &pCapture->edges );
    Vector_Clear( &pCapture->edgeEnds );
    pCapture->bCaptured = false;

    const editable_mesh_t *pMesh = &pSource->mesh;
    const usize cF = GenerationPool_Count( &pMesh->faces );
    const usize cH = GenerationPool_Count( &pMesh->halfEdges );
    const usize cE = GenerationPool_Count( &pMesh->edges );
    if ( !Vector_Reserve( &pCapture->faces, cF ) || !Vector_Reserve( &pCapture->corners, cH ) ||
         !Vector_Reserve( &pCapture->edges, cE ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    geometry_status_t st = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
            mesh_edit_face_record_t rec{};
            rec.hFace = hF;
            rec.sourceId = MeshSource_FaceId( pSource, hF );
            rec.attributes = MeshAttributeStore_GetFace( &pSource->attributes, hF );
            rec.normal = f.normal;
            rec.iFirstCorner = static_cast<u32>( pCapture->corners.nCount );
            const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
            if ( !GeometrySourceId_IsValid( rec.sourceId ) || pL == nullptr ) {
                st = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
            for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
                const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
                const mesh_vertex_record_t *pV = pH ? GenerationPool_Get( &pMesh->vertices, pH->hOrigin ) : nullptr;
                if ( pV == nullptr ) {
                    st = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                mesh_edit_corner_record_t c{};
                c.hFace = hF;
                c.hVertex = pH->hOrigin;
                c.position = pV->position;
                c.attributes = MeshAttributeStore_GetCorner( &pSource->attributes, h );
                c.bHasAttributes = MeshAttributeStore_HasCorner( &pSource->attributes, h );
                if ( !Vector_PushBack( &pCapture->corners, c ) ) {
                    st = geometry_status_t::ALLOCATION_FAILED;
                    return false;
                }
                rec.centroid = math::Vec3d_Add( rec.centroid, pV->position );
                h = pH->hNext;
            }
            rec.cCorners = pL->cHalfEdges;
            if ( rec.cCorners > 0u ) { rec.centroid = math::Vec3d_Scale( rec.centroid, 1.0 / rec.cCorners ); }
            (void)Vector_PushBack( &pCapture->faces, rec );
            return true;
        } );
    if ( st != geometry_status_t::OK ) { return st; }

    (void)GenerationPool_ForEach( &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t hE, const mesh_edge_record_t &e ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, e.hHalfEdge );
            const mesh_half_edge_record_t *pN = pH ? GenerationPool_Get( &pMesh->halfEdges, pH->hNext ) : nullptr;
            if ( pN == nullptr ) {
                st = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            mesh_edit_edge_record_t rec{};
            rec.hA = pH->hOrigin;
            rec.hB = pN->hOrigin;
            OrderPair( &rec.hA, &rec.hB );
            rec.idA = MeshSource_VertexId( pSource, rec.hA );
            rec.idB = MeshSource_VertexId( pSource, rec.hB );
            rec.positionA = GenerationPool_Get( &pMesh->vertices, rec.hA )->position;
            rec.positionB = GenerationPool_Get( &pMesh->vertices, rec.hB )->position;
            rec.attributes = MeshAttributeStore_GetEdge( &pSource->attributes, hE );
            rec.creaseWeight = e.creaseWeight;
            (void)Vector_PushBack( &pCapture->edges, rec );
            return true;
        } );
    if ( st != geometry_status_t::OK ) { return st; }
    std::sort( pCapture->edges.pData, pCapture->edges.pData + pCapture->edges.nCount,
               []( const mesh_edit_edge_record_t &a, const mesh_edit_edge_record_t &b ) {
                   return Key( a.hA ) != Key( b.hA ) ? Key( a.hA ) < Key( b.hA ) : Key( a.hB ) < Key( b.hB );
               } );

    const usize cEdges = pCapture->edges.nCount;
    if ( !Vector_Resize( &pCapture->edgeEnds, cEdges * 2u ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i < cEdges * 2u; ++i ) { pCapture->edgeEnds.pData[i] = static_cast<u32>( i ); }
    {
        const mesh_edit_edge_record_t *pE = pCapture->edges.pData;
        auto endKey = [pE]( u32 code ) noexcept {
            const mesh_edit_edge_record_t &e = pE[code >> 1];
            return ( code & 1u ) ? Key( e.hB ) : Key( e.hA );
        };
        std::sort( pCapture->edgeEnds.pData, pCapture->edgeEnds.pData + cEdges * 2u, [&]( u32 a, u32 b ) {
            const u64 ka = endKey( a ), kb = endKey( b );
            return ka != kb ? ka < kb : a < b;
        } );
    }

    const usize cC = pCapture->corners.nCount;
    if ( !Vector_Resize( &pCapture->cornerOrder, cC ) || !Vector_Resize( &pCapture->vertexOrder, cC ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cC; ++i ) {
        pCapture->cornerOrder.pData[i] = static_cast<u32>( i );
        pCapture->vertexOrder.pData[i] = static_cast<u32>( i );
    }
    const mesh_edit_corner_record_t *pC = pCapture->corners.pData;
    std::sort( pCapture->cornerOrder.pData, pCapture->cornerOrder.pData + cC, [&]( u32 a, u32 b ) {
        const u64 fa = Key( pC[a].hFace ), fb = Key( pC[b].hFace );
        return fa != fb ? fa < fb : Key( pC[a].hVertex ) < Key( pC[b].hVertex );
    } );
    std::sort( pCapture->vertexOrder.pData, pCapture->vertexOrder.pData + cC, [&]( u32 a, u32 b ) {
        const u64 va = Key( pC[a].hVertex ), vb = Key( pC[b].hVertex );
        return va != vb ? va < vb : Key( pC[a].hFace ) < Key( pC[b].hFace );
    } );
    pCapture->bCaptured = true;
    return geometry_status_t::OK;
}

geometry_status_t MeshEditCapture_TryResolve(
    const mesh_edit_capture_t *pCapture,
    mesh_source_t *pSource,
    span_t<const mesh_edit_face_parent_t> parents,
    mesh_edit_resolve_stats_t *pStatsOut,
    mesh_edit_provenance_t *pProvenanceOut ) noexcept
{
    mesh_edit_resolve_stats_t stats{};
    if ( pStatsOut ) { *pStatsOut = stats; }
    if ( pCapture == nullptr || !pCapture->bCaptured ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !MeshSource_IsInitialized( pSource ) || ( parents.nCount > 0u && parents.pData == nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    editable_mesh_t *pMesh = &pSource->mesh;

    // Validate the parent map before writing anything.
    for ( usize i = 0u; i < parents.nCount; ++i ) {
        const mesh_edit_face_parent_t &p = parents.pData[i];
        if ( FindFace( pCapture, p.hParent ) == kNone || !GenerationPool_Contains( &pMesh->faces, p.hNewFace ) ||
             FindFace( pCapture, p.hNewFace ) != kNone ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        // Taking over an identity is only sound when its owner is gone.
        if ( p.bInheritIdentity && GenerationPool_Contains( &pMesh->faces, p.hParent ) ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
    }

    const allocator_t *pAllocator = pCapture->faces.pAllocator;
    vector_t<u32> faceParent{};   // by current face slot: captured face index acting as source
    vector_t<affine_fit_t> fits{}; // by captured face index
    vector_t<u32> scratch{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &faceParent );
        Vector_Shutdown( &fits );
        Vector_Shutdown( &scratch );
    };
    if ( !Vector_Init( &faceParent, pAllocator ) || !Vector_Resize( &faceParent, pMesh->faces.cSlots ) ||
         !Vector_Init( &fits, pAllocator ) || !Vector_Resize( &fits, pCapture->faces.nCount ) ||
         !Vector_Init( &scratch, pAllocator ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < faceParent.nCount; ++i ) { faceParent.pData[i] = kNone; }
    for ( usize i = 0u; i < fits.nCount; ++i ) { fits.pData[i] = affine_fit_t{}; }

    // 1. Faces.
    geometry_status_t st = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t & ) noexcept -> bool_t {
            const u32 iSurvivor = FindFace( pCapture, hF );
            if ( iSurvivor != kNone ) {
                faceParent.pData[hF.nSlot] = iSurvivor;
                return true;
            }
            u32 iParent = kNone;
            bool bInherit = false;
            for ( usize i = 0u; i < parents.nCount; ++i ) {
                if ( Key( parents.pData[i].hNewFace ) == Key( hF ) ) {
                    iParent = FindFace( pCapture, parents.pData[i].hParent );
                    bInherit = parents.pData[i].bInheritIdentity;
                    break;
                }
            }
            if ( iParent == kNone ) {
                st = FindParentBySharedVertices(
                    pCapture, pSource, hF, &scratch, &iParent );
                if ( st != geometry_status_t::OK ) { return false; }
            }
            if ( iParent == kNone ) {
                ++stats.cFacesWithoutParent;
                return true;
            }
            faceParent.pData[hF.nSlot] = iParent;
            const mesh_edit_face_record_t &parent = pCapture->faces.pData[iParent];
            // A face that took over its parent's identity is the same face,
            // not a descendant; only true children enter the lineage.
            if ( pProvenanceOut != nullptr && !bInherit &&
                 !Vector_PushBack( &pProvenanceOut->faces, mesh_edit_face_origin_t{ hF, parent.sourceId } ) ) {
                st = geometry_status_t::ALLOCATION_FAILED;
                return false;
            }
            st = MeshAttributeStore_TrySetFace( &pSource->attributes, hF, parent.attributes, pMesh->faces.cSlots );
            if ( st != geometry_status_t::OK ) { return false; }
            ++stats.cFacesFromParent;
            if ( bInherit ) {
                st = MeshSource_TrySetFaceId( pSource, hF, parent.sourceId );
                if ( st != geometry_status_t::OK ) { return false; }
                ++stats.cFacesInheritingIdentity;
            }
            return true;
        } );
    if ( st != geometry_status_t::OK ) {
        cleanup();
        return st;
    }

    // 2. Corners.
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
            const bool bSurvivor = FindFace( pCapture, hF ) != kNone;
            const u32 iSource = faceParent.pData[hF.nSlot];
            const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
            if ( pL == nullptr ) {
                st = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
            for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
                const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
                if ( pH == nullptr ) {
                    st = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                const geometry_mesh_half_edge_handle_t hCorner = h;
                h = pH->hNext;
                u32 iExact = bSurvivor ? FindCorner( pCapture, hF, pH->hOrigin ) : kNone;
                if ( iExact != kNone ) {
                    if ( pCapture->corners.pData[iExact].bHasAttributes ) {
                        st = MeshAttributeStore_TrySetCorner( &pSource->attributes, hCorner,
                                                              pCapture->corners.pData[iExact].attributes,
                                                              pMesh->halfEdges.cSlots );
                        if ( st != geometry_status_t::OK ) { return false; }
                    }
                    ++stats.cCornersRestored;
                    continue;
                }
                if ( iSource == kNone ) {
                    ++stats.cCornersDefaulted;
                    continue;
                }
                const mesh_edit_face_record_t &src = pCapture->faces.pData[iSource];
                iExact = FindCorner( pCapture, src.hFace, pH->hOrigin );
                mesh_corner_attributes_t value{};
                if ( iExact != kNone ) {
                    value = pCapture->corners.pData[iExact].attributes;
                    ++stats.cCornersCopied;
                } else {
                    const math::vec3d_t p = GenerationPool_Get( &pMesh->vertices, pH->hOrigin )->position;
                    affine_fit_t &fit = fits.pData[iSource];
                    const u32 iNear = NearestCorner( pCapture, iSource, p );
                    value = pCapture->corners.pData[iNear].attributes;
                    // A new vertex sitting exactly on an old corner (detach,
                    // vertex duplication) takes that corner verbatim: exact
                    // even across UV seams, where an affine fit is not.
                    const bool bCoincident =
                        math::Vec3d_EqualsExact( pCapture->corners.pData[iNear].position, p );
                    if ( !bCoincident && !fit.bComputed ) { ComputeFit( pCapture, iSource, &fit ); }
                    if ( !bCoincident && fit.bValid ) {
                        const math::vec3d_t r = math::Vec3d_Subtract( p, fit.origin );
                        const f64 x = math::Vec3d_Dot( r, fit.e1 ), y = math::Vec3d_Dot( r, fit.e2 );
                        f64 out[4];
                        for ( int c = 0; c < 4; ++c ) { out[c] = fit.coef[c][0] * x + fit.coef[c][1] * y + fit.coef[c][2]; }
                        value.uv0 = math::vec2d_t{ out[0], out[1] };
                        value.uv1 = math::vec2d_t{ out[2], out[3] };
                        ++stats.cCornersInterpolated;
                    } else {
                        ++stats.cCornersCopied;
                    }
                }
                st = MeshAttributeStore_TrySetCorner( &pSource->attributes, hCorner, value, pMesh->halfEdges.cSlots );
                if ( st != geometry_status_t::OK ) { return false; }
            }
            return true;
        } );
    if ( st != geometry_status_t::OK ) {
        cleanup();
        return st;
    }

    // 3. Edges.
    // Old edges by exact endpoint positions (canonical order), built on
    // first need: an edge whose endpoints are both new but sit exactly on
    // an old edge's endpoints is that edge duplicated (detach, vertex
    // duplication, a rebuild at the same place) and keeps its attributes -
    // the edge counterpart of the coincident-corner rule above.
    struct edge_at_t {
        math::vec3d_t lo, hi;
        u32 index;
    };
    auto posLess = []( math::vec3d_t a, math::vec3d_t b ) noexcept {
        return a.x != b.x ? a.x < b.x : ( a.y != b.y ? a.y < b.y : a.z < b.z );
    };
    auto atLess = [&]( const edge_at_t &a, const edge_at_t &b ) noexcept {
        return posLess( a.lo, b.lo ) || ( !posLess( b.lo, a.lo ) && posLess( a.hi, b.hi ) );
    };
    vector_t<edge_at_t> edgesAt{};
    bool bEdgesAtBuilt = false;
    auto findCoincident = [&]( math::vec3d_t pa, math::vec3d_t pb, u32 *pIndexOut ) noexcept -> bool {
        if ( !bEdgesAtBuilt ) {
            bEdgesAtBuilt = true;
            if ( !Vector_Init( &edgesAt, pSource->attributes.faces.pAllocator ) ||
                 !Vector_Reserve( &edgesAt, pCapture->edges.nCount ) ) {
                st = geometry_status_t::ALLOCATION_FAILED;
                return false;
            }
            for ( usize i = 0u; i < pCapture->edges.nCount; ++i ) {
                const mesh_edit_edge_record_t &ed = pCapture->edges.pData[i];
                const bool bSwap = posLess( ed.positionB, ed.positionA );
                (void)Vector_PushBack( &edgesAt, edge_at_t{ bSwap ? ed.positionB : ed.positionA,
                                                            bSwap ? ed.positionA : ed.positionB, static_cast<u32>( i ) } );
            }
            std::sort( edgesAt.pData, edgesAt.pData + edgesAt.nCount, atLess );
        }
        const bool bSwap = posLess( pb, pa );
        const edge_at_t probe{ bSwap ? pb : pa, bSwap ? pa : pb, 0u };
        const edge_at_t *pBegin = edgesAt.pData, *pEnd = edgesAt.pData + edgesAt.nCount;
        const edge_at_t *pIt = std::lower_bound( pBegin, pEnd, probe, atLess );
        if ( pIt == pEnd || atLess( probe, *pIt ) ) { return false; }
        *pIndexOut = pIt->index;
        return true;
    };
    (void)GenerationPool_ForEach( &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t hE, const mesh_edge_record_t &e ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, e.hHalfEdge );
            const mesh_half_edge_record_t *pN = pH ? GenerationPool_Get( &pMesh->halfEdges, pH->hNext ) : nullptr;
            if ( pN == nullptr ) {
                st = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            geometry_mesh_vertex_handle_t a = pH->hOrigin, b = pN->hOrigin;
            u32 iSrc = FindEdge( pCapture, a, b );
            bool bRestored = iSrc != kNone;
            if ( iSrc == kNone ) {
                // One half of a split edge: shares an endpoint with an old
                // edge and runs along it.
                const bool bAOld = VertexExistedBefore( pCapture, a ), bBOld = VertexExistedBefore( pCapture, b );
                for ( int side = 0; side < 2 && iSrc == kNone; ++side ) {
                    const geometry_mesh_vertex_handle_t x = side == 0 ? a : b, y = side == 0 ? b : a;
                    if ( !( side == 0 ? bAOld : bBOld ) ) { continue; }
                    const math::vec3d_t py = GenerationPool_Get( &pMesh->vertices, y )->position;
                    // Only old edges that touch x can have been split at x.
                    const u64 xk = Key( x );
                    const mesh_edit_edge_record_t *pE = pCapture->edges.pData;
                    auto endKey = [pE]( u32 code ) noexcept {
                        const mesh_edit_edge_record_t &ed = pE[code >> 1];
                        return ( code & 1u ) ? Key( ed.hB ) : Key( ed.hA );
                    };
                    const u32 *pBegin = pCapture->edgeEnds.pData;
                    const u32 *pEnd = pBegin + pCapture->edgeEnds.nCount;
                    const u32 *pIt = std::lower_bound( pBegin, pEnd, 0u,
                                                       [&]( u32 code, u32 ) { return endKey( code ) < xk; } );
                    for ( ; pIt != pEnd && endKey( *pIt ) == xk; ++pIt ) {
                        const mesh_edit_edge_record_t &old = pE[*pIt >> 1];
                        if ( OnSegmentInterior( old.positionA, old.positionB, py ) ) {
                            iSrc = *pIt >> 1;
                            break;
                        }
                    }
                }
            }
            if ( iSrc == kNone && !VertexExistedBefore( pCapture, a ) && !VertexExistedBefore( pCapture, b ) ) {
                u32 iAt = kNone;
                if ( findCoincident( GenerationPool_Get( &pMesh->vertices, a )->position,
                                     GenerationPool_Get( &pMesh->vertices, b )->position, &iAt ) ) {
                    iSrc = iAt;
                } else if ( st != geometry_status_t::OK ) {
                    return false;
                }
            }
            if ( iSrc == kNone ) { return true; }
            const mesh_edit_edge_record_t &old = pCapture->edges.pData[iSrc];
            if ( !bRestored && pProvenanceOut != nullptr &&
                 !Vector_PushBack( &pProvenanceOut->edges, mesh_edit_edge_origin_t{ a, b, old.idA, old.idB } ) ) {
                st = geometry_status_t::ALLOCATION_FAILED;
                return false;
            }
            if ( old.attributes.flags == 0u && old.creaseWeight == 0.0 && !bRestored ) { return true; }
            mesh_edge_record_t *pEdge = GenerationPool_Get( &pMesh->edges, hE );
            pEdge->creaseWeight = old.creaseWeight;
            st = MeshAttributeStore_TrySetEdge( &pSource->attributes, hE, old.attributes, pMesh->edges.cSlots );
            if ( st != geometry_status_t::OK ) { return false; }
            if ( bRestored ) {
                ++stats.cEdgesRestored;
            } else {
                ++stats.cEdgesInherited;
            }
            return true;
        } );
    Vector_Shutdown( &edgesAt );
    cleanup();
    if ( st != geometry_status_t::OK ) { return st; }
    if ( pStatsOut ) { *pStatsOut = stats; }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
