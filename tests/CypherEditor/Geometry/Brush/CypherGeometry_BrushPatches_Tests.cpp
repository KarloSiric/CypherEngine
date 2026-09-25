//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushPatches_Tests.cpp
//  Purpose: Contract tests for creating patches from brush faces and for
//           shrinking a patch's control grid.
//  Details: A quad face becomes one patch lying exactly on the face, facing
//           out, with the face's UV at every point. A pentagon becomes five
//           patches tiling it: their areas sum to the face's. Removing a
//           sub-patch column right after inserting one restores the controls
//           exactly; removing a genuinely curved span keeps the outer
//           columns exactly and the surface close to its old shape.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushPatches.h"
#include "CypherGeometry_BrushShapes.h"
#include "CypherGeometry_PatchResize.h"
#include "CypherMath_UV.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

struct Env {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 100 } };
    brush_source_t src{};
    patch_surface_t out[16]{};
    ~Env() {
        BrushSource_Shutdown( &src );
        for ( auto &p : out ) { Patch_Shutdown( &p ); }
    }
    void Load( brush_solid_t *pSolid, common::u64 material ) {
        REQUIRE( BrushSource_TryBuildDefault( pSolid, &allocator, policy, &src ) == geometry_status_t::OK );
        BrushSolid_Shutdown( pSolid );
        for ( common::usize i = 0; i < src.solid.sides.nCount; ++i ) {
            geometry_brush_side_attributes_t rec{};
            rec.material.value = material;
            const vec3d_t n = src.solid.sides.pData[i].plane.normal;
            REQUIRE( math::Uvd_TryBuildPlanarMapping( Vec3d_Make( 0.2, 0.3, 0.4 ), n,
                                                      std::fabs( n.z ) > 0.9 ? Vec3d_Make( 0, 1, 0 ) : Vec3d_Make( 0, 0, 1 ), { 0.5, 0.25 },
                                                      0.3, { 0.1, 0.7 }, 1e-12, &rec.uvProjection ) );
            REQUIRE( BrushSideAttributeStore_TrySet( &src.attributes, policy.numerical, src.solid.sides.pData[i].iAttributeIndex, rec ) ==
                     geometry_status_t::OK );
        }
    }
    common::u32 Side( vec3d_t n ) const {
        for ( common::u32 i = 0; i < src.solid.sides.nCount; ++i ) {
            if ( math::Vec3d_Dot( src.solid.sides.pData[i].plane.normal, n ) > 0.999 ) { return i; }
        }
        return 0;
    }
    math::planar_uv_mappingd_t Projection( common::u32 iSide ) const {
        geometry_brush_side_attributes_t rec{};
        REQUIRE( BrushSideAttributeStore_TryGet( &src.attributes, src.solid.sides.pData[iSide].iAttributeIndex, &rec ) == geometry_status_t::OK );
        return rec.uvProjection;
    }
};

double QuadArea( const patch_surface_t &p ) {
    // Corners of a flat quad patch.
    const vec3d_t a = Patch_Control( &p, 0, 0 )->position, b = Patch_Control( &p, p.cColumns - 1, 0 )->position;
    const vec3d_t c = Patch_Control( &p, p.cColumns - 1, p.cRows - 1 )->position, d = Patch_Control( &p, 0, p.cRows - 1 )->position;
    const vec3d_t n1 = math::Vec3d_Cross( math::Vec3d_Subtract( b, a ), math::Vec3d_Subtract( c, a ) );
    const vec3d_t n2 = math::Vec3d_Cross( math::Vec3d_Subtract( c, a ), math::Vec3d_Subtract( d, a ) );
    return 0.5 * ( std::sqrt( math::Vec3d_LengthSquared( n1 ) ) + std::sqrt( math::Vec3d_LengthSquared( n2 ) ) );
}

} // namespace

TEST_CASE( "A quad face becomes one flat patch with the face's texture", "[geometry][brushpatches]" ) {
    for ( patch_basis_t basis : { patch_basis_t::BIQUADRATIC_BEZIER, patch_basis_t::BICUBIC_BEZIER } ) {
        CAPTURE( static_cast<int>( basis ) );
        Env e;
        brush_solid_t solid{};
        REQUIRE( BrushShapes_TryMakeCuboid( &solid, &e.allocator, e.policy, &e.ids,
                                            brush_shape_box_t{ Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 2, 1, 1 ) } ) == geometry_status_t::OK );
        e.Load( &solid, 7u );
        const common::u32 top = e.Side( Vec3d_Make( 0, 0, 1 ) );
        common::u32 n = 0;
        REQUIRE( BrushPatches_TryFromFace( &e.src, top, basis, &e.allocator, e.policy, &e.ids, e.out, 16, &n ) == geometry_status_t::OK );
        REQUIRE( n == 1u );
        const patch_surface_t &p = e.out[0];
        CHECK( p.materialId == 7u );
        CHECK( QuadArea( p ) == Approx( 2.0 ) );
        const math::planar_uv_mappingd_t uv = e.Projection( top );
        for ( double u : { 0.0, 0.3, 0.5, 1.0 } ) {
            for ( double v : { 0.0, 0.7, 1.0 } ) {
                const patch_sample_t s = Patch_Evaluate( &p, u, v );
                CHECK( s.position.z == Approx( 1.0 ) );
                CHECK( s.normal.z == Approx( 1.0 ) );
                math::vec2d_t want{};
                REQUIRE( math::Uvd_TryProjectPlanarPoint( uv, s.position, 0.0, &want ) );
                CHECK( s.uv.x == Approx( want.x ).margin( 1e-12 ) );
                CHECK( s.uv.y == Approx( want.y ).margin( 1e-12 ) );
            }
        }
    }
}

TEST_CASE( "A pentagon face becomes five patches tiling it", "[geometry][brushpatches]" ) {
    Env e;
    brush_solid_t solid{};
    REQUIRE( BrushShapes_TryMakeCylinder( &solid, &e.allocator, e.policy, &e.ids,
                                          brush_shape_box_t{ Vec3d_Make( -1, -1, 0 ), Vec3d_Make( 1, 1, 1 ) }, 2u, 5u,
                                          brush_circle_mode_t::VERTEX_ALIGNED ) == geometry_status_t::OK );
    e.Load( &solid, 3u );
    common::u32 n = 0;
    REQUIRE( BrushPatches_TryFromFace( &e.src, e.Side( Vec3d_Make( 0, 0, 1 ) ), patch_basis_t::BIQUADRATIC_BEZIER, &e.allocator, e.policy,
                                       &e.ids, e.out, 16, &n ) == geometry_status_t::OK );
    REQUIRE( n == 5u );
    double area = 0.0;
    for ( common::u32 i = 0; i < n; ++i ) {
        area += QuadArea( e.out[i] );
        CHECK( Patch_Evaluate( &e.out[i], 0.5, 0.5 ).normal.z == Approx( 1.0 ) );
    }
    const double pentagon = 0.5 * 5.0 * std::sin( 2.0 * 3.14159265358979323846 / 5.0 );
    CHECK( area == Approx( pentagon ) );
    // Capacity and material width.
    patch_surface_t few[2]{};
    const geometry_source_id_allocator_t before = e.ids;
    CHECK( BrushPatches_TryFromFace( &e.src, e.Side( Vec3d_Make( 0, 0, 1 ) ), patch_basis_t::BIQUADRATIC_BEZIER, &e.allocator, e.policy,
                                     &e.ids, few, 2, &n ) == geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( n == 5u );
    CHECK( e.ids.next.value == before.next.value );
}

TEST_CASE( "Shrinking a patch grid", "[geometry][brushpatches]" ) {
    for ( patch_basis_t basis : { patch_basis_t::BIQUADRATIC_BEZIER, patch_basis_t::BICUBIC_BEZIER } ) {
        CAPTURE( static_cast<int>( basis ) );
        Env e;
        const common::u32 d = Patch_Degree( basis );
        patch_surface_t p{};
        REQUIRE( Patch_TryInitFlat( &p, &e.allocator, basis, d + 1, d + 1, Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 4, 0, 0 ),
                                    Vec3d_Make( 0, 2, 0 ), geometry_source_id_t{ 5000 }, &e.ids ) == geometry_status_t::OK );
        // Bend it: raise the interior controls of the one sub-patch.
        for ( common::u32 r = 0; r <= d; ++r ) {
            for ( common::u32 c = 1; c < d; ++c ) {
                patch_control_t ctl = *Patch_Control( &p, c, r );
                ctl.position.z = 1.0 + 0.25 * c;
                REQUIRE( Patch_TrySetControl( &p, c, r, ctl ) == geometry_status_t::OK );
            }
        }
        patch_surface_t original{};
        REQUIRE( Patch_TryClone( &p, &e.allocator, &original ) == geometry_status_t::OK );
        // Insert then remove: exactly the original controls.
        REQUIRE( Patch_TryInsertColumn( &p, 0, &e.ids ) == geometry_status_t::OK );
        REQUIRE( p.cColumns == 2 * d + 1 );
        REQUIRE( Patch_TryRemoveColumn( &p, 0 ) == geometry_status_t::OK );
        REQUIRE( p.cColumns == d + 1 );
        for ( common::u32 r = 0; r <= d; ++r ) {
            for ( common::u32 c = 0; c <= d; ++c ) {
                const vec3d_t a = Patch_Control( &p, c, r )->position, b = Patch_Control( &original, c, r )->position;
                CHECK( a.x == Approx( b.x ).margin( 1e-12 ) );
                CHECK( a.y == Approx( b.y ).margin( 1e-12 ) );
                CHECK( a.z == Approx( b.z ).margin( 1e-12 ) );
            }
        }
        // Rows too.
        REQUIRE( Patch_TryInsertRow( &p, 0, &e.ids ) == geometry_status_t::OK );
        REQUIRE( Patch_TryRemoveRow( &p, 0 ) == geometry_status_t::OK );
        CHECK( Patch_Evaluate( &p, 0.37, 0.61 ).position.z == Approx( Patch_Evaluate( &original, 0.37, 0.61 ).position.z ).margin( 1e-12 ) );

        // A genuinely two-piece surface: approximate, outer columns exact.
        REQUIRE( Patch_TryInsertColumn( &p, 0, &e.ids ) == geometry_status_t::OK );
        for ( common::u32 r = 0; r <= d; ++r ) {
            patch_control_t ctl = *Patch_Control( &p, d + 1, r ); // an interior control of the second piece
            ctl.position.z += 0.3;
            REQUIRE( Patch_TrySetControl( &p, d + 1, r, ctl ) == geometry_status_t::OK );
        }
        patch_surface_t twoPiece{};
        REQUIRE( Patch_TryClone( &p, &e.allocator, &twoPiece ) == geometry_status_t::OK );
        REQUIRE( Patch_TryRemoveColumn( &p, 0 ) == geometry_status_t::OK );
        double worst = 0.0;
        for ( double u = 0.0; u <= 1.0; u += 0.125 ) {
            worst = std::max( worst, std::fabs( Patch_Evaluate( &p, u, 0.5 ).position.z - Patch_Evaluate( &twoPiece, u, 0.5 ).position.z ) );
        }
        // The fit must beat ignoring the bump: the raised control lifts the
        // second piece by at most 0.3 x its largest Bernstein weight (1/2 for
        // a quadratic, 4/9 for a cubic).
        const double bump = 0.3 * ( d == 2 ? 0.5 : 4.0 / 9.0 );
        CHECK( worst < bump );
        CHECK( Patch_Evaluate( &p, 0.0, 0.5 ).position.z == Approx( Patch_Evaluate( &twoPiece, 0.0, 0.5 ).position.z ) );
        CHECK( Patch_Evaluate( &p, 1.0, 0.5 ).position.z == Approx( Patch_Evaluate( &twoPiece, 1.0, 0.5 ).position.z ) );
        // Only one sub-patch left: nothing to merge.
        CHECK( Patch_TryRemoveColumn( &p, 0 ) == geometry_status_t::INVALID_ARGUMENT );
        CHECK( Patch_TryRemoveRow( &p, 3 ) == geometry_status_t::INVALID_ARGUMENT );
        Patch_Shutdown( &p );
        Patch_Shutdown( &original );
        Patch_Shutdown( &twoPiece );
    }
}

} // namespace cypher::editor::geometry
