//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSelectionQueries_Tests.cpp
//  Purpose: Contract tests for the selection queries (material, coplanar,
//           facing, sharp, boundary, face size, invert, volume), the
//           component vertex set / pivot, and component transforms.
//  Details: Every query is checked against the exact expected set on a unit
//           cube (sometimes cut with the knife to make coplanar pieces,
//           triangles, or split edges). Transforms are checked by volume:
//           lifting the top by 1 gives 2, scaling it by 1/2 about its centre
//           gives the frustum (1 + 1/4 + 1/2) / 3, offsetting it by 1/2
//           gives 1.5 - and a fold, a collapse, a mirror, or a position
//           outside the source domain is refused with the source unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSelectionQueries.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshKnife.h"
#include "CypherGeometry_MeshSourceComponents.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_MeshValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }
constexpr double kPi = 3.14159265358979323846;

// Unit cube, vertex IDs 10 + (x | y << 1 | z << 2), faces 20..25 (21 top,
// 20 bottom, 22 y=0, 23 y=1, 24 x=0, 25 x=1); face material = its ID.
struct Cube {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_t s{};
    mesh_source_description_t d{};
    mesh_selection_t sel{};
    geometry_source_id_allocator_t ids{ Id( 900 ) };
    Cube() {
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        for ( int i = 0; i < 8; ++i ) {
            REQUIRE( MeshSourceDescription_TryAddVertex(
                         &d, Vec3d_Make( ( i & 1 ) ? 1.0 : 0.0, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0 ),
                         Id( 10u + static_cast<common::u64>( i ) ), nullptr ) == geometry_status_t::OK );
        }
        const std::vector<std::vector<common::u32>> faces = {
            { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for ( common::usize f = 0; f < faces.size(); ++f ) {
            mesh_face_attributes_t attrs{};
            attrs.material.value = 20u + f;
            REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ faces[f].data(), faces[f].size() },
                                                       Id( 20u + f ), attrs, nullptr ) == geometry_status_t::OK );
        }
        REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK );
        REQUIRE( MeshSelection_Init( &sel, &allocator, Id( 1 ) ) == geometry_status_t::OK );
    }
    ~Cube() {
        MeshSelection_Shutdown( &sel );
        MeshSource_Shutdown( &s );
        MeshSourceDescription_Shutdown( &d );
    }
    void Ids() { REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK ); }
    std::vector<common::u64> Faces() const {
        std::vector<common::u64> v;
        for ( common::usize i = 0; i < sel.faces.nCount; ++i ) { v.push_back( sel.faces.pData[i].value ); }
        return v;
    }
    std::vector<common::u64> Vertices() const {
        std::vector<common::u64> v;
        for ( common::usize i = 0; i < sel.vertices.nCount; ++i ) { v.push_back( sel.vertices.pData[i].value ); }
        return v;
    }
    // Knife across the top from the middle of 14-15 to the middle of 16-17.
    void SplitTop() {
        mesh_knife_point_t p[2]{};
        p[0].kind = p[1].kind = mesh_knife_point_kind_t::EDGE;
        REQUIRE( MeshSourceEdit_TryFindEdge( &s, Id( 14 ), Id( 15 ), &p[0].hEdge ) );
        REQUIRE( MeshSourceEdit_TryFindEdge( &s, Id( 16 ), Id( 17 ), &p[1].hEdge ) );
        REQUIRE( MeshKnife_Cut( &s.mesh, common::span_t<const mesh_knife_point_t>{ p, 2 }, nullptr, nullptr ).status ==
                 geometry_status_t::OK );
        Ids();
    }
    double Volume() const { return MeshValidation_Validate( &s.mesh ).fSignedVolume; }
    void Snapshot( mesh_source_description_t *pOut ) {
        Ids();
        REQUIRE( MeshSource_TryDescribe( &s, pOut ) == geometry_status_t::OK );
    }
};

struct Desc {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    Desc() { REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK ); }
    ~Desc() { MeshSourceDescription_Shutdown( &d ); }
};

math::affine3d_t Affine( vec3d_t c0, vec3d_t c1, vec3d_t c2, vec3d_t t ) {
    return math::affine3d_t{ { c0.x, c0.y, c0.z, c1.x, c1.y, c1.z, c2.x, c2.y, c2.z, t.x, t.y, t.z } };
}

} // namespace

TEST_CASE( "Selection by material, facing direction, face size and invert", "[geometry][selection][selectionquery]" ) {
    Cube c;
    geometry_material_ref_t top{};
    top.value = 21u;
    REQUIRE( MeshSelection_TrySelectFacesByMaterial( &c.sel, &c.s, top ) == geometry_status_t::OK );
    CHECK( c.Faces() == std::vector<common::u64>{ 21 } );

    MeshSelection_Clear( &c.sel );
    REQUIRE( MeshSelection_TrySelectFacesFacing( &c.sel, &c.s, Vec3d_Make( 0, 0, -1 ), 0.1 ) == geometry_status_t::OK );
    CHECK( c.Faces() == std::vector<common::u64>{ 20 } );
    MeshSelection_Clear( &c.sel );
    // (1, 1, 0) is 45 degrees from +x and +y.
    REQUIRE( MeshSelection_TrySelectFacesFacing( &c.sel, &c.s, Vec3d_Make( 1, 1, 0 ), 50.0 * kPi / 180.0 ) ==
             geometry_status_t::OK );
    CHECK( c.Faces() == std::vector<common::u64>{ 23, 25 } );
    CHECK( MeshSelection_TrySelectFacesFacing( &c.sel, &c.s, Vec3d_Make( 0, 0, 0 ), 0.1 ) == geometry_status_t::INVALID_ARGUMENT );

    MeshSelection_Clear( &c.sel );
    REQUIRE( MeshSelection_TryAddFace( &c.sel, Id( 21 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryInvert( &c.sel, &c.s, mesh_selection_mode_t::FACE ) == geometry_status_t::OK );
    CHECK( c.Faces() == std::vector<common::u64>{ 20, 22, 23, 24, 25 } );
    REQUIRE( MeshSelection_TryAddVertex( &c.sel, Id( 10 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryInvert( &c.sel, &c.s, mesh_selection_mode_t::VERTEX ) == geometry_status_t::OK );
    CHECK( c.Vertices() == std::vector<common::u64>{ 11, 12, 13, 14, 15, 16, 17 } );
    CHECK( c.Faces().size() == 5u ); // other modes untouched

    // Face size after a diagonal cut of the top: two triangles.
    mesh_knife_point_t diag[2]{};
    REQUIRE( MeshSource_TryFindVertex( &c.s, Id( 14 ), &diag[0].hVertex ) );
    REQUIRE( MeshSource_TryFindVertex( &c.s, Id( 17 ), &diag[1].hVertex ) );
    REQUIRE( MeshKnife_Cut( &c.s.mesh, common::span_t<const mesh_knife_point_t>{ diag, 2 }, nullptr, nullptr ).status ==
             geometry_status_t::OK );
    c.Ids();
    MeshSelection_Clear( &c.sel );
    REQUIRE( MeshSelection_TrySelectFacesBySize( &c.sel, &c.s, 3u, 3u ) == geometry_status_t::OK );
    CHECK( c.Faces().size() == 2u );
    MeshSelection_Clear( &c.sel );
    REQUIRE( MeshSelection_TrySelectFacesBySize( &c.sel, &c.s, 4u, 4u ) == geometry_status_t::OK );
    CHECK( c.Faces().size() == 5u );
    CHECK( MeshSelection_TrySelectFacesBySize( &c.sel, &c.s, 5u, 4u ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Coplanar selection floods only across coplanar neighbours", "[geometry][selection][selectionquery]" ) {
    Cube c;
    c.SplitTop();
    REQUIRE( MeshSelection_TrySelectCoplanar( &c.sel, &c.s, Id( 21 ), 1e-6, 1e-9 ) == geometry_status_t::OK );
    const std::vector<common::u64> top = c.Faces();
    REQUIRE( top.size() == 2u ); // both halves of the top, nothing else
    CHECK( top[0] == 21u );
    // A loose angle still stops at the sides: they are 90 degrees away.
    MeshSelection_Clear( &c.sel );
    REQUIRE( MeshSelection_TrySelectCoplanar( &c.sel, &c.s, Id( 21 ), 80.0 * kPi / 180.0, 10.0 ) == geometry_status_t::OK );
    CHECK( c.Faces().size() == 2u );
    // Past 90 degrees with a generous distance the sides join (2 top halves
    // + 4 sides); the bottom, 180 degrees from the seed, still does not.
    MeshSelection_Clear( &c.sel );
    REQUIRE( MeshSelection_TrySelectCoplanar( &c.sel, &c.s, Id( 21 ), 91.0 * kPi / 180.0, 10.0 ) == geometry_status_t::OK );
    CHECK( c.Faces().size() == 6u );
    CHECK_FALSE( MeshSelection_HasFace( &c.sel, Id( 20 ) ) );
    CHECK( MeshSelection_TrySelectCoplanar( &c.sel, &c.s, Id( 999999 ), 0.1, 0.1 ) == geometry_status_t::INVALID_HANDLE );
    CHECK( MeshSelection_TrySelectCoplanar( &c.sel, &c.s, Id( 21 ), -1.0, 0.1 ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Sharp and boundary edge selection", "[geometry][selection][selectionquery]" ) {
    Cube c;
    REQUIRE( MeshSelection_TrySelectSharpEdges( &c.sel, &c.s, 45.0 * kPi / 180.0 ) == geometry_status_t::OK );
    CHECK( c.sel.edges.nCount == 12u );
    // After the top is split, the cut is flat (not sharp) and the two split
    // cube edges count as four sharp halves.
    c.SplitTop();
    MeshSelection_Clear( &c.sel );
    REQUIRE( MeshSelection_TrySelectSharpEdges( &c.sel, &c.s, 45.0 * kPi / 180.0 ) == geometry_status_t::OK );
    CHECK( c.sel.edges.nCount == 14u );
    MeshSelection_Clear( &c.sel );
    REQUIRE( MeshSelection_TrySelectBoundaryEdges( &c.sel, &c.s ) == geometry_status_t::OK );
    CHECK( c.sel.edges.nCount == 0u );

    Cube open;
    geometry_mesh_face_handle_t hTop{};
    REQUIRE( MeshSource_TryFindFace( &open.s, Id( 21 ), &hTop ) );
    REQUIRE( MeshBoundary_DeleteFaces( &open.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &hTop, 1 } ).status ==
             geometry_status_t::OK );
    REQUIRE( MeshSelection_TrySelectBoundaryEdges( &open.sel, &open.s ) == geometry_status_t::OK );
    CHECK( open.sel.edges.nCount == 4u );
    CHECK( MeshSelection_HasEdge( &open.sel, MeshEdgeRef_Make( Id( 15 ), Id( 14 ) ) ) );
}

TEST_CASE( "Volume selection judges vertices, edges and faces", "[geometry][selection][selectionquery]" ) {
    Cube c;
    // x <= 0.5 (outward normal +x).
    const math::planed_t half[1] = { math::planed_t{ Vec3d_Make( 1, 0, 0 ), -0.5 } };
    const common::span_t<const math::planed_t> planes{ half, 1 };
    REQUIRE( MeshSelection_TrySelectInVolume( &c.sel, &c.s, planes, mesh_selection_mode_t::VERTEX, mesh_volume_rule_t::CONTAINED ) ==
             geometry_status_t::OK );
    CHECK( c.Vertices() == std::vector<common::u64>{ 10, 12, 14, 16 } );
    REQUIRE( MeshSelection_TrySelectInVolume( &c.sel, &c.s, planes, mesh_selection_mode_t::EDGE, mesh_volume_rule_t::CONTAINED ) ==
             geometry_status_t::OK );
    CHECK( c.sel.edges.nCount == 4u );
    REQUIRE( MeshSelection_TrySelectInVolume( &c.sel, &c.s, planes, mesh_selection_mode_t::FACE, mesh_volume_rule_t::CONTAINED ) ==
             geometry_status_t::OK );
    CHECK( c.Faces() == std::vector<common::u64>{ 24 } );
    MeshSelection_Clear( &c.sel );
    REQUIRE( MeshSelection_TrySelectInVolume( &c.sel, &c.s, planes, mesh_selection_mode_t::FACE, mesh_volume_rule_t::ANY_VERTEX ) ==
             geometry_status_t::OK );
    CHECK( c.Faces() == std::vector<common::u64>{ 20, 21, 22, 23, 24 } );
    MeshSelection_Clear( &c.sel );
    REQUIRE( MeshSelection_TrySelectInVolume( &c.sel, &c.s, planes, mesh_selection_mode_t::FACE, mesh_volume_rule_t::CENTROID ) ==
             geometry_status_t::OK );
    CHECK( c.Faces() == std::vector<common::u64>{ 20, 21, 22, 23, 24 } ); // centroids at x = 0.5 are on the boundary: inside
    CHECK( MeshSelection_TrySelectInVolume( &c.sel, &c.s, common::span_t<const math::planed_t>{}, mesh_selection_mode_t::FACE,
                                            mesh_volume_rule_t::CONTAINED ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Pivot and gathered vertices", "[geometry][selection][selectionquery]" ) {
    Cube c;
    REQUIRE( MeshSelection_TryAddFace( &c.sel, Id( 21 ) ) == geometry_status_t::OK );
    vec3d_t p{};
    REQUIRE( MeshSelection_TryComputePivot( &c.sel, &c.s, mesh_selection_mode_t::FACE, mesh_pivot_mode_t::CENTROID, &p ) ==
             geometry_status_t::OK );
    CHECK( ( p.x == 0.5 && p.y == 0.5 && p.z == 1.0 ) );
    REQUIRE( MeshSelection_TryAddVertex( &c.sel, Id( 10 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryAddVertex( &c.sel, Id( 17 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryAddVertex( &c.sel, Id( 11 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryComputePivot( &c.sel, &c.s, mesh_selection_mode_t::VERTEX, mesh_pivot_mode_t::BOUNDS_CENTER, &p ) ==
             geometry_status_t::OK );
    CHECK( ( p.x == 0.5 && p.y == 0.5 && p.z == 0.5 ) );
    REQUIRE( MeshSelection_TryComputePivot( &c.sel, &c.s, mesh_selection_mode_t::VERTEX, mesh_pivot_mode_t::CENTROID, &p ) ==
             geometry_status_t::OK );
    CHECK( p.x == Approx( 2.0 / 3.0 ) );
    CHECK( MeshSelection_TryComputePivot( &c.sel, &c.s, mesh_selection_mode_t::EDGE, mesh_pivot_mode_t::CENTROID, &p ) ==
           geometry_status_t::INVALID_ARGUMENT ); // no edges selected
    REQUIRE( MeshSelection_TryAddFace( &c.sel, Id( 77777 ) ) == geometry_status_t::OK ); // stale reference
    CHECK( MeshSelection_TryComputePivot( &c.sel, &c.s, mesh_selection_mode_t::FACE, mesh_pivot_mode_t::CENTROID, &p ) ==
           geometry_status_t::INVALID_HANDLE );
}

TEST_CASE( "Component transforms move, scale and offset the selection", "[geometry][selection][components]" ) {
    SECTION( "Lift the top face" ) {
        Cube c;
        REQUIRE( MeshSelection_TryAddFace( &c.sel, Id( 21 ) ) == geometry_status_t::OK );
        const auto lift = Affine( Vec3d_Make( 1, 0, 0 ), Vec3d_Make( 0, 1, 0 ), Vec3d_Make( 0, 0, 1 ), Vec3d_Make( 0, 0, 1 ) );
        REQUIRE( MeshSourceEdit_TryTransformComponents( &c.s, &c.sel, mesh_selection_mode_t::FACE, lift ) == geometry_status_t::OK );
        CHECK( c.Volume() == Approx( 2.0 ) );
        geometry_mesh_face_handle_t h{};
        REQUIRE( MeshSource_TryFindFace( &c.s, Id( 22 ), &h ) );
        CHECK( EditableMesh_GetFace( &c.s.mesh, h )->normal.y == Approx( -1.0 ) );
    }
    SECTION( "Scale the top face about its centre" ) {
        Cube c;
        REQUIRE( MeshSelection_TryAddFace( &c.sel, Id( 21 ) ) == geometry_status_t::OK );
        // T(0.5, 0.5, 1) * S(0.5, 0.5, 1) * T(-0.5, -0.5, -1)
        const auto scale = Affine( Vec3d_Make( 0.5, 0, 0 ), Vec3d_Make( 0, 0.5, 0 ), Vec3d_Make( 0, 0, 1 ), Vec3d_Make( 0.25, 0.25, 0 ) );
        REQUIRE( MeshSourceEdit_TryTransformComponents( &c.s, &c.sel, mesh_selection_mode_t::FACE, scale ) == geometry_status_t::OK );
        CHECK( c.Volume() == Approx( ( 1.0 + 0.25 + 0.5 ) / 3.0 ) );
    }
    SECTION( "Offset along normals" ) {
        Cube c;
        REQUIRE( MeshSelection_TryAddFace( &c.sel, Id( 21 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSourceEdit_TryOffsetComponents( &c.s, &c.sel, mesh_selection_mode_t::FACE, 0.5 ) == geometry_status_t::OK );
        CHECK( c.Volume() == Approx( 1.5 ) );
        MeshSelection_Clear( &c.sel );
        REQUIRE( MeshSelection_TryAddVertex( &c.sel, Id( 10 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSourceEdit_TryOffsetComponents( &c.s, &c.sel, mesh_selection_mode_t::VERTEX, 0.1 ) == geometry_status_t::OK );
        geometry_mesh_vertex_handle_t h{};
        REQUIRE( MeshSource_TryFindVertex( &c.s, Id( 10 ), &h ) );
        const vec3d_t p = EditableMesh_GetVertex( &c.s.mesh, h )->position;
        CHECK( p.x == Approx( -0.1 / std::sqrt( 3.0 ) ) );
        CHECK( p.y == Approx( -0.1 / std::sqrt( 3.0 ) ) );
        CHECK( p.z == Approx( -0.1 / std::sqrt( 3.0 ) ) );
        CHECK( MeshValidation_Validate( &c.s.mesh ).status == geometry_status_t::OK );
    }
    SECTION( "Refusals leave the source unchanged" ) {
        Cube c;
        REQUIRE( MeshSelection_TryAddFace( &c.sel, Id( 21 ) ) == geometry_status_t::OK );
        Desc before, after;
        c.Snapshot( &before.d );
        // 180 degrees about the top's centre: the sides twist into bow-ties
        // (a symmetric bow-tie has zero net area, so either refusal fits).
        const auto twist = Affine( Vec3d_Make( -1, 0, 0 ), Vec3d_Make( 0, -1, 0 ), Vec3d_Make( 0, 0, 1 ), Vec3d_Make( 1, 1, 0 ) );
        const geometry_status_t twisted = MeshSourceEdit_TryTransformComponents( &c.s, &c.sel, mesh_selection_mode_t::FACE, twist );
        CHECK( ( twisted == geometry_status_t::SELF_INTERSECTING || twisted == geometry_status_t::DEGENERATE ) );
        // Flattening the top in x collapses it (and its side faces fold).
        const auto flatten = Affine( Vec3d_Make( 1e-300, 0, 0 ), Vec3d_Make( 0, 1, 0 ), Vec3d_Make( 0, 0, 1 ), Vec3d_Make( 0.5, 0, 0 ) );
        const geometry_status_t flat = MeshSourceEdit_TryTransformComponents( &c.s, &c.sel, mesh_selection_mode_t::FACE, flatten );
        CHECK( ( flat == geometry_status_t::DEGENERATE || flat == geometry_status_t::SELF_INTERSECTING ) );
        const auto mirror = Affine( Vec3d_Make( -1, 0, 0 ), Vec3d_Make( 0, 1, 0 ), Vec3d_Make( 0, 0, 1 ), Vec3d_Make( 1, 0, 0 ) );
        CHECK( MeshSourceEdit_TryTransformComponents( &c.s, &c.sel, mesh_selection_mode_t::FACE, mirror ) ==
               geometry_status_t::UNSUPPORTED );
        const auto far = Affine( Vec3d_Make( 1, 0, 0 ), Vec3d_Make( 0, 1, 0 ), Vec3d_Make( 0, 0, 1 ),
                                 Vec3d_Make( 0, 0, 2.0 * kMeshSourceCoordinateMax ) );
        CHECK( MeshSourceEdit_TryTransformComponents( &c.s, &c.sel, mesh_selection_mode_t::FACE, far ) ==
               geometry_status_t::NUMERIC_FAILURE );
        CHECK( MeshSourceEdit_TryOffsetComponents( &c.s, &c.sel, mesh_selection_mode_t::FACE, std::nan( "" ) ) ==
               geometry_status_t::NUMERIC_FAILURE );
        // Pushing the top down through the bottom inverts the side faces.
        CHECK( MeshSourceEdit_TryOffsetComponents( &c.s, &c.sel, mesh_selection_mode_t::FACE, -1.0 ) != geometry_status_t::OK );
        c.Snapshot( &after.d );
        CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
        // An empty selection is a no-op.
        MeshSelection_Clear( &c.sel );
        CHECK( MeshSourceEdit_TryTransformComponents( &c.s, &c.sel, mesh_selection_mode_t::FACE, twist ) == geometry_status_t::OK );
    }
}

TEST_CASE( "Select path finds the shortest route between vertices or faces", "[geometry][selection][selectionquery][path]" ) {
    SECTION( "Across a cube's diagonal" ) {
        Cube c;
        bool bFound = false;
        REQUIRE( MeshSelection_TrySelectVertexPath( &c.sel, &c.s, Id( 10 ), Id( 17 ), mesh_path_metric_t::STEPS, &bFound ) ==
                 geometry_status_t::OK );
        CHECK( bFound );
        CHECK( c.sel.vertices.nCount == 4u );
        CHECK( c.sel.edges.nCount == 3u );
        CHECK( MeshSelection_HasVertex( &c.sel, Id( 10 ) ) );
        CHECK( MeshSelection_HasVertex( &c.sel, Id( 17 ) ) );
        // Every selected edge joins two selected vertices.
        for ( common::usize i = 0; i < c.sel.edges.nCount; ++i ) {
            CHECK( MeshSelection_HasVertex( &c.sel, c.sel.edges.pData[i].a ) );
            CHECK( MeshSelection_HasVertex( &c.sel, c.sel.edges.pData[i].b ) );
        }
        // Bottom face to top face crosses one side.
        REQUIRE( MeshSelection_TrySelectFacePath( &c.sel, &c.s, Id( 20 ), Id( 21 ), mesh_path_metric_t::STEPS, &bFound ) ==
                 geometry_status_t::OK );
        CHECK( bFound );
        CHECK( c.sel.faces.nCount == 3u );
    }
    SECTION( "A vertex to itself" ) {
        Cube c;
        bool bFound = false;
        REQUIRE( MeshSelection_TrySelectVertexPath( &c.sel, &c.s, Id( 12 ), Id( 12 ), mesh_path_metric_t::LENGTH, &bFound ) ==
                 geometry_status_t::OK );
        CHECK( bFound );
        CHECK( c.sel.vertices.nCount == 1u );
        CHECK( c.sel.edges.nCount == 0u );
    }
    SECTION( "Length and steps choose different routes" ) {
        // A fan from X over P, a, b, Q: P-X-Q is two edges but 14.1 long;
        // P-a-b-Q is three edges and 10 long.
        common::allocator_t allocator{ *common::Allocator_GetSystem() };
        mesh_source_description_t d{};
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        const vec3d_t pts[5] = { { 0, 0, 0 }, { 3, 0, 0 }, { 7, 0, 0 }, { 10, 0, 0 }, { 5, 5, 0 } }; // P a b Q X
        common::u32 v[5];
        for ( int i = 0; i < 5; ++i ) {
            REQUIRE( MeshSourceDescription_TryAddVertex( &d, pts[i], Id( 10u + static_cast<common::u64>( i ) ), &v[i] ) == geometry_status_t::OK );
        }
        const common::u32 tris[3][3] = { { v[0], v[1], v[4] }, { v[1], v[2], v[4] }, { v[2], v[3], v[4] } };
        for ( int t = 0; t < 3; ++t ) {
            REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ tris[t], 3 }, Id( 20u + static_cast<common::u64>( t ) ),
                                                       mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
        }
        mesh_source_t s{};
        REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK );
        for ( const mesh_path_metric_t metric : { mesh_path_metric_t::STEPS, mesh_path_metric_t::LENGTH } ) {
            mesh_selection_t sel{};
            REQUIRE( MeshSelection_Init( &sel, &allocator, Id( 1 ) ) == geometry_status_t::OK );
            bool bFound = false;
            REQUIRE( MeshSelection_TrySelectVertexPath( &sel, &s, Id( 10 ), Id( 13 ), metric, &bFound ) == geometry_status_t::OK );
            CHECK( bFound );
            if ( metric == mesh_path_metric_t::STEPS ) {
                CHECK( sel.vertices.nCount == 3u );
                CHECK( MeshSelection_HasVertex( &sel, Id( 14 ) ) );
            } else {
                CHECK( sel.vertices.nCount == 4u );
                CHECK_FALSE( MeshSelection_HasVertex( &sel, Id( 14 ) ) );
            }
            MeshSelection_Shutdown( &sel );
        }
        MeshSource_Shutdown( &s );
        MeshSourceDescription_Shutdown( &d );
    }
    SECTION( "Disconnected parts and unknown IDs" ) {
        Cube c;
        REQUIRE( MeshSource_TryAssignMissingIds( &c.s, &c.ids, nullptr ) == geometry_status_t::OK );
        // Detach the top so it is a separate shell.
        geometry_mesh_face_handle_t top{};
        REQUIRE( MeshSource_TryFindFace( &c.s, Id( 21 ), &top ) );
        REQUIRE( MeshBoundary_DetachFaces( &c.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &top, 1 } ).status ==
                 geometry_status_t::OK );
        bool bFound = true;
        REQUIRE( MeshSelection_TrySelectFacePath( &c.sel, &c.s, Id( 20 ), Id( 21 ), mesh_path_metric_t::STEPS, &bFound ) ==
                 geometry_status_t::OK );
        CHECK_FALSE( bFound );
        CHECK( c.sel.faces.nCount == 0u );
        CHECK( MeshSelection_TrySelectVertexPath( &c.sel, &c.s, Id( 10 ), Id( 4242 ), mesh_path_metric_t::STEPS, nullptr ) ==
               geometry_status_t::INVALID_HANDLE );
    }
}

} // namespace cypher::editor::geometry
