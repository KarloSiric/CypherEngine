//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Cook_RenderMesh_Tests.cpp
//  Purpose: Contract tests for the EditableMesh render-mesh cook.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Cook_RenderMesh.h"
#include "CypherGeometry_MeshSurfacing.h"
#include "CypherGeometry_MeshTopologyOps.h"
#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_Sanitation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
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
    render_mesh_t rm{};

    Fixture() {
        polygon_soup_t soup{};
        REQUIRE( PolygonSoup_Init( &soup, &allocator ) == geometry_status_t::OK );
        for ( const vec3d_t &c : kCube ) { REQUIRE( PolygonSoup_TryAddVertex( &soup, c, nullptr ) == geometry_status_t::OK ); }
        for ( const auto &f : kCubeFaces ) {
            REQUIRE( PolygonSoup_TryAddFace( &soup, common::span_t<const common::u32>{ f, 4 }, {}, 0u, nullptr ) ==
                     geometry_status_t::OK );
        }
        REQUIRE( Sanitation_TryPolygonSoupToMesh( &soup, {}, &allocator, &mesh, nullptr ).status == geometry_status_t::OK );
        PolygonSoup_Shutdown( &soup );
        REQUIRE( MeshAttributeStore_Init( &store, &allocator ) == geometry_status_t::OK );
        REQUIRE( RenderMesh_Init( &rm, &allocator ) == geometry_status_t::OK );
    }
    ~Fixture() {
        RenderMesh_Shutdown( &rm );
        MeshAttributeStore_Shutdown( &store );
        EditableMesh_Shutdown( &mesh );
    }
};

} // namespace

TEST_CASE( "RenderMesh: smooth untextured cube shares corner vertices", "[Cook][RenderMesh]" )
{
    Fixture fx;
    REQUIRE( RenderMesh_TryCook( &fx.mesh, nullptr, &fx.rm ) == geometry_status_t::OK );
    CHECK( fx.rm.vertices.nCount == 8u );
    CHECK( fx.rm.indices.nCount == 36u );
    CHECK( fx.rm.triangleFace.nCount == 12u );
    REQUIRE( fx.rm.batches.nCount == 1u );
    CHECK( fx.rm.batches.pData[0].cIndices == 36u );
    CHECK( fx.rm.boundsMin[0] == 0.0f );
    CHECK( fx.rm.boundsMax[2] == 1.0f );
    CHECK( common::ContentHash_IsValid( fx.rm.contentHash ) );
}

TEST_CASE( "RenderMesh: hard edges and box UVs expand seams", "[Cook][RenderMesh]" )
{
    Fixture fx;
    REQUIRE( MeshSurfacing_TryMarkHardEdgesByAngle( &fx.store, &fx.mesh, kPi / 6.0, nullptr ) == geometry_status_t::OK );
    REQUIRE( RenderMesh_TryCook( &fx.mesh, &fx.store, &fx.rm ) == geometry_status_t::OK );
    CHECK( fx.rm.vertices.nCount == 24u ); // normals split per face

    // Smooth normals but box UVs: UV seams alone must also split.
    MeshAttributeStore_Clear( &fx.store );
    REQUIRE( MeshSurfacing_TryProjectBox( &fx.store, &fx.mesh, {}, math::Vec3d_Make( 0, 0, 0 ),
                                          math::Vec2d_Make( 1, 1 ), mesh_uv_set_t::MATERIAL ) == geometry_status_t::OK );
    REQUIRE( RenderMesh_TryCook( &fx.mesh, &fx.store, &fx.rm ) == geometry_status_t::OK );
    // Box mapping can give one cube corner identical UVs on two of its
    // faces; those corners legitimately share a render vertex. The
    // invariant is: split beyond the 8 smooth corners, never duplicate.
    CHECK( fx.rm.vertices.nCount > 8u );
    CHECK( fx.rm.vertices.nCount <= 24u );
    for ( common::usize i = 0; i < fx.rm.vertices.nCount; ++i ) {
        for ( common::usize j = i + 1; j < fx.rm.vertices.nCount; ++j ) {
            const render_vertex_t &a = fx.rm.vertices.pData[i], &b = fx.rm.vertices.pData[j];
            const bool same = a.position[0] == b.position[0] && a.position[1] == b.position[1] &&
                              a.position[2] == b.position[2] && a.uv0[0] == b.uv0[0] && a.uv0[1] == b.uv0[1];
            CHECK_FALSE( same );
        }
    }
}

TEST_CASE( "RenderMesh: tangents are unit, orthogonal to normals, right-handed", "[Cook][RenderMesh]" )
{
    Fixture fx;
    REQUIRE( MeshSurfacing_TryMarkHardEdgesByAngle( &fx.store, &fx.mesh, kPi / 6.0, nullptr ) == geometry_status_t::OK );
    REQUIRE( MeshSurfacing_TryProjectBox( &fx.store, &fx.mesh, {}, math::Vec3d_Make( 0, 0, 0 ),
                                          math::Vec2d_Make( 1, 1 ), mesh_uv_set_t::MATERIAL ) == geometry_status_t::OK );
    REQUIRE( RenderMesh_TryCook( &fx.mesh, &fx.store, &fx.rm ) == geometry_status_t::OK );
    for ( common::usize i = 0; i < fx.rm.vertices.nCount; ++i ) {
        const render_vertex_t &v = fx.rm.vertices.pData[i];
        const double tl = v.tangent[0] * v.tangent[0] + v.tangent[1] * v.tangent[1] + v.tangent[2] * v.tangent[2];
        const double dn = v.tangent[0] * v.normal[0] + v.tangent[1] * v.normal[1] + v.tangent[2] * v.normal[2];
        CHECK( tl == Approx( 1.0 ).epsilon( 1e-6 ) );
        CHECK( dn == Approx( 0.0 ).margin( 1e-6 ) );
        CHECK( v.tangent[3] == 1.0f ); // unmirrored box mapping
    }
}

TEST_CASE( "RenderMesh: materials become stable sorted batches", "[Cook][RenderMesh]" )
{
    Fixture fx;
    std::vector<geometry_mesh_face_handle_t> faces;
    (void)common::GenerationPool_ForEach( &fx.mesh.faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> common::bool_t {
            faces.push_back( h );
            return true;
        } );
    REQUIRE( MeshSurfacing_TryAssignMaterial( &fx.store, &fx.mesh, {}, geometry_material_ref_t{ 20 } ) == geometry_status_t::OK );
    REQUIRE( MeshSurfacing_TryAssignMaterial( &fx.store, &fx.mesh,
                                              common::span_t<const geometry_mesh_face_handle_t>{ faces.data() + 3, 2 },
                                              geometry_material_ref_t{ 5 } ) == geometry_status_t::OK );
    REQUIRE( RenderMesh_TryCook( &fx.mesh, &fx.store, &fx.rm ) == geometry_status_t::OK );
    REQUIRE( fx.rm.batches.nCount == 2u );
    CHECK( fx.rm.batches.pData[0].material.value == 5u );
    CHECK( fx.rm.batches.pData[0].iFirstIndex == 0u );
    CHECK( fx.rm.batches.pData[0].cIndices == 12u );
    CHECK( fx.rm.batches.pData[1].material.value == 20u );
    CHECK( fx.rm.batches.pData[1].iFirstIndex == 12u );
    CHECK( fx.rm.batches.pData[1].cIndices == 24u );
    // Triangle -> face map follows the batch order.
    for ( common::usize t = 0; t < 4; ++t ) {
        const geometry_mesh_face_handle_t hf = fx.rm.triangleFace.pData[t];
        CHECK( ( hf.nSlot == faces[3].nSlot || hf.nSlot == faces[4].nSlot ) );
    }
}

TEST_CASE( "RenderMesh: hash is deterministic and content-sensitive", "[Cook][RenderMesh]" )
{
    Fixture fx;
    REQUIRE( MeshSurfacing_TryProjectBox( &fx.store, &fx.mesh, {}, math::Vec3d_Make( 0, 0, 0 ),
                                          math::Vec2d_Make( 1, 1 ), mesh_uv_set_t::MATERIAL ) == geometry_status_t::OK );
    REQUIRE( RenderMesh_TryCook( &fx.mesh, &fx.store, &fx.rm ) == geometry_status_t::OK );
    const common::content_hash_t h1 = fx.rm.contentHash;
    REQUIRE( RenderMesh_TryCook( &fx.mesh, &fx.store, &fx.rm ) == geometry_status_t::OK );
    CHECK( common::ContentHash_Equals( h1, fx.rm.contentHash ) );

    REQUIRE( MeshSurfacing_TryProjectBox( &fx.store, &fx.mesh, {}, math::Vec3d_Make( 0, 0, 0 ),
                                          math::Vec2d_Make( 2, 2 ), mesh_uv_set_t::MATERIAL ) == geometry_status_t::OK );
    REQUIRE( RenderMesh_TryCook( &fx.mesh, &fx.store, &fx.rm ) == geometry_status_t::OK );
    CHECK_FALSE( common::ContentHash_Equals( h1, fx.rm.contentHash ) );
}

TEST_CASE( "RenderMesh: float overflow is a checked failure", "[Cook][RenderMesh]" )
{
    Fixture fx;
    geometry_mesh_vertex_handle_t hv{};
    (void)common::GenerationPool_ForEach( &fx.mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t & ) noexcept -> common::bool_t {
            hv = h;
            return false;
        } );
    REQUIRE( MeshOps_MoveVertex( &fx.mesh, hv, math::Vec3d_Make( 1e39, 0, 0 ) ) == geometry_status_t::OK );
    CHECK( RenderMesh_TryCook( &fx.mesh, nullptr, &fx.rm ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( fx.rm.vertices.nCount == 0u );
    CHECK_FALSE( common::ContentHash_IsValid( fx.rm.contentHash ) );
}

TEST_CASE( "RenderMesh: argument checks", "[Cook][RenderMesh]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    render_mesh_t rm{};
    editable_mesh_t mesh{};
    CHECK( RenderMesh_TryCook( &mesh, nullptr, &rm ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( RenderMesh_Init( &rm, &allocator ) == geometry_status_t::OK );
    CHECK( RenderMesh_TryCook( &mesh, nullptr, &rm ) == geometry_status_t::NOT_INITIALIZED );
    RenderMesh_Shutdown( &rm );
}

} // namespace cypher::editor::geometry
