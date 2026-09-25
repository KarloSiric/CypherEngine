//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshAttributes_Tests.cpp
//  Purpose: Contract tests for the mesh attribute store, surfacing
//           operations (UV projections, materials, smoothing, hard edges),
//           and attribute-driven corner normals.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Attributes_MeshStore.h"
#include "CypherGeometry_MeshSurfacing.h"
#include "CypherGeometry_MeshCornerNormals.h"
#include "CypherGeometry_PlanarExtrude.h"
#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_Sanitation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <set>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;

namespace {

constexpr double kPi = 3.14159265358979323846;

const vec3d_t kCube[8] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 },
                           { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } };
const common::u32 kCubeFaces[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
                                       { 2, 3, 7, 6 }, { 1, 2, 6, 5 }, { 0, 4, 7, 3 } };

struct Fixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    editable_mesh_t mesh{};
    mesh_attribute_store_t store{};

    Fixture() { REQUIRE( MeshAttributeStore_Init( &store, &allocator ) == geometry_status_t::OK ); }
    ~Fixture() {
        MeshAttributeStore_Shutdown( &store );
        EditableMesh_Shutdown( &mesh );
    }
    void Cube() {
        polygon_soup_t soup{};
        REQUIRE( PolygonSoup_Init( &soup, &allocator ) == geometry_status_t::OK );
        for ( const vec3d_t &c : kCube ) { REQUIRE( PolygonSoup_TryAddVertex( &soup, c, nullptr ) == geometry_status_t::OK ); }
        for ( const auto &f : kCubeFaces ) {
            REQUIRE( PolygonSoup_TryAddFace( &soup, common::span_t<const common::u32>{ f, 4 }, {}, 0u, nullptr ) ==
                     geometry_status_t::OK );
        }
        REQUIRE( Sanitation_TryPolygonSoupToMesh( &soup, {}, &allocator, &mesh, nullptr ).status ==
                 geometry_status_t::OK );
        PolygonSoup_Shutdown( &soup );
    }
    // Regular n-gon prism of radius 1 around +Z, height 2.
    void Prism( int n ) {
        planar_frame_t f{};
        REQUIRE( PlanarFrame_TryFromPlane( math::Planed_Make( math::Vec3d_Make( 0, 0, 1 ), 0.0 ), &f ) ==
                 geometry_status_t::OK );
        planar_region_t region{};
        REQUIRE( PlanarRegion_Init( &region, &allocator, f, geometry_source_id_t{ 1 } ) == geometry_status_t::OK );
        std::vector<vec2d_t> pts;
        for ( int i = 0; i < n; ++i ) {
            const vec3d_t w = math::Vec3d_Make( std::cos( 2 * kPi * i / n ), std::sin( 2 * kPi * i / n ), 0 );
            pts.push_back( PlanarFrame_Project( f, w ) );
        }
        REQUIRE( PlanarRegion_TryAddPolygon( &region, geometry_source_id_t{ 2 }, geometry_source_id_t{ 3 },
                                             common::span_t<const vec2d_t>{ pts.data(), pts.size() },
                                             nullptr ) == geometry_status_t::OK );
        REQUIRE( PlanarExtrude_TryExtrude( &region, 2.0, geometry_policy_t{}, &allocator, &mesh, nullptr ) ==
                 geometry_status_t::OK );
        PlanarRegion_Shutdown( &region );
    }
    template <typename fn_t> void EachFace( fn_t &&fn ) {
        (void)common::GenerationPool_ForEach( &mesh.faces,
            [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t &f ) noexcept -> common::bool_t {
                fn( h, f );
                return true;
            } );
    }
    // Signed UV-space area of a face's corners in a given set.
    double UvArea( const mesh_face_record_t &f, bool lightmap = false ) {
        const mesh_loop_record_t *pL = EditableMesh_GetLoop( &mesh, f.hOuterLoop );
        std::vector<vec2d_t> uv;
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( common::u32 k = 0; k < pL->cHalfEdges; ++k ) {
            const mesh_corner_attributes_t c = MeshAttributeStore_GetCorner( &store, h );
            uv.push_back( lightmap ? c.uv1 : c.uv0 );
            h = EditableMesh_GetHalfEdge( &mesh, h )->hNext;
        }
        double a = 0;
        for ( size_t i = 0; i < uv.size(); ++i ) {
            const vec2d_t p = uv[i], q = uv[( i + 1 ) % uv.size()];
            a += p.x * q.y - q.x * p.y;
        }
        return 0.5 * a;
    }
};

} // namespace

// ===========================================================================
// Store
// ===========================================================================

TEST_CASE( "MeshAttributeStore: set, get, defaults, and stamps", "[Attributes][MeshStore]" )
{
    Fixture fx;
    fx.Cube();
    geometry_mesh_face_handle_t hF{};
    fx.EachFace( [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) { if ( !hF.nGeneration ) hF = h; } );

    CHECK_FALSE( MeshAttributeStore_HasFace( &fx.store, hF ) );
    CHECK( MeshAttributeStore_GetFace( &fx.store, hF ).smoothingGroups == 1u ); // default
    CHECK_FALSE( GeometryMaterialRef_IsAssigned( MeshAttributeStore_GetFace( &fx.store, hF ).material ) );

    mesh_face_attributes_t a{};
    a.material = geometry_material_ref_t{ 42u };
    a.smoothingGroups = 0x6u;
    REQUIRE( MeshAttributeStore_TrySetFace( &fx.store, hF, a, 1024u ) == geometry_status_t::OK );
    CHECK( MeshAttributeStore_HasFace( &fx.store, hF ) );
    CHECK( MeshAttributeStore_GetFace( &fx.store, hF ).material.value == 42u );

    // Same slot, different generation: not inherited.
    geometry_mesh_face_handle_t reused = hF;
    reused.nGeneration += 1u;
    CHECK_FALSE( MeshAttributeStore_HasFace( &fx.store, reused ) );
    CHECK( MeshAttributeStore_GetFace( &fx.store, reused ).material.value == 0u );

    CHECK( MeshAttributeStore_TrySetFace( &fx.store, geometry_mesh_face_handle_t{}, a, 1024u ) ==
           geometry_status_t::INVALID_HANDLE );
    geometry_mesh_face_handle_t far = hF;
    far.nSlot = 5000u;
    CHECK( MeshAttributeStore_TrySetFace( &fx.store, far, a, 1024u ) == geometry_status_t::LIMIT_EXCEEDED );

    const mesh_attribute_counts_t live = MeshAttributeStore_CountLive( &fx.store, &fx.mesh );
    CHECK( live.cFaces == 1u );
    CHECK( live.cCorners == 0u );

    MeshAttributeStore_Clear( &fx.store );
    CHECK_FALSE( MeshAttributeStore_HasFace( &fx.store, hF ) );
}

TEST_CASE( "MeshAttributeStore: validation catches non-finite UVs", "[Attributes][MeshStore]" )
{
    Fixture fx;
    fx.Cube();
    geometry_mesh_half_edge_handle_t hC{};
    (void)common::GenerationPool_ForEach( &fx.mesh.halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t & ) noexcept -> common::bool_t {
            hC = h;
            return false;
        } );
    mesh_corner_attributes_t c{};
    c.uv0 = math::Vec2d_Make( NAN, 0 );
    REQUIRE( MeshAttributeStore_TrySetCorner( &fx.store, hC, c, 4096u ) == geometry_status_t::OK );
    geometry_mesh_half_edge_handle_t bad{};
    CHECK( MeshAttributeStore_Validate( &fx.store, &fx.mesh, &bad ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( bad.nSlot == hC.nSlot );
    CHECK( MeshAttributeStore_GetCorner( &fx.store, hC ).colorRgba == 0xFFFFFFFFu );
}

// ===========================================================================
// Surfacing
// ===========================================================================

TEST_CASE( "MeshSurfacing: box projection gives unmirrored unit squares", "[Attributes][Surfacing]" )
{
    Fixture fx;
    fx.Cube();
    REQUIRE( MeshSurfacing_TryProjectBox( &fx.store, &fx.mesh, {}, math::Vec3d_Make( 0, 0, 0 ),
                                          math::Vec2d_Make( 1, 1 ), mesh_uv_set_t::MATERIAL ) ==
             geometry_status_t::OK );
    int faces = 0;
    fx.EachFace( [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) {
        CHECK( fx.UvArea( f ) == Approx( 1.0 ) ); // positive: not mirrored
        ++faces;
    } );
    CHECK( faces == 6 );
    CHECK( MeshAttributeStore_CountLive( &fx.store, &fx.mesh ).cCorners == 24u );
}

TEST_CASE( "MeshSurfacing: planar projection into the lightmap set", "[Attributes][Surfacing]" )
{
    Fixture fx;
    fx.Cube();
    math::planar_uv_mappingd_t m{};
    REQUIRE( math::Uvd_TryBuildPlanarMapping( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 0, 0, 1 ),
                                              math::Vec3d_Make( 0, 1, 0 ), math::Vec2d_Make( 0.5, 0.5 ), 0.0,
                                              math::Vec2d_Make( 0, 0 ), 1e-12, &m ) );
    geometry_mesh_face_handle_t top{};
    fx.EachFace( [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t &f ) { if ( f.normal.z > 0.9 ) top = h; } );
    REQUIRE( MeshSurfacing_TryProjectPlanar( &fx.store, &fx.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &top, 1 },
                                             m, mesh_uv_set_t::LIGHTMAP ) == geometry_status_t::OK );
    fx.EachFace( [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t &f ) {
        if ( h.nSlot == top.nSlot ) {
            CHECK( fx.UvArea( f, true ) == Approx( 4.0 ) ); // 0.5 world units per UV -> 2x2
            CHECK( fx.UvArea( f, false ) == Approx( 0.0 ) ); // material set untouched
        } else {
            CHECK( fx.UvArea( f, true ) == Approx( 0.0 ) ); // other faces untouched
        }
    } );
}

TEST_CASE( "MeshSurfacing: cylindrical projection has no seam smear", "[Attributes][Surfacing]" )
{
    Fixture fx;
    fx.Prism( 12 );
    REQUIRE( MeshSurfacing_TryProjectCylindrical( &fx.store, &fx.mesh, {}, math::Vec3d_Make( 0, 0, 0 ),
                                                  math::Vec3d_Make( 0, 0, 1 ), 1.0, math::Vec2d_Make( 1, 1 ),
                                                  mesh_uv_set_t::MATERIAL ) == geometry_status_t::OK );
    fx.EachFace( [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) {
        if ( std::fabs( f.normal.z ) > 0.5 ) { return; } // caps
        const mesh_loop_record_t *pL = EditableMesh_GetLoop( &fx.mesh, f.hOuterLoop );
        double lo = 1e9, hi = -1e9;
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( common::u32 k = 0; k < pL->cHalfEdges; ++k ) {
            const double u = MeshAttributeStore_GetCorner( &fx.store, h ).uv0.x;
            lo = std::min( lo, u );
            hi = std::max( hi, u );
            h = EditableMesh_GetHalfEdge( &fx.mesh, h )->hNext;
        }
        CHECK( hi - lo == Approx( 2.0 * kPi / 12.0 ).epsilon( 1e-9 ) ); // one segment's arc
    } );
}

TEST_CASE( "MeshSurfacing: materials and smoothing groups", "[Attributes][Surfacing]" )
{
    Fixture fx;
    fx.Cube();
    std::vector<geometry_mesh_face_handle_t> two;
    fx.EachFace( [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) { if ( two.size() < 2 ) two.push_back( h ); } );
    REQUIRE( MeshSurfacing_TryAssignMaterial( &fx.store, &fx.mesh, {}, geometry_material_ref_t{ 7 } ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSurfacing_TryAssignMaterial( &fx.store, &fx.mesh,
                                              common::span_t<const geometry_mesh_face_handle_t>{ two.data(), 2 },
                                              geometry_material_ref_t{ 9 } ) == geometry_status_t::OK );
    REQUIRE( MeshSurfacing_TrySetSmoothingGroups( &fx.store, &fx.mesh,
                                                  common::span_t<const geometry_mesh_face_handle_t>{ two.data(), 1 },
                                                  0u ) == geometry_status_t::OK );
    int nine = 0, seven = 0;
    fx.EachFace( [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) {
        const mesh_face_attributes_t a = MeshAttributeStore_GetFace( &fx.store, h );
        nine += a.material.value == 9u;
        seven += a.material.value == 7u;
    } );
    CHECK( nine == 2 );
    CHECK( seven == 4 );
    CHECK( MeshAttributeStore_GetFace( &fx.store, two[0] ).smoothingGroups == 0u );
    CHECK( MeshAttributeStore_GetFace( &fx.store, two[0] ).material.value == 9u ); // preserved

    geometry_mesh_face_handle_t stale = two[0];
    stale.nGeneration += 5u;
    CHECK( MeshSurfacing_TryAssignMaterial( &fx.store, &fx.mesh,
                                            common::span_t<const geometry_mesh_face_handle_t>{ &stale, 1 },
                                            geometry_material_ref_t{ 1 } ) == geometry_status_t::STALE_HANDLE );
}

TEST_CASE( "MeshSurfacing: hard edges by angle", "[Attributes][Surfacing]" )
{
    Fixture fx;
    fx.Cube();
    common::u32 hard = 0;
    REQUIRE( MeshSurfacing_TryMarkHardEdgesByAngle( &fx.store, &fx.mesh, kPi / 6.0, &hard ) == geometry_status_t::OK );
    CHECK( hard == 12u );
    REQUIRE( MeshSurfacing_TryMarkHardEdgesByAngle( &fx.store, &fx.mesh, kPi * 0.6, &hard ) == geometry_status_t::OK );
    CHECK( hard == 0u ); // 90 degree dihedrals are below 108 degrees: all cleared
}

// ===========================================================================
// Corner normals
// ===========================================================================

TEST_CASE( "MeshNormals: fully smooth cube averages three faces", "[Attributes][Normals]" )
{
    Fixture fx;
    fx.Cube();
    common::vector_t<mesh_corner_normal_record_t> n{};
    REQUIRE( common::Vector_Init( &n, &fx.allocator ) );
    REQUIRE( MeshNormals_TryComputeCornerNormals( &fx.mesh, nullptr, &n ) == geometry_status_t::OK );
    REQUIRE( n.nCount == 24u );
    std::set<common::u32> groups;
    for ( common::usize i = 0; i < n.nCount; ++i ) {
        const vec3d_t v = n.pData[i].normal;
        CHECK( std::fabs( v.x ) == Approx( 1.0 / std::sqrt( 3.0 ) ) );
        CHECK( std::fabs( v.y ) == Approx( 1.0 / std::sqrt( 3.0 ) ) );
        CHECK( std::fabs( v.z ) == Approx( 1.0 / std::sqrt( 3.0 ) ) );
        groups.insert( n.pData[i].iSmoothGroupId );
    }
    CHECK( groups.size() == 8u ); // one render vertex per cube corner
    common::Vector_Shutdown( &n );
}

TEST_CASE( "MeshNormals: hard edges and flat groups split normals", "[Attributes][Normals]" )
{
    Fixture fx;
    fx.Cube();
    common::vector_t<mesh_corner_normal_record_t> n{};
    REQUIRE( common::Vector_Init( &n, &fx.allocator ) );

    REQUIRE( MeshSurfacing_TryMarkHardEdgesByAngle( &fx.store, &fx.mesh, kPi / 6.0, nullptr ) == geometry_status_t::OK );
    REQUIRE( MeshNormals_TryComputeCornerNormals( &fx.mesh, &fx.store, &n ) == geometry_status_t::OK );
    std::set<common::u32> groups;
    for ( common::usize i = 0; i < n.nCount; ++i ) {
        const mesh_half_edge_record_t *pH = EditableMesh_GetHalfEdge( &fx.mesh, n.pData[i].hCorner );
        const mesh_face_record_t *pF = EditableMesh_GetFace( &fx.mesh, EditableMesh_GetLoop( &fx.mesh, pH->hLoop )->hFace );
        CHECK( math::Vec3d_Dot( n.pData[i].normal, pF->normal ) == Approx( 1.0 ) ); // flat
        groups.insert( n.pData[i].iSmoothGroupId );
    }
    CHECK( groups.size() == 24u );

    // Same result from smoothing group 0 with no hard edges.
    MeshAttributeStore_Clear( &fx.store );
    REQUIRE( MeshSurfacing_TrySetSmoothingGroups( &fx.store, &fx.mesh, {}, 0u ) == geometry_status_t::OK );
    REQUIRE( MeshNormals_TryComputeCornerNormals( &fx.mesh, &fx.store, &n ) == geometry_status_t::OK );
    groups.clear();
    for ( common::usize i = 0; i < n.nCount; ++i ) { groups.insert( n.pData[i].iSmoothGroupId ); }
    CHECK( groups.size() == 24u );
    common::Vector_Shutdown( &n );
}

TEST_CASE( "MeshNormals: prism sides smooth, caps hard", "[Attributes][Normals]" )
{
    Fixture fx;
    fx.Prism( 16 );
    REQUIRE( MeshSurfacing_TryMarkHardEdgesByAngle( &fx.store, &fx.mesh, kPi / 4.0, nullptr ) == geometry_status_t::OK );
    common::vector_t<mesh_corner_normal_record_t> n{};
    REQUIRE( common::Vector_Init( &n, &fx.allocator ) );
    REQUIRE( MeshNormals_TryComputeCornerNormals( &fx.mesh, &fx.store, &n ) == geometry_status_t::OK );
    for ( common::usize i = 0; i < n.nCount; ++i ) {
        const mesh_half_edge_record_t *pH = EditableMesh_GetHalfEdge( &fx.mesh, n.pData[i].hCorner );
        const mesh_face_record_t *pF = EditableMesh_GetFace( &fx.mesh, EditableMesh_GetLoop( &fx.mesh, pH->hLoop )->hFace );
        const vec3d_t v = n.pData[i].normal;
        if ( std::fabs( pF->normal.z ) > 0.5 ) {
            CHECK( v.z == Approx( pF->normal.z ) ); // cap corners stay flat
        } else {
            // Side corners: radial (smooth around), no vertical tilt.
            const vec3d_t p = EditableMesh_GetVertex( &fx.mesh, pH->hOrigin )->position;
            CHECK( v.z == Approx( 0.0 ).margin( 1e-12 ) );
            CHECK( v.x * p.x + v.y * p.y == Approx( std::sqrt( p.x * p.x + p.y * p.y ) ) );
        }
    }
    common::Vector_Shutdown( &n );
}

} // namespace cypher::editor::geometry
