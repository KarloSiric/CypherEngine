//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshUvTransform_Tests.cpp
//  Purpose: Tests the mesh face-edit UV tools: justify and fit (per face and
//           treated as one), transforms about each pivot kind, align to
//           face on walls and slopes, and all-or-nothing rejection.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSource.h"
#include "CypherGeometry_MeshUvTransform.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

// Two unit quads at z = 0, x in [0, 1] (face 1000) and [2, 3] (face 1001),
// with UV = (x + 0.3, y + 0.2) unless bNoUvs.
struct Quads {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_t s{};
    explicit Quads( bool bNoUvs = false ) {
        mesh_source_description_t d{};
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        common::u64 id = 100u;
        for ( const double x0 : { 0.0, 2.0 } ) {
            common::u32 v[4];
            const double xs[4] = { x0, x0 + 1, x0 + 1, x0 }, ys[4] = { 0, 0, 1, 1 };
            for ( int i = 0; i < 4; ++i ) {
                REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( xs[i], ys[i], 0 ), Id( id++ ), &v[i] ) == geometry_status_t::OK );
            }
            REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ v, 4 }, Id( x0 == 0.0 ? 1000u : 1001u ),
                                                       mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
        }
        if ( !bNoUvs ) {
            for ( common::usize i = 0; i < d.corners.nCount; ++i ) {
                const vec3d_t p = d.vertices.pData[d.corners.pData[i].iVertex].position;
                d.corners.pData[i].attributes.uv0 = vec2d_t{ p.x + 0.3, p.y + 0.2 };
            }
        }
        REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK );
        MeshSourceDescription_Shutdown( &d );
    }
    ~Quads() { MeshSource_Shutdown( &s ); }
    geometry_mesh_face_handle_t Face( common::u64 id ) {
        geometry_mesh_face_handle_t h{};
        REQUIRE( MeshSource_TryFindFace( &s, Id( id ), &h ) );
        return h;
    }
    // (position, uv) of every corner of a face.
    std::vector<std::pair<vec3d_t, vec2d_t>> Corners( geometry_mesh_face_handle_t hFace ) {
        std::vector<std::pair<vec3d_t, vec2d_t>> out;
        const mesh_face_record_t *pF = common::GenerationPool_Get( &s.mesh.faces, hFace );
        const mesh_loop_record_t *pL = common::GenerationPool_Get( &s.mesh.loops, pF->hOuterLoop );
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( common::u32 k = 0; k < pL->cHalfEdges; ++k ) {
            const mesh_half_edge_record_t *pH = common::GenerationPool_Get( &s.mesh.halfEdges, h );
            out.emplace_back( common::GenerationPool_Get( &s.mesh.vertices, pH->hOrigin )->position,
                              MeshAttributeStore_GetCorner( &s.attributes, h ).uv0 );
            h = pH->hNext;
        }
        return out;
    }
    template <typename fn_t> void Each( fn_t &&fn ) {
        for ( const common::u64 id : { 1000u, 1001u } ) {
            for ( const auto &c : Corners( Face( id ) ) ) { fn( id, c.first, c.second ); }
        }
    }
};

} // namespace

TEST_CASE( "Justify moves the chosen bounds edge onto 0 or 1", "[geometry][meshuv]" ) {
    SECTION( "Treated as one: the faces keep their relative layout" ) {
        Quads m;
        REQUIRE( MeshUv_TryJustify( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, mesh_uv_justify_t::MIN_U, true ) ==
                 geometry_status_t::OK );
        m.Each( []( common::u64, vec3d_t p, vec2d_t uv ) {
            CHECK( uv.x == Approx( p.x ).margin( 1e-12 ) );
            CHECK( uv.y == Approx( p.y + 0.2 ).margin( 1e-12 ) );
        } );
        REQUIRE( MeshUv_TryJustify( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, mesh_uv_justify_t::MAX_V, true ) ==
                 geometry_status_t::OK );
        m.Each( []( common::u64, vec3d_t p, vec2d_t uv ) { CHECK( uv.y == Approx( p.y ).margin( 1e-12 ) ); } );
    }
    SECTION( "Per face: each face starts at 0" ) {
        Quads m;
        REQUIRE( MeshUv_TryJustify( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, mesh_uv_justify_t::MIN_U, false ) ==
                 geometry_status_t::OK );
        m.Each( []( common::u64 id, vec3d_t p, vec2d_t uv ) { CHECK( uv.x == Approx( id == 1000u ? p.x : p.x - 2.0 ).margin( 1e-12 ) ); } );
        REQUIRE( MeshUv_TryJustify( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, mesh_uv_justify_t::MAX_U, false ) ==
                 geometry_status_t::OK );
        m.Each( []( common::u64 id, vec3d_t p, vec2d_t uv ) { CHECK( uv.x == Approx( id == 1000u ? p.x : p.x - 2.0 ).margin( 1e-12 ) ); } );
    }
    SECTION( "Center and fit" ) {
        Quads m;
        REQUIRE( MeshUv_TryJustify( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, mesh_uv_justify_t::CENTER, true ) ==
                 geometry_status_t::OK );
        // Bounds u [0.3, 3.3], v [0.2, 1.2] -> centred on (0.5, 0.5).
        m.Each( []( common::u64, vec3d_t p, vec2d_t uv ) {
            CHECK( uv.x == Approx( p.x - 1.0 ).margin( 1e-12 ) );
            CHECK( uv.y == Approx( p.y ).margin( 1e-12 ) );
        } );
        REQUIRE( MeshUv_TryJustify( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, mesh_uv_justify_t::FIT, true ) ==
                 geometry_status_t::OK );
        m.Each( []( common::u64, vec3d_t p, vec2d_t uv ) {
            CHECK( uv.x == Approx( p.x / 3.0 ).margin( 1e-12 ) );
            CHECK( uv.y == Approx( p.y ).margin( 1e-12 ) );
        } );
        // Fit per face: each face shows the texture exactly once.
        REQUIRE( MeshUv_TryJustify( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, mesh_uv_justify_t::FIT, false ) ==
                 geometry_status_t::OK );
        m.Each( []( common::u64 id, vec3d_t p, vec2d_t uv ) { CHECK( uv.x == Approx( id == 1000u ? p.x : p.x - 2.0 ).margin( 1e-12 ) ); } );
    }
}

TEST_CASE( "UV transforms turn about the chosen pivot", "[geometry][meshuv]" ) {
    const double kHalfPi = 1.5707963267948966;
    SECTION( "Rotate a quarter turn about the selection centre" ) {
        Quads m;
        mesh_uv_transform_t t{};
        t.rotationRadians = kHalfPi;
        REQUIRE( MeshUv_TryTransform( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, t, mesh_uv_pivot_t::SELECTION_CENTER, {} ) ==
                 geometry_status_t::OK );
        // Centre (1.8, 0.7): (u, v) -> (1.8 - (v - 0.7), 0.7 + (u - 1.8)).
        m.Each( []( common::u64, vec3d_t p, vec2d_t uv ) {
            const double u = p.x + 0.3, v = p.y + 0.2;
            CHECK( uv.x == Approx( 1.8 - ( v - 0.7 ) ).margin( 1e-12 ) );
            CHECK( uv.y == Approx( 0.7 + ( u - 1.8 ) ).margin( 1e-12 ) );
        } );
    }
    SECTION( "Scale each face about its own centre, mirroring V" ) {
        Quads m;
        mesh_uv_transform_t t{};
        t.scale = vec2d_t{ 2.0, -1.0 };
        REQUIRE( MeshUv_TryTransform( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, t, mesh_uv_pivot_t::FACE_CENTER, {} ) ==
                 geometry_status_t::OK );
        m.Each( []( common::u64 id, vec3d_t p, vec2d_t uv ) {
            const double cu = id == 1000u ? 0.8 : 2.8, cv = 0.7;
            CHECK( uv.x == Approx( cu + 2.0 * ( p.x + 0.3 - cu ) ).margin( 1e-12 ) );
            CHECK( uv.y == Approx( cv - ( p.y + 0.2 - cv ) ).margin( 1e-12 ) );
        } );
    }
    SECTION( "Scale about a point, then shift; only the selected face changes" ) {
        Quads m;
        mesh_uv_transform_t t{};
        t.scale = vec2d_t{ 0.5, 0.5 };
        t.shift = vec2d_t{ 0.25, -1.0 };
        const geometry_mesh_face_handle_t one[] = { m.Face( 1001 ) };
        REQUIRE( MeshUv_TryTransform( &m.s.attributes, &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ one, 1 },
                                      mesh_uv_set_t::MATERIAL, t, mesh_uv_pivot_t::POINT, vec2d_t{ 0.0, 0.0 } ) == geometry_status_t::OK );
        m.Each( []( common::u64 id, vec3d_t p, vec2d_t uv ) {
            if ( id == 1000u ) {
                CHECK( uv.x == Approx( p.x + 0.3 ).margin( 1e-12 ) );
            } else {
                CHECK( uv.x == Approx( 0.5 * ( p.x + 0.3 ) + 0.25 ).margin( 1e-12 ) );
                CHECK( uv.y == Approx( 0.5 * ( p.y + 0.2 ) - 1.0 ).margin( 1e-12 ) );
            }
        } );
    }
}

TEST_CASE( "Align to face projects each face in its own plane", "[geometry][meshuv]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
    // A 45-degree slope and a wall facing -y.
    const vec3d_t pts[8] = { { 0, 0, 0 }, { 2, 0, 0 }, { 2, 1, 1 }, { 0, 1, 1 }, { 4, 0, 0 }, { 6, 0, 0 }, { 6, 0, 3 }, { 4, 0, 3 } };
    common::u32 v[8];
    for ( int i = 0; i < 8; ++i ) {
        REQUIRE( MeshSourceDescription_TryAddVertex( &d, pts[i], Id( 100u + static_cast<common::u64>( i ) ), &v[i] ) == geometry_status_t::OK );
    }
    REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ v, 4 }, Id( 1000 ), mesh_face_attributes_t{}, nullptr ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ v + 4, 4 }, Id( 1001 ), mesh_face_attributes_t{},
                                               nullptr ) == geometry_status_t::OK );
    mesh_source_t s{};
    REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK );
    MeshSourceDescription_Shutdown( &d );
    REQUIRE( MeshUv_TryAlignToFace( &s.attributes, &s.mesh, {}, mesh_uv_set_t::MATERIAL, vec2d_t{ 0.5, 0.5 } ) == geometry_status_t::OK );
    // No stretch: UV distance = world distance / 0.5 along every edge.
    (void)common::GenerationPool_ForEach( &s.mesh.halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t &he ) noexcept -> common::bool_t {
            const mesh_half_edge_record_t *pN = common::GenerationPool_Get( &s.mesh.halfEdges, he.hNext );
            const vec3d_t a = common::GenerationPool_Get( &s.mesh.vertices, he.hOrigin )->position;
            const vec3d_t b = common::GenerationPool_Get( &s.mesh.vertices, pN->hOrigin )->position;
            const vec2d_t ua = MeshAttributeStore_GetCorner( &s.attributes, h ).uv0;
            const vec2d_t ub = MeshAttributeStore_GetCorner( &s.attributes, he.hNext ).uv0;
            const double world = std::sqrt( ( b.x - a.x ) * ( b.x - a.x ) + ( b.y - a.y ) * ( b.y - a.y ) + ( b.z - a.z ) * ( b.z - a.z ) );
            const double uv = std::sqrt( ( ub.x - ua.x ) * ( ub.x - ua.x ) + ( ub.y - ua.y ) * ( ub.y - ua.y ) );
            CHECK( uv == Approx( world / 0.5 ).margin( 1e-12 ) );
            return true;
        } );
    // On the wall, U runs horizontally: corners at equal height share V.
    std::vector<std::pair<vec3d_t, vec2d_t>> wall;
    geometry_mesh_face_handle_t hWall{};
    REQUIRE( MeshSource_TryFindFace( &s, Id( 1001 ), &hWall ) );
    const mesh_loop_record_t *pL =
        common::GenerationPool_Get( &s.mesh.loops, common::GenerationPool_Get( &s.mesh.faces, hWall )->hOuterLoop );
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( common::u32 k = 0; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = common::GenerationPool_Get( &s.mesh.halfEdges, h );
        wall.emplace_back( common::GenerationPool_Get( &s.mesh.vertices, pH->hOrigin )->position,
                           MeshAttributeStore_GetCorner( &s.attributes, h ).uv0 );
        h = pH->hNext;
    }
    for ( const auto &a : wall ) {
        for ( const auto &b : wall ) {
            if ( a.first.z == b.first.z ) { CHECK( a.second.y == Approx( b.second.y ).margin( 1e-12 ) ); }
        }
    }
    MeshSource_Shutdown( &s );
}

TEST_CASE( "UV tools reject bad input and write nothing", "[geometry][meshuv]" ) {
    Quads m;
    auto snapshot = [&]() {
        std::vector<vec2d_t> uvs;
        m.Each( [&]( common::u64, vec3d_t, vec2d_t uv ) { uvs.push_back( uv ); } );
        return uvs;
    };
    const std::vector<vec2d_t> before = snapshot();
    mesh_uv_transform_t shift{};
    shift.shift = vec2d_t{ 1.0, 0.0 };
    // A valid face then a stale one: the valid face must not be written.
    const geometry_mesh_face_handle_t stale[] = { m.Face( 1000 ), geometry_mesh_face_handle_t{ 999u, 1u } };
    CHECK( MeshUv_TryTransform( &m.s.attributes, &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ stale, 2 },
                                mesh_uv_set_t::MATERIAL, shift, mesh_uv_pivot_t::SELECTION_CENTER, {} ) == geometry_status_t::STALE_HANDLE );
    const geometry_mesh_face_handle_t twice[] = { m.Face( 1000 ), m.Face( 1000 ) };
    CHECK( MeshUv_TryTransform( &m.s.attributes, &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ twice, 2 },
                                mesh_uv_set_t::MATERIAL, shift, mesh_uv_pivot_t::SELECTION_CENTER, {} ) == geometry_status_t::INVALID_ARGUMENT );
    mesh_uv_transform_t flat{};
    flat.scale = vec2d_t{ 0.0, 1.0 };
    CHECK( MeshUv_TryTransform( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, flat, mesh_uv_pivot_t::SELECTION_CENTER, {} ) ==
           geometry_status_t::INVALID_ARGUMENT );
    mesh_uv_transform_t nan{};
    nan.rotationRadians = std::nan( "" );
    CHECK( MeshUv_TryTransform( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, nan, mesh_uv_pivot_t::SELECTION_CENTER, {} ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( MeshUv_TryAlignToFace( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, vec2d_t{ 0.0, 1.0 } ) ==
           geometry_status_t::INVALID_ARGUMENT );
    const std::vector<vec2d_t> after = snapshot();
    REQUIRE( after.size() == before.size() );
    for ( std::size_t i = 0; i < after.size(); ++i ) {
        CHECK( after[i].x == before[i].x );
        CHECK( after[i].y == before[i].y );
    }

    // Fitting UVs with no extent (a mesh that never had UVs).
    Quads bare( true );
    CHECK( MeshUv_TryJustify( &bare.s.attributes, &bare.s.mesh, {}, mesh_uv_set_t::MATERIAL, mesh_uv_justify_t::FIT, true ) ==
           geometry_status_t::DEGENERATE );
    bare.Each( []( common::u64, vec3d_t, vec2d_t uv ) {
        CHECK( uv.x == 0.0 );
        CHECK( uv.y == 0.0 );
    } );
}

namespace {

// Separate quads in the plane z = 0 with the given sizes (face IDs 1000...).
struct Panels {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_t s{};
    std::vector<geometry_mesh_face_handle_t> faces;
    explicit Panels( const std::vector<std::pair<double, double>> &sizes ) {
        mesh_source_description_t d{};
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        common::u64 id = 100u;
        double x0 = 0.0;
        for ( common::usize f = 0; f < sizes.size(); ++f ) {
            const double w = sizes[f].first, h = sizes[f].second;
            const double xs[4] = { x0, x0 + w, x0 + w, x0 }, ys[4] = { 0, 0, h, h };
            common::u32 v[4];
            for ( int i = 0; i < 4; ++i ) {
                REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( xs[i], ys[i], 0 ), Id( id++ ), &v[i] ) == geometry_status_t::OK );
            }
            REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ v, 4 }, Id( 1000u + f ), mesh_face_attributes_t{},
                                                       nullptr ) == geometry_status_t::OK );
            x0 += w + 10.0;
        }
        REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK );
        MeshSourceDescription_Shutdown( &d );
        for ( common::usize f = 0; f < sizes.size(); ++f ) {
            geometry_mesh_face_handle_t h{};
            REQUIRE( MeshSource_TryFindFace( &s, Id( 1000u + f ), &h ) );
            faces.push_back( h );
        }
    }
    ~Panels() { MeshSource_Shutdown( &s ); }
    std::vector<vec2d_t> Uvs( common::usize f ) {
        std::vector<vec2d_t> out;
        const mesh_loop_record_t *pL =
            common::GenerationPool_Get( &s.mesh.loops, common::GenerationPool_Get( &s.mesh.faces, faces[f] )->hOuterLoop );
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( common::u32 k = 0; k < pL->cHalfEdges; ++k ) {
            out.push_back( MeshAttributeStore_GetCorner( &s.attributes, h ).uv0 );
            h = common::GenerationPool_Get( &s.mesh.halfEdges, h )->hNext;
        }
        return out;
    }
};

double SignedArea( const std::vector<vec2d_t> &uv ) {
    double a = 0.0;
    for ( std::size_t i = 0; i < uv.size(); ++i ) {
        const vec2d_t p = uv[i], q = uv[( i + 1 ) % uv.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return 0.5 * a;
}

void CheckBounds( const std::vector<vec2d_t> &uv, vec2d_t lo, vec2d_t hi ) {
    vec2d_t mn = uv[0], mx = uv[0];
    for ( const vec2d_t &p : uv ) {
        mn = vec2d_t{ std::min( mn.x, p.x ), std::min( mn.y, p.y ) };
        mx = vec2d_t{ std::max( mx.x, p.x ), std::max( mx.y, p.y ) };
    }
    CHECK( mn.x == Approx( lo.x ).margin( 1e-12 ) );
    CHECK( mn.y == Approx( lo.y ).margin( 1e-12 ) );
    CHECK( mx.x == Approx( hi.x ).margin( 1e-12 ) );
    CHECK( mx.y == Approx( hi.y ).margin( 1e-12 ) );
}

} // namespace

TEST_CASE( "Hotspots give each face the region that matches its size", "[geometry][meshuv][hotspot]" ) {
    std::vector<mesh_hotspot_rect_t> atlas( 3 );
    atlas[0].uvMin = vec2d_t{ 0.0, 0.0 };
    atlas[0].uvMax = vec2d_t{ 0.5, 0.5 };
    atlas[0].worldSize = vec2d_t{ 64, 64 };
    atlas[1].uvMin = vec2d_t{ 0.5, 0.0 };
    atlas[1].uvMax = vec2d_t{ 1.0, 0.25 };
    atlas[1].worldSize = vec2d_t{ 128, 32 };
    atlas[2].uvMin = vec2d_t{ 0.0, 0.75 };
    atlas[2].uvMax = vec2d_t{ 1.0, 1.0 };
    atlas[2].worldSize = vec2d_t{ 64, 16 };
    atlas[2].bTileU = true; // a trim that repeats along its length
    // A square panel, a wide one, a tall one (measured along its longest
    // edge it is wide too), and a long strip.
    Panels m( { { 64, 64 }, { 128, 32 }, { 32, 128 }, { 500, 16 } } );
    common::vector_t<common::u32> chosen{};
    REQUIRE( common::Vector_Init( &chosen, &m.allocator ) );
    REQUIRE( MeshUv_TryApplyHotspots( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL,
                                      common::span_t<const mesh_hotspot_rect_t>{ atlas.data(), atlas.size() }, &chosen ) == geometry_status_t::OK );
    REQUIRE( chosen.nCount == 4u );
    CHECK( chosen.pData[0] == 0u );
    CHECK( chosen.pData[1] == 1u );
    CHECK( chosen.pData[2] == 1u );
    CHECK( chosen.pData[3] == 2u );
    CheckBounds( m.Uvs( 0 ), atlas[0].uvMin, atlas[0].uvMax );
    CheckBounds( m.Uvs( 1 ), atlas[1].uvMin, atlas[1].uvMax );
    CheckBounds( m.Uvs( 2 ), atlas[1].uvMin, atlas[1].uvMax );
    // The strip keeps the trim's density along its length: 500 / 64 repeats.
    CheckBounds( m.Uvs( 3 ), vec2d_t{ 0.0, 0.75 }, vec2d_t{ 500.0 / 64.0, 1.0 } );
    // No face is mirrored.
    for ( common::usize f = 0; f < 4; ++f ) { CHECK( SignedArea( m.Uvs( f ) ) > 0.0 ); }
    common::Vector_Shutdown( &chosen );
}

TEST_CASE( "Hotspot regions taller than wide are used a quarter turn round", "[geometry][meshuv][hotspot]" ) {
    for ( const bool bAllowRotation : { true, false } ) {
        CAPTURE( bAllowRotation );
        mesh_hotspot_rect_t tall{};
        tall.uvMin = vec2d_t{ 0.25, 0.0 };
        tall.uvMax = vec2d_t{ 0.5, 1.0 };
        tall.worldSize = vec2d_t{ 32, 128 };
        tall.bAllowRotation = bAllowRotation;
        Panels m( { { 128, 32 } } );
        REQUIRE( MeshUv_TryApplyHotspots( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL,
                                          common::span_t<const mesh_hotspot_rect_t>{ &tall, 1 }, nullptr ) == geometry_status_t::OK );
        const std::vector<vec2d_t> uv = m.Uvs( 0 );
        CheckBounds( uv, tall.uvMin, tall.uvMax );
        CHECK( SignedArea( uv ) > 0.0 );
        // Rotated, the face's long edge (world x) runs along the region's
        // long axis (V); unrotated it is squeezed along U.
        const double du = std::fabs( uv[1].x - uv[0].x ), dv = std::fabs( uv[1].y - uv[0].y );
        if ( bAllowRotation ) {
            CHECK( du == Approx( 0.0 ).margin( 1e-12 ) );
            CHECK( dv == Approx( 1.0 ) );
        } else {
            CHECK( du == Approx( 0.25 ) );
            CHECK( dv == Approx( 0.0 ).margin( 1e-12 ) );
        }
    }
}

TEST_CASE( "Hotspots reject bad atlases and write nothing", "[geometry][meshuv][hotspot]" ) {
    Panels m( { { 64, 64 } } );
    const std::vector<vec2d_t> before = m.Uvs( 0 );
    CHECK( MeshUv_TryApplyHotspots( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, {}, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    mesh_hotspot_rect_t flat{};
    flat.uvMax = vec2d_t{ 1.0, 0.0 };
    CHECK( MeshUv_TryApplyHotspots( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL, common::span_t<const mesh_hotspot_rect_t>{ &flat, 1 },
                                    nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    mesh_hotspot_rect_t noSize{};
    noSize.worldSize = vec2d_t{ 0.0, 1.0 };
    CHECK( MeshUv_TryApplyHotspots( &m.s.attributes, &m.s.mesh, {}, mesh_uv_set_t::MATERIAL,
                                    common::span_t<const mesh_hotspot_rect_t>{ &noSize, 1 }, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    const std::vector<vec2d_t> after = m.Uvs( 0 );
    for ( std::size_t i = 0; i < after.size(); ++i ) {
        CHECK( after[i].x == before[i].x );
        CHECK( after[i].y == before[i].y );
    }
}

} // namespace cypher::editor::geometry
