//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTessellation_Tests.cpp
//  Purpose: Verifies deterministic brush tessellation with side provenance.
//  Details: Covers the Tessellation acceptance gate: the box tessellates to
//           exactly 12 outward triangles and every triangle maps to one
//           source side.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "../CypherGeometry_TestSupport.h"

#include "CypherGeometry_BrushTessellation.h"

#include <catch2/catch_approx.hpp>

#include <algorithm>

namespace cypher::editor::geometry {

using namespace test;
using Catch::Approx;

namespace {

struct tessellation_holder_t {
    geometry_brush_tessellation_t tessellation{};
    tessellation_holder_t()
    {
        REQUIRE( BrushTessellation_Init( &tessellation, common::Allocator_GetSystem() ) ==
                 geometry_status_t::OK );
    }
    ~tessellation_holder_t() { BrushTessellation_Shutdown( &tessellation ); }
};

vec3d_t TriangleCross( const brush_boundary_t &boundary, const geometry_brush_triangle_t &t )
{
    const vec3d_t a = boundary.vertices.pData[t.iVertex0];
    const vec3d_t b = boundary.vertices.pData[t.iVertex1];
    const vec3d_t c = boundary.vertices.pData[t.iVertex2];
    return cypher::math::Vec3d_Cross( cypher::math::Vec3d_Subtract( b, a ),
                                      cypher::math::Vec3d_Subtract( c, a ) );
}

} // namespace

TEST_CASE( "box tessellates to 12 outward triangles, two per side",
           "[editor][geometry][tessellation]" ) {
    brush_holder_t brush{};
    BuildBrush( &brush.brush, UnitBoxPlanes() );
    boundary_holder_t boundary{};
    Reconstruct( &boundary.boundary, &brush.brush );
    tessellation_holder_t holder{};
    const geometry_policy_t policy{};

    REQUIRE( BrushTessellation_TryBuild( &holder.tessellation, &brush.brush, &boundary.boundary,
                                         policy ) == geometry_status_t::OK );
    REQUIRE( BrushTessellation_TriangleCount( &holder.tessellation ) == 12u );

    common::u32 perSide[6]{};
    f64 area = 0.0;
    for ( common::usize i = 0u; i < 12u; ++i ) {
        geometry_brush_triangle_t triangle{};
        REQUIRE( BrushTessellation_TryGetTriangle( &holder.tessellation, i, &triangle ) ==
                 geometry_status_t::OK );
        REQUIRE( triangle.iSide < 6u );
        ++perSide[triangle.iSide];
        REQUIRE( triangle.sideId.value == brush.brush.sides.pData[triangle.iSide].sourceId.value );
        const vec3d_t cross = TriangleCross( boundary.boundary, triangle );
        const vec3d_t normal = brush.brush.sides.pData[triangle.iSide].plane.normal;
        REQUIRE( cypher::math::Vec3d_Dot( cross, normal ) > 0.0 );
        area += 0.5 * cypher::math::Vec3d_Dot( cross, normal );
    }
    for ( common::u32 count : perSide ) {
        REQUIRE( count == 2u );
    }
    REQUIRE( area == Approx( 24.0 ) );
}

TEST_CASE( "tessellation is identical under side permutation by side identity",
           "[editor][geometry][tessellation]" ) {
    const std::vector<planed_t> planes = UnitBoxPlanes();
    std::vector<common::usize> order{ 0u, 1u, 2u, 3u, 4u, 5u };
    const geometry_policy_t policy{};

    std::vector<std::vector<common::u32>> reference;
    do {
        brush_holder_t brush{};
        REQUIRE( BrushSolid_Init( &brush.brush, common::Allocator_GetSystem(),
                                  geometry_source_id_t{ 1u } ) == geometry_status_t::OK );
        for ( common::usize i = 0u; i < order.size(); ++i ) {
            brush_solid_side_t side{};
            side.plane = planes[order[i]];
            side.sourceId = geometry_source_id_t{ 100u + order[i] };
            REQUIRE( BrushSolid_TryAddSide( &brush.brush, policy.limits, side, nullptr ) ==
                     geometry_status_t::OK );
        }
        boundary_holder_t boundary{};
        Reconstruct( &boundary.boundary, &brush.brush );
        tessellation_holder_t holder{};
        REQUIRE( BrushTessellation_TryBuild( &holder.tessellation, &brush.brush,
                                             &boundary.boundary, policy ) == geometry_status_t::OK );

        std::vector<std::vector<common::u32>> triangles;
        for ( common::usize i = 0u; i < BrushTessellation_TriangleCount( &holder.tessellation ); ++i ) {
            const geometry_brush_triangle_t &t = holder.tessellation.triangles.pData[i];
            triangles.push_back( { static_cast<common::u32>( t.sideId.value ),
                                   t.iVertex0, t.iVertex1, t.iVertex2 } );
        }
        std::sort( triangles.begin(), triangles.end() );
        if ( reference.empty() ) {
            reference = triangles;
        }
        REQUIRE( triangles == reference );
    } while ( std::next_permutation( order.begin(), order.end() ) );
}

TEST_CASE( "chamfered box fans every face without holes",
           "[editor][geometry][tessellation]" ) {
    std::vector<planed_t> planes = UnitBoxPlanes();
    planes.push_back( UnitPlane( 1.0, 1.0, 1.0, 2.5 / std::sqrt( 3.0 ) ) );
    brush_holder_t brush{};
    BuildBrush( &brush.brush, planes );
    boundary_holder_t boundary{};
    Reconstruct( &boundary.boundary, &brush.brush );
    tessellation_holder_t holder{};
    REQUIRE( BrushTessellation_TryBuild( &holder.tessellation, &brush.brush, &boundary.boundary,
                                         geometry_policy_t{} ) == geometry_status_t::OK );
    // Three pentagons (3 triangles each), three quads (2 each), one triangle.
    REQUIRE( BrushTessellation_TriangleCount( &holder.tessellation ) == 16u );
    // V - E + F on the triangulation: 10 - 24 + 16 = 2.
}

TEST_CASE( "tessellation rejects mismatched and uninitialized inputs atomically",
           "[editor][geometry][tessellation]" ) {
    brush_holder_t box{};
    BuildBrush( &box.brush, UnitBoxPlanes() );
    boundary_holder_t boundary{};
    Reconstruct( &boundary.boundary, &box.brush );
    tessellation_holder_t holder{};
    const geometry_policy_t policy{};
    REQUIRE( BrushTessellation_TryBuild( &holder.tessellation, &box.brush, &boundary.boundary,
                                         policy ) == geometry_status_t::OK );

    // A 4-sided brush cannot own a boundary with faces for sides 4 and 5.
    brush_holder_t tetra{};
    BuildBrush( &tetra.brush, { UnitPlane( 1, 1, 1, 1 ), UnitPlane( -1, -1, 1, 1 ),
                                UnitPlane( -1, 1, -1, 1 ), UnitPlane( 1, -1, -1, 1 ) } );
    REQUIRE( BrushTessellation_TryBuild( &holder.tessellation, &tetra.brush, &boundary.boundary,
                                         policy ) == geometry_status_t::CORRUPT_STATE );
    REQUIRE( BrushTessellation_TriangleCount( &holder.tessellation ) == 0u );

    boundary_holder_t empty{};
    REQUIRE( BrushBoundary_Init( &empty.boundary, common::Allocator_GetSystem() ) ==
             geometry_status_t::OK );
    REQUIRE( BrushTessellation_TryBuild( &holder.tessellation, &box.brush, &empty.boundary,
                                         policy ) == geometry_status_t::DEGENERATE );

    geometry_brush_tessellation_t uninitialized{};
    REQUIRE( BrushTessellation_TryBuild( &uninitialized, &box.brush, &boundary.boundary,
                                         policy ) == geometry_status_t::NOT_INITIALIZED );
    geometry_brush_triangle_t triangle{};
    REQUIRE( BrushTessellation_TryGetTriangle( &holder.tessellation, 0u, &triangle ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "tessellation reports allocation failure with an empty result",
           "[editor][geometry][tessellation]" ) {
    brush_holder_t box{};
    BuildBrush( &box.brush, UnitBoxPlanes() );
    boundary_holder_t boundary{};
    Reconstruct( &boundary.boundary, &box.brush );
    failing_allocator_state_t state{};
    const common::allocator_t allocator = MakeFailingAllocator( &state );
    geometry_brush_tessellation_t tessellation{};
    REQUIRE( BrushTessellation_Init( &tessellation, &allocator ) == geometry_status_t::OK );
    state.iFailure = state.cAllocationCalls;
    REQUIRE( BrushTessellation_TryBuild( &tessellation, &box.brush, &boundary.boundary,
                                         geometry_policy_t{} ) ==
             geometry_status_t::ALLOCATION_FAILED );
    REQUIRE( BrushTessellation_TriangleCount( &tessellation ) == 0u );
    BrushTessellation_Shutdown( &tessellation );
}

} // namespace cypher::editor::geometry
