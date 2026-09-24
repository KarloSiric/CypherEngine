//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshGeometricValidation.cpp
//  Purpose: Implements geometric mesh validation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshGeometricValidation.h"
#include "CypherGeometry_Kernel_TriangleIntersection.h"
#include "CypherGeometry_Planar_Triangulate.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr u32 kMaxFaceCorners = 256u;

template <typename type_t>
struct buf_t {
    vector_t<type_t> v{};
    bool ok{ true };
    bool Init( const allocator_t *a, usize cap ) noexcept { return ok = Vector_Init( &v, a, cap ); }
    ~buf_t() { Vector_Shutdown( &v ); }
    void Push( const type_t &x ) noexcept { if ( ok && !Vector_PushBack( &v, x ) ) { ok = false; } }
    usize Size() const noexcept { return v.nCount; }
    type_t &operator[]( usize i ) noexcept { return v.pData[i]; }
    type_t *Data() noexcept { return v.pData; }
};

struct tri_t {
    math::vec3d_t p[3]{};
    u32 id[3]{};                      // vertex slots (identity)
    geometry_mesh_face_handle_t hFace{};
    f64 minX{ 0.0 }, maxX{ 0.0 }, minY{ 0.0 }, maxY{ 0.0 }, minZ{ 0.0 }, maxZ{ 0.0 };
};

void Record( mesh_geometric_validation_t *r, const mesh_geometric_issue_t &issue ) noexcept
{
    ++r->cTotalIssues;
    if ( r->cIssues < kMeshGeometricIssuesMax ) { r->issues[r->cIssues++] = issue; }
}

} // namespace

const char *MeshGeometricIssue_Name( mesh_geometric_issue_kind_t kind ) noexcept
{
    switch ( kind ) {
        case mesh_geometric_issue_kind_t::NON_FINITE_VERTEX: return "non_finite_vertex";
        case mesh_geometric_issue_kind_t::SHORT_EDGE: return "short_edge";
        case mesh_geometric_issue_kind_t::DEGENERATE_FACE: return "degenerate_face";
        case mesh_geometric_issue_kind_t::NON_PLANAR_FACE: return "non_planar_face";
        case mesh_geometric_issue_kind_t::COINCIDENT_VERTICES: return "coincident_vertices";
        case mesh_geometric_issue_kind_t::SELF_INTERSECTION: return "self_intersection";
    }
    return "unknown";
}

mesh_geometric_validation_t MeshValidation_ValidateGeometry(
    const editable_mesh_t *pMesh,
    const geometry_policy_t &policy,
    const mesh_geometric_options_t &options ) noexcept
{
    mesh_geometric_validation_t r{};
    if ( pMesh == nullptr || !EditableMesh_IsInitialized( pMesh ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    const allocator_t *pAlloc = pMesh->pAllocator;
    const geometry_numerical_policy_t &np = policy.numerical;

    // ---- Vertices ------------------------------------------------------------
    buf_t<geometry_mesh_vertex_handle_t> verts;
    if ( !verts.Init( pAlloc, EditableMesh_VertexCount( pMesh ) ) ) {
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t &v ) noexcept -> bool_t {
            verts.Push( h );
            if ( !math::Vec3d_IsFinite( v.position ) ) {
                ++r.cNonFinite;
                mesh_geometric_issue_t i{};
                i.kind = mesh_geometric_issue_kind_t::NON_FINITE_VERTEX;
                i.hVertexA = h;
                Record( &r, i );
            }
            return true;
        } );
    if ( !verts.ok ) { r.status = geometry_status_t::ALLOCATION_FAILED; return r; }
    if ( r.cNonFinite > 0u ) {
        // Every later check does arithmetic on positions; stop here.
        r.status = geometry_status_t::NUMERIC_FAILURE;
        return r;
    }

    // ---- Edges -----------------------------------------------------------------
    (void)GenerationPool_ForEach( &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t hE, const mesh_edge_record_t &e ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, e.hHalfEdge );
            const mesh_half_edge_record_t *pN = pH ? GenerationPool_Get( &pMesh->halfEdges, pH->hNext ) : nullptr;
            const mesh_vertex_record_t *pA = pH ? GenerationPool_Get( &pMesh->vertices, pH->hOrigin ) : nullptr;
            const mesh_vertex_record_t *pB = pN ? GenerationPool_Get( &pMesh->vertices, pN->hOrigin ) : nullptr;
            if ( pA == nullptr || pB == nullptr ) { return true; } // structural, not ours
            const f64 len = std::sqrt( math::Vec3d_LengthSquared(
                math::Vec3d_Subtract( pA->position, pB->position ) ) );
            if ( len < np.fMinimumEdgeLength ) {
                ++r.cShortEdges;
                mesh_geometric_issue_t i{};
                i.kind = mesh_geometric_issue_kind_t::SHORT_EDGE;
                i.hEdge = hE;
                i.fValue = len;
                Record( &r, i );
            }
            return true;
        } );

    // ---- Faces: area, planarity, triangulation -----------------------------------
    buf_t<tri_t> tris;
    buf_t<planar_ring_triangle_t> ringTris;
    if ( !tris.Init( pAlloc, EditableMesh_FaceCount( pMesh ) * 2u + 4u ) ||
         !ringTris.Init( pAlloc, kMaxFaceCorners ) ) {
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    math::vec3d_t corners[kMaxFaceCorners];
    u32 ids[kMaxFaceCorners];
    math::vec2d_t ring[kMaxFaceCorners];
    bool allocFailed = false;
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
            const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
            if ( pL == nullptr || pL->cHalfEdges < 3u || pL->cHalfEdges > kMaxFaceCorners ) {
                return true;
            }
            const u32 n = pL->cHalfEdges;
            geometry_mesh_half_edge_handle_t hCur = pL->hFirstHalfEdge;
            math::vec3d_t newell = math::Vec3d_Make( 0.0, 0.0, 0.0 );
            math::vec3d_t centroid = math::Vec3d_Make( 0.0, 0.0, 0.0 );
            for ( u32 k = 0u; k < n; ++k ) {
                const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, hCur );
                const mesh_vertex_record_t *pV = pH ? GenerationPool_Get( &pMesh->vertices, pH->hOrigin ) : nullptr;
                if ( pV == nullptr ) { return true; }
                corners[k] = pV->position;
                ids[k] = pH->hOrigin.nSlot;
                centroid = math::Vec3d_Add( centroid, corners[k] );
                hCur = pH->hNext;
            }
            for ( u32 k = 0u; k < n; ++k ) {
                const math::vec3d_t a = corners[k], b = corners[( k + 1u ) % n];
                newell.x += ( a.y - b.y ) * ( a.z + b.z );
                newell.y += ( a.z - b.z ) * ( a.x + b.x );
                newell.z += ( a.x - b.x ) * ( a.y + b.y );
            }
            const f64 twiceArea = std::sqrt( math::Vec3d_LengthSquared( newell ) );
            if ( !( 0.5 * twiceArea > np.fMinimumFaceArea ) ) {
                ++r.cDegenerateFaces;
                mesh_geometric_issue_t i{};
                i.kind = mesh_geometric_issue_kind_t::DEGENERATE_FACE;
                i.hFaceA = hF;
                i.fValue = 0.5 * twiceArea;
                Record( &r, i );
                return true;
            }
            const math::vec3d_t nrm = math::Vec3d_Scale( newell, 1.0 / twiceArea );
            centroid = math::Vec3d_Scale( centroid, 1.0 / static_cast<f64>( n ) );
            f64 maxDev = 0.0;
            for ( u32 k = 0u; k < n; ++k ) {
                const f64 d = std::fabs( math::Vec3d_Dot( nrm, math::Vec3d_Subtract( corners[k], centroid ) ) );
                maxDev = d > maxDev ? d : maxDev;
            }
            if ( maxDev > np.fPlanarityTolerance ) {
                ++r.cNonPlanarFaces;
                mesh_geometric_issue_t i{};
                i.kind = mesh_geometric_issue_kind_t::NON_PLANAR_FACE;
                i.hFaceA = hF;
                i.fValue = maxDev;
                Record( &r, i );
            }
            if ( !options.bCheckSelfIntersection ) { return true; }

            // Triangulate in the face's dominant projection.
            const f64 ax = std::fabs( nrm.x ), ay = std::fabs( nrm.y ), az = std::fabs( nrm.z );
            for ( u32 k = 0u; k < n; ++k ) {
                const math::vec3d_t p = corners[k];
                ring[k] = ( az >= ax && az >= ay ) ? math::Vec2d_Make( p.x, p.y )
                        : ( ay >= ax ) ? math::Vec2d_Make( p.z, p.x ) : math::Vec2d_Make( p.y, p.z );
            }
            Vector_Clear( &ringTris.v );
            const geometry_status_t triangulationStatus =
                Planar_TryTriangulateRing(
                    span_t<const math::vec2d_t>{ ring, n },
                    pAlloc, &ringTris.v );
            if ( triangulationStatus ==
                 geometry_status_t::ALLOCATION_FAILED ) {
                allocFailed = true;
                return false;
            }
            if ( triangulationStatus != geometry_status_t::OK ) {
                // A face that cannot be triangulated in its plane overlaps
                // itself: report it as a self-intersection of the face.
                ++r.cSelfIntersections;
                mesh_geometric_issue_t i{};
                i.kind = mesh_geometric_issue_kind_t::SELF_INTERSECTION;
                i.hFaceA = hF;
                i.hFaceB = hF;
                Record( &r, i );
                return true;
            }
            for ( usize t = 0u; t < ringTris.Size(); ++t ) {
                tri_t tr{};
                const u32 c[3] = { ringTris[t].a, ringTris[t].b, ringTris[t].c };
                for ( int k = 0; k < 3; ++k ) {
                    tr.p[k] = corners[c[k]];
                    tr.id[k] = ids[c[k]];
                }
                tr.hFace = hF;
                tr.minX = std::fmin( tr.p[0].x, std::fmin( tr.p[1].x, tr.p[2].x ) );
                tr.maxX = std::fmax( tr.p[0].x, std::fmax( tr.p[1].x, tr.p[2].x ) );
                tr.minY = std::fmin( tr.p[0].y, std::fmin( tr.p[1].y, tr.p[2].y ) );
                tr.maxY = std::fmax( tr.p[0].y, std::fmax( tr.p[1].y, tr.p[2].y ) );
                tr.minZ = std::fmin( tr.p[0].z, std::fmin( tr.p[1].z, tr.p[2].z ) );
                tr.maxZ = std::fmax( tr.p[0].z, std::fmax( tr.p[1].z, tr.p[2].z ) );
                tris.Push( tr );
            }
            if ( !tris.ok ) { allocFailed = true; return false; }
            return true;
        } );
    if ( allocFailed ) { r.status = geometry_status_t::ALLOCATION_FAILED; return r; }

    // ---- Self-intersection: sort-and-sweep on x, exact narrow phase ----------------
    if ( options.bCheckSelfIntersection && tris.Size() > 1u ) {
        tri_t *tp = tris.Data();
        std::sort( tp, tp + tris.Size(), []( const tri_t &a, const tri_t &b ) {
            return a.minX < b.minX;
        } );
        // One report per face pair: consecutive triangles of the same pair
        // would otherwise flood the bounded issue list.
        geometry_mesh_face_handle_t lastA{}, lastB{};
        for ( usize i = 0u; i < tris.Size(); ++i ) {
            for ( usize j = i + 1u; j < tris.Size() && tp[j].minX <= tp[i].maxX; ++j ) {
                if ( tp[i].hFace.nSlot == tp[j].hFace.nSlot &&
                     tp[i].hFace.nGeneration == tp[j].hFace.nGeneration ) {
                    continue;
                }
                if ( tp[j].minY > tp[i].maxY || tp[j].maxY < tp[i].minY ||
                     tp[j].minZ > tp[i].maxZ || tp[j].maxZ < tp[i].minZ ) {
                    continue;
                }
                ++r.cCandidatePairsTested;
                if ( !Kernel_TrianglesIntersectExcludingShared( tp[i].p, tp[i].id, tp[j].p, tp[j].id ) ) {
                    continue;
                }
                const bool sameAsLast =
                    ( lastA.nSlot == tp[i].hFace.nSlot && lastB.nSlot == tp[j].hFace.nSlot ) ||
                    ( lastA.nSlot == tp[j].hFace.nSlot && lastB.nSlot == tp[i].hFace.nSlot );
                if ( sameAsLast ) { continue; }
                lastA = tp[i].hFace;
                lastB = tp[j].hFace;
                ++r.cSelfIntersections;
                mesh_geometric_issue_t is{};
                is.kind = mesh_geometric_issue_kind_t::SELF_INTERSECTION;
                is.hFaceA = tp[i].hFace;
                is.hFaceB = tp[j].hFace;
                Record( &r, is );
            }
        }
    }

    // ---- Coincident distinct vertices (weld candidates, warning) --------------------
    if ( options.bCheckCoincidentVertices && verts.Size() > 1u ) {
        struct vp_t { math::vec3d_t p; geometry_mesh_vertex_handle_t h; };
        buf_t<vp_t> vps;
        if ( !vps.Init( pAlloc, verts.Size() ) ) { r.status = geometry_status_t::ALLOCATION_FAILED; return r; }
        for ( usize i = 0u; i < verts.Size(); ++i ) {
            vps.Push( vp_t{ EditableMesh_GetVertex( pMesh, verts[i] )->position, verts[i] } );
        }
        vp_t *vp = vps.Data();
        std::sort( vp, vp + vps.Size(), []( const vp_t &a, const vp_t &b ) { return a.p.x < b.p.x; } );
        const f64 eps = np.fWeldDistance;
        for ( usize i = 0u; i < vps.Size(); ++i ) {
            for ( usize j = i + 1u; j < vps.Size() && vp[j].p.x - vp[i].p.x <= eps; ++j ) {
                const f64 d = std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Subtract( vp[i].p, vp[j].p ) ) );
                if ( d <= eps ) {
                    ++r.cCoincidentPairs;
                    mesh_geometric_issue_t is{};
                    is.kind = mesh_geometric_issue_kind_t::COINCIDENT_VERTICES;
                    is.hVertexA = vp[i].h;
                    is.hVertexB = vp[j].h;
                    is.fValue = d;
                    Record( &r, is );
                }
            }
        }
    }

    if ( r.cSelfIntersections > 0u ) { r.status = geometry_status_t::SELF_INTERSECTING; }
    else if ( r.cDegenerateFaces > 0u ) { r.status = geometry_status_t::DEGENERATE; }
    else { r.status = geometry_status_t::OK; }
    return r;
}

} // namespace cypher::editor::geometry
