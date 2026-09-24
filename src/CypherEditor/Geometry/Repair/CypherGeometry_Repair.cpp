//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Repair.cpp
//  Purpose: Implements the fixed-order soup repair pipeline and the mesh
//           preview wrapper.
//  Details: Orientation propagation: two faces sharing a manifold edge are
//           consistent when they traverse it in opposite directions. With
//           dir(f) = "f runs the edge lo->hi" and flip(f) the pending
//           reversal, the effective direction is dir ^ flip, and a
//           neighbour g must satisfy dir(g) ^ flip(g) == !(dir(f) ^ flip(f)).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Repair.h"
#include "CypherGeometry_PointWeld.h"
#include "CypherGeometry_Planar_Frame.h"
#include "CypherGeometry_Planar_Triangulate.h"

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

struct rface_t {
    u32 iFirst{ 0u };
    u32 cCorners{ 0u };
    geometry_source_id_t sourceId{};
    u32 iGroup{ 0u };
    bool alive{ true };
    bool flip{ false };
    u32 component{ CY_INVALID_INDEX };
};

struct dedge_t {
    u32 lo{ 0u }, hi{ 0u };
    u32 face{ 0u };
    bool forward{ false }; // face runs lo -> hi
};

repair_report_t Fail( repair_report_t r, geometry_status_t s ) noexcept { r.status = s; return r; }

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

repair_report_t Repair_TryRepairSoup(
    const polygon_soup_t *pIn,
    const repair_plan_t &plan,
    polygon_soup_t *pOut ) noexcept
{
    repair_report_t r{};
    if ( pIn == nullptr || pOut == nullptr || !( plan.fWeldDistance >= 0.0 ) ||
         !( plan.fMinimumFaceArea >= 0.0 ) || !( plan.fPlanarityTolerance >= 0.0 ) ) {
        return r;
    }
    if ( !PolygonSoup_IsInitialized( pIn ) || !PolygonSoup_IsInitialized( pOut ) ) {
        return Fail( r, geometry_status_t::NOT_INITIALIZED );
    }
    if ( PolygonSoup_VertexCount( pOut ) != 0u || PolygonSoup_FaceCount( pOut ) != 0u ) {
        return Fail( r, geometry_status_t::INVALID_ARGUMENT );
    }
    {
        const polygon_soup_validation_t v = PolygonSoup_Validate( pIn, 0.0 );
        if ( v.fault == polygon_soup_fault_t::NON_FINITE ||
             v.fault == polygon_soup_fault_t::INDEX_OUT_OF_RANGE ) {
            return Fail( r, v.status );
        }
    }
    const allocator_t *pAlloc = pIn->positions.pAllocator;
    const usize cVin = pIn->positions.nCount;

    // ---- 1. Weld -------------------------------------------------------------
    buf_t<u32> remap, reps;
    if ( !remap.Init( pAlloc, cVin ) || !reps.Init( pAlloc, cVin ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED );
    }
    if ( plan.bWeld ) {
        const point_weld_result_t w = PointWeld_BuildRemap(
            span_t<const math::vec3d_t>{ pIn->positions.pData, cVin }, plan.fWeldDistance, pAlloc,
            &remap.v, &reps.v );
        if ( w.status != geometry_status_t::OK ) { return Fail( r, w.status ); }
        r.cVerticesWelded = w.cMerged;
    } else {
        if ( !remap.Resize( cVin ) || !reps.Resize( cVin ) ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
        for ( usize i = 0u; i < cVin; ++i ) { remap[i] = static_cast<u32>( i ); reps[i] = static_cast<u32>( i ); }
    }
    const usize cV = reps.Size();
    buf_t<math::vec3d_t> pos;
    if ( !pos.Init( pAlloc, cV ) ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
    for ( usize c = 0u; c < cV; ++c ) { pos.Push( pIn->positions.pData[reps[c]] ); }

    buf_t<u32> corners;
    buf_t<rface_t> faces;
    if ( !corners.Init( pAlloc, pIn->corners.nCount ) || !faces.Init( pAlloc, pIn->faces.nCount ) || !pos.ok ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize fi = 0u; fi < pIn->faces.nCount; ++fi ) {
        const polygon_soup_face_t &sf = pIn->faces.pData[fi];
        rface_t f{};
        f.iFirst = static_cast<u32>( corners.Size() );
        f.sourceId = sf.sourceId;
        f.iGroup = sf.iGroup;
        for ( u32 k = 0u; k < sf.cCorners; ++k ) {
            const u32 c = remap[pIn->corners.pData[sf.iFirstCorner + k]];
            if ( f.cCorners > 0u && corners[corners.Size() - 1u] == c ) { continue; }
            corners.Push( c );
            ++f.cCorners;
        }
        while ( f.cCorners > 1u && corners[f.iFirst + f.cCorners - 1u] == corners[f.iFirst] ) {
            --f.cCorners;
            --corners.v.nCount;
        }
        faces.Push( f );
    }
    if ( !corners.ok || !faces.ok ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
    const usize cF = faces.Size();

    // ---- 2. Degenerate faces -----------------------------------------------------
    if ( plan.bRemoveDegenerateFaces ) {
        for ( usize f = 0u; f < cF; ++f ) {
            rface_t &fc = faces[f];
            bool bad = fc.cCorners < 3u;
            for ( u32 a = 0u; a < fc.cCorners && !bad; ++a ) {
                for ( u32 b = a + 1u; b < fc.cCorners; ++b ) {
                    if ( corners[fc.iFirst + a] == corners[fc.iFirst + b] ) { bad = true; break; }
                }
            }
            if ( !bad ) {
                const f64 area = 0.5 * std::sqrt( math::Vec3d_LengthSquared(
                                           Newell( pos.Data(), corners.Data() + fc.iFirst, fc.cCorners ) ) );
                bad = !( area > plan.fMinimumFaceArea );
            }
            if ( bad ) { fc.alive = false; ++r.cDegenerateFacesRemoved; }
        }
    }

    // ---- 3. Duplicate faces (same vertex set, any winding or rotation) ----------
    if ( plan.bRemoveDuplicateFaces && cF > 1u ) {
        // Sorted vertex lists for each face, compared lexicographically.
        buf_t<u32> sortedCorners, order;
        if ( !sortedCorners.Init( pAlloc, corners.Size() ) || !order.Init( pAlloc, cF ) ) {
            return Fail( r, geometry_status_t::ALLOCATION_FAILED );
        }
        for ( usize k = 0u; k < corners.Size(); ++k ) { sortedCorners.Push( corners[k] ); }
        for ( usize f = 0u; f < cF; ++f ) {
            std::sort( sortedCorners.Data() + faces[f].iFirst,
                       sortedCorners.Data() + faces[f].iFirst + faces[f].cCorners );
            order.Push( static_cast<u32>( f ) );
        }
        if ( !sortedCorners.ok || !order.ok ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
        rface_t *fp = faces.Data();
        u32 *sc = sortedCorners.Data();
        auto cmp = [fp, sc]( u32 a, u32 b ) {
            if ( fp[a].cCorners != fp[b].cCorners ) { return fp[a].cCorners < fp[b].cCorners; }
            for ( u32 k = 0u; k < fp[a].cCorners; ++k ) {
                const u32 x = sc[fp[a].iFirst + k], y = sc[fp[b].iFirst + k];
                if ( x != y ) { return x < y; }
            }
            return a < b;
        };
        std::sort( order.Data(), order.Data() + cF, cmp );
        for ( usize i = 1u; i < cF; ++i ) {
            const u32 a = order[i - 1u], b = order[i];
            if ( !fp[a].alive || !fp[b].alive || fp[a].cCorners != fp[b].cCorners ) { continue; }
            bool same = true;
            for ( u32 k = 0u; k < fp[a].cCorners && same; ++k ) {
                same = sc[fp[a].iFirst + k] == sc[fp[b].iFirst + k];
            }
            if ( same ) { fp[b].alive = false; ++r.cDuplicateFacesRemoved; } // keep the earlier face (a < b)
        }
    }

    // ---- 4. Orientation ------------------------------------------------------------
    buf_t<dedge_t> dedges;
    if ( !dedges.Init( pAlloc, corners.Size() ) ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
    auto buildEdges = [&]() noexcept {
        Vector_Clear( &dedges.v );
        for ( usize f = 0u; f < cF; ++f ) {
            const rface_t &fc = faces[f];
            if ( !fc.alive || fc.cCorners < 3u ) { continue; }
            for ( u32 k = 0u; k < fc.cCorners; ++k ) {
                const u32 u = corners[fc.iFirst + k], v = corners[fc.iFirst + ( k + 1u ) % fc.cCorners];
                dedge_t e{};
                e.lo = u < v ? u : v;
                e.hi = u < v ? v : u;
                e.face = static_cast<u32>( f );
                e.forward = u < v;
                dedges.Push( e );
            }
        }
        std::sort( dedges.Data(), dedges.Data() + dedges.Size(), []( const dedge_t &a, const dedge_t &b ) {
            if ( a.lo != b.lo ) { return a.lo < b.lo; }
            if ( a.hi != b.hi ) { return a.hi < b.hi; }
            return a.face < b.face;
        } );
    };
    buildEdges();
    if ( !dedges.ok ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }

    // Adjacency across manifold edges (exactly two faces): CSR arrays.
    struct adj_t { u32 other; bool myForward; bool otherForward; };
    buf_t<adj_t> adj;
    buf_t<u32> adjStart, adjFill, boundaryOfFace;
    if ( !adj.Init( pAlloc, dedges.Size() ) || !adjStart.Init( pAlloc, cF + 1u ) || !adjStart.Resize( cF + 1u ) ||
         !adjFill.Init( pAlloc, cF ) || !adjFill.Resize( cF ) || !boundaryOfFace.Init( pAlloc, cF ) ||
         !boundaryOfFace.Resize( cF ) || !adj.Resize( dedges.Size() ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize f = 0u; f <= cF; ++f ) { adjStart[f] = 0u; }
    for ( usize f = 0u; f < cF; ++f ) { adjFill[f] = 0u; boundaryOfFace[f] = 0u; }
    auto groupEnd = [&]( usize i ) noexcept {
        usize j = i + 1u;
        while ( j < dedges.Size() && dedges[j].lo == dedges[i].lo && dedges[j].hi == dedges[i].hi ) { ++j; }
        return j;
    };
    for ( usize i = 0u; i < dedges.Size(); ) {
        const usize j = groupEnd( i );
        if ( j - i == 2u ) { ++adjStart[dedges[i].face + 1u]; ++adjStart[dedges[i + 1u].face + 1u]; }
        else if ( j - i == 1u ) { ++boundaryOfFace[dedges[i].face]; }
        else { ++r.cNonManifoldEdges; }
        i = j;
    }
    for ( usize f = 0u; f < cF; ++f ) { adjStart[f + 1u] += adjStart[f]; }
    for ( usize i = 0u; i < dedges.Size(); ) {
        const usize j = groupEnd( i );
        if ( j - i == 2u ) {
            const dedge_t &a = dedges[i], &b = dedges[i + 1u];
            adj[adjStart[a.face] + adjFill[a.face]++] = adj_t{ b.face, a.forward, b.forward };
            adj[adjStart[b.face] + adjFill[b.face]++] = adj_t{ a.face, b.forward, a.forward };
        }
        i = j;
    }

    if ( plan.bOrientConsistently ) {
        buf_t<u32> queue, compFaces;
        if ( !queue.Init( pAlloc, cF ) || !compFaces.Init( pAlloc, cF ) ) {
            return Fail( r, geometry_status_t::ALLOCATION_FAILED );
        }
        u32 comp = 0u;
        for ( usize seed = 0u; seed < cF; ++seed ) {
            if ( !faces[seed].alive || faces[seed].cCorners < 3u || faces[seed].component != CY_INVALID_INDEX ) {
                continue;
            }
            Vector_Clear( &queue.v );
            Vector_Clear( &compFaces.v );
            faces[seed].component = comp;
            faces[seed].flip = false;
            queue.Push( static_cast<u32>( seed ) );
            bool orientable = true;
            bool closed = true;
            for ( usize qi = 0u; qi < queue.Size(); ++qi ) {
                const u32 f = queue[qi];
                compFaces.Push( f );
                if ( boundaryOfFace[f] > 0u ) { closed = false; }
                for ( u32 a = adjStart[f]; a < adjStart[f + 1u]; ++a ) {
                    const adj_t &e = adj[a];
                    const bool effF = e.myForward != faces[f].flip;
                    const bool wantFlipG = e.otherForward != !effF;
                    rface_t &g = faces[e.other];
                    if ( g.component == CY_INVALID_INDEX ) {
                        g.component = comp;
                        g.flip = wantFlipG;
                        queue.Push( e.other );
                    } else if ( g.flip != wantFlipG ) {
                        orientable = false;
                    }
                }
            }
            if ( !queue.ok || !compFaces.ok ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
            if ( !orientable ) {
                ++r.cNonOrientableComponents;
                for ( usize i = 0u; i < compFaces.Size(); ++i ) { faces[compFaces[i]].flip = false; }
            } else if ( closed ) {
                // Outward: signed volume of the component with flips applied.
                f64 vol = 0.0;
                for ( usize i = 0u; i < compFaces.Size(); ++i ) {
                    const rface_t &fc = faces[compFaces[i]];
                    const math::vec3d_t o = pos[corners[fc.iFirst]];
                    for ( u32 k = 1u; k + 1u < fc.cCorners; ++k ) {
                        u32 i1 = k, i2 = k + 1u;
                        if ( fc.flip ) { i1 = fc.cCorners - k; i2 = fc.cCorners - k - 1u; }
                        const math::vec3d_t b = pos[corners[fc.iFirst + i1]];
                        const math::vec3d_t c = pos[corners[fc.iFirst + i2]];
                        vol += math::Vec3d_Dot( o, math::Vec3d_Cross( b, c ) );
                    }
                }
                if ( vol < 0.0 ) {
                    for ( usize i = 0u; i < compFaces.Size(); ++i ) {
                        faces[compFaces[i]].flip = !faces[compFaces[i]].flip;
                    }
                }
            } else {
                // Open: keep the majority orientation (fewest faces changed).
                usize flipped = 0u;
                for ( usize i = 0u; i < compFaces.Size(); ++i ) { flipped += faces[compFaces[i]].flip ? 1u : 0u; }
                if ( 2u * flipped > compFaces.Size() ) {
                    for ( usize i = 0u; i < compFaces.Size(); ++i ) {
                        faces[compFaces[i]].flip = !faces[compFaces[i]].flip;
                    }
                }
            }
            ++comp;
        }
        r.cComponents = comp;
        for ( usize f = 0u; f < cF; ++f ) {
            if ( faces[f].alive && faces[f].flip ) {
                ++r.cFacesFlipped;
                // Apply the flip to the corner list now so hole filling sees
                // effective winding.
                std::reverse( corners.Data() + faces[f].iFirst,
                              corners.Data() + faces[f].iFirst + faces[f].cCorners );
                faces[f].flip = false;
            }
        }
        buildEdges(); // directions changed
        if ( !dedges.ok ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
    }

    // ---- 5. Hole filling -----------------------------------------------------------
    buf_t<u32> capCorners;
    buf_t<u32> capFaceStart;
    if ( !capCorners.Init( pAlloc, 16u ) || !capFaceStart.Init( pAlloc, 16u ) ) {
        return Fail( r, geometry_status_t::ALLOCATION_FAILED );
    }
    if ( plan.bFillHoles ) {
        // capSucc[b] = a for each boundary face edge a -> b: the cap runs b -> a.
        buf_t<u32> capSucc;
        buf_t<u8> ambiguous, visited;
        if ( !capSucc.Init( pAlloc, cV ) || !capSucc.Resize( cV ) || !ambiguous.Init( pAlloc, cV ) ||
             !ambiguous.Resize( cV ) || !visited.Init( pAlloc, cV ) || !visited.Resize( cV ) ) {
            return Fail( r, geometry_status_t::ALLOCATION_FAILED );
        }
        for ( usize v = 0u; v < cV; ++v ) { capSucc[v] = CY_INVALID_INDEX; ambiguous[v] = 0u; visited[v] = 0u; }
        for ( usize i = 0u; i < dedges.Size(); ) {
            const usize j = groupEnd( i );
            if ( j - i == 1u ) {
                const dedge_t &e = dedges[i];
                const u32 a = e.forward ? e.lo : e.hi;
                const u32 b = e.forward ? e.hi : e.lo;
                if ( capSucc[b] != CY_INVALID_INDEX ) { ambiguous[b] = 1u; }
                capSucc[b] = a;
            }
            i = j;
        }
        buf_t<u32> loop;
        buf_t<math::vec3d_t> loopPos;
        buf_t<math::vec2d_t> ring;
        buf_t<planar_ring_triangle_t> tris;
        if ( !loop.Init( pAlloc, 16u ) || !loopPos.Init( pAlloc, 16u ) || !ring.Init( pAlloc, 16u ) ||
             !tris.Init( pAlloc, 16u ) ) {
            return Fail( r, geometry_status_t::ALLOCATION_FAILED );
        }
        for ( usize start = 0u; start < cV; ++start ) {
            if ( capSucc[start] == CY_INVALID_INDEX || visited[start] ) { continue; }
            Vector_Clear( &loop.v );
            bool bad = false;
            u32 v = static_cast<u32>( start );
            for ( usize guard = 0u; guard <= cV; ++guard ) {
                if ( ambiguous[v] ) { bad = true; }
                visited[v] = 1u;
                loop.Push( v );
                const u32 nxt = capSucc[v];
                if ( nxt == CY_INVALID_INDEX ) { bad = true; break; }
                v = nxt;
                if ( v == start ) { break; }
                if ( visited[v] ) { bad = true; break; } // joins another loop: not a simple hole
            }
            if ( !loop.ok ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
            if ( bad || loop.Size() < 3u || loop.Size() > kPlanarTriangulateCornersMax ) {
                ++r.cHolesLeftOpen;
                continue;
            }
            Vector_Clear( &loopPos.v );
            for ( usize k = 0u; k < loop.Size(); ++k ) { loopPos.Push( pos[loop[k]] ); }
            const planar_frame_result_t fr =
                PlanarFrame_TryFromLoop( loopPos.Data(), loopPos.Size(), plan.fPlanarityTolerance );
            if ( fr.status != geometry_status_t::OK ) { ++r.cHolesLeftOpen; continue; }
            Vector_Clear( &ring.v );
            for ( usize k = 0u; k < loop.Size(); ++k ) { ring.Push( PlanarFrame_Project( fr.frame, loopPos[k] ) ); }
            Vector_Clear( &tris.v );
            if ( !ring.ok || Planar_TryTriangulateRing( span_t<const math::vec2d_t>{ ring.Data(), ring.Size() },
                                                        pAlloc, &tris.v ) != geometry_status_t::OK ) {
                ++r.cHolesLeftOpen;
                continue;
            }
            for ( usize t = 0u; t < tris.Size(); ++t ) {
                capFaceStart.Push( static_cast<u32>( capCorners.Size() ) );
                capCorners.Push( loop[tris[t].a] );
                capCorners.Push( loop[tris[t].b] );
                capCorners.Push( loop[tris[t].c] );
                ++r.cCapTriangles;
            }
            ++r.cHolesFilled;
        }
        if ( !capCorners.ok || !capFaceStart.ok ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
    }

    // ---- 6. Emit ---------------------------------------------------------------------
    geometry_status_t s = geometry_status_t::OK;
    for ( usize c = 0u; c < cV && s == geometry_status_t::OK; ++c ) {
        s = PolygonSoup_TryAddVertex( pOut, pos[c], nullptr );
    }
    for ( usize f = 0u; f < cF && s == geometry_status_t::OK; ++f ) {
        const rface_t &fc = faces[f];
        if ( !fc.alive || fc.cCorners == 0u ) { continue; }
        s = PolygonSoup_TryAddFace( pOut, span_t<const u32>{ corners.Data() + fc.iFirst, fc.cCorners },
                                    fc.sourceId, fc.iGroup, nullptr );
    }
    for ( usize t = 0u; t < capFaceStart.Size() && s == geometry_status_t::OK; ++t ) {
        s = PolygonSoup_TryAddFace( pOut, span_t<const u32>{ capCorners.Data() + capFaceStart[t], 3u },
                                    GEOMETRY_SOURCE_ID_INVALID, 0u, nullptr );
    }
    if ( s != geometry_status_t::OK ) {
        PolygonSoup_Clear( pOut );
        return Fail( repair_report_t{}, s );
    }
    r.status = geometry_status_t::OK;
    return r;
}

repair_report_t Repair_TryPreviewMesh(
    const editable_mesh_t *pMesh,
    const repair_plan_t &plan,
    const sanitation_policy_t &rebuildPolicy,
    const allocator_t *pAllocator,
    editable_mesh_t *pMeshOut,
    sanitation_report_t *pSanitationOut ) noexcept
{
    repair_report_t r{};
    if ( pMesh == nullptr || pMeshOut == nullptr || !Allocator_IsValid( pAllocator ) ) { return r; }
    polygon_soup_t in{}, out{};
    struct guard_t {
        polygon_soup_t *a, *b;
        ~guard_t() { PolygonSoup_Shutdown( a ); PolygonSoup_Shutdown( b ); }
    } guard{ &in, &out };
    geometry_status_t s = PolygonSoup_Init( &in, pAllocator );
    if ( s == geometry_status_t::OK ) { s = PolygonSoup_Init( &out, pAllocator ); }
    if ( s == geometry_status_t::OK ) { s = Sanitation_TryMeshToPolygonSoup( pMesh, &in ); }
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }
    r = Repair_TryRepairSoup( &in, plan, &out );
    if ( r.status != geometry_status_t::OK ) { return r; }
    const sanitation_report_t sr = Sanitation_TryPolygonSoupToMesh( &out, rebuildPolicy, pAllocator, pMeshOut, nullptr );
    if ( pSanitationOut ) { *pSanitationOut = sr; }
    if ( sr.status != geometry_status_t::OK ) { r.status = sr.status; }
    return r;
}

} // namespace cypher::editor::geometry
