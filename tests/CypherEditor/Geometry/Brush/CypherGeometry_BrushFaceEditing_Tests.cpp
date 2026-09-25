//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushFaceEditing_Tests.cpp
//  Purpose: Contract tests for multi-brush face and vertex editing: the
//           cross-brush coplanar flood fill and vertex clumping.
//  Details: A floor of unit blocks: touching tops form one surface, a gap
//           breaks it until a bridging block closes it, a lower block and a
//           downward-facing ceiling face on the same plane never join.
//           Clumping must move a shared vertex in both brushes or in none.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushFaceQueries.h"
#include "CypherGeometry_BrushShapes.h"
#include "CypherGeometry_BrushVertexClump.h"
#include "CypherGeometry_BrushValidation.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using math::vec3d_t;
using math::Vec3d_Make;

namespace {

struct Scene {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 100 } };
    std::vector<brush_solid_t *> brushes;
    ~Scene() {
        for ( brush_solid_t *p : brushes ) {
            BrushSolid_Shutdown( p );
            delete p;
        }
    }
    common::u32 Box( vec3d_t lo, vec3d_t hi ) {
        brush_solid_t *p = new brush_solid_t{};
        REQUIRE( BrushShapes_TryMakeCuboid( p, &allocator, policy, &ids, brush_shape_box_t{ lo, hi } ) == geometry_status_t::OK );
        brushes.push_back( p );
        return static_cast<common::u32>( brushes.size() - 1 );
    }
    common::u32 Side( common::u32 iBrush, vec3d_t n ) const {
        const brush_solid_t *p = brushes[iBrush];
        for ( common::u32 i = 0; i < p->sides.nCount; ++i ) {
            if ( math::Vec3d_Dot( p->sides.pData[i].plane.normal, n ) > 0.999 ) { return i; }
        }
        FAIL( "no such side" );
        return 0;
    }
    std::vector<brush_face_ref_t> Coplanar( common::u32 iBrush, vec3d_t n ) {
        common::vector_t<brush_face_ref_t> out{};
        REQUIRE( common::Vector_Init( &out, &allocator ) );
        std::vector<const brush_solid_t *> cb( brushes.begin(), brushes.end() );
        REQUIRE( BrushFaces_TrySelectCoplanar( common::span_t<const brush_solid_t *const>{ cb.data(), cb.size() },
                                               brush_face_ref_t{ iBrush, Side( iBrush, n ) }, policy, &allocator, &out ) ==
                 geometry_status_t::OK );
        std::vector<brush_face_ref_t> r( out.pData, out.pData + out.nCount );
        common::Vector_Shutdown( &out );
        return r;
    }
    bool HasVertex( common::u32 iBrush, vec3d_t p ) {
        brush_boundary_t bd{};
        REQUIRE( BrushBoundary_Init( &bd, &allocator ) == geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct( &bd, brushes[iBrush], policy ) == geometry_status_t::OK );
        bool bFound = false;
        for ( common::usize k = 0; k < bd.vertices.nCount; ++k ) {
            const vec3d_t q = bd.vertices.pData[k];
            bFound = bFound || ( std::fabs( q.x - p.x ) < 1e-9 && std::fabs( q.y - p.y ) < 1e-9 && std::fabs( q.z - p.z ) < 1e-9 );
        }
        BrushBoundary_Shutdown( &bd );
        return bFound;
    }
};

std::vector<common::u32> Brushes( const std::vector<brush_face_ref_t> &faces ) {
    std::vector<common::u32> r;
    for ( const auto &f : faces ) { r.push_back( f.iBrush ); }
    return r;
}

} // namespace

TEST_CASE( "Coplanar flood fill spans touching brushes only", "[geometry][brushfaces]" ) {
    Scene s;
    const vec3d_t up = Vec3d_Make( 0, 0, 1 );
    const auto a = s.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    const auto b = s.Box( Vec3d_Make( 1, 0, 0 ), Vec3d_Make( 2, 1, 1 ) );    // shares an edge with a's top
    const auto c = s.Box( Vec3d_Make( 3.5, 0, 0 ), Vec3d_Make( 4.5, 1, 1 ) ); // across a gap (f's corner is at x = 3)
    const auto d = s.Box( Vec3d_Make( 0, 1, 0 ), Vec3d_Make( 1, 2, 0.5 ) );  // lower top: not coplanar
    const auto e = s.Box( Vec3d_Make( 0, 0, 1 ), Vec3d_Make( 1, 1, 2 ) );    // its bottom lies on z = 1 but faces down
    const auto f = s.Box( Vec3d_Make( 2, 1, 0 ), Vec3d_Make( 3, 2, 1 ) );    // touches b only at a corner
    (void)d;
    (void)e;
    CHECK( Brushes( s.Coplanar( a, up ) ) == std::vector<common::u32>{ a, b, f } );
    // Seeding from the isolated block finds only itself.
    CHECK( Brushes( s.Coplanar( c, up ) ) == std::vector<common::u32>{ c } );
    // A bridge between b and c joins everything.
    const auto bridge = s.Box( Vec3d_Make( 2, 0, 0 ), Vec3d_Make( 3.5, 1, 1 ) );
    CHECK( Brushes( s.Coplanar( a, up ) ) == std::vector<common::u32>{ a, b, c, f, bridge } );
    // The downward face at z = 1 belongs to a different surface.
    CHECK( Brushes( s.Coplanar( e, Vec3d_Make( 0, 0, -1 ) ) ) == std::vector<common::u32>{ e } );
    // Bad seeds.
    common::vector_t<brush_face_ref_t> out{};
    REQUIRE( common::Vector_Init( &out, &s.allocator ) );
    std::vector<const brush_solid_t *> cb( s.brushes.begin(), s.brushes.end() );
    CHECK( BrushFaces_TrySelectCoplanar( common::span_t<const brush_solid_t *const>{ cb.data(), cb.size() }, brush_face_ref_t{ 99, 0 },
                                         s.policy, &s.allocator, &out ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushFaces_TrySelectCoplanar( common::span_t<const brush_solid_t *const>{ cb.data(), cb.size() }, brush_face_ref_t{ a, 99 },
                                         s.policy, &s.allocator, &out ) == geometry_status_t::INVALID_ARGUMENT );
    common::Vector_Shutdown( &out );
}

TEST_CASE( "Vertex clumping moves a shared vertex in every brush or in none", "[geometry][brushfaces]" ) {
    Scene s;
    const auto a = s.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    const auto b = s.Box( Vec3d_Make( 1, 0, 0 ), Vec3d_Make( 2, 1, 1 ) );
    const auto c = s.Box( Vec3d_Make( 5, 5, 5 ), Vec3d_Make( 6, 6, 6 ) ); // does not have the vertex
    std::vector<brush_solid_t *> all = s.brushes;
    const common::span_t<brush_solid_t *const> span{ all.data(), all.size() };
    common::u32 moved = 0;
    REQUIRE( BrushVertexOps_TryMoveVertexClump( span, Vec3d_Make( 1, 1, 1 ), Vec3d_Make( 1, 1, 1.5 ), &s.allocator, s.policy, &s.ids,
                                                &moved ) == geometry_status_t::OK );
    CHECK( moved == 2u );
    CHECK( s.HasVertex( a, Vec3d_Make( 1, 1, 1.5 ) ) );
    CHECK( s.HasVertex( b, Vec3d_Make( 1, 1, 1.5 ) ) );
    CHECK_FALSE( s.HasVertex( a, Vec3d_Make( 1, 1, 1 ) ) );
    CHECK_FALSE( s.HasVertex( b, Vec3d_Make( 1, 1, 1 ) ) );
    CHECK( s.HasVertex( c, Vec3d_Make( 5, 5, 5 ) ) );
    for ( brush_solid_t *p : s.brushes ) { CHECK( BrushValidation_Deep( p, s.policy, &s.allocator ).status == geometry_status_t::OK ); }

    // Refusals: no brush has the vertex; non-finite target.
    const geometry_source_id_allocator_t before = s.ids;
    CHECK( BrushVertexOps_TryMoveVertexClump( span, Vec3d_Make( 9, 9, 9 ), Vec3d_Make( 1, 1, 1 ), &s.allocator, s.policy, &s.ids,
                                              nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushVertexOps_TryMoveVertexClump( span, Vec3d_Make( 1, 1, 1.5 ), Vec3d_Make( 1, 1, std::nan( "" ) ), &s.allocator, s.policy,
                                              &s.ids, nullptr ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( s.ids.next.value == before.next.value );
    CHECK( s.HasVertex( a, Vec3d_Make( 1, 1, 1.5 ) ) );
    CHECK( s.HasVertex( b, Vec3d_Make( 1, 1, 1.5 ) ) );
}

} // namespace cypher::editor::geometry
