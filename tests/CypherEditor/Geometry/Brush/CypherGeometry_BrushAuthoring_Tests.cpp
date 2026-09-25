//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushAuthoring_Tests.cpp
//  Purpose: Contract tests for extrude-split and mirror on authored brushes.
//  Details: A unit box whose six sides carry distinct materials and
//           world-aligned projections. Extrude split must reproduce exactly
//           the volume an extrude would add, give every new side the
//           surface of the side it came from, keep the original bit-identical
//           (outward) or keep the face's identity and surface at the cut
//           (inward). Mirror with texture lock must map every boundary point
//           to the exact UV it had before; without it each side's projection
//           must face its new normal.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushAuthoring.h"
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

struct Env {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 100 } };
};

struct Source {
    brush_source_t s{};
    ~Source() { BrushSource_Shutdown( &s ); }
};

// Box [lo, hi] whose side i has material 10 + i and a world-aligned
// projection facing its own normal.
void MakeBox( Env &e, Source *pOut, vec3d_t lo, vec3d_t hi ) {
    brush_solid_t solid{};
    REQUIRE( BrushShapes_TryMakeCuboid( &solid, &e.allocator, e.policy, &e.ids, brush_shape_box_t{ lo, hi } ) == geometry_status_t::OK );
    REQUIRE( BrushSource_TryBuildDefault( &solid, &e.allocator, e.policy, &pOut->s ) == geometry_status_t::OK );
    BrushSolid_Shutdown( &solid );
    for ( common::usize i = 0; i < pOut->s.solid.sides.nCount; ++i ) {
        const brush_solid_side_t &side = pOut->s.solid.sides.pData[i];
        geometry_brush_side_attributes_t rec{};
        rec.material.value = 10u + i;
        const vec3d_t nrm = side.plane.normal;
        const vec3d_t up = std::fabs( nrm.z ) > 0.9 ? Vec3d_Make( 0, 1, 0 ) : Vec3d_Make( 0, 0, 1 );
        REQUIRE( math::Uvd_TryBuildPlanarMapping( Vec3d_Make( 0.25, 0.5, 0.75 ), nrm, up, { 0.5, 0.25 }, 0.3, { 0.1, 0.2 }, 1e-12,
                                                  &rec.uvProjection ) );
        REQUIRE( BrushSideAttributeStore_TrySet( &pOut->s.attributes, e.policy.numerical, side.iAttributeIndex, rec ) ==
                 geometry_status_t::OK );
    }
}

double Volume( Env &e, const brush_solid_t &b ) {
    REQUIRE( BrushValidation_Deep( &b, e.policy, &e.allocator ).status == geometry_status_t::OK );
    brush_boundary_t bd{};
    REQUIRE( BrushBoundary_Init( &bd, &e.allocator ) == geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &bd, &b, e.policy ) == geometry_status_t::OK );
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

// Side index whose outward normal is (closest to) n.
common::usize SideFacing( const brush_source_t &s, vec3d_t n ) {
    common::usize best = 0;
    double bestDot = -2.0;
    for ( common::usize i = 0; i < s.solid.sides.nCount; ++i ) {
        const double d = math::Vec3d_Dot( s.solid.sides.pData[i].plane.normal, n );
        if ( d > bestDot ) {
            bestDot = d;
            best = i;
        }
    }
    return best;
}

geometry_brush_side_attributes_t Record( const brush_source_t &s, common::usize iSide ) {
    geometry_brush_side_attributes_t rec{};
    REQUIRE( BrushSideAttributeStore_TryGet( &s.attributes, s.solid.sides.pData[iSide].iAttributeIndex, &rec ) == geometry_status_t::OK );
    return rec;
}

} // namespace

TEST_CASE( "Extrude split outward adds exactly the swept volume as a new brush", "[geometry][brushauthoring]" ) {
    Env e;
    Source box, copy, fresh;
    MakeBox( e, &box, Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    REQUIRE( BrushSource_TryClone( &box.s, &e.allocator, e.policy, &copy.s ) == geometry_status_t::OK );
    const common::usize top = SideFacing( box.s, Vec3d_Make( 0, 0, 1 ) );
    REQUIRE( BrushAuthoring_TryExtrudeSplit( &box.s, top, 0.5, &e.allocator, e.policy, &e.ids, &fresh.s ) == geometry_status_t::OK );
    CHECK( BrushSource_Equal( &box.s, &copy.s ) ); // the original is untouched
    CHECK( Volume( e, fresh.s.solid ) == Approx( 0.5 ) );
    CHECK( BrushSource_Validate( &fresh.s, e.policy ).status == geometry_status_t::OK );
    CHECK( fresh.s.solid.sides.nCount == 6u );
    // Surfaces continue: each side of the slab carries the surface of the
    // box side facing the same way; the bottom (internal) face the top's.
    for ( const vec3d_t n : { Vec3d_Make( 1, 0, 0 ), Vec3d_Make( -1, 0, 0 ), Vec3d_Make( 0, 1, 0 ), Vec3d_Make( 0, -1, 0 ),
                              Vec3d_Make( 0, 0, 1 ) } ) {
        CHECK( Record( fresh.s, SideFacing( fresh.s, n ) ).material.value == Record( box.s, SideFacing( box.s, n ) ).material.value );
    }
    CHECK( Record( fresh.s, SideFacing( fresh.s, Vec3d_Make( 0, 0, -1 ) ) ).material.value == Record( box.s, top ).material.value );
    // Fresh identities.
    for ( common::usize i = 0; i < fresh.s.solid.sides.nCount; ++i ) {
        for ( common::usize j = 0; j < box.s.solid.sides.nCount; ++j ) {
            CHECK( fresh.s.solid.sides.pData[i].sourceId.value != box.s.solid.sides.pData[j].sourceId.value );
        }
    }
    CHECK( fresh.s.solid.sourceId.value != box.s.solid.sourceId.value );
}

TEST_CASE( "Extrude split inward cuts the brush and keeps the face's identity", "[geometry][brushauthoring]" ) {
    Env e;
    Source box, slab;
    MakeBox( e, &box, Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    const common::usize top = SideFacing( box.s, Vec3d_Make( 0, 0, 1 ) );
    const geometry_source_id_t topId = box.s.solid.sides.pData[top].sourceId;
    const common::u64 topMaterial = Record( box.s, top ).material.value;
    REQUIRE( BrushAuthoring_TryExtrudeSplit( &box.s, top, -0.25, &e.allocator, e.policy, &e.ids, &slab.s ) == geometry_status_t::OK );
    CHECK( Volume( e, box.s.solid ) == Approx( 0.75 ) );
    CHECK( Volume( e, slab.s.solid ) == Approx( 0.25 ) );
    const common::usize newTop = SideFacing( box.s, Vec3d_Make( 0, 0, 1 ) );
    CHECK( box.s.solid.sides.pData[newTop].sourceId.value == topId.value );
    CHECK( Record( box.s, newTop ).material.value == topMaterial );
    CHECK( box.s.solid.sides.pData[newTop].plane.d == Approx( -0.75 ) );
    CHECK( Record( slab.s, SideFacing( slab.s, Vec3d_Make( 0, 0, -1 ) ) ).material.value == topMaterial );
    CHECK( BrushSource_Validate( &box.s, e.policy ).status == geometry_status_t::OK );
    CHECK( BrushSource_Validate( &slab.s, e.policy ).status == geometry_status_t::OK );
}

TEST_CASE( "Extrude split refusals change nothing", "[geometry][brushauthoring]" ) {
    Env e;
    Source box, copy, out;
    MakeBox( e, &box, Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    REQUIRE( BrushSource_TryClone( &box.s, &e.allocator, e.policy, &copy.s ) == geometry_status_t::OK );
    const geometry_source_id_allocator_t before = e.ids;
    const common::usize top = SideFacing( box.s, Vec3d_Make( 0, 0, 1 ) );
    CHECK( BrushAuthoring_TryExtrudeSplit( &box.s, top, -1.0, &e.allocator, e.policy, &e.ids, &out.s ) != geometry_status_t::OK );
    CHECK( BrushAuthoring_TryExtrudeSplit( &box.s, top, -1.5, &e.allocator, e.policy, &e.ids, &out.s ) != geometry_status_t::OK );
    CHECK( BrushAuthoring_TryExtrudeSplit( &box.s, top, 0.0, &e.allocator, e.policy, &e.ids, &out.s ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushAuthoring_TryExtrudeSplit( &box.s, 99u, 1.0, &e.allocator, e.policy, &e.ids, &out.s ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushAuthoring_TryExtrudeSplit( &box.s, top, std::nan( "" ), &e.allocator, e.policy, &e.ids, &out.s ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( BrushSource_Equal( &box.s, &copy.s ) );
    CHECK( e.ids.next.value == before.next.value );
    CHECK( out.s.solid.sides.pData == nullptr );
}

TEST_CASE( "Mirror reflects the brush; texture lock keeps every UV", "[geometry][brushauthoring]" ) {
    Env e;
    SECTION( "Texture lock" ) {
        Source box, before;
        MakeBox( e, &box, Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 2, 3 ) );
        REQUIRE( BrushSource_TryClone( &box.s, &e.allocator, e.policy, &before.s ) == geometry_status_t::OK );
        const math::planed_t mirror{ Vec3d_Make( 1, 0, 0 ), -5.0 }; // x = 5
        REQUIRE( BrushAuthoring_TryMirror( &box.s, mirror, true, e.policy ) == geometry_status_t::OK );
        CHECK( Volume( e, box.s.solid ) == Approx( 6.0 ) );
        CHECK( BrushSource_Validate( &box.s, e.policy ).status == geometry_status_t::OK );
        // Same sides, same identities; every face's corner projects to the
        // UV its unmirrored twin had.
        brush_boundary_t bd{};
        REQUIRE( BrushBoundary_Init( &bd, &e.allocator ) == geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct( &bd, &before.s.solid, e.policy ) == geometry_status_t::OK );
        for ( common::usize f = 0; f < BrushBoundary_FaceCount( &bd ); ++f ) {
            const common::u32 iSide = bd.faces.pData[f].iSide;
            CHECK( box.s.solid.sides.pData[iSide].sourceId.value == before.s.solid.sides.pData[iSide].sourceId.value );
            common::u32 idx[16];
            common::usize n = 0;
            REQUIRE( BrushBoundary_TryGetFaceVertexIndices( &bd, f, idx, 16, &n ) == geometry_status_t::OK );
            for ( common::usize k = 0; k < n; ++k ) {
                const vec3d_t p = bd.vertices.pData[idx[k]];
                const vec3d_t q = Vec3d_Make( 10.0 - p.x, p.y, p.z );
                math::vec2d_t uvBefore{}, uvAfter{};
                REQUIRE( math::Uvd_TryProjectPlanarPoint( Record( before.s, iSide ).uvProjection, p, 1e-12, &uvBefore ) );
                REQUIRE( math::Uvd_TryProjectPlanarPoint( Record( box.s, iSide ).uvProjection, q, 1e-12, &uvAfter ) );
                CHECK( uvAfter.x == Approx( uvBefore.x ).margin( 1e-12 ) );
                CHECK( uvAfter.y == Approx( uvBefore.y ).margin( 1e-12 ) );
            }
        }
        BrushBoundary_Shutdown( &bd );
        // The mirrored box occupies x in [9, 10].
        const common::usize px = SideFacing( box.s, Vec3d_Make( 1, 0, 0 ) ), nx = SideFacing( box.s, Vec3d_Make( -1, 0, 0 ) );
        CHECK( box.s.solid.sides.pData[px].plane.d == Approx( -10.0 ) );
        CHECK( box.s.solid.sides.pData[nx].plane.d == Approx( 9.0 ) );
    }
    SECTION( "Without texture lock: projections face the new sides" ) {
        // An asymmetric cone, so a wrong reflection would show in the volume
        // or the projections.
        Source wedge;
        brush_solid_t solid{};
        REQUIRE( BrushShapes_TryMakeCone( &solid, &e.allocator, e.policy, &e.ids,
                                          brush_shape_box_t{ Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 2, 1, 3 ) }, 2u, 8u,
                                          brush_circle_mode_t::VERTEX_ALIGNED ) == geometry_status_t::OK );
        REQUIRE( BrushSource_TryBuildDefault( &solid, &e.allocator, e.policy, &wedge.s ) == geometry_status_t::OK );
        BrushSolid_Shutdown( &solid );
        const double v0 = Volume( e, wedge.s.solid );
        // Mirror across a slanted plane.
        const double k = 1.0 / std::sqrt( 3.0 );
        REQUIRE( BrushAuthoring_TryMirror( &wedge.s, math::planed_t{ Vec3d_Make( k, k, k ), -1.0 }, false, e.policy ) ==
                 geometry_status_t::OK );
        CHECK( Volume( e, wedge.s.solid ) == Approx( v0 ) );
        for ( common::usize i = 0; i < wedge.s.solid.sides.nCount; ++i ) {
            const geometry_brush_side_attributes_t rec = Record( wedge.s, i );
            CHECK( math::Vec3d_Dot( rec.uvProjection.normal, wedge.s.solid.sides.pData[i].plane.normal ) == Approx( 1.0 ) );
        }
        CHECK( BrushSource_Validate( &wedge.s, e.policy ).status == geometry_status_t::OK );
    }
    SECTION( "Mirroring twice restores the planes" ) {
        Source box, before;
        MakeBox( e, &box, Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 2, 3 ) );
        REQUIRE( BrushSource_TryClone( &box.s, &e.allocator, e.policy, &before.s ) == geometry_status_t::OK );
        const math::planed_t mirror{ Vec3d_Make( 0, 0, 1 ), -0.5 };
        REQUIRE( BrushAuthoring_TryMirror( &box.s, mirror, true, e.policy ) == geometry_status_t::OK );
        REQUIRE( BrushAuthoring_TryMirror( &box.s, mirror, true, e.policy ) == geometry_status_t::OK );
        for ( common::usize i = 0; i < box.s.solid.sides.nCount; ++i ) {
            CHECK( box.s.solid.sides.pData[i].plane.d == Approx( before.s.solid.sides.pData[i].plane.d ).margin( 1e-12 ) );
            CHECK( Record( box.s, i ).material.value == Record( before.s, i ).material.value );
        }
        CHECK( BrushAuthoring_TryMirror( &box.s, math::planed_t{ Vec3d_Make( 2, 0, 0 ), 0.0 }, true, e.policy ) ==
               geometry_status_t::INVALID_ARGUMENT ); // not a unit normal
    }
}

} // namespace cypher::editor::geometry
