//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Repair_Tests.cpp
//  Purpose: Contract tests for explicit soup/mesh repair.
//  Details: Each fixture is built to exhibit exactly one defect, the plan
//           enables exactly the matching step, and the result must pass
//           Sanitation (closed where the fixture should end up closed).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Repair.h"
#include "CypherGeometry_MeshValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec3d_t;

namespace {

const vec3d_t kCube[8] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 },
                           { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } };
const common::u32 kCubeFaces[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
                                       { 2, 3, 7, 6 }, { 1, 2, 6, 5 }, { 0, 4, 7, 3 } };

struct Soup {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    polygon_soup_t s{};
    Soup() { REQUIRE( PolygonSoup_Init( &s, &allocator ) == geometry_status_t::OK ); }
    ~Soup() { PolygonSoup_Shutdown( &s ); }
    void V( vec3d_t p ) { REQUIRE( PolygonSoup_TryAddVertex( &s, p, nullptr ) == geometry_status_t::OK ); }
    void F( std::vector<common::u32> idx, common::u64 id = 0 ) {
        REQUIRE( PolygonSoup_TryAddFace( &s, common::span_t<const common::u32>{ idx.data(), idx.size() },
                                         geometry_source_id_t{ id }, 0u, nullptr ) == geometry_status_t::OK );
    }
    // Cube with per-face control: flip[f] reverses face f, skip[f] omits it.
    void Cube( std::vector<int> flip = {}, std::vector<int> skip = {} ) {
        for ( const vec3d_t &c : kCube ) { V( c ); }
        for ( int f = 0; f < 6; ++f ) {
            if ( std::find( skip.begin(), skip.end(), f ) != skip.end() ) { continue; }
            std::vector<common::u32> q( kCubeFaces[f], kCubeFaces[f] + 4 );
            if ( std::find( flip.begin(), flip.end(), f ) != flip.end() ) { std::reverse( q.begin(), q.end() ); }
            F( q, static_cast<common::u64>( 10 + f ) );
        }
    }
};

// Repairs `in` with `plan`, then sanitizes; returns both reports.
struct Run {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    polygon_soup_t out{};
    editable_mesh_t mesh{};
    repair_report_t rep{};
    sanitation_report_t san{};
    Run( const polygon_soup_t *in, const repair_plan_t &plan, bool requireClosed = true ) {
        REQUIRE( PolygonSoup_Init( &out, &allocator ) == geometry_status_t::OK );
        rep = Repair_TryRepairSoup( in, plan, &out );
        if ( rep.status == geometry_status_t::OK ) {
            sanitation_policy_t p{};
            p.bRequireClosed = requireClosed;
            san = Sanitation_TryPolygonSoupToMesh( &out, p, &allocator, &mesh, nullptr );
        }
    }
    ~Run() {
        EditableMesh_Shutdown( &mesh );
        PolygonSoup_Shutdown( &out );
    }
};

} // namespace

TEST_CASE( "Repair: empty plan is the identity", "[Repair]" )
{
    Soup in;
    in.Cube();
    Run r( &in.s, repair_plan_t{} );
    REQUIRE( r.rep.status == geometry_status_t::OK );
    CHECK( PolygonSoup_FaceCount( &r.out ) == 6u );
    CHECK( r.rep.cFacesFlipped == 0u );
    CHECK( r.san.status == geometry_status_t::OK );
    CHECK( r.out.faces.pData[3].sourceId.value == 13u ); // provenance kept
}

TEST_CASE( "Repair: one flipped face is re-oriented", "[Repair]" )
{
    Soup in;
    in.Cube( { 2 } );
    {
        // Without repair Sanitation refuses the soup.
        Run raw( &in.s, repair_plan_t{} );
        CHECK( raw.san.fault == sanitation_fault_t::INCONSISTENT_ORIENTATION );
    }
    repair_plan_t plan{};
    plan.bOrientConsistently = true;
    Run r( &in.s, plan );
    REQUIRE( r.rep.status == geometry_status_t::OK );
    CHECK( r.rep.cFacesFlipped == 1u );
    CHECK( r.rep.cComponents == 1u );
    REQUIRE( r.san.status == geometry_status_t::OK );
    CHECK( EditableMesh_SignedVolume( &r.mesh ) == Approx( 1.0 ) );
}

TEST_CASE( "Repair: inside-out cube is turned outward", "[Repair]" )
{
    Soup in;
    in.Cube( { 0, 1, 2, 3, 4, 5 } );
    repair_plan_t plan{};
    plan.bOrientConsistently = true;
    Run r( &in.s, plan );
    REQUIRE( r.san.status == geometry_status_t::OK );
    CHECK( r.rep.cFacesFlipped == 6u );
    CHECK( EditableMesh_SignedVolume( &r.mesh ) == Approx( 1.0 ) );
}

TEST_CASE( "Repair: weld closes an importer-split cube", "[Repair]" )
{
    Soup in;
    for ( int f = 0; f < 6; ++f ) {
        std::vector<common::u32> q;
        for ( int k = 0; k < 4; ++k ) {
            q.push_back( static_cast<common::u32>( PolygonSoup_VertexCount( &in.s ) ) );
            in.V( kCube[kCubeFaces[f][k]] );
        }
        in.F( q );
    }
    repair_plan_t plan{};
    plan.bWeld = true;
    Run r( &in.s, plan );
    CHECK( r.rep.cVerticesWelded == 16u );
    REQUIRE( r.san.status == geometry_status_t::OK );
    CHECK( EditableMesh_VertexCount( &r.mesh ) == 8u );
}

TEST_CASE( "Repair: planar hole is capped, warped hole is left open", "[Repair]" )
{
    SECTION( "missing top face" ) {
        Soup in;
        in.Cube( {}, { 1 } );
        repair_plan_t plan{};
        plan.bFillHoles = true;
        Run r( &in.s, plan );
        REQUIRE( r.rep.status == geometry_status_t::OK );
        CHECK( r.rep.cHolesFilled == 1u );
        CHECK( r.rep.cCapTriangles == 2u );
        REQUIRE( r.san.status == geometry_status_t::OK );
        CHECK( EditableMesh_SignedVolume( &r.mesh ) == Approx( 1.0 ) );
        const mesh_validation_result_t v = MeshValidation_Validate( &r.mesh );
        CHECK( v.bEulerValid );
        CHECK( v.bConsistentWinding );
    }
    SECTION( "missing top face with a raised corner" ) {
        Soup in;
        in.Cube( {}, { 1 } );
        in.s.positions.pData[6].z = 1.3; // top ring no longer planar
        repair_plan_t plan{};
        plan.bFillHoles = true;
        Run r( &in.s, plan, false );
        REQUIRE( r.rep.status == geometry_status_t::OK );
        CHECK( r.rep.cHolesFilled == 0u );
        CHECK( r.rep.cHolesLeftOpen == 1u );
        CHECK( r.san.cBoundaryEdges == 4u );
    }
}

TEST_CASE( "Repair: duplicate and degenerate faces are removed", "[Repair]" )
{
    Soup in;
    in.Cube();
    in.F( { 7, 6, 5, 4 } );    // duplicate of the top face, reversed and rotated
    in.V( { 5, 5, 5 } );       // vertices of a collinear sliver
    in.V( { 6, 5, 5 } );
    in.V( { 7, 5, 5 } );
    in.F( { 8, 9, 10 } );
    repair_plan_t plan{};
    plan.bRemoveDuplicateFaces = true;
    plan.bRemoveDegenerateFaces = true;
    Run r( &in.s, plan );
    CHECK( r.rep.cDuplicateFacesRemoved == 1u );
    CHECK( r.rep.cDegenerateFacesRemoved == 1u );
    CHECK( PolygonSoup_FaceCount( &r.out ) == 6u );
    REQUIRE( r.san.status == geometry_status_t::OK );
    CHECK( r.san.cUnreferencedVertices == 3u );
}

TEST_CASE( "Repair: Möbius strip is reported non-orientable", "[Repair]" )
{
    // 4-segment strip whose end is glued back with a half twist.
    Soup in;
    const common::u32 n = 4u;
    for ( common::u32 i = 0u; i < n; ++i ) {
        const double a = 2.0 * 3.14159265358979 * i / n;
        in.V( { std::cos( a ) * 3, std::sin( a ) * 3, -1 } );
        in.V( { std::cos( a ) * 3, std::sin( a ) * 3, 1 } );
    }
    for ( common::u32 i = 0u; i < n - 1u; ++i ) {
        const common::u32 b0 = 2u * i;
        const common::u32 t0 = b0 + 1u;
        const common::u32 b1 = b0 + 2u;
        const common::u32 t1 = b0 + 3u;
        in.F( { b0, b1, t1, t0 } );
    }
    in.F( { 2u * ( n - 1u ), 1u, 0u, 2u * ( n - 1u ) + 1u } ); // twist: bottom meets top
    repair_plan_t plan{};
    plan.bOrientConsistently = true;
    Run r( &in.s, plan, false );
    REQUIRE( r.rep.status == geometry_status_t::OK );
    CHECK( r.rep.cNonOrientableComponents == 1u );
    CHECK( r.rep.cFacesFlipped == 0u );
}

TEST_CASE( "Repair: open surface keeps its majority orientation", "[Repair]" )
{
    Soup in;
    for ( int y = 0; y <= 1; ++y ) {
        for ( int x = 0; x <= 3; ++x ) { in.V( { double( x ), double( y ), 0 } ); }
    }
    in.F( { 0, 1, 5, 4 } );
    in.F( { 1, 2, 6, 5 } );
    in.F( { 3, 2, 6, 7 } ); // the odd one out: clockwise seen from +Z
    repair_plan_t plan{};
    plan.bOrientConsistently = true;
    Run r( &in.s, plan, false );
    REQUIRE( r.san.status == geometry_status_t::OK );
    CHECK( r.rep.cFacesFlipped == 1u );
}

TEST_CASE( "Repair: mesh preview leaves the input untouched", "[Repair]" )
{
    Soup in;
    in.Cube( {}, { 4 } );
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    editable_mesh_t open{};
    sanitation_policy_t openPolicy{};
    openPolicy.bRequireClosed = false;
    REQUIRE( Sanitation_TryPolygonSoupToMesh( &in.s, openPolicy, &allocator, &open, nullptr ).status ==
             geometry_status_t::OK );
    const common::usize facesBefore = EditableMesh_FaceCount( &open );

    repair_plan_t plan{};
    plan.bFillHoles = true;
    editable_mesh_t repaired{};
    sanitation_report_t sr{};
    const repair_report_t rr = Repair_TryPreviewMesh( &open, plan, sanitation_policy_t{}, &allocator, &repaired, &sr );
    REQUIRE( rr.status == geometry_status_t::OK );
    CHECK( rr.cHolesFilled == 1u );
    CHECK( sr.cBoundaryEdges == 0u );
    CHECK( EditableMesh_SignedVolume( &repaired ) == Approx( 1.0 ) );
    CHECK( EditableMesh_FaceCount( &open ) == facesBefore );
    EditableMesh_Shutdown( &repaired );
    EditableMesh_Shutdown( &open );
}

TEST_CASE( "Repair: argument contract", "[Repair]" )
{
    Soup in;
    in.Cube();
    Soup notEmpty;
    notEmpty.V( { 0, 0, 0 } );
    CHECK( Repair_TryRepairSoup( &in.s, {}, &notEmpty.s ).status == geometry_status_t::INVALID_ARGUMENT );
    repair_plan_t bad{};
    bad.fWeldDistance = -1;
    Soup out;
    CHECK( Repair_TryRepairSoup( &in.s, bad, &out.s ).status == geometry_status_t::INVALID_ARGUMENT );
    polygon_soup_t un{};
    CHECK( Repair_TryRepairSoup( &un, {}, &out.s ).status == geometry_status_t::NOT_INITIALIZED );
}

} // namespace cypher::editor::geometry
