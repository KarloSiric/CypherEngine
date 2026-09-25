//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBooleans_Tests.cpp
//  Purpose: Gate 16 contract tests for mesh boolean operations.
//  Details: Verifies union, subtraction, and intersection of closed
//           convex meshes built from brush boundaries. Tests use axis-
//           aligned boxes with known overlap geometry so face counts
//           and Euler characteristics are deterministic.
//
//           BrushGenerator_TryMakeBox takes (center, halfExtents), so a
//           call with center=(1,0,0), halfExtents=(1,0.5,0.5) creates
//           the box [0,-0.5,-0.5] to [2,0.5,0.5].
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBooleans.h"
#include "CypherGeometry_MeshBuilder.h"
#include "CypherGeometry_MeshValidation.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using Catch::Approx;

namespace {

// Builds a closed half-edge mesh from a box brush. Parameters are
// center and halfExtents (matching BrushGenerator_TryMakeBox).
struct BoolFixtureMesh {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};
    bool valid{ false };

    BoolFixtureMesh(
        math::vec3d_t center,
        math::vec3d_t halfExtents )
    {
        if ( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 center, halfExtents ) != geometry_status_t::OK ) {
            return;
        }
        if ( BrushBoundary_Init( &boundary, &allocator ) !=
             geometry_status_t::OK ) {
            return;
        }
        if ( BrushBoundary_TryReconstruct( &boundary, &brush, policy ) !=
             geometry_status_t::OK ) {
            return;
        }
        if ( EditableMesh_Init( &mesh, &allocator ) !=
             geometry_status_t::OK ) {
            return;
        }
        if ( MeshBuilder_TryBuildFromBoundary( &mesh, &boundary ) !=
             geometry_status_t::OK ) {
            return;
        }
        valid = true;
    }

    ~BoolFixtureMesh() {
        EditableMesh_Shutdown( &mesh );
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
};

// Tolerance matching the default geometry policy.
static constexpr common::f64 kBoolTolerance = 1.0e-6;

} // namespace

// ---------------------------------------------------------------------------
// Intersection tests
// ---------------------------------------------------------------------------

TEST_CASE( "MeshBool: intersect of two overlapping boxes produces 6-face box",
           "[Gate16][MeshBool]" )
{
    // Box A: center=(0,0,0), halfExtents=(1,1,1) → [-1,-1,-1] to [1,1,1]
    // Box B: center=(1,0,0), halfExtents=(1,1,1) → [ 0,-1,-1] to [2,1,1]
    // Intersection: [0,-1,-1] to [1,1,1] → 2x2x2 = volume 8.
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 1.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    const geometry_status_t status = MeshBool_TryIntersect(
        &a.mesh, &b.mesh, &a.allocator, kBoolTolerance, &result );

    REQUIRE( status == geometry_status_t::OK );
    CHECK( EditableMesh_FaceCount( &result ) == 6u );
    CHECK( EditableMesh_EulerCharacteristic( &result ) == 2 );

    EditableMesh_Shutdown( &result );
}

TEST_CASE( "MeshBool: intersect of identical boxes produces same box",
           "[Gate16][MeshBool]" )
{
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    const geometry_status_t status = MeshBool_TryIntersect(
        &a.mesh, &b.mesh, &a.allocator, kBoolTolerance, &result );

    REQUIRE( status == geometry_status_t::OK );
    CHECK( EditableMesh_FaceCount( &result ) == 6u );
    CHECK( EditableMesh_EulerCharacteristic( &result ) == 2 );

    EditableMesh_Shutdown( &result );
}

TEST_CASE( "MeshBool: intersect of contained box returns inner box",
           "[Gate16][MeshBool]" )
{
    // Big box: [-2,-2,-2] to [2,2,2], small box: [-1,-1,-1] to [1,1,1].
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 2.0, 2.0, 2.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    const geometry_status_t status = MeshBool_TryIntersect(
        &a.mesh, &b.mesh, &a.allocator, kBoolTolerance, &result );

    REQUIRE( status == geometry_status_t::OK );
    CHECK( EditableMesh_FaceCount( &result ) == 6u );
    CHECK( EditableMesh_EulerCharacteristic( &result ) == 2 );

    EditableMesh_Shutdown( &result );
}

TEST_CASE( "MeshBool: intersect is commutative",
           "[Gate16][MeshBool]" )
{
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 1.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t resultAB{};
    editable_mesh_t resultBA{};

    REQUIRE( MeshBool_TryIntersect(
        &a.mesh, &b.mesh, &a.allocator, kBoolTolerance,
        &resultAB ) == geometry_status_t::OK );
    REQUIRE( MeshBool_TryIntersect(
        &b.mesh, &a.mesh, &a.allocator, kBoolTolerance,
        &resultBA ) == geometry_status_t::OK );

    CHECK( EditableMesh_FaceCount( &resultAB ) ==
           EditableMesh_FaceCount( &resultBA ) );

    EditableMesh_Shutdown( &resultAB );
    EditableMesh_Shutdown( &resultBA );
}

// ---------------------------------------------------------------------------
// Union tests
// ---------------------------------------------------------------------------

TEST_CASE( "MeshBool: union of two overlapping boxes produces closed mesh",
           "[Gate16][MeshBool]" )
{
    // Box A: center=(0,0,0), halfExtents=(1,1,1) → [-1,-1,-1] to [1,1,1]
    // Box B: center=(1,0,0), halfExtents=(1,1,1) → [ 0,-1,-1] to [2,1,1]
    // Union: [-1,-1,-1] to [2,1,1] → a 3x2x2 box → 6 faces.
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 1.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    const geometry_status_t status = MeshBool_TryUnion(
        &a.mesh, &b.mesh, &a.allocator, kBoolTolerance, &result );

    REQUIRE( status == geometry_status_t::OK );
    CHECK( EditableMesh_FaceCount( &result ) == 6u );
    CHECK( EditableMesh_EulerCharacteristic( &result ) == 2 );

    EditableMesh_Shutdown( &result );
}

TEST_CASE( "MeshBool: union of identical boxes produces 6-face box",
           "[Gate16][MeshBool]" )
{
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    const geometry_status_t status = MeshBool_TryUnion(
        &a.mesh, &b.mesh, &a.allocator, kBoolTolerance, &result );

    REQUIRE( status == geometry_status_t::OK );
    CHECK( EditableMesh_FaceCount( &result ) == 6u );
    CHECK( EditableMesh_EulerCharacteristic( &result ) == 2 );

    EditableMesh_Shutdown( &result );
}

// ---------------------------------------------------------------------------
// Subtraction tests
// ---------------------------------------------------------------------------

TEST_CASE( "MeshBool: subtract of overlapping boxes produces closed mesh",
           "[Gate16][MeshBool]" )
{
    // Box A: [-1,-1,-1] to [1,1,1]
    // Box B: [ 0,-1,-1] to [2,1,1]
    // A \ B = [-1,-1,-1] to [0,1,1] → a unit box → 6 faces.
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 1.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    const geometry_status_t status = MeshBool_TrySubtract(
        &a.mesh, &b.mesh, &a.allocator, kBoolTolerance, &result );

    REQUIRE( status == geometry_status_t::OK );
    CHECK( EditableMesh_FaceCount( &result ) == 6u );
    CHECK( EditableMesh_EulerCharacteristic( &result ) == 2 );

    EditableMesh_Shutdown( &result );
}

TEST_CASE( "MeshBool: multi-fragment subtraction fails without data loss",
           "[Gate16][MeshBool][contract]" )
{
    // Removing a fully contained box from a larger box produces six convex
    // brush fragments. Until the mesh layer can remap and merge all six
    // shells, success would necessarily discard valid result volume.
    BoolFixtureMesh outer( Vec3d_Make( 0.0, 0.0, 0.0 ),
                           Vec3d_Make( 2.0, 2.0, 2.0 ) );
    BoolFixtureMesh inner( Vec3d_Make( 0.0, 0.0, 0.0 ),
                           Vec3d_Make( 0.5, 0.5, 0.5 ) );
    REQUIRE( outer.valid );
    REQUIRE( inner.valid );

    editable_mesh_t result{};
    CHECK( MeshBool_TrySubtract(
               &outer.mesh, &inner.mesh, &outer.allocator,
               kBoolTolerance, &result ) ==
           geometry_status_t::UNSUPPORTED );
    CHECK_FALSE( EditableMesh_IsInitialized( &result ) );
    CHECK( EditableMesh_VertexCount( &result ) == 0u );
    CHECK( EditableMesh_FaceCount( &result ) == 0u );
}

// ---------------------------------------------------------------------------
// Volume verification
// ---------------------------------------------------------------------------

TEST_CASE( "MeshBool: intersection volume matches expected overlap",
           "[Gate16][MeshBool]" )
{
    // Box A: [-1,-1,-1] to [1,1,1], volume = 8.
    // Box B: [ 0,-1,-1] to [2,1,1], volume = 8.
    // Intersection: [0,-1,-1] to [1,1,1], volume = 4.
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 1.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t intersectResult{};
    REQUIRE( MeshBool_TryIntersect(
        &a.mesh, &b.mesh, &a.allocator, kBoolTolerance,
        &intersectResult ) == geometry_status_t::OK );

    const common::f64 volA = std::abs(
        EditableMesh_SignedVolume( &a.mesh ) );
    const common::f64 volIntersect = std::abs(
        EditableMesh_SignedVolume( &intersectResult ) );

    CHECK( volA == Approx( 8.0 ).margin( 0.01 ) );
    CHECK( volIntersect == Approx( 4.0 ).margin( 0.1 ) );

    EditableMesh_Shutdown( &intersectResult );
}

// ---------------------------------------------------------------------------
// Null / invalid input rejection
// ---------------------------------------------------------------------------

TEST_CASE( "MeshBool: null mesh A rejected",
           "[Gate16][MeshBool]" )
{
    BoolFixtureMesh b( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    CHECK( MeshBool_TryUnion(
        nullptr, &b.mesh, &b.allocator, kBoolTolerance,
        &result ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshBool: null mesh B rejected",
           "[Gate16][MeshBool]" )
{
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );

    editable_mesh_t result{};
    CHECK( MeshBool_TrySubtract(
        &a.mesh, nullptr, &a.allocator, kBoolTolerance,
        &result ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshBool: null allocator rejected",
           "[Gate16][MeshBool]" )
{
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    CHECK( MeshBool_TryIntersect(
        &a.mesh, &b.mesh, nullptr, kBoolTolerance,
        &result ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshBool: null output rejected",
           "[Gate16][MeshBool]" )
{
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    CHECK( MeshBool_TryUnion(
        &a.mesh, &b.mesh, &a.allocator, kBoolTolerance,
        nullptr ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshBool: zero tolerance rejected",
           "[Gate16][MeshBool]" )
{
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    CHECK( MeshBool_TrySubtract(
        &a.mesh, &b.mesh, &a.allocator, 0.0,
        &result ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshBool: negative tolerance rejected",
           "[Gate16][MeshBool]" )
{
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    CHECK( MeshBool_TryIntersect(
        &a.mesh, &b.mesh, &a.allocator, -1.0,
        &result ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshBool: non-finite tolerance rejected",
           "[Gate16][MeshBool][contract]" )
{
    BoolFixtureMesh a( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    BoolFixtureMesh b( Vec3d_Make( 0.5, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( a.valid );
    REQUIRE( b.valid );

    editable_mesh_t result{};
    CHECK( MeshBool_TryIntersect(
               &a.mesh, &b.mesh, &a.allocator,
               std::numeric_limits<common::f64>::quiet_NaN(),
               &result ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK_FALSE( EditableMesh_IsInitialized( &result ) );
}

TEST_CASE( "MeshBool: uninitialized mesh A rejected",
           "[Gate16][MeshBool]" )
{
    BoolFixtureMesh b( Vec3d_Make( 0.0, 0.0, 0.0 ),
                       Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( b.valid );

    editable_mesh_t uninit{};
    editable_mesh_t result{};
    CHECK( MeshBool_TryUnion(
        &uninit, &b.mesh, &b.allocator, kBoolTolerance,
        &result ) == geometry_status_t::NOT_INITIALIZED );
}

} // namespace cypher::editor::geometry
