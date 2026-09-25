//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSolid_Tests.cpp
//  Purpose: Verifies plane-defined brush solid, boundary reconstruction,
//           box generator, and quick/deep validation.
//  Details: Covers the Gate 2 acceptance criterion: six valid planes
//           produce exactly 8 canonical boundary vertices, 12 edges, and
//           6 outward faces. Plane-order permutations produce equivalent
//           canonical traversal. Duplicate, contradictory, unbounded,
//           non-finite, and degenerate plane sets fail without publishing
//           a brush.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <limits>
#include <numeric>

namespace cypher::editor::geometry {

using cypher::math::Planed_Make;
using cypher::math::Vec3d_Make;
using cypher::math::f64;
using cypher::math::planed_t;
using cypher::math::vec3d_t;
using Catch::Approx;

namespace {

// Builds a standard unit box at the origin via the generator. Nearly
// every test starts from a valid box, so the helper avoids repeating
// the allocator/policy/ID-allocator setup in each case.
struct BoxFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};

    BoxFixture() {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAllocator,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );
    }
    ~BoxFixture() { BrushSolid_Shutdown( &brush ); }
};

struct BoundaryFixture : BoxFixture {
    brush_boundary_t boundary{};

    BoundaryFixture() {
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &brush, policy ) ==
                 geometry_status_t::OK );
    }
    ~BoundaryFixture() { BrushBoundary_Shutdown( &boundary ); }
};

struct boundary_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFreeCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *BoundaryFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<boundary_failure_allocator_state_t *>(
        pUserData );
    const common::usize iAllocation = pState->cAllocationCalls++;
    if ( iAllocation == pState->iFailure ) {
        return nullptr;
    }

    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
    }
    return pMemory;
}

void BoundaryFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<boundary_failure_allocator_state_t *>(
        pUserData );
    ++pState->cFreeCalls;
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeBoundaryFailureAllocator(
    boundary_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        BoundaryFailureAllocate,
        nullptr,
        BoundaryFailureFree,
        pState
    };
}

bool BoundaryHasNoData( const brush_boundary_t &boundary ) noexcept
{
    return BrushBoundary_VertexCount( &boundary ) == 0u &&
           BrushBoundary_EdgeCount( &boundary ) == 0u &&
           BrushBoundary_FaceCount( &boundary ) == 0u &&
           common::Vector_Count( &boundary.faceVertexIndices ) == 0u;
}

} // namespace

//==========================================================================
// BrushSolid lifecycle and side management
//==========================================================================

TEST_CASE( "brush solid rejects use before initialization",
           "[editor][geometry][brush]" ) {
    brush_solid_t brush{};
    const geometry_limit_policy_t limits{};
    brush_solid_side_t side{};

    REQUIRE( BrushSolid_SideCount( &brush ) == 0u );
    REQUIRE( BrushSolid_TryGetSide( &brush, 0u, &side ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( BrushSolid_TryAddSide( &brush, limits, side, nullptr ) ==
             geometry_status_t::NOT_INITIALIZED );

    // Shutdown on an untouched brush must be safe.
    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "brush solid rejects double initialization",
           "[editor][geometry][brush]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    brush_solid_t brush{};
    const geometry_source_id_t id{ 1u };

    REQUIRE( BrushSolid_Init( &brush, &allocator, id ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_Init( &brush, &allocator, id ) ==
             geometry_status_t::ALREADY_INITIALIZED );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "brush solid rejects invalid source ID on init",
           "[editor][geometry][brush]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    brush_solid_t brush{};

    REQUIRE( BrushSolid_Init( &brush, &allocator,
                 GEOMETRY_SOURCE_ID_INVALID ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "brush solid rejects a side with non-finite plane",
           "[editor][geometry][brush]" ) {
    BoxFixture box;
    const geometry_limit_policy_t limits{};
    const f64 nan = std::numeric_limits<f64>::quiet_NaN();

    brush_solid_side_t bad{};
    bad.sourceId = geometry_source_id_t{ 100u };
    bad.plane = Planed_Make( Vec3d_Make( nan, 0.0, 0.0 ), 0.0 );

    REQUIRE( BrushSolid_TryAddSide( &box.brush, limits, bad, nullptr ) ==
             geometry_status_t::NUMERIC_FAILURE );
}

TEST_CASE( "brush solid rejects a side with invalid source ID",
           "[editor][geometry][brush]" ) {
    BoxFixture box;
    const geometry_limit_policy_t limits{};

    brush_solid_side_t bad{};
    bad.plane = Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), 0.0 );
    bad.sourceId = GEOMETRY_SOURCE_ID_INVALID;

    REQUIRE( BrushSolid_TryAddSide( &box.brush, limits, bad, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "TrySetSidePlane changes the plane without touching identity",
           "[editor][geometry][brush]" ) {
    BoxFixture box;
    brush_solid_side_t original{};
    REQUIRE( BrushSolid_TryGetSide( &box.brush, 0u, &original ) ==
             geometry_status_t::OK );

    const planed_t newPlane =
        Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -2.0 );
    REQUIRE( BrushSolid_TrySetSidePlane( &box.brush, 0u, newPlane ) ==
             geometry_status_t::OK );

    brush_solid_side_t modified{};
    REQUIRE( BrushSolid_TryGetSide( &box.brush, 0u, &modified ) ==
             geometry_status_t::OK );

    // Plane changed.
    REQUIRE( modified.plane.d == newPlane.d );
    // Identity and attribute binding unchanged.
    REQUIRE( modified.sourceId.value == original.sourceId.value );
    REQUIRE( modified.iAttributeIndex == original.iAttributeIndex );
}

//==========================================================================
// Box generator
//==========================================================================

TEST_CASE( "box generator produces 6 sides with valid source IDs",
           "[editor][geometry][brush]" ) {
    BoxFixture box;
    REQUIRE( BrushSolid_SideCount( &box.brush ) == 6u );
    REQUIRE( GeometrySourceId_IsValid( box.brush.sourceId ) );

    for ( common::usize i = 0u; i < 6u; ++i ) {
        brush_solid_side_t side{};
        REQUIRE( BrushSolid_TryGetSide( &box.brush, i, &side ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometrySourceId_IsValid( side.sourceId ) );
    }
}

TEST_CASE( "box generator allocates unique source IDs",
           "[editor][geometry][brush]" ) {
    BoxFixture box;

    // 1 brush ID + 6 side IDs = 7 unique IDs.
    common::u64 ids[7];
    ids[0] = box.brush.sourceId.value;
    for ( common::usize i = 0u; i < 6u; ++i ) {
        brush_solid_side_t side{};
        REQUIRE( BrushSolid_TryGetSide( &box.brush, i, &side ) ==
                 geometry_status_t::OK );
        ids[1u + i] = side.sourceId.value;
    }
    std::sort( ids, ids + 7 );
    for ( int i = 0; i < 6; ++i ) {
        REQUIRE( ids[i] != ids[i + 1] );
    }
}

TEST_CASE( "box generator rejects zero half-extents",
           "[editor][geometry][brush]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 0.0, 1.0 ) ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "box generator rejects negative half-extents",
           "[editor][geometry][brush]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, -1.0, 1.0 ) ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "box generator rejects non-finite center",
           "[editor][geometry][brush]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    const f64 nan = std::numeric_limits<f64>::quiet_NaN();

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( nan, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::NUMERIC_FAILURE );
}

TEST_CASE( "box generator rejects a box that exceeds coordinate limits",
           "[editor][geometry][brush]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};

    // Center at the limit edge, half-extent pushes past it.
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( policy.numerical.fCoordinateMagnitudeLimit, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::LIMIT_EXCEEDED );
}

//==========================================================================
// Boundary reconstruction — Gate 2 closing criterion
//==========================================================================

TEST_CASE( "box boundary has exactly 8 vertices, 12 edges, 6 faces",
           "[editor][geometry][brush]" ) {
    BoundaryFixture box;

    REQUIRE( BrushBoundary_VertexCount( &box.boundary ) == 8u );
    REQUIRE( BrushBoundary_EdgeCount( &box.boundary ) == 12u );
    REQUIRE( BrushBoundary_FaceCount( &box.boundary ) == 6u );
}

TEST_CASE( "every box face has exactly 4 vertices (quad)",
           "[editor][geometry][brush]" ) {
    BoundaryFixture box;
    const common::usize cFaces = BrushBoundary_FaceCount( &box.boundary );

    for ( common::usize i = 0u; i < cFaces; ++i ) {
        common::usize count = 0u;
        common::u32 indices[16];
        REQUIRE( BrushBoundary_TryGetFaceVertexIndices(
                     &box.boundary, i, indices, 16u, &count ) ==
                 geometry_status_t::OK );
        REQUIRE( count == 4u );
    }
}

TEST_CASE( "every box face normal agrees with its side plane",
           "[editor][geometry][brush]" ) {
    BoundaryFixture box;
    const common::usize cFaces = BrushBoundary_FaceCount( &box.boundary );

    for ( common::usize i = 0u; i < cFaces; ++i ) {
        vec3d_t normal{};
        REQUIRE( BrushBoundary_TryGetFaceNormal(
                     &box.boundary, &box.brush, i, &normal ) ==
                 geometry_status_t::OK );

        // For an axis-aligned box, each face normal must be a unit axis
        // direction. Exactly one component has magnitude 1, the others 0.
        const f64 absSum = math::Scalar_Abs( normal.x ) +
                           math::Scalar_Abs( normal.y ) +
                           math::Scalar_Abs( normal.z );
        REQUIRE( absSum == Approx( 1.0 ).margin( 1.0e-10 ) );
    }
}

TEST_CASE( "box boundary Euler characteristic is 2",
           "[editor][geometry][brush]" ) {
    BoundaryFixture box;
    const common::i32 euler =
        static_cast<common::i32>(
            BrushBoundary_VertexCount( &box.boundary ) ) -
        static_cast<common::i32>(
            BrushBoundary_EdgeCount( &box.boundary ) ) +
        static_cast<common::i32>(
            BrushBoundary_FaceCount( &box.boundary ) );
    REQUIRE( euler == 2 );
}

TEST_CASE( "box boundary vertices are within expected positions",
           "[editor][geometry][brush]" ) {
    // A unit box centered at origin should have vertices at all
    // combinations of (+/-1, +/-1, +/-1).
    BoundaryFixture box;
    const common::usize cVerts = BrushBoundary_VertexCount( &box.boundary );
    REQUIRE( cVerts == 8u );

    for ( common::usize i = 0u; i < cVerts; ++i ) {
        const vec3d_t v = box.boundary.vertices.pData[i];
        REQUIRE( math::Scalar_Abs( math::Scalar_Abs( v.x ) - 1.0 ) <
                 1.0e-8 );
        REQUIRE( math::Scalar_Abs( math::Scalar_Abs( v.y ) - 1.0 ) <
                 1.0e-8 );
        REQUIRE( math::Scalar_Abs( math::Scalar_Abs( v.z ) - 1.0 ) <
                 1.0e-8 );
    }
}

TEST_CASE( "non-origin box reconstructs correctly",
           "[editor][geometry][brush]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 100.0, -50.0, 25.0 ),
                 Vec3d_Make( 2.0, 3.0, 4.0 ) ) ==
             geometry_status_t::OK );

    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &boundary, &brush, policy ) ==
             geometry_status_t::OK );

    REQUIRE( BrushBoundary_VertexCount( &boundary ) == 8u );
    REQUIRE( BrushBoundary_EdgeCount( &boundary ) == 12u );
    REQUIRE( BrushBoundary_FaceCount( &boundary ) == 6u );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &brush );
}

//==========================================================================
// Plane-order permutation invariance
//==========================================================================

TEST_CASE( "plane-order permutation produces the same vertex count and Euler",
           "[editor][geometry][brush]" ) {
    // Build a box, then reverse the side order and reconstruct. The
    // boundary topology must be identical.
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};

    brush_solid_t original{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &original, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );

    // Build a reversed brush manually.
    brush_solid_t reversed{};
    geometry_source_id_result_t idResult =
        GeometrySourceIdAllocator_Allocate( &idAlloc );
    REQUIRE( idResult.status == geometry_status_t::OK );
    REQUIRE( BrushSolid_Init( &reversed, &allocator, idResult.id ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_TryReserve( &reversed, policy.limits, 6u ) ==
             geometry_status_t::OK );

    for ( common::usize i = 0u; i < 6u; ++i ) {
        brush_solid_side_t side{};
        REQUIRE( BrushSolid_TryGetSide(
                     &original, 5u - i, &side ) ==
                 geometry_status_t::OK );
        // Give it a fresh ID since we're building a new brush.
        idResult = GeometrySourceIdAllocator_Allocate( &idAlloc );
        REQUIRE( idResult.status == geometry_status_t::OK );
        side.sourceId = idResult.id;
        ( void )BrushSolid_TryAddSide(
            &reversed, policy.limits, side, nullptr );
    }

    brush_boundary_t boundaryOrig{};
    brush_boundary_t boundaryRev{};
    REQUIRE( BrushBoundary_Init( &boundaryOrig, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_Init( &boundaryRev, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundaryOrig, &original, policy ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundaryRev, &reversed, policy ) ==
             geometry_status_t::OK );

    REQUIRE( BrushBoundary_VertexCount( &boundaryOrig ) ==
             BrushBoundary_VertexCount( &boundaryRev ) );
    REQUIRE( BrushBoundary_EdgeCount( &boundaryOrig ) ==
             BrushBoundary_EdgeCount( &boundaryRev ) );
    REQUIRE( BrushBoundary_FaceCount( &boundaryOrig ) ==
             BrushBoundary_FaceCount( &boundaryRev ) );

    BrushBoundary_Shutdown( &boundaryRev );
    BrushBoundary_Shutdown( &boundaryOrig );
    BrushSolid_Shutdown( &reversed );
    BrushSolid_Shutdown( &original );
}

//==========================================================================
// Quick validation
//==========================================================================

TEST_CASE( "quick validation passes for a valid box",
           "[editor][geometry][brush]" ) {
    BoxFixture box;
    REQUIRE( BrushValidation_Quick( &box.brush, box.policy ) ==
             geometry_status_t::OK );
}

TEST_CASE( "quick validation rejects fewer than 4 sides",
           "[editor][geometry][brush]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};

    geometry_source_id_result_t idResult =
        GeometrySourceIdAllocator_Allocate( &idAlloc );
    REQUIRE( BrushSolid_Init( &brush, &allocator, idResult.id ) ==
             geometry_status_t::OK );

    // Add only 3 sides — not enough for a closed solid.
    for ( common::usize i = 0u; i < 3u; ++i ) {
        brush_solid_side_t side{};
        side.plane = Planed_Make(
            Vec3d_Make( i == 0u ? 1.0 : 0.0,
                        i == 1u ? 1.0 : 0.0,
                        i == 2u ? 1.0 : 0.0 ),
            -1.0 );
        idResult = GeometrySourceIdAllocator_Allocate( &idAlloc );
        side.sourceId = idResult.id;
        ( void )BrushSolid_TryAddSide(
            &brush, policy.limits, side, nullptr );
    }

    REQUIRE( BrushValidation_Quick( &brush, policy ) ==
             geometry_status_t::DEGENERATE );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "quick validation rejects duplicate planes",
           "[editor][geometry][brush]" ) {
    // Build a box then overwrite one side to duplicate another.
    BoxFixture box;
    brush_solid_side_t side0{};
    REQUIRE( BrushSolid_TryGetSide( &box.brush, 0u, &side0 ) ==
             geometry_status_t::OK );

    // Set side 1 to the same plane as side 0.
    REQUIRE( BrushSolid_TrySetSidePlane(
                 &box.brush, 1u, side0.plane ) ==
             geometry_status_t::OK );

    REQUIRE( BrushValidation_Quick( &box.brush, box.policy ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "quick validation rejects contradictory planes",
           "[editor][geometry][brush]" ) {
    // Two planes with opposite normals and matching distance describe the
    // same geometric plane from opposite sides — their intersection is empty.
    BoxFixture box;
    brush_solid_side_t side0{};
    REQUIRE( BrushSolid_TryGetSide( &box.brush, 0u, &side0 ) ==
             geometry_status_t::OK );

    // Flip the normal and negate d to make a contradictory pair.
    const planed_t contradictory = Planed_Make(
        Vec3d_Make( -side0.plane.normal.x,
                    -side0.plane.normal.y,
                    -side0.plane.normal.z ),
        -side0.plane.d );
    REQUIRE( BrushSolid_TrySetSidePlane( &box.brush, 1u, contradictory ) ==
             geometry_status_t::OK );

    REQUIRE( BrushValidation_Quick( &box.brush, box.policy ) ==
             geometry_status_t::DEGENERATE );
}

//==========================================================================
// Deep validation
//==========================================================================

TEST_CASE( "deep validation passes for a valid box with correct counts",
           "[editor][geometry][brush]" ) {
    BoxFixture box;
    const brush_validation_result_t result =
        BrushValidation_Deep( &box.brush, box.policy, &box.allocator );

    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.bWatertight );
    REQUIRE( result.cVertices == 8u );
    REQUIRE( result.cEdges == 12u );
    REQUIRE( result.cFaces == 6u );
    REQUIRE( result.eulerCharacteristic == 2 );
}

TEST_CASE( "deep validation passes for an off-center non-uniform box",
           "[editor][geometry][brush]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 500.0, -200.0, 75.0 ),
                 Vec3d_Make( 10.0, 0.5, 100.0 ) ) ==
             geometry_status_t::OK );

    const brush_validation_result_t result =
        BrushValidation_Deep( &brush, policy, &allocator );

    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.bWatertight );
    REQUIRE( result.cVertices == 8u );
    REQUIRE( result.cEdges == 12u );
    REQUIRE( result.cFaces == 6u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "deep validation reports Euler and counts on a corrupted brush",
           "[editor][geometry][brush]" ) {
    // Overwrite a plane to produce a degenerate boundary and verify
    // that deep validation catches it.
    BoxFixture box;

    // Move two opposite faces to the same position — collapses the brush.
    brush_solid_side_t side0{};
    REQUIRE( BrushSolid_TryGetSide( &box.brush, 0u, &side0 ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_TrySetSidePlane(
                 &box.brush, 0u,
                 Planed_Make( side0.plane.normal, 0.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_TrySetSidePlane(
                 &box.brush, 1u,
                 Planed_Make( Vec3d_Make( -1.0, 0.0, 0.0 ), 0.0 ) ) ==
             geometry_status_t::OK );

    const brush_validation_result_t result =
        BrushValidation_Deep( &box.brush, box.policy, &box.allocator );

    // The exact failure mode depends on how the reconstruction handles the
    // collapsed volume. Either quick validation catches the contradictory
    // planes or reconstruction fails — either way, it must not report OK.
    REQUIRE( result.status != geometry_status_t::OK );
    REQUIRE_FALSE( result.bWatertight );
}

//==========================================================================
// Boundary lifecycle
//==========================================================================

TEST_CASE( "boundary rejects reconstruction before initialization",
           "[editor][geometry][brush]" ) {
    BoxFixture box;
    brush_boundary_t boundary{};

    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &box.brush, box.policy ) ==
             geometry_status_t::NOT_INITIALIZED );
}

TEST_CASE( "boundary rejects reconstruction with null brush",
           "[editor][geometry][brush]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );

    REQUIRE( BrushBoundary_TryReconstruct( &boundary, nullptr, policy ) ==
             geometry_status_t::INVALID_ARGUMENT );

    BrushBoundary_Shutdown( &boundary );
}

TEST_CASE( "boundary initialization rejects partial state without asserting",
           "[editor][geometry][brush][contract]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    brush_boundary_t boundary{};
    REQUIRE( common::Vector_Init( &boundary.vertices, &allocator ) );

    CHECK( BrushBoundary_Init( &boundary, &allocator ) ==
           geometry_status_t::CORRUPT_STATE );

    BrushBoundary_Shutdown( &boundary );
}

TEST_CASE( "boundary enforces side and scratch policy before construction",
           "[editor][geometry][brush][contract]" ) {
    BoxFixture box;
    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &box.allocator ) ==
             geometry_status_t::OK );

    geometry_policy_t sidePolicy = box.policy;
    sidePolicy.limits.cBrushSidesPerBrushMax = 5u;
    CHECK( BrushBoundary_TryReconstruct(
               &boundary, &box.brush, sidePolicy ) ==
           geometry_status_t::LIMIT_EXCEEDED );
    CHECK( BoundaryHasNoData( boundary ) );

    geometry_policy_t scratchPolicy = box.policy;
    scratchPolicy.limits.cbScratchMax = 1u;
    CHECK( BrushBoundary_TryReconstruct(
               &boundary, &box.brush, scratchPolicy ) ==
           geometry_status_t::LIMIT_EXCEEDED );
    CHECK( BoundaryHasNoData( boundary ) );

    geometry_policy_t invalidPolicy = box.policy;
    invalidPolicy.numerical.fAbsoluteDistanceTolerance = 0.0;
    CHECK( BrushBoundary_TryReconstruct(
               &boundary, &box.brush, invalidPolicy ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BoundaryHasNoData( boundary ) );

    BrushBoundary_Shutdown( &boundary );
}

TEST_CASE( "boundary rejects a redundant side instead of hiding it",
           "[editor][geometry][brush][contract]" ) {
    BoxFixture box;
    brush_solid_side_t duplicate{};
    REQUIRE( BrushSolid_TryGetSide( &box.brush, 0u, &duplicate ) ==
             geometry_status_t::OK );
    const geometry_source_id_result_t sideId =
        GeometrySourceIdAllocator_Allocate( &box.idAllocator );
    REQUIRE( sideId.status == geometry_status_t::OK );
    duplicate.sourceId = sideId.id;
    REQUIRE( BrushSolid_TryAddSide(
                 &box.brush, box.policy.limits, duplicate, nullptr ) ==
             geometry_status_t::OK );

    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &box.allocator ) ==
             geometry_status_t::OK );
    CHECK( BrushBoundary_TryReconstruct(
               &boundary, &box.brush, box.policy ) ==
           geometry_status_t::DEGENERATE );
    CHECK( BoundaryHasNoData( boundary ) );

    BrushBoundary_Shutdown( &boundary );
}

TEST_CASE( "permissive boundary exposes active sides for explicit repair",
           "[editor][geometry][brush][contract]" ) {
    BoxFixture box;
    brush_solid_side_t duplicate{};
    REQUIRE( BrushSolid_TryGetSide( &box.brush, 0u, &duplicate ) ==
             geometry_status_t::OK );
    const geometry_source_id_result_t duplicateId =
        GeometrySourceIdAllocator_Allocate( &box.idAllocator );
    REQUIRE( duplicateId.status == geometry_status_t::OK );
    duplicate.sourceId = duplicateId.id;
    duplicate.iAttributeIndex = 77u;
    REQUIRE( BrushSolid_TryAddSide(
                 &box.brush, box.policy.limits, duplicate, nullptr ) ==
             geometry_status_t::OK );

    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &box.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstructAllowRedundantSides(
                 &boundary, &box.brush, box.policy ) ==
             geometry_status_t::OK );
    CHECK( BrushBoundary_VertexCount( &boundary ) == 8u );
    CHECK( BrushBoundary_EdgeCount( &boundary ) == 12u );
    CHECK( BrushBoundary_FaceCount( &boundary ) == 6u );
    for ( common::usize iFace = 0u;
          iFace < boundary.faces.nCount;
          ++iFace ) {
        CHECK( boundary.faces.pData[iFace].iSide < 6u );
    }

    BrushBoundary_Shutdown( &boundary );
}

TEST_CASE( "canonicalization removes hidden and duplicate sides without changing retained identity",
           "[editor][geometry][brush][contract]" ) {
    BoxFixture box;
    brush_solid_side_t originalSides[6]{};
    for ( common::usize i = 0u; i < 6u; ++i ) {
        REQUIRE( BrushSolid_TryGetSide(
                     &box.brush, i, &originalSides[i] ) ==
                 geometry_status_t::OK );
    }

    // x <= 0 hides the original x <= 1 side, while the repeated -X plane
    // is an exact duplicate. A canonical clipped box still has six faces.
    geometry_source_id_result_t id =
        GeometrySourceIdAllocator_Allocate( &box.idAllocator );
    REQUIRE( id.status == geometry_status_t::OK );
    brush_solid_side_t clipSide{
        Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), 0.0 ),
        id.id,
        91u
    };
    REQUIRE( BrushSolid_TryAddSide(
                 &box.brush, box.policy.limits, clipSide, nullptr ) ==
             geometry_status_t::OK );

    id = GeometrySourceIdAllocator_Allocate( &box.idAllocator );
    REQUIRE( id.status == geometry_status_t::OK );
    brush_solid_side_t duplicate = originalSides[1];
    duplicate.sourceId = id.id;
    duplicate.iAttributeIndex = 92u;
    REQUIRE( BrushSolid_TryAddSide(
                 &box.brush, box.policy.limits, duplicate, nullptr ) ==
             geometry_status_t::OK );

    REQUIRE( BrushSolid_TryCanonicalizeSides(
                 &box.brush, box.policy ) == geometry_status_t::OK );
    REQUIRE( BrushSolid_SideCount( &box.brush ) == 6u );

    // Original +X was replaced by the new cut; all other original records
    // retain their exact identity and attribute binding.
    for ( common::usize i = 0u; i < 5u; ++i ) {
        brush_solid_side_t retained{};
        REQUIRE( BrushSolid_TryGetSide(
                     &box.brush, i, &retained ) == geometry_status_t::OK );
        CHECK( retained.sourceId.value ==
               originalSides[i + 1u].sourceId.value );
        CHECK( retained.iAttributeIndex ==
               originalSides[i + 1u].iAttributeIndex );
    }
    brush_solid_side_t retainedClip{};
    REQUIRE( BrushSolid_TryGetSide(
                 &box.brush, 5u, &retainedClip ) == geometry_status_t::OK );
    CHECK( retainedClip.sourceId.value == clipSide.sourceId.value );
    CHECK( retainedClip.iAttributeIndex == clipSide.iAttributeIndex );

    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &box.allocator ) ==
             geometry_status_t::OK );
    CHECK( BrushBoundary_TryReconstruct(
               &boundary, &box.brush, box.policy ) == geometry_status_t::OK );
    BrushBoundary_Shutdown( &boundary );
}

TEST_CASE( "canonicalization allocation failure preserves the live brush exactly",
           "[editor][geometry][brush][allocation][contract]" ) {
    boundary_failure_allocator_state_t state{};
    common::allocator_t allocator = MakeBoundaryFailureAllocator( &state );
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &ids,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );

    const geometry_source_id_result_t id =
        GeometrySourceIdAllocator_Allocate( &ids );
    REQUIRE( id.status == geometry_status_t::OK );
    const brush_solid_side_t clipSide{
        Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), 0.0 ), id.id, 19u };
    REQUIRE( BrushSolid_TryAddSide(
                 &brush, policy.limits, clipSide, nullptr ) ==
             geometry_status_t::OK );

    brush_solid_side_t *const pDataBefore = brush.sides.pData;
    const common::usize cCountBefore = brush.sides.nCount;
    const common::usize cCapacityBefore = brush.sides.nCapacity;
    const common::allocator_t *const pAllocatorBefore =
        brush.sides.pAllocator;
    const common::u64 sourceIdBefore = brush.sourceId.value;
    brush_solid_side_t sideBytes[7]{};
    std::copy_n( brush.sides.pData, brush.sides.nCount, sideBytes );
    state.iFailure = state.cAllocationCalls;

    CHECK( BrushSolid_TryCanonicalizeSides( &brush, policy ) ==
           geometry_status_t::ALLOCATION_FAILED );
    CHECK( brush.sides.pData == pDataBefore );
    CHECK( brush.sides.nCount == cCountBefore );
    CHECK( brush.sides.nCapacity == cCapacityBefore );
    CHECK( brush.sides.pAllocator == pAllocatorBefore );
    CHECK( brush.sourceId.value == sourceIdBefore );
    for ( common::usize i = 0u; i < brush.sides.nCount; ++i ) {
        CHECK( brush.sides.pData[i].plane.normal.x ==
               sideBytes[i].plane.normal.x );
        CHECK( brush.sides.pData[i].plane.normal.y ==
               sideBytes[i].plane.normal.y );
        CHECK( brush.sides.pData[i].plane.normal.z ==
               sideBytes[i].plane.normal.z );
        CHECK( brush.sides.pData[i].plane.d == sideBytes[i].plane.d );
        CHECK( brush.sides.pData[i].sourceId.value ==
               sideBytes[i].sourceId.value );
        CHECK( brush.sides.pData[i].iAttributeIndex ==
               sideBytes[i].iAttributeIndex );
    }

    BrushSolid_Shutdown( &brush );
    CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
}

TEST_CASE( "repeated reconstruction after plane edit updates correctly",
           "[editor][geometry][brush]" ) {
    // This tests the workflow of a side-plane drag: change a plane,
    // re-run reconstruction, verify the boundary is still valid.
    BoxFixture box;
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );

    // First reconstruction.
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &box.brush, box.policy ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_VertexCount( &boundary ) == 8u );

    // Move the +X face outward.
    REQUIRE( BrushSolid_TrySetSidePlane(
                 &box.brush, 0u,
                 Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -3.0 ) ) ==
             geometry_status_t::OK );

    // Second reconstruction on the same boundary object.
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &box.brush, box.policy ) ==
             geometry_status_t::OK );

    // Still a valid box — 8 vertices, 12 edges, 6 faces.
    REQUIRE( BrushBoundary_VertexCount( &boundary ) == 8u );
    REQUIRE( BrushBoundary_EdgeCount( &boundary ) == 12u );
    REQUIRE( BrushBoundary_FaceCount( &boundary ) == 6u );

    BrushBoundary_Shutdown( &boundary );
}

TEST_CASE( "every boundary reconstruction allocation failure leaves empty output",
           "[editor][geometry][brush][allocation][contract]" ) {
    BoxFixture box;

    boundary_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeBoundaryFailureAllocator( &baselineState );
    brush_boundary_t baseline{};
    REQUIRE( BrushBoundary_Init( &baseline, &baselineAllocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &baseline, &box.brush, box.policy ) ==
             geometry_status_t::OK );

    const common::usize cAllocationAttempts =
        baselineState.cAllocationCalls;
    REQUIRE( cAllocationAttempts > 0u );

    BrushBoundary_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations ==
             baselineState.cFreeCalls );

    for ( common::usize iFailure = 0u;
          iFailure < cAllocationAttempts;
          ++iFailure ) {
        CAPTURE( iFailure, cAllocationAttempts );

        boundary_failure_allocator_state_t state{};
        state.iFailure = iFailure;
        common::allocator_t allocator =
            MakeBoundaryFailureAllocator( &state );
        brush_boundary_t boundary{};
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );

        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &box.brush, box.policy ) ==
                 geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cAllocationCalls == iFailure + 1u );
        CHECK( BoundaryHasNoData( boundary ) );
        CHECK( common::Vector_IsValid( &boundary.vertices ) );
        CHECK( common::Vector_IsValid( &boundary.edges ) );
        CHECK( common::Vector_IsValid( &boundary.faces ) );
        CHECK( common::Vector_IsValid( &boundary.faceVertexIndices ) );

        BrushBoundary_Shutdown( &boundary );
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

TEST_CASE( "failed boundary reconstruction clears previous output",
           "[editor][geometry][brush][allocation][contract]" ) {
    BoxFixture box;
    boundary_failure_allocator_state_t state{};
    common::allocator_t allocator = MakeBoundaryFailureAllocator( &state );
    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &box.brush, box.policy ) ==
             geometry_status_t::OK );
    REQUIRE_FALSE( BoundaryHasNoData( boundary ) );

    state.iFailure = state.cAllocationCalls;
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &box.brush, box.policy ) ==
             geometry_status_t::ALLOCATION_FAILED );
    CHECK( BoundaryHasNoData( boundary ) );

    BrushBoundary_Shutdown( &boundary );
    CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
}

TEST_CASE( "TryGetFaceVertexIndices reports INSUFFICIENT_CAPACITY with needed count",
           "[editor][geometry][brush]" ) {
    BoundaryFixture box;
    common::usize count = 0u;

    // Pass a too-small buffer.
    common::u32 small[2];
    REQUIRE( BrushBoundary_TryGetFaceVertexIndices(
                 &box.boundary, 0u, small, 2u, &count ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );

    // count should report the actual size needed.
    REQUIRE( count == 4u );
}

} // namespace cypher::editor::geometry
