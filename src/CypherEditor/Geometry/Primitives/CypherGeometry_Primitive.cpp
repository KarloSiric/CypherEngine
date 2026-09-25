//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Primitive.cpp
//  Purpose: Implements the primitive creation front end.
//  Details: Mesh shapes are written straight into a mesh description in a
//           local frame, then a signed-volume check flips every face if the
//           frame turned out mirrored in world space (a stair climbing -X
//           is the +X stair reflected). MeshSource_TryBuild then proves the
//           winding is consistent: two faces walking a shared edge the same
//           way cannot form a half-edge mesh.
//
//           Brushes reuse the box-fitted BrushShapes generators where one
//           exists; ramp, pyramid and sphere are the convex hull of the same
//           points the mesh uses, so both outputs agree.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Primitive.h"
#include "CypherGeometry_BrushSource.h"
#include "CypherGeometry_Kernel_ConvexHull.h"
#include "CypherGeometry_MeshSurfacing.h"
#include "CypherGeometry_PatchPrimitives.h"

#include <cmath>
#include <new>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

using kind_t = geometry_primitive_kind_t;
using output_t = geometry_primitive_output_t;

constexpr u32 kCirclePointsMax = kBrushCircleSidesMax + 4u;

f64 C( math::vec3d_t v, u32 i ) noexcept { return math::Vec3d_Component( v, i ); }

math::vec3d_t Compose( u32 a0, f64 v0, u32 a1, f64 v1, u32 a2, f64 v2 ) noexcept
{
    math::vec3d_t p{};
    math::Vec3d_SetComponent( &p, a0, v0 );
    math::Vec3d_SetComponent( &p, a1, v1 );
    math::Vec3d_SetComponent( &p, a2, v2 );
    return p;
}

// Axes perpendicular to `a`, ordered so (u, v, a) is right-handed: a
// polygon counter-clockwise in (u, v) faces +a.
u32 AxisU( u32 a ) noexcept { return ( a + 1u ) % 3u; }
u32 AxisV( u32 a ) noexcept { return ( a + 2u ) % 3u; }

bool IsRound( kind_t k ) noexcept
{
    return k == kind_t::CYLINDER || k == kind_t::CONE || k == kind_t::SPHERE || k == kind_t::TORUS;
}

// The circle polygon of a round kind in (u, v) of its axis. The torus ring
// runs through the tube centres, so its rectangle is the box shrunk by the
// tube radius.
geometry_status_t Circle( const geometry_primitive_t &p, math::vec2d_t *pPts, u32 *pCount ) noexcept
{
    const u32 a = p.axis, u = AxisU( a ), v = AxisV( a );
    const f64 inset = p.kind == kind_t::TORUS ? 0.5 * ( C( p.box.hi, a ) - C( p.box.lo, a ) ) : 0.0;
    const math::vec2d_t lo{ C( p.box.lo, u ) + inset, C( p.box.lo, v ) + inset };
    const math::vec2d_t hi{ C( p.box.hi, u ) - inset, C( p.box.hi, v ) - inset };
    return BrushShapes_TryMakeCircle( lo, hi, p.cSides, p.circleMode, pPts, kCirclePointsMax, pCount );
}

u32 StepCount( const geometry_primitive_t &p ) noexcept
{
    const f64 h = p.box.hi.z - p.box.lo.z;
    const f64 n = std::ceil( h / p.stepHeight );
    return n >= 1.0 && n <= static_cast<f64>( kPrimitiveStepsMax ) ? static_cast<u32>( n ) : kPrimitiveStepsMax + 1u;
}

// Step tread heights above the floor: step i reaches min((i+1) * stepHeight,
// H), the last one exactly H (BrushShapes_TryMakeStairs' rule).
f64 StepTop( const geometry_primitive_t &p, u32 i, u32 cSteps ) noexcept
{
    const f64 h = p.box.hi.z - p.box.lo.z;
    return i + 1u >= cSteps ? h : std::fmin( ( i + 1u ) * p.stepHeight, h );
}

// ---------------------------------------------------------------------------
// Mesh construction
// ---------------------------------------------------------------------------

struct mesh_writer_t {
    mesh_source_description_t *pDesc;
    geometry_source_id_allocator_t *pIds;
    geometry_material_ref_t material;
    geometry_status_t st{ geometry_status_t::OK };

    u32 Vertex( math::vec3d_t p ) noexcept
    {
        u32 index = 0u;
        if ( st != geometry_status_t::OK ) { return 0u; }
        const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( pIds );
        st = id.status;
        if ( st == geometry_status_t::OK ) { st = MeshSourceDescription_TryAddVertex( pDesc, p, id.id, &index ); }
        return index;
    }

    void Face( const u32 *pIndices, u32 cIndices, u32 smoothingGroups ) noexcept
    {
        if ( st != geometry_status_t::OK ) { return; }
        const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( pIds );
        st = id.status;
        if ( st != geometry_status_t::OK ) { return; }
        mesh_face_attributes_t attributes{};
        attributes.material = material;
        attributes.smoothingGroups = smoothingGroups;
        st = MeshSourceDescription_TryAddFace( pDesc, span_t<const u32>{ pIndices, cIndices }, id.id, attributes, nullptr );
    }

    void Quad( u32 a, u32 b, u32 c, u32 d, u32 smoothing ) noexcept
    {
        const u32 q[4] = { a, b, c, d };
        Face( q, 4u, smoothing );
    }

    void Tri( u32 a, u32 b, u32 c, u32 smoothing ) noexcept
    {
        const u32 t[3] = { a, b, c };
        Face( t, 3u, smoothing );
    }
};

// Six times the signed volume enclosed by the description's faces
// (positive when they wind counter-clockwise seen from outside).
f64 SignedVolume6( const mesh_source_description_t &d ) noexcept
{
    f64 v = 0.0;
    for ( usize f = 0u; f < d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = d.faces.pData[f];
        const math::vec3d_t p0 = d.vertices.pData[d.corners.pData[face.iFirstCorner].iVertex].position;
        for ( u32 k = 1u; k + 1u < face.cCorners; ++k ) {
            const math::vec3d_t p1 = d.vertices.pData[d.corners.pData[face.iFirstCorner + k].iVertex].position;
            const math::vec3d_t p2 = d.vertices.pData[d.corners.pData[face.iFirstCorner + k + 1u].iVertex].position;
            v += math::Vec3d_Dot( p0, math::Vec3d_Cross( p1, p2 ) );
        }
    }
    return v;
}

void ReverseAllFaces( mesh_source_description_t *pDesc ) noexcept
{
    for ( usize f = 0u; f < pDesc->faces.nCount; ++f ) {
        const mesh_source_face_t &face = pDesc->faces.pData[f];
        mesh_source_corner_t *pFirst = pDesc->corners.pData + face.iFirstCorner;
        for ( u32 i = 0u, j = face.cCorners - 1u; i < j; ++i, --j ) {
            const mesh_source_corner_t t = pFirst[i];
            pFirst[i] = pFirst[j];
            pFirst[j] = t;
        }
    }
}

// A subdivided box: a lattice of (sx+1)(sy+1)(sz+1) points of which only
// the surface ones become vertices, so neighbouring face grids share them.
void WriteBox( const geometry_primitive_t &p, const allocator_t *pScratch, mesh_writer_t *pW ) noexcept
{
    const u32 s[3] = { p.cSegments[0], p.cSegments[1], p.cSegments[2] };
    vector_t<u32> lattice{};
    const usize cLattice = static_cast<usize>( s[0] + 1u ) * ( s[1] + 1u ) * ( s[2] + 1u );
    if ( !Vector_Init( &lattice, pScratch ) || !Vector_Resize( &lattice, cLattice ) ) {
        pW->st = geometry_status_t::ALLOCATION_FAILED;
        return;
    }
    for ( usize i = 0u; i < cLattice; ++i ) { lattice.pData[i] = CY_U32_MAX; }
    auto at = [&]( u32 i, u32 j, u32 k ) noexcept -> u32 {
        u32 &slot = lattice.pData[( static_cast<usize>( k ) * ( s[1] + 1u ) + j ) * ( s[0] + 1u ) + i];
        if ( slot == CY_U32_MAX ) {
            const math::vec3d_t q = math::Vec3d_Make( p.box.lo.x + ( p.box.hi.x - p.box.lo.x ) * i / s[0],
                                                      p.box.lo.y + ( p.box.hi.y - p.box.lo.y ) * j / s[1],
                                                      p.box.lo.z + ( p.box.hi.z - p.box.lo.z ) * k / s[2] );
            slot = pW->Vertex( q );
        }
        return slot;
    };
    // Windings follow the unit cube: each face is counter-clockwise from outside.
    for ( u32 i = 0u; i < s[0]; ++i ) {
        for ( u32 j = 0u; j < s[1]; ++j ) {
            pW->Quad( at( i, j, 0 ), at( i, j + 1, 0 ), at( i + 1, j + 1, 0 ), at( i + 1, j, 0 ), 0u );
            pW->Quad( at( i, j, s[2] ), at( i + 1, j, s[2] ), at( i + 1, j + 1, s[2] ), at( i, j + 1, s[2] ), 0u );
        }
    }
    for ( u32 i = 0u; i < s[0]; ++i ) {
        for ( u32 k = 0u; k < s[2]; ++k ) {
            pW->Quad( at( i, 0, k ), at( i + 1, 0, k ), at( i + 1, 0, k + 1 ), at( i, 0, k + 1 ), 0u );
            pW->Quad( at( i, s[1], k ), at( i, s[1], k + 1 ), at( i + 1, s[1], k + 1 ), at( i + 1, s[1], k ), 0u );
        }
    }
    for ( u32 j = 0u; j < s[1]; ++j ) {
        for ( u32 k = 0u; k < s[2]; ++k ) {
            pW->Quad( at( 0, j, k ), at( 0, j, k + 1 ), at( 0, j + 1, k + 1 ), at( 0, j + 1, k ), 0u );
            pW->Quad( at( s[0], j, k ), at( s[0], j + 1, k ), at( s[0], j + 1, k + 1 ), at( s[0], j, k + 1 ), 0u );
        }
    }
}

void WritePlane( const geometry_primitive_t &p, mesh_writer_t *pW, vector_t<u32> *pScratch ) noexcept
{
    const u32 a = p.axis, u = AxisU( a ), v = AxisV( a );
    const u32 su = p.cSegments[u], sv = p.cSegments[v];
    if ( !Vector_Resize( pScratch, static_cast<usize>( su + 1u ) * ( sv + 1u ) ) ) {
        pW->st = geometry_status_t::ALLOCATION_FAILED;
        return;
    }
    for ( u32 j = 0u; j <= sv; ++j ) {
        for ( u32 i = 0u; i <= su; ++i ) {
            pScratch->pData[j * ( su + 1u ) + i] =
                pW->Vertex( Compose( a, C( p.box.lo, a ), u, C( p.box.lo, u ) + ( C( p.box.hi, u ) - C( p.box.lo, u ) ) * i / su, v,
                                     C( p.box.lo, v ) + ( C( p.box.hi, v ) - C( p.box.lo, v ) ) * j / sv ) );
        }
    }
    const u32 *g = pScratch->pData;
    for ( u32 j = 0u; j < sv; ++j ) {
        for ( u32 i = 0u; i < su; ++i ) {
            const u32 r0 = j * ( su + 1u ), r1 = ( j + 1u ) * ( su + 1u );
            pW->Quad( g[r0 + i], g[r0 + i + 1u], g[r1 + i + 1u], g[r1 + i], 0u );
        }
    }
}

// Rings of the circle polygon stacked along the axis. scale(k) shrinks ring
// k toward the axis (1 = full circle), height(k) places it; a scale of 0
// collapses the ring to a single apex vertex.
template <typename scale_t, typename height_t>
void WriteRevolved( const geometry_primitive_t &p, const math::vec2d_t *pRing, u32 n, u32 cRings, scale_t &&scale, height_t &&height,
                    bool bCapBottom, bool bCapTop, u32 sideSmoothing, mesh_writer_t *pW, vector_t<u32> *pScratch ) noexcept
{
    const u32 a = p.axis, u = AxisU( a ), v = AxisV( a );
    const f64 cu = 0.5 * ( C( p.box.lo, u ) + C( p.box.hi, u ) ), cv = 0.5 * ( C( p.box.lo, v ) + C( p.box.hi, v ) );
    if ( !Vector_Resize( pScratch, static_cast<usize>( cRings ) * n ) ) {
        pW->st = geometry_status_t::ALLOCATION_FAILED;
        return;
    }
    u32 *idx = pScratch->pData;
    for ( u32 k = 0u; k < cRings; ++k ) {
        const f64 s = scale( k ), h = height( k );
        if ( s == 0.0 ) {
            const u32 apex = pW->Vertex( Compose( a, h, u, cu, v, cv ) );
            for ( u32 i = 0u; i < n; ++i ) { idx[k * n + i] = apex; }
            continue;
        }
        for ( u32 i = 0u; i < n; ++i ) {
            idx[k * n + i] = pW->Vertex( Compose( a, h, u, cu + ( pRing[i].x - cu ) * s, v, cv + ( pRing[i].y - cv ) * s ) );
        }
    }
    for ( u32 k = 0u; k + 1u < cRings; ++k ) {
        const bool bLowApex = scale( k ) == 0.0, bHighApex = scale( k + 1u ) == 0.0;
        for ( u32 i = 0u; i < n; ++i ) {
            const u32 i1 = ( i + 1u ) % n;
            const u32 a0 = idx[k * n + i], a1 = idx[k * n + i1], b0 = idx[( k + 1u ) * n + i], b1 = idx[( k + 1u ) * n + i1];
            if ( bLowApex ) {
                pW->Tri( a0, b1, b0, sideSmoothing );
            } else if ( bHighApex ) {
                pW->Tri( a0, a1, b0, sideSmoothing );
            } else {
                pW->Quad( a0, a1, b1, b0, sideSmoothing );
            }
        }
    }
    if ( bCapBottom ) {
        u32 rev[kCirclePointsMax];
        for ( u32 i = 0u; i < n; ++i ) { rev[i] = idx[n - 1u - i]; }
        pW->Face( rev, n, 0u );
    }
    if ( bCapTop ) { pW->Face( idx + static_cast<usize>( cRings - 1u ) * n, n, 0u ); }
}

// Extrudes 2D polygons (counter-clockwise in their plane) between depth 0
// and 1: one cap face per polygon at each end plus one side face per
// boundary edge. map(x, y, d) places a profile point in the world;
// sideSmoothing(e) picks each boundary edge's group. The profile polygons
// are given as index lists into `pts`; edges shared by two polygons are
// interior and get no side face.
template <typename map_t, typename smooth_t>
void WriteExtrusion( const math::vec2d_t *pts, u32 cPts, const u32 *pPolyIndices, const u32 *pPolyStarts, u32 cPolys, map_t &&map,
                     smooth_t &&sideSmoothing, mesh_writer_t *pW, vector_t<u32> *pScratch ) noexcept
{
    if ( !Vector_Resize( pScratch, 2u * static_cast<usize>( cPts ) ) ) {
        pW->st = geometry_status_t::ALLOCATION_FAILED;
        return;
    }
    u32 *front = pScratch->pData, *back = pScratch->pData + cPts;
    for ( u32 i = 0u; i < cPts; ++i ) {
        front[i] = pW->Vertex( map( pts[i].x, pts[i].y, 0.0 ) );
        back[i] = pW->Vertex( map( pts[i].x, pts[i].y, 1.0 ) );
    }
    u32 face[kMeshSourceCornersPerFaceMax];
    for ( u32 f = 0u; f < cPolys; ++f ) {
        const u32 first = pPolyStarts[f], count = pPolyStarts[f + 1u] - first;
        // (x, y, depth) is right-handed by convention; the front cap faces
        // -depth, so it winds backwards. A mirrored map is fixed globally.
        for ( u32 k = 0u; k < count; ++k ) { face[k] = front[pPolyIndices[first + count - 1u - k]]; }
        pW->Face( face, count, 0u );
        for ( u32 k = 0u; k < count; ++k ) { face[k] = back[pPolyIndices[first + k]]; }
        pW->Face( face, count, 0u );
        for ( u32 k = 0u; k < count; ++k ) {
            const u32 i0 = pPolyIndices[first + k], i1 = pPolyIndices[first + ( k + 1u ) % count];
            // Interior when another polygon walks the same edge backwards.
            bool bInterior = false;
            for ( u32 g = 0u; g < cPolys && !bInterior; ++g ) {
                if ( g == f ) { continue; }
                const u32 gFirst = pPolyStarts[g], gCount = pPolyStarts[g + 1u] - gFirst;
                for ( u32 m = 0u; m < gCount; ++m ) {
                    if ( pPolyIndices[gFirst + m] == i1 && pPolyIndices[gFirst + ( m + 1u ) % gCount] == i0 ) { bInterior = true; }
                }
            }
            if ( !bInterior ) { pW->Quad( front[i0], front[i1], back[i1], back[i0], sideSmoothing( i0, i1 ) ); }
        }
    }
}

// Maps a profile (run r, height z, depth d) for the ramp and stairs: r runs
// along the climb direction, depth across it, z up.
struct climb_frame_t {
    u32 runAxis, widthAxis;
    f64 runStart, runSign, widthLo, widthHi, floorZ;
};

climb_frame_t ClimbFrame( const geometry_primitive_t &p ) noexcept
{
    const bool bX = p.stairsDirection == brush_stairs_direction_t::POS_X || p.stairsDirection == brush_stairs_direction_t::NEG_X;
    const bool bPos = p.stairsDirection == brush_stairs_direction_t::POS_X || p.stairsDirection == brush_stairs_direction_t::POS_Y;
    const u32 run = bX ? 0u : 1u, width = bX ? 1u : 0u;
    return climb_frame_t{ run,
                          width,
                          bPos ? C( p.box.lo, run ) : C( p.box.hi, run ),
                          bPos ? 1.0 : -1.0,
                          C( p.box.lo, width ),
                          C( p.box.hi, width ),
                          p.box.lo.z };
}

math::vec3d_t ClimbPoint( const climb_frame_t &f, f64 r, f64 z, f64 d ) noexcept
{
    return Compose( f.runAxis, f.runStart + f.runSign * r, f.widthAxis, f.widthLo + ( f.widthHi - f.widthLo ) * d, 2u, f.floorZ + z );
}

f64 RunLength( const geometry_primitive_t &p ) noexcept
{
    const bool bX = p.stairsDirection == brush_stairs_direction_t::POS_X || p.stairsDirection == brush_stairs_direction_t::NEG_X;
    return bX ? p.box.hi.x - p.box.lo.x : p.box.hi.y - p.box.lo.y;
}

// Ramp profile (r, z): floor, back wall at the far end, slope.
u32 RampProfile( const geometry_primitive_t &p, math::vec2d_t *pOut ) noexcept
{
    const f64 L = RunLength( p ), H = p.box.hi.z - p.box.lo.z;
    pOut[0] = { 0.0, 0.0 };
    pOut[1] = { L, 0.0 };
    pOut[2] = { L, H };
    return 3u;
}

// Stairs side profile, counter-clockwise: along the floor, up the back,
// then down the steps toward the start.
u32 StairsProfile( const geometry_primitive_t &p, u32 cSteps, math::vec2d_t *pOut ) noexcept
{
    const f64 L = RunLength( p ), d = L / cSteps;
    u32 n = 0u;
    pOut[n++] = { 0.0, 0.0 };
    pOut[n++] = { L, 0.0 };
    for ( u32 i = cSteps; i-- > 0u; ) {
        const f64 top = StepTop( p, i, cSteps );
        pOut[n++] = { i + 1u == cSteps ? L : ( i + 1u ) * d, top };
        pOut[n++] = { i * d, top };
    }
    return n;
}

// Arch cross-section in (across, up): a band between the outer half
// ellipse and the inner one (outer scaled so the wall is `thickness` thick
// at the feet and the crown), as one quad per segment.
geometry_status_t MeshArch( const geometry_primitive_t &p, mesh_writer_t *pW, vector_t<u32> *pScratch, const allocator_t *pA ) noexcept
{
    const u32 cSeg = p.cSides / 2u;
    const u32 run = p.axis, across = run == 0u ? 1u : 0u;
    const f64 c = 0.5 * ( C( p.box.lo, across ) + C( p.box.hi, across ) );
    const f64 hw = 0.5 * ( C( p.box.hi, across ) - C( p.box.lo, across ) ), H = p.box.hi.z - p.box.lo.z;
    const f64 t = p.archThickness;
    vector_t<math::vec2d_t> pts{};
    vector_t<u32> polys{}, starts{};
    if ( !Vector_Init( &pts, pA ) || !Vector_Init( &polys, pA ) || !Vector_Init( &starts, pA ) ||
         !Vector_Resize( &pts, 2u * ( cSeg + 1u ) ) || !Vector_Resize( &polys, 4u * cSeg ) || !Vector_Resize( &starts, cSeg + 1u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 k = 0u; k <= cSeg; ++k ) {
        // Exact axis-aligned values at the feet and the crown.
        const f64 cs = k == 0u ? 1.0 : k == cSeg ? -1.0 : 2u * k == cSeg ? 0.0 : std::cos( math::CY_PI_D * k / cSeg );
        const f64 sn = k == 0u || k == cSeg ? 0.0 : 2u * k == cSeg ? 1.0 : std::sin( math::CY_PI_D * k / cSeg );
        pts.pData[k] = { c + hw * cs, H * sn };                            // outer
        pts.pData[cSeg + 1u + k] = { c + ( hw - t ) * cs, ( H - t ) * sn }; // inner
    }
    for ( u32 k = 0u; k < cSeg; ++k ) {
        starts.pData[k] = 4u * k;
        polys.pData[4u * k + 0u] = k;
        polys.pData[4u * k + 1u] = k + 1u;
        polys.pData[4u * k + 2u] = cSeg + 2u + k;
        polys.pData[4u * k + 3u] = cSeg + 1u + k;
    }
    starts.pData[cSeg] = 4u * cSeg;
    const f64 runLo = C( p.box.lo, run ), runHi = C( p.box.hi, run ), floorZ = p.box.lo.z;
    auto map = [&]( f64 x, f64 y, f64 d ) noexcept { return Compose( across, x, 2u, floorZ + y, run, runLo + ( runHi - runLo ) * d ); };
    // Outer and inner surfaces are curved; the feet are flat.
    auto smooth = [&]( u32 i0, u32 i1 ) noexcept -> u32 {
        const bool bOuter = i0 <= cSeg && i1 <= cSeg;
        const bool bInner = i0 > cSeg && i1 > cSeg;
        return bOuter ? 1u : bInner ? 2u : 0u;
    };
    WriteExtrusion( pts.pData, static_cast<u32>( pts.nCount ), polys.pData, starts.pData, cSeg, map, smooth, pW, pScratch );
    return pW->st;
}

geometry_status_t BuildMesh( const geometry_primitive_t &p, const allocator_t *pA, geometry_source_id_allocator_t *pIds, mesh_source_t *pOut ) noexcept
{
    const geometry_source_id_result_t root = GeometrySourceIdAllocator_Allocate( pIds );
    if ( root.status != geometry_status_t::OK ) { return root.status; }
    mesh_source_description_t desc{};
    geometry_status_t st = MeshSourceDescription_Init( &desc, pA, root.id );
    if ( st != geometry_status_t::OK ) { return st; }
    vector_t<u32> scratch{};
    if ( !Vector_Init( &scratch, pA ) ) {
        MeshSourceDescription_Shutdown( &desc );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    mesh_writer_t w{ &desc, pIds, p.material };
    math::vec2d_t ring[kCirclePointsMax];
    u32 n = 0u;
    if ( IsRound( p.kind ) ) { w.st = Circle( p, ring, &n ); }
    const u32 a = p.axis;
    const f64 lo = C( p.box.lo, a ), hi = C( p.box.hi, a );
    switch ( p.kind ) {
    case kind_t::BOX:
        WriteBox( p, pA, &w );
        break;
    case kind_t::PLANE:
        WritePlane( p, &w, &scratch );
        break;
    case kind_t::CYLINDER: {
        const u32 bands = p.cSegments[a];
        WriteRevolved( p, ring, n, bands + 1u, []( u32 ) noexcept { return 1.0; },
                       [&]( u32 k ) noexcept { return k == bands ? hi : lo + ( hi - lo ) * k / bands; }, true, true, 1u, &w, &scratch );
        break;
    }
    case kind_t::CONE: {
        const u32 bands = p.cSegments[a];
        WriteRevolved( p, ring, n, bands + 1u, [&]( u32 k ) noexcept { return k == bands ? 0.0 : 1.0 - static_cast<f64>( k ) / bands; },
                       [&]( u32 k ) noexcept { return k == bands ? hi : lo + ( hi - lo ) * k / bands; }, true, false, 1u, &w, &scratch );
        break;
    }
    case kind_t::PYRAMID: {
        const u32 u = AxisU( a ), v = AxisV( a );
        const math::vec2d_t base[4] = { { C( p.box.lo, u ), C( p.box.lo, v ) },
                                        { C( p.box.hi, u ), C( p.box.lo, v ) },
                                        { C( p.box.hi, u ), C( p.box.hi, v ) },
                                        { C( p.box.lo, u ), C( p.box.hi, v ) } };
        WriteRevolved( p, base, 4u, 2u, []( u32 k ) noexcept { return k == 0u ? 1.0 : 0.0; },
                       [&]( u32 k ) noexcept { return k == 0u ? lo : hi; }, true, false, 0u, &w, &scratch );
        break;
    }
    case kind_t::SPHERE: {
        const u32 R = p.cRings;
        const f64 c = 0.5 * ( lo + hi ), h = 0.5 * ( hi - lo );
        // Ring k at polar angle pi k / R from the low pole; the poles are apexes.
        WriteRevolved( p, ring, n, R + 1u,
                       [&]( u32 k ) noexcept { return k == 0u || k == R ? 0.0 : 2u * k == R ? 1.0 : std::sin( math::CY_PI_D * k / R ); },
                       [&]( u32 k ) noexcept {
                           return k == 0u ? lo : k == R ? hi : 2u * k == R ? c : c - h * std::cos( math::CY_PI_D * k / R );
                       },
                       false, false, 1u, &w, &scratch );
        break;
    }
    case kind_t::TORUS: {
        const u32 u = AxisU( a ), v = AxisV( a ), m = p.cRings;
        const f64 r = 0.5 * ( hi - lo ), ca = 0.5 * ( lo + hi );
        if ( w.st == geometry_status_t::OK && !Vector_Resize( &scratch, static_cast<usize>( n ) * m ) ) { w.st = geometry_status_t::ALLOCATION_FAILED; }
        for ( u32 i = 0u; w.st == geometry_status_t::OK && i < n; ++i ) {
            // Outward normal of the ring polygon at vertex i (bisecting its two
            // edges), so the tube keeps a round cross-section on an oval ring.
            const math::vec2d_t prev = ring[( i + n - 1u ) % n], next = ring[( i + 1u ) % n];
            f64 nx = next.y - prev.y, ny = -( next.x - prev.x );
            const f64 len = std::sqrt( nx * nx + ny * ny );
            nx /= len;
            ny /= len;
            for ( u32 j = 0u; j < m; ++j ) {
                const f64 phi = 2.0 * math::CY_PI_D * j / m;
                const f64 cp = std::cos( phi ), sp = std::sin( phi );
                scratch.pData[i * m + j] = w.Vertex( Compose( u, ring[i].x + nx * r * cp, v, ring[i].y + ny * r * cp, a, ca + r * sp ) );
            }
        }
        for ( u32 i = 0u; w.st == geometry_status_t::OK && i < n; ++i ) {
            for ( u32 j = 0u; j < m; ++j ) {
                const u32 i1 = ( i + 1u ) % n, j1 = ( j + 1u ) % m;
                w.Quad( scratch.pData[i * m + j], scratch.pData[i1 * m + j], scratch.pData[i1 * m + j1], scratch.pData[i * m + j1], 1u );
            }
        }
        break;
    }
    case kind_t::RAMP:
    case kind_t::STAIRS: {
        const climb_frame_t f = ClimbFrame( p );
        math::vec2d_t prof[2u * kPrimitiveStepsMax + 2u];
        const u32 cPts = p.kind == kind_t::RAMP ? RampProfile( p, prof ) : StairsProfile( p, StepCount( p ), prof );
        u32 polyIdx[2u * kPrimitiveStepsMax + 2u];
        for ( u32 i = 0u; i < cPts; ++i ) { polyIdx[i] = i; }
        const u32 starts[2] = { 0u, cPts };
        WriteExtrusion( prof, cPts, polyIdx, starts, 1u, [&]( f64 x, f64 y, f64 d ) noexcept { return ClimbPoint( f, x, y, d ); },
                        []( u32, u32 ) noexcept -> u32 { return 0u; }, &w, &scratch );
        break;
    }
    case kind_t::ARCH:
        if ( w.st == geometry_status_t::OK ) { w.st = MeshArch( p, &w, &scratch, pA ); }
        break;
    }
    st = w.st;
    // A plane is open (no volume); its grid is counter-clockwise in (u, v),
    // which faces +axis by construction.
    if ( st == geometry_status_t::OK && p.kind != kind_t::PLANE && SignedVolume6( desc ) < 0.0 ) { ReverseAllFaces( &desc ); }
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryBuild( &desc, pA, pOut ); }
    MeshSourceDescription_Shutdown( &desc );
    if ( st != geometry_status_t::OK ) { return st; }

    // World-aligned box projection from the box's low corner, like brushes.
    vector_t<geometry_mesh_face_handle_t> faces{};
    bool bOk = Vector_Init( &faces, pA ) && Vector_Reserve( &faces, GenerationPool_Count( &pOut->mesh.faces ) );
    if ( bOk ) {
        (void)GenerationPool_ForEach( &pOut->mesh.faces, [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> bool_t {
            (void)Vector_PushBack( &faces, h );
            return true;
        } );
        st = MeshSurfacing_TryProjectBox( &pOut->attributes, &pOut->mesh, Vector_Span( static_cast<const vector_t<geometry_mesh_face_handle_t> *>( &faces ) ),
                                          p.box.lo, p.worldUnitsPerUv, mesh_uv_set_t::MATERIAL );
    } else {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st != geometry_status_t::OK ) { MeshSource_Shutdown( pOut ); }
    return st;
}

// ---------------------------------------------------------------------------
// Brushes
// ---------------------------------------------------------------------------

// Every convex brush made here gets one default record per side, then the
// record's material and UV density.
geometry_status_t SolidToSource( const brush_solid_t &solid, const geometry_primitive_t &p, const allocator_t *pA, const geometry_policy_t &policy,
                                 brush_source_t *pOut ) noexcept
{
    geometry_status_t st = BrushSource_TryBuildDefault( &solid, pA, policy, pOut );
    for ( usize i = 0u; st == geometry_status_t::OK && i < BrushSideAttributeStore_Count( &pOut->attributes ); ++i ) {
        geometry_brush_side_attributes_t r{};
        st = BrushSideAttributeStore_TryGet( &pOut->attributes, i, &r );
        if ( st != geometry_status_t::OK ) { break; }
        r.material = p.material;
        r.uvProjection.worldUnitsPerUv = p.worldUnitsPerUv;
        st = BrushSideAttributeStore_TrySet( &pOut->attributes, policy.numerical, i, r );
    }
    if ( st != geometry_status_t::OK ) { BrushSource_Shutdown( pOut ); }
    return st;
}

// Points of the convex kinds built through the hull (ramp, pyramid,
// sphere): the same positions their mesh has.
u32 HullPoints( const geometry_primitive_t &p, math::vec3d_t *pOut, u32 cCapacity ) noexcept
{
    u32 n = 0u;
    auto push = [&]( math::vec3d_t q ) noexcept {
        if ( n < cCapacity ) { pOut[n] = q; }
        ++n;
    };
    const u32 a = p.axis, u = AxisU( a ), v = AxisV( a );
    const f64 lo = C( p.box.lo, a ), hi = C( p.box.hi, a );
    if ( p.kind == kind_t::RAMP ) {
        const climb_frame_t f = ClimbFrame( p );
        math::vec2d_t prof[3];
        (void)RampProfile( p, prof );
        for ( const math::vec2d_t &q : prof ) {
            push( ClimbPoint( f, q.x, q.y, 0.0 ) );
            push( ClimbPoint( f, q.x, q.y, 1.0 ) );
        }
    } else if ( p.kind == kind_t::PYRAMID ) {
        push( Compose( a, lo, u, C( p.box.lo, u ), v, C( p.box.lo, v ) ) );
        push( Compose( a, lo, u, C( p.box.hi, u ), v, C( p.box.lo, v ) ) );
        push( Compose( a, lo, u, C( p.box.hi, u ), v, C( p.box.hi, v ) ) );
        push( Compose( a, lo, u, C( p.box.lo, u ), v, C( p.box.hi, v ) ) );
        push( Compose( a, hi, u, 0.5 * ( C( p.box.lo, u ) + C( p.box.hi, u ) ), v, 0.5 * ( C( p.box.lo, v ) + C( p.box.hi, v ) ) ) );
    } else if ( p.kind == kind_t::SPHERE ) {
        math::vec2d_t ring[kCirclePointsMax];
        u32 cRing = 0u;
        if ( Circle( p, ring, &cRing ) != geometry_status_t::OK ) { return 0u; }
        const f64 cu = 0.5 * ( C( p.box.lo, u ) + C( p.box.hi, u ) ), cv = 0.5 * ( C( p.box.lo, v ) + C( p.box.hi, v ) );
        const f64 c = 0.5 * ( lo + hi ), h = 0.5 * ( hi - lo );
        push( Compose( a, lo, u, cu, v, cv ) );
        push( Compose( a, hi, u, cu, v, cv ) );
        for ( u32 k = 1u; k < p.cRings; ++k ) {
            const bool bEquator = 2u * k == p.cRings;
            const f64 s = bEquator ? 1.0 : std::sin( math::CY_PI_D * k / p.cRings );
            const f64 z = bEquator ? c : c - h * std::cos( math::CY_PI_D * k / p.cRings );
            for ( u32 i = 0u; i < cRing; ++i ) { push( Compose( a, z, u, cu + ( ring[i].x - cu ) * s, v, cv + ( ring[i].y - cv ) * s ) ); }
        }
    }
    return n;
}

void FreeSolids( brush_solid_t *pSolids, u32 cSolids ) noexcept
{
    for ( u32 i = 0u; i < cSolids; ++i ) { BrushSolid_Shutdown( &pSolids[i] ); }
}

geometry_status_t BuildBrushes( const geometry_primitive_t &p, const geometry_policy_t &policy, const allocator_t *pA,
                                geometry_source_id_allocator_t *pIds, geometry_fragment_t *pFragment ) noexcept
{
    // Solids first (the generators are failure-atomic on the allocator),
    // then sources, then one all-or-nothing append.
    const u32 cCapacity = p.kind == kind_t::STAIRS ? StepCount( p ) : p.kind == kind_t::ARCH ? p.cSides / 2u + 4u : 1u;
    void *pSolidMemory = Allocator_AllocateZeroed( pA, sizeof( brush_solid_t ) * cCapacity, alignof( brush_solid_t ) );
    void *pSourceMemory = Allocator_AllocateZeroed( pA, sizeof( brush_source_t ) * cCapacity, alignof( brush_source_t ) );
    if ( pSolidMemory == nullptr || pSourceMemory == nullptr ) {
        Allocator_Free( pA, pSolidMemory, sizeof( brush_solid_t ) * cCapacity, alignof( brush_solid_t ) );
        Allocator_Free( pA, pSourceMemory, sizeof( brush_source_t ) * cCapacity, alignof( brush_source_t ) );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    // One placement new per element: array placement new may prepend an
    // implementation-defined cookie the allocation does not include.
    brush_solid_t *pSolids = static_cast<brush_solid_t *>( pSolidMemory );
    brush_source_t *pSources = static_cast<brush_source_t *>( pSourceMemory );
    for ( u32 i = 0u; i < cCapacity; ++i ) {
        new ( &pSolids[i] ) brush_solid_t{};
        new ( &pSources[i] ) brush_source_t{};
    }
    geometry_source_id_allocator_t ids = *pIds;
    u32 cSolids = 0u;
    geometry_status_t st = geometry_status_t::OK;
    switch ( p.kind ) {
    case kind_t::BOX:
        st = BrushShapes_TryMakeCuboid( &pSolids[0], pA, policy, &ids, p.box );
        cSolids = 1u;
        break;
    case kind_t::CYLINDER:
        st = BrushShapes_TryMakeCylinder( &pSolids[0], pA, policy, &ids, p.box, p.axis, p.cSides, p.circleMode );
        cSolids = 1u;
        break;
    case kind_t::CONE:
        st = BrushShapes_TryMakeCone( &pSolids[0], pA, policy, &ids, p.box, p.axis, p.cSides, p.circleMode );
        cSolids = 1u;
        break;
    case kind_t::ARCH:
        st = BrushShapes_TryMakeArch( pSolids, cCapacity, &cSolids, pA, policy, &ids, p.box, p.axis, p.cSides, p.circleMode, p.archThickness );
        break;
    case kind_t::STAIRS:
        st = BrushShapes_TryMakeStairs( pSolids, cCapacity, &cSolids, pA, policy, &ids, p.box, p.stairsDirection, p.stepHeight );
        break;
    case kind_t::RAMP:
    case kind_t::PYRAMID:
    case kind_t::SPHERE: {
        constexpr u32 kHullPointsMax = kBrushCircleSidesMax * kPrimitiveRingsMax + 2u;
        void *pPointMemory = Allocator_Allocate( pA, sizeof( math::vec3d_t ) * kHullPointsMax, alignof( math::vec3d_t ) );
        if ( pPointMemory == nullptr ) {
            st = geometry_status_t::ALLOCATION_FAILED;
            break;
        }
        math::vec3d_t *pPoints = static_cast<math::vec3d_t *>( pPointMemory );
        const u32 cPoints = HullPoints( p, pPoints, kHullPointsMax );
        st = cPoints >= 4u && cPoints <= kHullPointsMax ? ConvexHull_TryBuildBrush( &pSolids[0], pA, policy, &ids, pPoints, cPoints )
                                                         : geometry_status_t::INVALID_ARGUMENT;
        cSolids = 1u;
        Allocator_Free( pA, pPointMemory, sizeof( math::vec3d_t ) * kHullPointsMax, alignof( math::vec3d_t ) );
        break;
    }
    case kind_t::TORUS:
    case kind_t::PLANE:
        st = geometry_status_t::UNSUPPORTED;
        break;
    }
    if ( st != geometry_status_t::OK ) { cSolids = 0u; } // generators leave their outputs empty on failure
    u32 cSources = 0u;
    for ( ; st == geometry_status_t::OK && cSources < cSolids; ++cSources ) {
        st = SolidToSource( pSolids[cSources], p, pA, policy, &pSources[cSources] );
        if ( st != geometry_status_t::OK ) { break; }
    }
    if ( st == geometry_status_t::OK ) {
        st = GeometryFragment_TryAppendCopies( pFragment, policy, span_t<const brush_source_t>{ pSources, cSources }, {}, {}, {} );
    }
    if ( st == geometry_status_t::OK ) { *pIds = ids; }
    for ( u32 i = 0u; i < cSources; ++i ) { BrushSource_Shutdown( &pSources[i] ); }
    FreeSolids( pSolids, cCapacity );
    for ( u32 i = 0u; i < cCapacity; ++i ) {
        pSolids[i].~brush_solid_t();
        pSources[i].~brush_source_t();
    }
    Allocator_Free( pA, pSolidMemory, sizeof( brush_solid_t ) * cCapacity, alignof( brush_solid_t ) );
    Allocator_Free( pA, pSourceMemory, sizeof( brush_source_t ) * cCapacity, alignof( brush_source_t ) );
    return st;
}

// ---------------------------------------------------------------------------
// Patches
// ---------------------------------------------------------------------------

// Scales controls about `center` per axis: an affine map of a Bezier
// surface's controls is the same map of the surface, so a unit-circle patch
// scaled this way is exactly the inscribed ellipse.
void ScalePatch( patch_surface_t *pPatch, math::vec3d_t center, math::vec3d_t scale ) noexcept
{
    for ( usize i = 0u; i < pPatch->controls.nCount; ++i ) {
        math::vec3d_t &q = pPatch->controls.pData[i].position;
        q = math::Vec3d_Make( center.x + ( q.x - center.x ) * scale.x, center.y + ( q.y - center.y ) * scale.y,
                              center.z + ( q.z - center.z ) * scale.z );
    }
}

geometry_status_t BuildPatch( const geometry_primitive_t &p, const allocator_t *pA, geometry_source_id_allocator_t *pIds,
                              patch_surface_t *pOut ) noexcept
{
    const u32 a = p.axis, u = AxisU( a ), v = AxisV( a );
    const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( pIds );
    if ( id.status != geometry_status_t::OK ) { return id.status; }
    const math::vec3d_t center = math::Vec3d_Scale( math::Vec3d_Add( p.box.lo, p.box.hi ), 0.5 );
    const f64 hu = 0.5 * ( C( p.box.hi, u ) - C( p.box.lo, u ) ), hv = 0.5 * ( C( p.box.hi, v ) - C( p.box.lo, v ) );
    const f64 ha = 0.5 * ( C( p.box.hi, a ) - C( p.box.lo, a ) );
    patch_revolve_frame_t frame{};
    frame.center = center;
    frame.axis = Compose( a, 1.0, u, 0.0, v, 0.0 );
    frame.reference = Compose( u, 1.0, v, 0.0, a, 0.0 );
    patch_primitive_common_t common{};
    common.cSegments = p.cSides;
    common.materialId = static_cast<u32>( p.material.value );
    geometry_status_t st = geometry_status_t::UNSUPPORTED;
    switch ( p.kind ) {
    case kind_t::CYLINDER:
    case kind_t::CONE:
        frame.center = Compose( a, C( p.box.lo, a ), u, C( center, u ), v, C( center, v ) );
        st = PatchPrimitive_TryMakeCone( frame, 1.0, p.kind == kind_t::CONE ? 0.0 : 1.0, 2.0 * ha, common, id.id, pIds, pA, pOut );
        if ( st == geometry_status_t::OK ) { ScalePatch( pOut, frame.center, Compose( u, hu, v, hv, a, 1.0 ) ); }
        break;
    case kind_t::SPHERE:
        st = PatchPrimitive_TryMakeSphere( frame, 1.0, p.cRings, common, id.id, pIds, pA, pOut );
        if ( st == geometry_status_t::OK ) { ScalePatch( pOut, center, Compose( u, hu, v, hv, a, ha ) ); }
        break;
    case kind_t::TORUS:
        // Scaling would flatten the tube, so the footprint must be square.
        st = std::fabs( hu - hv ) <= 1e-9 * hu ? PatchPrimitive_TryMakeTorus( frame, hu - ha, ha, p.cRings, common, id.id, pIds, pA, pOut )
                                               : geometry_status_t::INVALID_ARGUMENT;
        break;
    case kind_t::PLANE: {
        const math::vec3d_t origin = Compose( a, C( p.box.lo, a ), u, C( p.box.lo, u ), v, C( p.box.lo, v ) );
        st = Patch_TryInitFlat( pOut, pA, patch_basis_t::BIQUADRATIC_BEZIER, 2u * p.cSegments[u] + 1u, 2u * p.cSegments[v] + 1u, origin,
                                Compose( u, 2.0 * hu, v, 0.0, a, 0.0 ), Compose( v, 2.0 * hv, u, 0.0, a, 0.0 ), id.id, pIds );
        if ( st == geometry_status_t::OK ) { pOut->materialId = common.materialId; }
        break;
    }
    default:
        break;
    }
    return st;
}

} // namespace

geometry_status_t Primitive_Validate( const geometry_primitive_t &p, geometry_primitive_output_t output ) noexcept
{
    const bool bKnownKind = p.kind <= kind_t::STAIRS;
    const bool bKnownOutput = output <= output_t::PATCHES;
    if ( !bKnownKind || !bKnownOutput || p.axis > 2u ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !math::Vec3d_IsFinite( p.box.lo ) || !math::Vec3d_IsFinite( p.box.hi ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    for ( u32 i = 0u; i < 3u; ++i ) {
        const bool bFlatAllowed = p.kind == kind_t::PLANE && i == p.axis;
        if ( bFlatAllowed ? !( C( p.box.lo, i ) <= C( p.box.hi, i ) ) : !( C( p.box.lo, i ) < C( p.box.hi, i ) ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    if ( !std::isfinite( p.worldUnitsPerUv.x ) || !std::isfinite( p.worldUnitsPerUv.y ) || !( p.worldUnitsPerUv.x > 0.0 ) ||
         !( p.worldUnitsPerUv.y > 0.0 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // Which outputs each kind has (the table in the header).
    const bool bBrush = p.kind != kind_t::TORUS && p.kind != kind_t::PLANE;
    const bool bPatch = IsRound( p.kind ) || p.kind == kind_t::PLANE;
    if ( ( output == output_t::BRUSHES && !bBrush ) || ( output == output_t::PATCHES && !bPatch ) ) { return geometry_status_t::UNSUPPORTED; }

    const u32 a = p.axis, u = AxisU( a ), v = AxisV( a );
    const f64 ha = 0.5 * ( C( p.box.hi, a ) - C( p.box.lo, a ) );
    const f64 hu = 0.5 * ( C( p.box.hi, u ) - C( p.box.lo, u ) ), hv = 0.5 * ( C( p.box.hi, v ) - C( p.box.lo, v ) );
    for ( const u32 s : p.cSegments ) {
        if ( s < 1u || s > kPrimitiveSegmentsMax ) { return geometry_status_t::INVALID_ARGUMENT; }
    }
    if ( p.kind == kind_t::SPHERE && ( p.cRings < 2u || p.cRings > kPrimitiveRingsMax ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( p.kind == kind_t::TORUS && ( p.cRings < 3u || p.cRings > kPrimitiveRingsMax ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( p.kind == kind_t::TORUS && !( std::fmin( hu, hv ) > 2.0 * ha ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( p.kind == kind_t::ARCH ) {
        const u32 across = p.axis == 0u ? 1u : 0u;
        const f64 hw = 0.5 * ( C( p.box.hi, across ) - C( p.box.lo, across ) );
        if ( p.axis > 1u || p.cSides < 4u || p.cSides % 2u != 0u || p.cSides > kBrushCircleSidesMax || !std::isfinite( p.archThickness ) ||
             !( p.archThickness > 0.0 ) || !( p.archThickness < hw ) || !( p.archThickness < p.box.hi.z - p.box.lo.z ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    if ( p.kind == kind_t::STAIRS ) {
        if ( !std::isfinite( p.stepHeight ) || !( p.stepHeight > 0.0 ) ) { return geometry_status_t::INVALID_ARGUMENT; }
        const u32 cSteps = StepCount( p );
        if ( cSteps > kPrimitiveStepsMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
        // The mesh side face walks every step: two corners each plus the floor.
        if ( output == output_t::MESH && 2u * cSteps + 2u > kMeshSourceCornersPerFaceMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    }
    if ( output == output_t::PATCHES ) {
        if ( p.material.value > CY_U32_MAX ) { return geometry_status_t::INVALID_ARGUMENT; }
        if ( IsRound( p.kind ) && ( p.cSides < kPatchPrimitiveSegmentsMin || p.cSides > kPatchPrimitiveSegmentsMax ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        if ( p.kind == kind_t::TORUS && !( std::fabs( hu - hv ) <= 1e-9 * hu ) ) { return geometry_status_t::INVALID_ARGUMENT; }
        if ( p.kind == kind_t::PLANE && ( 2u * p.cSegments[u] + 1u > kPatchControlsPerAxisMax || 2u * p.cSegments[v] + 1u > kPatchControlsPerAxisMax ) ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
    } else if ( IsRound( p.kind ) ) {
        // Circle modes constrain the side count (BrushShapes_TryMakeCircle);
        // a cap face holds every ring vertex.
        math::vec2d_t ring[kCirclePointsMax];
        u32 n = 0u;
        if ( Circle( p, ring, &n ) != geometry_status_t::OK ) { return geometry_status_t::INVALID_ARGUMENT; }
        if ( output == output_t::MESH && n > kMeshSourceCornersPerFaceMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    }
    return geometry_status_t::OK;
}

geometry_status_t Primitive_TryBuild(
    const geometry_primitive_t &primitive,
    geometry_primitive_output_t output,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    geometry_fragment_t *pFragment ) noexcept
{
    if ( pIdAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !GeometryFragment_IsInitialized( pFragment ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_status_t st = Primitive_Validate( primitive, output );
    if ( st != geometry_status_t::OK ) { return st; }
    const allocator_t *pA = pFragment->pAllocator;
    if ( output == output_t::BRUSHES ) { return BuildBrushes( primitive, policy, pA, pIdAllocator, pFragment ); }

    geometry_source_id_allocator_t ids = *pIdAllocator;
    if ( output == output_t::MESH ) {
        mesh_source_t mesh{};
        st = BuildMesh( primitive, pA, &ids, &mesh );
        if ( st == geometry_status_t::OK ) {
            st = GeometryFragment_TryAppendCopies( pFragment, policy, {}, span_t<const mesh_source_t>{ &mesh, 1u }, {}, {} );
            MeshSource_Shutdown( &mesh );
        }
    } else {
        patch_surface_t patch{};
        st = BuildPatch( primitive, pA, &ids, &patch );
        if ( st == geometry_status_t::OK ) { st = GeometryFragment_TryAppendCopies( pFragment, policy, {}, {}, span_t<const patch_surface_t>{ &patch, 1u }, {} ); }
        Patch_Shutdown( &patch );
    }
    if ( st == geometry_status_t::OK ) { *pIdAllocator = ids; }
    return st;
}

} // namespace cypher::editor::geometry
