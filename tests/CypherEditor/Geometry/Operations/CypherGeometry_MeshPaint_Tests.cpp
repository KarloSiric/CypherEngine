//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshPaint_Tests.cpp
//  Purpose: Tests the blend-painting brush: falloff weights, channel
//           isolation, face restriction, and all-or-nothing rejection.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshPaint.h"
#include "CypherGeometry_MeshSource.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

// A 4 x 4 grid of unit quads at z = 0 (x, y in [0, 4]); face IDs 1000 +
// row * 4 + column.
struct Grid {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_t s{};
    Grid() {
        mesh_source_description_t d{};
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        common::u32 v[25];
        for ( int j = 0; j <= 4; ++j ) {
            for ( int i = 0; i <= 4; ++i ) {
                REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( i, j, 0 ), Id( 100u + static_cast<common::u64>( j * 5 + i ) ),
                                                             &v[j * 5 + i] ) == geometry_status_t::OK );
            }
        }
        for ( int j = 0; j < 4; ++j ) {
            for ( int i = 0; i < 4; ++i ) {
                const int a = j * 5 + i;
                const common::u32 q[4] = { v[a], v[a + 1], v[a + 6], v[a + 5] };
                REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ q, 4 },
                                                           Id( 1000u + static_cast<common::u64>( j * 4 + i ) ), mesh_face_attributes_t{},
                                                           nullptr ) == geometry_status_t::OK );
            }
        }
        REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK );
        MeshSourceDescription_Shutdown( &d );
    }
    ~Grid() { MeshSource_Shutdown( &s ); }
    geometry_mesh_face_handle_t Face( common::u64 id ) {
        geometry_mesh_face_handle_t h{};
        REQUIRE( MeshSource_TryFindFace( &s, Id( id ), &h ) );
        return h;
    }
    // Visits (face, position, color) for every corner.
    template <typename fn_t> void Each( fn_t &&fn ) {
        (void)common::GenerationPool_ForEach( &s.mesh.faces,
            [&]( geometry_mesh_face_handle_t hFace, const mesh_face_record_t &f ) noexcept -> common::bool_t {
                const mesh_loop_record_t *pL = common::GenerationPool_Get( &s.mesh.loops, f.hOuterLoop );
                geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
                for ( common::u32 k = 0; k < pL->cHalfEdges; ++k ) {
                    const mesh_half_edge_record_t *pH = common::GenerationPool_Get( &s.mesh.halfEdges, h );
                    fn( hFace, common::GenerationPool_Get( &s.mesh.vertices, pH->hOrigin )->position,
                        MeshAttributeStore_GetCorner( &s.attributes, h ).colorRgba );
                    h = pH->hNext;
                }
                return true;
            } );
    }
};

common::u32 Channel( common::u32 color, int c ) { return ( color >> ( 24 - 8 * c ) ) & 0xFFu; }

double Dist( vec3d_t p, vec3d_t q ) { return std::sqrt( ( p.x - q.x ) * ( p.x - q.x ) + ( p.y - q.y ) * ( p.y - q.y ) + ( p.z - q.z ) * ( p.z - q.z ) ); }

} // namespace

TEST_CASE( "A dab fades from full at the centre to nothing at the radius", "[geometry][meshpaint]" ) {
    for ( const mesh_paint_falloff_t falloff : { mesh_paint_falloff_t::CONSTANT, mesh_paint_falloff_t::LINEAR, mesh_paint_falloff_t::SMOOTH } ) {
        CAPTURE( static_cast<int>( falloff ) );
        Grid m;
        mesh_paint_brush_t b{};
        b.center = Vec3d_Make( 2, 2, 0 );
        b.radius = 1.5;
        b.strength = 0.8;
        b.falloff = falloff;
        b.channels = MESH_PAINT_CHANNEL_R;
        b.value = 0u;
        common::u32 cChanged = 0u;
        REQUIRE( MeshPaint_TryBrush( &m.s.attributes, &m.s.mesh, {}, b, &cChanged ) == geometry_status_t::OK );
        common::u32 cExpected = 0u;
        m.Each( [&]( geometry_mesh_face_handle_t, vec3d_t p, common::u32 color ) {
            const double d = Dist( p, b.center );
            double w = 0.0;
            if ( d <= b.radius ) {
                const double t = d / b.radius;
                const double f = falloff == mesh_paint_falloff_t::CONSTANT ? 1.0 : falloff == mesh_paint_falloff_t::LINEAR ? 1.0 - t : 1.0 - t * t * ( 3.0 - 2.0 * t );
                w = b.strength * f;
            }
            const common::u32 want = static_cast<common::u32>( std::lround( 255.0 - 255.0 * w ) );
            CHECK( Channel( color, 0 ) == want );
            // Unpainted channels stay white.
            CHECK( Channel( color, 1 ) == 255u );
            CHECK( Channel( color, 2 ) == 255u );
            CHECK( Channel( color, 3 ) == 255u );
            cExpected += want != 255u ? 1u : 0u;
        } );
        CHECK( cChanged == cExpected );
        CHECK( cChanged > 0u );
    }
}

TEST_CASE( "Painting several channels and restricting to faces", "[geometry][meshpaint]" ) {
    Grid m;
    mesh_paint_brush_t b{};
    b.center = Vec3d_Make( 1, 1, 0 );
    b.radius = 10.0; // covers everything
    b.falloff = mesh_paint_falloff_t::CONSTANT;
    b.channels = MESH_PAINT_CHANNEL_G | MESH_PAINT_CHANNEL_A;
    b.value = 10u;
    const geometry_mesh_face_handle_t one[] = { m.Face( 1005 ) };
    REQUIRE( MeshPaint_TryBrush( &m.s.attributes, &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ one, 1 }, b, nullptr ) ==
             geometry_status_t::OK );
    m.Each( [&]( geometry_mesh_face_handle_t hFace, vec3d_t, common::u32 color ) {
        const bool bPainted = hFace.nSlot == one[0].nSlot && hFace.nGeneration == one[0].nGeneration;
        CHECK( Channel( color, 0 ) == 255u );
        CHECK( Channel( color, 1 ) == ( bPainted ? 10u : 255u ) );
        CHECK( Channel( color, 2 ) == 255u );
        CHECK( Channel( color, 3 ) == ( bPainted ? 10u : 255u ) );
    } );
    // A zero-strength dab changes nothing.
    b.strength = 0.0;
    common::u32 cChanged = 7u;
    REQUIRE( MeshPaint_TryBrush( &m.s.attributes, &m.s.mesh, {}, b, &cChanged ) == geometry_status_t::OK );
    CHECK( cChanged == 0u );
}

TEST_CASE( "A rejected dab paints nothing", "[geometry][meshpaint]" ) {
    Grid m;
    mesh_paint_brush_t good{};
    good.center = Vec3d_Make( 2, 2, 0 );
    good.radius = 3.0;
    good.value = 0u;
    auto expectUntouched = [&]() {
        m.Each( []( geometry_mesh_face_handle_t, vec3d_t, common::u32 color ) { CHECK( color == 0xFFFFFFFFu ); } );
    };
    const geometry_mesh_face_handle_t stale[] = { m.Face( 1000 ), geometry_mesh_face_handle_t{ 999u, 1u } };
    CHECK( MeshPaint_TryBrush( &m.s.attributes, &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ stale, 2 }, good, nullptr ) ==
           geometry_status_t::STALE_HANDLE );
    const geometry_mesh_face_handle_t twice[] = { m.Face( 1000 ), m.Face( 1000 ) };
    CHECK( MeshPaint_TryBrush( &m.s.attributes, &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ twice, 2 }, good, nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
    mesh_paint_brush_t b = good;
    b.radius = 0.0;
    CHECK( MeshPaint_TryBrush( &m.s.attributes, &m.s.mesh, {}, b, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    b = good;
    b.strength = 1.5;
    CHECK( MeshPaint_TryBrush( &m.s.attributes, &m.s.mesh, {}, b, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    b = good;
    b.channels = 0u;
    CHECK( MeshPaint_TryBrush( &m.s.attributes, &m.s.mesh, {}, b, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    b = good;
    b.center.x = std::nan( "" );
    CHECK( MeshPaint_TryBrush( &m.s.attributes, &m.s.mesh, {}, b, nullptr ) == geometry_status_t::NUMERIC_FAILURE );
    expectUntouched();
}

} // namespace cypher::editor::geometry
