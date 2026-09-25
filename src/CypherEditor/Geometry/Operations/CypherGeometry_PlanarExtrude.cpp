//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PlanarExtrude.cpp
//  Purpose: Implements PlanarRegion extrusion via an indexed polygon soup
//           and the checked Sanitation builder.
//  Details: Building through Sanitation (rather than writing pools
//           directly) means the extruded solid passes exactly the same
//           manifold checks as any imported mesh; a bug here cannot publish
//           invalid topology. Vertices are shared by index, so no weld is
//           needed or requested.
//
//           Orientation: region contours are CCW (outer) / CW (holes) in the
//           frame, i.e. CCW about +normal. For height > 0 the top cap keeps
//           that winding and the bottom cap is reversed; each side quad for
//           contour edge a -> b is (a_bottom, b_bottom, b_top, a_top), which
//           faces outward for outers and — because holes run CW — into the
//           hole, which is also "outward" from the solid. For height < 0 the
//           roles of the caps swap, which is handled by reversing every face.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PlanarExtrude.h"
#include "CypherGeometry_PlanarRegionValidation.h"
#include "CypherGeometry_Planar_Triangulate.h"
#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_Sanitation.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

geometry_status_t PlanarExtrude_TryExtrude(
    const planar_region_t *pRegion,
    f64 height,
    const geometry_policy_t &policy,
    const allocator_t *pAllocator,
    editable_mesh_t *pMeshOut,
    vector_t<geometry_source_id_t> *pFaceSourceOut ) noexcept
{
    if ( pRegion == nullptr || pMeshOut == nullptr || !Allocator_IsValid( pAllocator ) ||
         !std::isfinite( height ) ||
         !( std::fabs( height ) > policy.numerical.fMinimumEdgeLength ) ||
         ( pFaceSourceOut != nullptr && pFaceSourceOut->pAllocator == nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !PlanarRegion_IsInitialized( pRegion ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( EditableMesh_IsInitialized( pMeshOut ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( PlanarRegion_PolygonCount( pRegion ) == 0u ) {
        return geometry_status_t::DEGENERATE;
    }
    const planar_region_validation_t rv = PlanarRegion_Validate( pRegion, policy );
    if ( rv.status != geometry_status_t::OK ) { return rv.status; }

    polygon_soup_t soup{};
    vector_t<planar_triangle_t> caps{};
    vector_t<geometry_source_id_t> faceSource{};
    auto cleanup = [&]() noexcept {
        PolygonSoup_Shutdown( &soup );
        Vector_Shutdown( &caps );
        Vector_Shutdown( &faceSource );
    };
    geometry_status_t s = PolygonSoup_Init( &soup, pAllocator );
    if ( s != geometry_status_t::OK ) { cleanup(); return s; }
    if ( !Vector_Init( &caps, pAllocator ) || !Vector_Init( &faceSource, pAllocator ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    s = Planar_TryTriangulateRegion( pRegion, &caps );
    if ( s != geometry_status_t::OK ) { cleanup(); return s; }

    // Vertices: bottom ring = region point i, top ring = cPts + i.
    const planar_frame_t &f = pRegion->frame;
    const usize cPts = pRegion->points.nCount;
    const math::vec3d_t lift = math::Vec3d_Scale( f.normal, height );
    for ( usize i = 0u; i < cPts && s == geometry_status_t::OK; ++i ) {
        s = PolygonSoup_TryAddVertex( &soup, PlanarFrame_Unproject( f, pRegion->points.pData[i] ), nullptr );
    }
    for ( usize i = 0u; i < cPts && s == geometry_status_t::OK; ++i ) {
        s = PolygonSoup_TryAddVertex(
            &soup, math::Vec3d_Add( PlanarFrame_Unproject( f, pRegion->points.pData[i] ), lift ), nullptr );
    }

    const bool flip = height < 0.0;
    auto addFace = [&]( u32 *idx, u32 n, geometry_source_id_t src ) noexcept {
        if ( s != geometry_status_t::OK ) { return; }
        if ( flip ) {
            for ( u32 a = 0u, b = n - 1u; a < b; ++a, --b ) {
                const u32 t = idx[a]; idx[a] = idx[b]; idx[b] = t;
            }
        }
        s = PolygonSoup_TryAddFace( &soup, span_t<const u32>{ idx, n }, src, 0u, nullptr );
        if ( s == geometry_status_t::OK && !Vector_PushBack( &faceSource, src ) ) {
            s = geometry_status_t::ALLOCATION_FAILED;
        }
    };

    const u32 top = static_cast<u32>( cPts );
    for ( usize t = 0u; t < caps.nCount; ++t ) {
        const planar_triangle_t &tr = caps.pData[t];
        const geometry_source_id_t polyId = pRegion->polygons.pData[tr.iPolygon].sourceId;
        u32 topTri[3] = { top + tr.a, top + tr.b, top + tr.c };     // faces +normal
        addFace( topTri, 3u, polyId );
        u32 bottomTri[3] = { tr.a, tr.c, tr.b };                    // reversed: faces -normal
        addFace( bottomTri, 3u, polyId );
    }
    for ( usize c = 0u; c < pRegion->contours.nCount; ++c ) {
        const planar_region_contour_t &ct = pRegion->contours.pData[c];
        for ( u32 k = 0u; k < ct.cPoints; ++k ) {
            const u32 a = ct.iFirstPoint + k;
            const u32 b = ct.iFirstPoint + ( k + 1u ) % ct.cPoints;
            u32 quad[4] = { a, b, top + b, top + a };
            addFace( quad, 4u, ct.sourceId );
        }
    }
    if ( s != geometry_status_t::OK ) { cleanup(); return s; }

    sanitation_policy_t sp{};
    sp.bRequireClosed = true;
    sp.fMinimumFaceArea = policy.numerical.fMinimumFaceArea;
    const sanitation_report_t rep = Sanitation_TryPolygonSoupToMesh( &soup, sp, pAllocator, pMeshOut, nullptr );
    if ( rep.status != geometry_status_t::OK ) { cleanup(); return rep.status; }

    if ( pFaceSourceOut != nullptr ) {
        Vector_Clear( pFaceSourceOut );
        if ( !Vector_Reserve( pFaceSourceOut, faceSource.nCount ) ) {
            EditableMesh_Shutdown( pMeshOut );
            cleanup();
            return geometry_status_t::ALLOCATION_FAILED;
        }
        for ( usize i = 0u; i < faceSource.nCount; ++i ) {
            (void)Vector_PushBack( pFaceSourceOut, faceSource.pData[i] );
        }
    }
    cleanup();
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
