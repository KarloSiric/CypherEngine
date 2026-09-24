//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushQueries_Tests.cpp
//  Purpose: Verifies derived brush queries — bounds, centroid, and
//           component lookups.
//  Details: Covers Gate 4 query acceptance: AABB enclosing all boundary
//           vertices, vertex/edge/face counts matching boundary, centroid
//           at box center, and error paths.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushQueries.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using Catch::Approx;

namespace {

struct QueryFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};

    QueryFixture()
    {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAlloc,
                     Vec3d_Make( 1.0, 2.0, 3.0 ),
                     Vec3d_Make( 1.0, 2.0, 3.0 ) ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &brush, policy ) ==
                 geometry_status_t::OK );
    }

    ~QueryFixture()
    {
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

TEST_CASE( "Queries: f64 bounds enclose box vertices",
           "[Gate4][Queries]" )
{
    QueryFixture f;
    const math::aabbd_t bounds =
        BrushQueries_ComputeBoundsd( &f.boundary );

    // Box from (0,0,0) to (2,4,6).
    REQUIRE( bounds.minimum.x == Approx( 0.0 ) );
    REQUIRE( bounds.minimum.y == Approx( 0.0 ) );
    REQUIRE( bounds.minimum.z == Approx( 0.0 ) );
    REQUIRE( bounds.maximum.x == Approx( 2.0 ) );
    REQUIRE( bounds.maximum.y == Approx( 4.0 ) );
    REQUIRE( bounds.maximum.z == Approx( 6.0 ) );
}

TEST_CASE( "Queries: f32 bounds match f64 for small box",
           "[Gate4][Queries]" )
{
    QueryFixture f;
    const math::aabb_t bounds =
        BrushQueries_ComputeBounds( &f.boundary );

    REQUIRE( bounds.minimum.x == Approx( 0.0f ) );
    REQUIRE( bounds.minimum.y == Approx( 0.0f ) );
    REQUIRE( bounds.minimum.z == Approx( 0.0f ) );
    REQUIRE( bounds.maximum.x == Approx( 2.0f ) );
    REQUIRE( bounds.maximum.y == Approx( 4.0f ) );
    REQUIRE( bounds.maximum.z == Approx( 6.0f ) );
}

TEST_CASE( "Queries: null boundary returns empty bounds",
           "[Gate4][Queries]" )
{
    const math::aabbd_t bounds = BrushQueries_ComputeBoundsd( nullptr );
    REQUIRE( math::Aabbd_IsEmpty( bounds ) );
}

// ---------------------------------------------------------------------------
// Component counts
// ---------------------------------------------------------------------------

TEST_CASE( "Queries: box has 8 vertices, 12 edges, 6 faces",
           "[Gate4][Queries]" )
{
    QueryFixture f;
    REQUIRE( BrushQueries_VertexCount( &f.boundary ) == 8u );
    REQUIRE( BrushQueries_EdgeCount( &f.boundary ) == 12u );
    REQUIRE( BrushQueries_FaceCount( &f.boundary ) == 6u );
}

// ---------------------------------------------------------------------------
// Vertex lookup
// ---------------------------------------------------------------------------

TEST_CASE( "Queries: vertex lookup returns valid positions",
           "[Gate4][Queries]" )
{
    QueryFixture f;
    const common::usize cVerts = BrushQueries_VertexCount( &f.boundary );

    for ( common::usize i = 0u; i < cVerts; ++i ) {
        math::vec3d_t pos{};
        REQUIRE( BrushQueries_TryGetVertex( &f.boundary, i, &pos ) ==
                 geometry_status_t::OK );
        // All box vertices are within [0,2] x [0,4] x [0,6].
        REQUIRE( pos.x >= -0.001 );
        REQUIRE( pos.x <= 2.001 );
        REQUIRE( pos.y >= -0.001 );
        REQUIRE( pos.y <= 4.001 );
        REQUIRE( pos.z >= -0.001 );
        REQUIRE( pos.z <= 6.001 );
    }
}

TEST_CASE( "Queries: vertex out of range fails",
           "[Gate4][Queries]" )
{
    QueryFixture f;
    math::vec3d_t pos{};
    REQUIRE( BrushQueries_TryGetVertex( &f.boundary, 999u, &pos ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// Edge lookup
// ---------------------------------------------------------------------------

TEST_CASE( "Queries: edge lookup returns valid indices",
           "[Gate4][Queries]" )
{
    QueryFixture f;
    const common::usize cEdges = BrushQueries_EdgeCount( &f.boundary );
    const common::usize cVerts = BrushQueries_VertexCount( &f.boundary );

    for ( common::usize i = 0u; i < cEdges; ++i ) {
        brush_boundary_edge_t edge{};
        REQUIRE( BrushQueries_TryGetEdge( &f.boundary, i, &edge ) ==
                 geometry_status_t::OK );
        REQUIRE( edge.iVertex0 < cVerts );
        REQUIRE( edge.iVertex1 < cVerts );
        // Canonical ordering: v0 < v1.
        REQUIRE( edge.iVertex0 < edge.iVertex1 );
    }
}

TEST_CASE( "Queries: edge out of range fails",
           "[Gate4][Queries]" )
{
    QueryFixture f;
    brush_boundary_edge_t edge{};
    REQUIRE( BrushQueries_TryGetEdge( &f.boundary, 999u, &edge ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// Centroid
// ---------------------------------------------------------------------------

TEST_CASE( "Queries: centroid is at box center",
           "[Gate4][Queries]" )
{
    QueryFixture f;
    const math::vec3d_t center =
        BrushQueries_ComputeCentroid( &f.boundary );

    // Box (0,0,0)-(2,4,6) → center (1,2,3).
    REQUIRE( center.x == Approx( 1.0 ) );
    REQUIRE( center.y == Approx( 2.0 ) );
    REQUIRE( center.z == Approx( 3.0 ) );
}

TEST_CASE( "Queries: centroid of null boundary is zero",
           "[Gate4][Queries]" )
{
    const math::vec3d_t center = BrushQueries_ComputeCentroid( nullptr );
    REQUIRE( center.x == Approx( 0.0 ) );
    REQUIRE( center.y == Approx( 0.0 ) );
    REQUIRE( center.z == Approx( 0.0 ) );
}

} // namespace cypher::editor::geometry
