//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSweep_Tests.cpp
//  Purpose: Contract tests for the sweep tool.
//  Details: Oracles: a straight sweep of a unit face by (0, 0, 2) in two
//           segments is two unit prisms; an S-bend moves the face parallel
//           to itself, so every segment is a prism between parallel planes
//           and the total is area x (offset along the normal) exactly,
//           however far it bends sideways; an arc is bounded by Pappus'
//           volume (chords cut slightly inside the circular path). The far
//           cap's texture must continue: every point of a segment's end cap
//           projects to the UV of the original face point it came from.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushSweep.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushShapes.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherMath_UV.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Env {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 100 } };
    brush_source_t box{};
    brush_source_t out[64]{};
    Env() {
        brush_solid_t solid{};
        REQUIRE( BrushShapes_TryMakeCuboid( &solid, &allocator, policy, &ids,
                                            brush_shape_box_t{ Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) } ) == geometry_status_t::OK );
        REQUIRE( BrushSource_TryBuildDefault( &solid, &allocator, policy, &box ) == geometry_status_t::OK );
        BrushSolid_Shutdown( &solid );
        // A distinctive projection on every side.
        for ( common::usize i = 0; i < box.solid.sides.nCount; ++i ) {
            geometry_brush_side_attributes_t rec{};
            rec.material.value = 50u + i;
            const vec3d_t n = box.solid.sides.pData[i].plane.normal;
            REQUIRE( math::Uvd_TryBuildPlanarMapping( Vec3d_Make( 0.3, 0.1, 0.7 ), n,
                                                      std::fabs( n.z ) > 0.9 ? Vec3d_Make( 0, 1, 0 ) : Vec3d_Make( 0, 0, 1 ), { 0.25, 0.5 },
                                                      0.4, { 0.3, 0.6 }, 1e-12, &rec.uvProjection ) );
            REQUIRE( BrushSideAttributeStore_TrySet( &box.attributes, policy.numerical, box.solid.sides.pData[i].iAttributeIndex, rec ) ==
                     geometry_status_t::OK );
        }
    }
    ~Env() {
        BrushSource_Shutdown( &box );
        for ( auto &s : out ) { BrushSource_Shutdown( &s ); }
    }
    common::u32 Side( vec3d_t n ) const {
        for ( common::u32 i = 0; i < box.solid.sides.nCount; ++i ) {
            if ( math::Vec3d_Dot( box.solid.sides.pData[i].plane.normal, n ) > 0.999 ) { return i; }
        }
        return 0;
    }
    geometry_status_t Sweep( vec3d_t faceNormal, const brush_sweep_params_t &p, common::u32 *pCount ) {
        const brush_sweep_face_t f{ &box, Side( faceNormal ) };
        return BrushSweep_TryBuild( common::span_t<const brush_sweep_face_t>{ &f, 1 }, p, &allocator, policy, &ids, out, 64, pCount );
    }
    double Volume( const brush_solid_t &b ) {
        REQUIRE( BrushValidation_Deep( &b, policy, &allocator ).status == geometry_status_t::OK );
        brush_boundary_t bd{};
        REQUIRE( BrushBoundary_Init( &bd, &allocator ) == geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct( &bd, &b, policy ) == geometry_status_t::OK );
        double v = 0.0;
        for ( common::usize f = 0; f < BrushBoundary_FaceCount( &bd ); ++f ) {
            common::u32 idx[64];
            common::usize n = 0;
            REQUIRE( BrushBoundary_TryGetFaceVertexIndices( &bd, f, idx, 64, &n ) == geometry_status_t::OK );
            for ( common::usize k = 1; k + 1 < n; ++k ) {
                v += math::Vec3d_Dot( bd.vertices.pData[idx[0]], math::Vec3d_Cross( bd.vertices.pData[idx[k]], bd.vertices.pData[idx[k + 1]] ) ) / 6.0;
            }
        }
        BrushBoundary_Shutdown( &bd );
        return v;
    }
};

} // namespace

TEST_CASE( "Straight sweep: prisms with continuing caps", "[geometry][brushsweep]" ) {
    Env e;
    brush_sweep_params_t p{};
    p.offset = Vec3d_Make( 0, 0, 2 );
    p.cSegments = 2;
    common::u32 n = 0;
    REQUIRE( e.Sweep( Vec3d_Make( 0, 0, 1 ), p, &n ) == geometry_status_t::OK );
    REQUIRE( n == 2u );
    for ( common::u32 i = 0; i < n; ++i ) {
        CHECK( e.Volume( e.out[i].solid ) == Approx( 1.0 ) );
        CHECK( BrushSource_Validate( &e.out[i], e.policy ).status == geometry_status_t::OK );
    }
    // The last segment's top cap (z = 3) carries the top face's surface moved
    // up by 2: the point above a top-face point projects to the same UV.
    const brush_source_t &last = e.out[1];
    geometry_brush_side_attributes_t top{}, orig{};
    REQUIRE( BrushSideAttributeStore_TryGet( &e.box.attributes, e.box.solid.sides.pData[e.Side( Vec3d_Make( 0, 0, 1 ) )].iAttributeIndex,
                                             &orig ) == geometry_status_t::OK );
    bool bFound = false;
    for ( common::usize i = 0; i < last.solid.sides.nCount; ++i ) {
        if ( last.solid.sides.pData[i].plane.normal.z > 0.999 ) {
            REQUIRE( BrushSideAttributeStore_TryGet( &last.attributes, last.solid.sides.pData[i].iAttributeIndex, &top ) ==
                     geometry_status_t::OK );
            bFound = true;
        }
    }
    REQUIRE( bFound );
    CHECK( top.material.value == orig.material.value );
    for ( const vec3d_t p0 : { Vec3d_Make( 0, 0, 1 ), Vec3d_Make( 1, 0, 1 ), Vec3d_Make( 0.3, 0.8, 1 ) } ) {
        math::vec2d_t a{}, b{};
        REQUIRE( math::Uvd_TryProjectPlanarPoint( orig.uvProjection, p0, 1e-12, &a ) );
        REQUIRE( math::Uvd_TryProjectPlanarPoint( top.uvProjection, math::Vec3d_Add( p0, Vec3d_Make( 0, 0, 2 ) ), 1e-12, &b ) );
        CHECK( b.x == Approx( a.x ).margin( 1e-12 ) );
        CHECK( b.y == Approx( a.y ).margin( 1e-12 ) );
    }
    // Walls face their own normals.
    for ( common::usize i = 0; i < last.solid.sides.nCount; ++i ) {
        const vec3d_t nrm = last.solid.sides.pData[i].plane.normal;
        if ( std::fabs( nrm.z ) > 0.5 ) { continue; }
        geometry_brush_side_attributes_t wall{};
        REQUIRE( BrushSideAttributeStore_TryGet( &last.attributes, last.solid.sides.pData[i].iAttributeIndex, &wall ) == geometry_status_t::OK );
        CHECK( math::Vec3d_Dot( wall.uvProjection.normal, nrm ) == Approx( 1.0 ) );
        CHECK( wall.material.value == orig.material.value );
    }
}

TEST_CASE( "S-bend and arc sweeps", "[geometry][brushsweep]" ) {
    SECTION( "S-bend keeps exact volume" ) {
        Env e;
        brush_sweep_params_t p{};
        p.path = brush_sweep_path_t::S_BEND;
        p.offset = Vec3d_Make( 0, 3, 2 ); // 2 along the normal, 3 sideways
        p.cSegments = 6;
        common::u32 n = 0;
        REQUIRE( e.Sweep( Vec3d_Make( 0, 0, 1 ), p, &n ) == geometry_status_t::OK );
        REQUIRE( n == 6u );
        double v = 0.0;
        for ( common::u32 i = 0; i < n; ++i ) { v += e.Volume( e.out[i].solid ); }
        CHECK( v == Approx( 2.0 ) );
    }
    SECTION( "Arc approaches Pappus from below" ) {
        Env e;
        brush_sweep_params_t p{};
        p.path = brush_sweep_path_t::ARC;
        // The +X face (x = 1) turned 90 degrees about a vertical axis at
        // (1, -1): its centroid circles at radius 1.5.
        p.axisOrigin = Vec3d_Make( 1, -1, 0 );
        p.axisDirection = Vec3d_Make( 0, 0, 1 );
        p.angleRadians = 0.5 * kPi;
        p.cSegments = 8;
        common::u32 n = 0;
        REQUIRE( e.Sweep( Vec3d_Make( 1, 0, 0 ), p, &n ) == geometry_status_t::OK );
        REQUIRE( n == 8u );
        double v = 0.0;
        for ( common::u32 i = 0; i < n; ++i ) { v += e.Volume( e.out[i].solid ); }
        const double pappus = 1.0 * 1.5 * 0.5 * kPi;
        CHECK( v < pappus );
        CHECK( v > 0.99 * pappus );
    }
    SECTION( "Iterations continue from the previous cap" ) {
        Env e;
        brush_sweep_params_t p{};
        p.offset = Vec3d_Make( 0, 0, 1 );
        p.cSegments = 1;
        p.cIterations = 3;
        common::u32 n = 0;
        REQUIRE( e.Sweep( Vec3d_Make( 0, 0, 1 ), p, &n ) == geometry_status_t::OK );
        REQUIRE( n == 3u );
        double v = 0.0;
        for ( common::u32 i = 0; i < n; ++i ) { v += e.Volume( e.out[i].solid ); }
        CHECK( v == Approx( 3.0 ) );
    }
}

TEST_CASE( "Sweep refusals leave outputs and allocator untouched", "[geometry][brushsweep]" ) {
    Env e;
    const geometry_source_id_allocator_t before = e.ids;
    brush_sweep_params_t p{};
    common::u32 n = 0;
    p.offset = Vec3d_Make( 0, 0, 0 );
    CHECK( e.Sweep( Vec3d_Make( 0, 0, 1 ), p, &n ) != geometry_status_t::OK ); // no volume
    p.offset = Vec3d_Make( 0, 0, 1 );
    p.cSegments = 0;
    CHECK( e.Sweep( Vec3d_Make( 0, 0, 1 ), p, &n ) == geometry_status_t::INVALID_ARGUMENT );
    p.cSegments = 100;
    CHECK( e.Sweep( Vec3d_Make( 0, 0, 1 ), p, &n ) == geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( n == 100u );
    p.cSegments = 1;
    p.path = brush_sweep_path_t::ARC;
    p.axisDirection = Vec3d_Make( 0, 0, 0 );
    p.angleRadians = 1.0;
    CHECK( e.Sweep( Vec3d_Make( 0, 0, 1 ), p, &n ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( e.ids.next.value == before.next.value );
    for ( const auto &s : e.out ) { CHECK( s.solid.sides.pData == nullptr ); }
}

} // namespace cypher::editor::geometry
