//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Intermediates_Tests.cpp
//  Purpose: Contract tests for PolygonSoup, TriangleSoup, point welding,
//           and Sanitation (soup <-> EditableMesh).
//  Details: The central fixture is a unit cube written the way importers
//           deliver it: six quads that each carry their own four corners
//           (24 vertices, nothing shared), or twelve independent
//           triangles. Only an explicit weld makes it a closed solid.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_TriangleSoup.h"
#include "CypherGeometry_PointWeld.h"
#include "CypherGeometry_Sanitation.h"
#include "CypherGeometry_MeshValidation.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <catch2/catch_approx.hpp>

#include <string>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

// Cube corners, then faces as CCW (outward) corner quadruples.
const vec3d_t kCube[8] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 },
                           { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } };
const common::u32 kCubeFaces[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
                                       { 2, 3, 7, 6 }, { 1, 2, 6, 5 }, { 0, 4, 7, 3 } };

struct Soup {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    polygon_soup_t s{};
    Soup() { REQUIRE( PolygonSoup_Init( &s, &allocator ) == geometry_status_t::OK ); }
    ~Soup() { PolygonSoup_Shutdown( &s ); }

    common::u32 V( vec3d_t p ) {
        common::u32 i = 0;
        REQUIRE( PolygonSoup_TryAddVertex( &s, p, &i ) == geometry_status_t::OK );
        return i;
    }
    void F( std::initializer_list<common::u32> idx, common::u64 id = 0 ) {
        const common::u32 *p = idx.begin();
        REQUIRE( PolygonSoup_TryAddFace( &s, common::span_t<const common::u32>{ p, idx.size() },
                                         geometry_source_id_t{ id }, 0u, nullptr ) ==
                 geometry_status_t::OK );
    }
    // Shared-vertex cube (8 vertices).
    void IndexedCube( vec3d_t offset = { 0, 0, 0 } ) {
        const common::u32 base = static_cast<common::u32>( PolygonSoup_VertexCount( &s ) );
        for ( const vec3d_t &c : kCube ) { V( math::Vec3d_Add( c, offset ) ); }
        for ( int f = 0; f < 6; ++f ) {
            F( { base + kCubeFaces[f][0], base + kCubeFaces[f][1], base + kCubeFaces[f][2],
                 base + kCubeFaces[f][3] },
               static_cast<common::u64>( 100 + f ) );
        }
    }
    // Importer-style cube: every face owns its corners (24 vertices).
    void SplitCube() {
        for ( int f = 0; f < 6; ++f ) {
            common::u32 i[4];
            for ( int k = 0; k < 4; ++k ) { i[k] = V( kCube[kCubeFaces[f][k]] ); }
            F( { i[0], i[1], i[2], i[3] }, static_cast<common::u64>( 200 + f ) );
        }
    }
};

struct Mesh {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    editable_mesh_t m{};
    ~Mesh() { EditableMesh_Shutdown( &m ); }
};

void RequireValidClosed( const editable_mesh_t *pMesh )
{
    const mesh_validation_result_t v = MeshValidation_Validate( pMesh );
    CHECK( v.bReciprocalTwins );
    CHECK( v.bClosedLoops );
    CHECK( v.bAllVerticesReferenced );
    CHECK( v.bEdgeLinks );
    CHECK( v.bEulerValid );
    CHECK( v.bConsistentWinding );
    CHECK( v.bPositiveVolume );
}

} // namespace

// ===========================================================================
// PolygonSoup
// ===========================================================================

TEST_CASE( "PolygonSoup: building and queries", "[Intermediates][PolygonSoup]" )
{
    Soup sp;
    sp.IndexedCube();
    CHECK( PolygonSoup_VertexCount( &sp.s ) == 8u );
    CHECK( PolygonSoup_FaceCount( &sp.s ) == 6u );
    CHECK( PolygonSoup_FaceCorners( &sp.s, 1 ).nCount == 4u );
    CHECK( PolygonSoup_FaceCorners( &sp.s, 99 ).nCount == 0u );
    const vec3d_t a = PolygonSoup_FaceVectorArea( &sp.s, 1 ); // +Z face
    CHECK( a.z == Approx( 2.0 ) ); // Newell length is twice the area
    CHECK( PolygonSoup_Validate( &sp.s, 1e-12 ).status == geometry_status_t::OK );
}

TEST_CASE( "PolygonSoup: validation findings", "[Intermediates][PolygonSoup]" )
{
    using F = polygon_soup_fault_t;
    SECTION( "out of range index" ) {
        Soup sp;
        sp.V( { 0, 0, 0 } );
        sp.V( { 1, 0, 0 } );
        sp.F( { 0, 1, 7 } );
        const auto v = PolygonSoup_Validate( &sp.s, 0.0 );
        CHECK( v.fault == F::INDEX_OUT_OF_RANGE );
        CHECK( v.status == geometry_status_t::INVALID_ARGUMENT );
        CHECK( v.iCorner == 2u );
    }
    SECTION( "non-finite vertex" ) {
        Soup sp;
        sp.V( { NAN, 0, 0 } );
        CHECK( PolygonSoup_Validate( &sp.s, 0.0 ).fault == F::NON_FINITE );
    }
    SECTION( "too few corners, repeated corner, zero area" ) {
        Soup sp;
        const auto a = sp.V( { 0, 0, 0 } ), b = sp.V( { 1, 0, 0 } ), c = sp.V( { 2, 0, 0 } ),
                   d = sp.V( { 0, 1, 0 } );
        sp.F( { a, b } );
        sp.F( { a, b, d, b } );
        sp.F( { a, b, c } );
        const auto v = PolygonSoup_Validate( &sp.s, 1e-12 );
        CHECK( v.fault == F::TOO_FEW_CORNERS );
        CHECK( v.cFaultyFaces == 3u );
    }
}

TEST_CASE( "PolygonSoup: bounds and failure atomicity", "[Intermediates][PolygonSoup]" )
{
    Soup sp;
    common::u32 big[kPolygonSoupFaceCornersMax + 1] = {};
    CHECK( PolygonSoup_TryAddFace( &sp.s, common::span_t<const common::u32>{ big, sizeof( big ) / 4 },
                                   {}, 0u, nullptr ) == geometry_status_t::LIMIT_EXCEEDED );
    CHECK( PolygonSoup_TryAddFace( &sp.s, common::span_t<const common::u32>{}, {}, 0u, nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( PolygonSoup_FaceCount( &sp.s ) == 0u );
    polygon_soup_t un{};
    CHECK( PolygonSoup_TryAddVertex( &un, { 0, 0, 0 }, nullptr ) == geometry_status_t::NOT_INITIALIZED );
    CHECK( std::string( PolygonSoupFault_Name( polygon_soup_fault_t::REPEATED_CORNER ) ) ==
           "repeated_corner" );
}

// ===========================================================================
// PointWeld
// ===========================================================================

TEST_CASE( "PointWeld: exact and tolerance welding", "[Intermediates][Weld]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    common::vector_t<common::u32> remap{}, reps{};
    REQUIRE( common::Vector_Init( &remap, &allocator ) );
    REQUIRE( common::Vector_Init( &reps, &allocator ) );
    const vec3d_t pts[] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1.0000001, 0, 0 }, { 5, 5, 5 } };

    point_weld_result_t w = PointWeld_BuildRemap( common::span_t<const vec3d_t>{ pts, 5 }, 0.0,
                                                  &allocator, &remap, &reps );
    REQUIRE( w.status == geometry_status_t::OK );
    CHECK( w.cUnique == 4u );
    CHECK( remap.pData[0] == remap.pData[2] );
    CHECK( remap.pData[0] == 0u ); // first-appearance numbering
    CHECK( remap.pData[1] != remap.pData[3] );

    w = PointWeld_BuildRemap( common::span_t<const vec3d_t>{ pts, 5 }, 1e-6, &allocator, &remap, &reps );
    REQUIRE( w.status == geometry_status_t::OK );
    CHECK( w.cUnique == 3u );
    CHECK( remap.pData[1] == remap.pData[3] );
    // Representative is an input point, never an average.
    CHECK( pts[reps.pData[remap.pData[3]]].x == 1.0 );
    CHECK( w.fMaxDisplacement == Approx( 1e-7 ).epsilon( 1e-3 ) );

    // Already unique input welds to the identity.
    const vec3d_t uniq[] = { { 3, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };
    w = PointWeld_BuildRemap( common::span_t<const vec3d_t>{ uniq, 3 }, 0.0, &allocator, &remap, &reps );
    CHECK( remap.pData[0] == 0u );
    CHECK( remap.pData[1] == 1u );
    CHECK( remap.pData[2] == 2u );
    common::Vector_Shutdown( &remap );
    common::Vector_Shutdown( &reps );
}

// ===========================================================================
// TriangleSoup
// ===========================================================================

TEST_CASE( "TriangleSoup: STL-style cube welds into an indexed soup",
           "[Intermediates][TriangleSoup]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    triangle_soup_t tri{};
    REQUIRE( TriangleSoup_Init( &tri, &allocator ) == geometry_status_t::OK );
    for ( common::u32 f = 0u; f < 6u; ++f ) {
        const vec3d_t a = kCube[kCubeFaces[f][0]], b = kCube[kCubeFaces[f][1]],
                      c = kCube[kCubeFaces[f][2]], d = kCube[kCubeFaces[f][3]];
        REQUIRE( TriangleSoup_TryAdd( &tri, a, b, c, geometry_source_id_t{ 1u + f }, 0u ) ==
                 geometry_status_t::OK );
        REQUIRE( TriangleSoup_TryAdd( &tri, a, c, d, geometry_source_id_t{ 1u + f }, 0u ) ==
                 geometry_status_t::OK );
    }
    // A sliver triangle whose corners weld together is dropped, not kept.
    REQUIRE( TriangleSoup_TryAdd( &tri, { 9, 9, 9 }, { 9, 9, 9.0000000001 }, { 10, 9, 9 },
                                  geometry_source_id_t{ 99 }, 0u ) == geometry_status_t::OK );
    CHECK( TriangleSoup_Count( &tri ) == 13u );

    Soup out;
    const triangle_weld_result_t w = TriangleSoup_TryWeldToPolygonSoup( &tri, 1e-6, &out.s );
    REQUIRE( w.status == geometry_status_t::OK );
    CHECK( w.cVertices == 10u ); // 8 cube corners + 2 sliver clusters
    CHECK( w.cTrianglesKept == 12u );
    CHECK( w.cTrianglesCollapsed == 1u );
    CHECK( PolygonSoup_FaceCount( &out.s ) == 12u );
    CHECK( out.s.faces.pData[0].sourceId.value == 1u );

    // Non-finite corners cannot be welded.
    REQUIRE( TriangleSoup_TryAdd( &tri, { NAN, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, {}, 0u ) ==
             geometry_status_t::OK );
    Soup out2;
    CHECK( TriangleSoup_TryWeldToPolygonSoup( &tri, 1e-6, &out2.s ).status ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( PolygonSoup_FaceCount( &out2.s ) == 0u );
    TriangleSoup_Shutdown( &tri );
}

// ===========================================================================
// Sanitation
// ===========================================================================

TEST_CASE( "Sanitation: indexed cube becomes a valid closed mesh",
           "[Intermediates][Sanitation]" )
{
    Soup sp;
    sp.IndexedCube();
    Mesh out;
    common::vector_t<geometry_mesh_face_handle_t> map{};
    REQUIRE( common::Vector_Init( &map, &out.allocator ) );
    const sanitation_report_t r =
        Sanitation_TryPolygonSoupToMesh( &sp.s, sanitation_policy_t{}, &out.allocator, &out.m, &map );
    INFO( SanitationFault_Name( r.fault ) );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( EditableMesh_VertexCount( &out.m ) == 8u );
    CHECK( EditableMesh_EdgeCount( &out.m ) == 12u );
    CHECK( EditableMesh_FaceCount( &out.m ) == 6u );
    CHECK( EditableMesh_ShellCount( &out.m ) == 1u );
    CHECK( r.cShells == 1u );
    CHECK( EditableMesh_SignedVolume( &out.m ) == Approx( 1.0 ) );
    RequireValidClosed( &out.m );
    REQUIRE( map.nCount == 6u );
    for ( common::usize i = 0; i < 6; ++i ) { CHECK( GeometryHandle_IsValid( map.pData[i] ) ); }
    common::Vector_Shutdown( &map );
}

TEST_CASE( "Sanitation: split cube needs an explicit weld", "[Intermediates][Sanitation]" )
{
    Soup sp;
    sp.SplitCube();
    {
        Mesh out;
        const sanitation_report_t r = Sanitation_TryPolygonSoupToMesh(
            &sp.s, sanitation_policy_t{}, &out.allocator, &out.m, nullptr );
        CHECK( r.status == geometry_status_t::OPEN_VOLUME ); // no weld: 6 islands
        CHECK( r.fault == sanitation_fault_t::OPEN_BOUNDARY );
        CHECK_FALSE( EditableMesh_IsInitialized( &out.m ) );
    }
    {
        // Coincident corners are bit-identical here, so even an exact weld
        // (distance 0) closes the cube — but only because it was asked for.
        Mesh out;
        sanitation_policy_t p{};
        p.bWeld = true;
        p.fWeldDistance = 0.0;
        const sanitation_report_t r =
            Sanitation_TryPolygonSoupToMesh( &sp.s, p, &out.allocator, &out.m, nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cWeldedVertices == 16u );
        CHECK( EditableMesh_VertexCount( &out.m ) == 8u );
        RequireValidClosed( &out.m );
    }
}

TEST_CASE( "Sanitation: open surfaces obey the closed policy", "[Intermediates][Sanitation]" )
{
    Soup sp;
    // 2x1 grid of quads.
    const auto a = sp.V( { 0, 0, 0 } ), b = sp.V( { 1, 0, 0 } ), c = sp.V( { 2, 0, 0 } ),
               d = sp.V( { 0, 1, 0 } ), e = sp.V( { 1, 1, 0 } ), f = sp.V( { 2, 1, 0 } );
    sp.F( { a, b, e, d } );
    sp.F( { b, c, f, e } );
    {
        Mesh out;
        CHECK( Sanitation_TryPolygonSoupToMesh( &sp.s, sanitation_policy_t{}, &out.allocator,
                                                &out.m, nullptr ).status ==
               geometry_status_t::OPEN_VOLUME );
    }
    Mesh out;
    sanitation_policy_t p{};
    p.bRequireClosed = false;
    const sanitation_report_t r = Sanitation_TryPolygonSoupToMesh( &sp.s, p, &out.allocator, &out.m, nullptr );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cBoundaryEdges == 6u );
    CHECK( EditableMesh_EdgeCount( &out.m ) == 7u );
    // Every vertex's outgoing half-edge must originate at it even on the rim.
    const mesh_validation_result_t v = MeshValidation_Validate( &out.m );
    CHECK( v.bAllVerticesReferenced );
    CHECK( v.bClosedLoops );
}

TEST_CASE( "Sanitation: defects are reported, never repaired", "[Intermediates][Sanitation]" )
{
    SECTION( "edge shared by three faces" ) {
        Soup sp;
        const auto a = sp.V( { 0, 0, 0 } ), b = sp.V( { 1, 0, 0 } ), c = sp.V( { 0, 1, 0 } ),
                   d = sp.V( { 0, -1, 0 } ), e = sp.V( { 0, 0, 1 } );
        sp.F( { a, b, c } );
        sp.F( { b, a, d } );
        sp.F( { a, b, e } );
        Mesh out;
        sanitation_policy_t p{};
        p.bRequireClosed = false;
        const auto r = Sanitation_TryPolygonSoupToMesh( &sp.s, p, &out.allocator, &out.m, nullptr );
        CHECK( r.status == geometry_status_t::NON_MANIFOLD );
        CHECK( r.fault == sanitation_fault_t::NON_MANIFOLD_EDGE );
        CHECK( r.iVertexA == a );
        CHECK( r.iVertexB == b );
    }
    SECTION( "one face flipped" ) {
        Soup sp;
        for ( const vec3d_t &c : kCube ) { sp.V( c ); }
        for ( int f = 0; f < 6; ++f ) {
            if ( f == 1 ) {
                sp.F( { kCubeFaces[f][3], kCubeFaces[f][2], kCubeFaces[f][1], kCubeFaces[f][0] } );
            } else {
                sp.F( { kCubeFaces[f][0], kCubeFaces[f][1], kCubeFaces[f][2], kCubeFaces[f][3] } );
            }
        }
        Mesh out;
        const auto r = Sanitation_TryPolygonSoupToMesh( &sp.s, sanitation_policy_t{}, &out.allocator,
                                                        &out.m, nullptr );
        CHECK( r.status == geometry_status_t::INVALID_TOPOLOGY );
        CHECK( r.fault == sanitation_fault_t::INCONSISTENT_ORIENTATION );
    }
    SECTION( "bow-tie vertex" ) {
        Soup sp;
        const auto o = sp.V( { 0, 0, 0 } );
        const auto a = sp.V( { 1, 0, 0 } ), b = sp.V( { 1, 1, 0 } );
        const auto c = sp.V( { -1, 0, 0 } ), d = sp.V( { -1, -1, 0 } );
        sp.F( { o, a, b } );
        sp.F( { o, c, d } );
        Mesh out;
        sanitation_policy_t p{};
        p.bRequireClosed = false;
        const auto r = Sanitation_TryPolygonSoupToMesh( &sp.s, p, &out.allocator, &out.m, nullptr );
        CHECK( r.status == geometry_status_t::NON_MANIFOLD );
        CHECK( r.fault == sanitation_fault_t::NON_MANIFOLD_VERTEX );
        CHECK( r.iVertexA == o );
    }
    SECTION( "degenerate face fails unless dropping is requested" ) {
        Soup sp;
        sp.IndexedCube();
        const auto x = sp.V( { 5, 0, 0 } ), y = sp.V( { 6, 0, 0 } ), z = sp.V( { 7, 0, 0 } );
        sp.F( { x, y, z } ); // collinear
        Mesh out;
        auto r = Sanitation_TryPolygonSoupToMesh( &sp.s, sanitation_policy_t{}, &out.allocator,
                                                  &out.m, nullptr );
        CHECK( r.status == geometry_status_t::DEGENERATE );
        CHECK( r.iFace == 6u );
        sanitation_policy_t p{};
        p.bDropDegenerateFaces = true;
        Mesh out2;
        common::vector_t<geometry_mesh_face_handle_t> map{};
        REQUIRE( common::Vector_Init( &map, &out2.allocator ) );
        r = Sanitation_TryPolygonSoupToMesh( &sp.s, p, &out2.allocator, &out2.m, &map );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cDroppedFaces == 1u );
        CHECK( r.cUnreferencedVertices == 3u );
        CHECK_FALSE( GeometryHandle_IsValid( map.pData[6] ) );
        RequireValidClosed( &out2.m );
        common::Vector_Shutdown( &map );
    }
    SECTION( "bad input index" ) {
        Soup sp;
        sp.V( { 0, 0, 0 } );
        sp.F( { 0, 1, 2 } );
        Mesh out;
        const auto r = Sanitation_TryPolygonSoupToMesh( &sp.s, sanitation_policy_t{}, &out.allocator,
                                                        &out.m, nullptr );
        CHECK( r.fault == sanitation_fault_t::INVALID_INPUT );
    }
}

TEST_CASE( "Sanitation: disconnected components become shells", "[Intermediates][Sanitation]" )
{
    Soup sp;
    sp.IndexedCube();
    sp.IndexedCube( { 3, 0, 0 } );
    Mesh out;
    const auto r = Sanitation_TryPolygonSoupToMesh( &sp.s, sanitation_policy_t{}, &out.allocator,
                                                    &out.m, nullptr );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cShells == 2u );
    CHECK( EditableMesh_ShellCount( &out.m ) == 2u );
    CHECK( EditableMesh_SignedVolume( &out.m ) == Approx( 2.0 ) );
    RequireValidClosed( &out.m );
}

TEST_CASE( "Sanitation: mesh -> soup -> mesh round trip", "[Intermediates][Sanitation]" )
{
    Soup sp;
    sp.IndexedCube();
    Mesh a;
    REQUIRE( Sanitation_TryPolygonSoupToMesh( &sp.s, sanitation_policy_t{}, &a.allocator, &a.m,
                                              nullptr ).status == geometry_status_t::OK );
    Soup exported;
    REQUIRE( Sanitation_TryMeshToPolygonSoup( &a.m, &exported.s ) == geometry_status_t::OK );
    CHECK( PolygonSoup_VertexCount( &exported.s ) == 8u );
    CHECK( PolygonSoup_FaceCount( &exported.s ) == 6u );
    Mesh b;
    REQUIRE( Sanitation_TryPolygonSoupToMesh( &exported.s, sanitation_policy_t{}, &b.allocator, &b.m,
                                              nullptr ).status == geometry_status_t::OK );
    CHECK( EditableMesh_SignedVolume( &b.m ) == Approx( EditableMesh_SignedVolume( &a.m ) ) );
    RequireValidClosed( &b.m );

    // Export requires an empty soup.
    CHECK( Sanitation_TryMeshToPolygonSoup( &a.m, &exported.s ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Sanitation: argument checks", "[Intermediates][Sanitation]" )
{
    Soup sp;
    sp.IndexedCube();
    Mesh out;
    CHECK( Sanitation_TryPolygonSoupToMesh( nullptr, {}, &out.allocator, &out.m, nullptr ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    sanitation_policy_t bad{};
    bad.fWeldDistance = -1.0;
    CHECK( Sanitation_TryPolygonSoupToMesh( &sp.s, bad, &out.allocator, &out.m, nullptr ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( Sanitation_TryPolygonSoupToMesh( &sp.s, {}, &out.allocator, &out.m, nullptr ).status ==
             geometry_status_t::OK );
    CHECK( Sanitation_TryPolygonSoupToMesh( &sp.s, {}, &out.allocator, &out.m, nullptr ).status ==
           geometry_status_t::ALREADY_INITIALIZED );
}

} // namespace cypher::editor::geometry
