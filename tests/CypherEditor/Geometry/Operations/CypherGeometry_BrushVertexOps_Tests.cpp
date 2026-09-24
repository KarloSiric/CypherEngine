//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushVertexOps_Tests.cpp
//  Purpose: Contract tests for brush vertex manipulation operations.
//  Details: Tests the full modify-rebuild-validate pipeline for each
//           vertex operation: move vertex, move edge, move face, add
//           vertex, remove vertex, and snap-to-grid. Each test creates
//           a box brush, performs an edit, and verifies the result.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushVertexOps.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushQueries.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_Kernel_ConvexHull.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Vec3d_Add;
using cypher::math::Vec3d_Subtract;
using cypher::math::Vec3d_LengthSquared;
using Catch::Approx;

namespace {

struct vertex_ops_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFreeCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *VertexOpsFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<vertex_ops_failure_allocator_state_t *>(
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

void VertexOpsFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<vertex_ops_failure_allocator_state_t *>(
        pUserData );
    ++pState->cFreeCalls;
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeVertexOpsFailureAllocator(
    vertex_ops_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        VertexOpsFailureAllocate,
        nullptr,
        VertexOpsFailureFree,
        pState
    };
}

bool SidesEqual(
    const brush_solid_side_t &a,
    const brush_solid_side_t &b ) noexcept
{
    return a.plane.normal.x == b.plane.normal.x &&
           a.plane.normal.y == b.plane.normal.y &&
           a.plane.normal.z == b.plane.normal.z &&
           a.plane.d == b.plane.d &&
           a.sourceId.value == b.sourceId.value &&
           a.iAttributeIndex == b.iAttributeIndex;
}

// Helper fixture that creates a unit box brush with reconstructed boundary.
struct VertexOpsFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};

    VertexOpsFixture() {
        REQUIRE( BrushGenerator_TryMakeBox(
            &brush, &allocator, policy, &idAllocator,
            Vec3d_Make( 0.0, 0.0, 0.0 ),
            Vec3d_Make( 64.0, 64.0, 64.0 ) ) == geometry_status_t::OK );

        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
            &boundary, &brush, policy ) == geometry_status_t::OK );
    }

    // Reconstructs boundary after an edit so subsequent queries work.
    void Reconstruct() {
        REQUIRE( BrushBoundary_TryReconstruct(
            &boundary, &brush, policy ) == geometry_status_t::OK );
    }

    ~VertexOpsFixture() {
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
};

// Checks whether any boundary vertex is close to the target position.
bool HasVertexNear(
    const brush_boundary_t &boundary,
    math::vec3d_t target,
    common::f64 tolerance = 0.1 )
{
    const common::usize count = BrushBoundary_VertexCount( &boundary );
    const math::vec3d_t *pVerts =
        common::Vector_Data( &boundary.vertices );
    for ( common::usize i = 0u; i < count; ++i ) {
        const math::vec3d_t diff = Vec3d_Subtract( pVerts[i], target );
        if ( Vec3d_LengthSquared( diff ) < tolerance * tolerance ) {
            return true;
        }
    }
    return false;
}

} // namespace

// ===========================================================================
// MoveVertex
// ===========================================================================

TEST_CASE( "BrushVertexOps: move vertex produces valid brush",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    // Move vertex 0 by (10, 0, 0).
    math::vec3d_t oldPos{};
    REQUIRE( BrushQueries_TryGetVertex( &f.boundary, 0, &oldPos ) ==
             geometry_status_t::OK );
    const math::vec3d_t newPos = Vec3d_Add( oldPos, Vec3d_Make( 10.0, 0.0, 0.0 ) );

    const auto result = BrushVertexOps_TryMoveVertex(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0, newPos );

    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.cVertices >= 4u );
    REQUIRE( result.cFaces >= 4u );

    // Deep-validate the modified brush independently.
    const auto validation = BrushValidation_Deep(
        &f.brush, f.policy, &f.allocator );
    REQUIRE( validation.status == geometry_status_t::OK );
    REQUIRE( validation.bWatertight );
}

TEST_CASE( "BrushVertexOps: moved vertex appears in new boundary",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const math::vec3d_t newPos = Vec3d_Make( 80.0, 32.0, 32.0 );

    const auto result = BrushVertexOps_TryMoveVertex(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0, newPos );
    REQUIRE( result.status == geometry_status_t::OK );

    // Reconstruct and check the vertex appears.
    f.Reconstruct();
    REQUIRE( HasVertexNear( f.boundary, newPos ) );
}

TEST_CASE( "BrushVertexOps: move vertex preserves brush identity",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;
    const geometry_source_id_t originalId = f.brush.sourceId;

    const math::vec3d_t newPos = Vec3d_Make( -10.0, -10.0, -10.0 );
    const auto result = BrushVertexOps_TryMoveVertex(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0, newPos );
    REQUIRE( result.status == geometry_status_t::OK );

    // Brush source ID must survive the rebuild.
    REQUIRE( f.brush.sourceId.value == originalId.value );
}

TEST_CASE( "BrushVertexOps: move vertex out-of-range rejected",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const auto result = BrushVertexOps_TryMoveVertex(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        999, Vec3d_Make( 0.0, 0.0, 0.0 ) );
    REQUIRE( result.status == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "BrushVertexOps: move vertex null args rejected",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const auto result = BrushVertexOps_TryMoveVertex(
        nullptr, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0, Vec3d_Make( 0.0, 0.0, 0.0 ) );
    REQUIRE( result.status == geometry_status_t::INVALID_ARGUMENT );
}

// ===========================================================================
// MoveEdge
// ===========================================================================

TEST_CASE( "BrushVertexOps: move edge produces valid brush",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const auto result = BrushVertexOps_TryMoveEdge(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0, Vec3d_Make( 8.0, 0.0, 0.0 ) );

    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.cVertices >= 4u );

    const auto validation = BrushValidation_Deep(
        &f.brush, f.policy, &f.allocator );
    REQUIRE( validation.status == geometry_status_t::OK );
    REQUIRE( validation.bWatertight );
}

TEST_CASE( "BrushVertexOps: move edge out-of-range rejected",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const auto result = BrushVertexOps_TryMoveEdge(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        999, Vec3d_Make( 0.0, 0.0, 0.0 ) );
    REQUIRE( result.status == geometry_status_t::INVALID_ARGUMENT );
}

// ===========================================================================
// MoveFace
// ===========================================================================

TEST_CASE( "BrushVertexOps: move face produces valid brush",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    // Move face 0 outward by 16 units.
    const auto result = BrushVertexOps_TryMoveFace(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0, Vec3d_Make( 16.0, 0.0, 0.0 ) );

    REQUIRE( result.status == geometry_status_t::OK );

    const auto validation = BrushValidation_Deep(
        &f.brush, f.policy, &f.allocator );
    REQUIRE( validation.status == geometry_status_t::OK );
    REQUIRE( validation.bWatertight );
}

TEST_CASE( "BrushVertexOps: move face preserves face count for box",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    // Translating a face along one axis on a box should keep 6 faces.
    const auto result = BrushVertexOps_TryMoveFace(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0, Vec3d_Make( 0.0, 0.0, 16.0 ) );

    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.cFaces == 6u );
}

TEST_CASE( "BrushVertexOps: move face out-of-range rejected",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const auto result = BrushVertexOps_TryMoveFace(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        999, Vec3d_Make( 0.0, 0.0, 0.0 ) );
    REQUIRE( result.status == geometry_status_t::INVALID_ARGUMENT );
}

// ===========================================================================
// AddVertex
// ===========================================================================

TEST_CASE( "BrushVertexOps: add exterior vertex grows brush",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const common::usize oldVertexCount =
        BrushBoundary_VertexCount( &f.boundary );

    // Add a point well outside the box.
    const math::vec3d_t newPt = Vec3d_Make( 96.0, 32.0, 32.0 );
    const auto result = BrushVertexOps_TryAddVertex(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        newPt );

    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.cVertices > oldVertexCount );

    f.Reconstruct();
    REQUIRE( HasVertexNear( f.boundary, newPt ) );
}

TEST_CASE( "BrushVertexOps: add interior vertex doesn't change hull",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    // Interior point — hull stays the same.
    const math::vec3d_t interior = Vec3d_Make( 32.0, 32.0, 32.0 );
    brush_solid_side_t *const pSidesBefore = f.brush.sides.pData;
    const common::usize cCapacityBefore = f.brush.sides.nCapacity;
    const common::u64 nextIdBefore = f.idAllocator.next.value;
    const auto result = BrushVertexOps_TryAddVertex(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        interior );

    REQUIRE( result.status == geometry_status_t::OK );
    // Vertex count unchanged — interior points are hull-interior.
    REQUIRE( result.cVertices == 8u );
    CHECK( f.brush.sides.pData == pSidesBefore );
    CHECK( f.brush.sides.nCapacity == cCapacityBefore );
    CHECK( f.idAllocator.next.value == nextIdBefore );
}

TEST_CASE( "BrushVertexOps: add vertex null rejected",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const auto result = BrushVertexOps_TryAddVertex(
        nullptr, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        Vec3d_Make( 0.0, 0.0, 0.0 ) );
    REQUIRE( result.status == geometry_status_t::INVALID_ARGUMENT );
}

// ===========================================================================
// RemoveVertex
// ===========================================================================

TEST_CASE( "BrushVertexOps: remove vertex from box produces valid brush",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    // Removing one corner from a box leaves a tetrahedron-like shape
    // with 7 vertices (still convex, still valid).
    const auto result = BrushVertexOps_TryRemoveVertex(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0 );

    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.cVertices == 7u );
    REQUIRE( result.cFaces >= 4u );

    const auto validation = BrushValidation_Deep(
        &f.brush, f.policy, &f.allocator );
    REQUIRE( validation.status == geometry_status_t::OK );
    REQUIRE( validation.bWatertight );
}

TEST_CASE( "BrushVertexOps: remove vertex from tetrahedron rejected",
           "[Gate11][VertexOps]" )
{
    // Build a tetrahedron (4 vertices — removing any leaves too few).
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};

    const math::vec3d_t tetraPoints[4] = {
        { 0.0, 0.0, 0.0 }, { 64.0, 0.0, 0.0 },
        { 32.0, 64.0, 0.0 }, { 32.0, 32.0, 64.0 }
    };

    brush_solid_t brush{};
    REQUIRE( ConvexHull_TryBuildBrush(
        &brush, &allocator, policy, &idAllocator,
        tetraPoints, 4u ) == geometry_status_t::OK );

    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
        &boundary, &brush, policy ) == geometry_status_t::OK );

    // Should be exactly 4 vertices.
    REQUIRE( BrushBoundary_VertexCount( &boundary ) == 4u );

    // Removing any vertex should fail — can't have <4 points.
    const auto result = BrushVertexOps_TryRemoveVertex(
        &brush, &boundary, &allocator, policy, &idAllocator, 0 );
    REQUIRE( result.status == geometry_status_t::DEGENERATE );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "BrushVertexOps: remove vertex out-of-range rejected",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const auto result = BrushVertexOps_TryRemoveVertex(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        999 );
    REQUIRE( result.status == geometry_status_t::INVALID_ARGUMENT );
}

// ===========================================================================
// SnapToGrid
// ===========================================================================

TEST_CASE( "BrushVertexOps: snap to grid produces valid brush",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    // Grid snap at spacing 8 — the 64-unit box is already aligned,
    // so this should produce the same brush.
    brush_solid_side_t *const pSidesBefore = f.brush.sides.pData;
    const common::u64 nextIdBefore = f.idAllocator.next.value;
    const auto result = BrushVertexOps_TrySnapToGrid(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        8.0 );

    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.cVertices == 8u );
    REQUIRE( result.cFaces == 6u );
    CHECK( f.brush.sides.pData == pSidesBefore );
    CHECK( f.idAllocator.next.value == nextIdBefore );
}

TEST_CASE( "BrushVertexOps: snap to coarse grid can merge vertices",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    // Snap to a grid larger than the brush — all 8 vertices collapse.
    const auto result = BrushVertexOps_TrySnapToGrid(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        256.0 );

    // All vertices snap to the same point → DEGENERATE.
    REQUIRE( result.status == geometry_status_t::DEGENERATE );
}

TEST_CASE( "BrushVertexOps: snap zero spacing rejected",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const auto result = BrushVertexOps_TrySnapToGrid(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0.0 );
    REQUIRE( result.status == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "BrushVertexOps: snap negative spacing rejected",
           "[Gate11][VertexOps]" )
{
    VertexOpsFixture f;

    const auto result = BrushVertexOps_TrySnapToGrid(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        -8.0 );
    REQUIRE( result.status == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "BrushVertexOps: rebuilt sides have unique identity and preserve matched attributes",
           "[Gate11][VertexOps][identity]" )
{
    VertexOpsFixture f;
    brush_solid_side_t original[6]{};
    for ( common::usize i = 0u; i < 6u; ++i ) {
        original[i] = f.brush.sides.pData[i];
        f.brush.sides.pData[i].iAttributeIndex =
            static_cast<common::u32>( 100u + i );
        original[i].iAttributeIndex = static_cast<common::u32>( 100u + i );
    }

    const auto result = BrushVertexOps_TryMoveVertex(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0u, Vec3d_Make( -80.0, -80.0, -80.0 ) );
    REQUIRE( result.status == geometry_status_t::OK );

    for ( common::usize i = 0u; i < f.brush.sides.nCount; ++i ) {
        const brush_solid_side_t &side = f.brush.sides.pData[i];
        REQUIRE( GeometrySourceId_IsValid( side.sourceId ) );
        for ( common::usize j = 0u; j < i; ++j ) {
            CHECK( side.sourceId.value !=
                   f.brush.sides.pData[j].sourceId.value );
        }
        for ( const brush_solid_side_t &oldSide : original ) {
            if ( side.sourceId.value == oldSide.sourceId.value ) {
                CHECK( side.iAttributeIndex == oldSide.iAttributeIndex );
            }
        }
    }
}

TEST_CASE( "BrushVertexOps: source ID exhaustion preserves brush and allocator",
           "[Gate11][VertexOps][identity][contract]" )
{
    VertexOpsFixture f;
    brush_solid_side_t *const pSidesBefore = f.brush.sides.pData;
    const common::usize cSidesBefore = f.brush.sides.nCount;
    const common::usize cCapacityBefore = f.brush.sides.nCapacity;
    const common::u64 brushIdBefore = f.brush.sourceId.value;
    brush_solid_side_t sidesBefore[6]{};
    std::copy_n( f.brush.sides.pData, 6u, sidesBefore );
    f.idAllocator.next = GEOMETRY_SOURCE_ID_INVALID;

    const auto result = BrushVertexOps_TryMoveVertex(
        &f.brush, &f.boundary, &f.allocator, f.policy, &f.idAllocator,
        0u, Vec3d_Make( -80.0, -80.0, -80.0 ) );
    CHECK( result.status == geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( f.brush.sides.pData == pSidesBefore );
    CHECK( f.brush.sides.nCount == cSidesBefore );
    CHECK( f.brush.sides.nCapacity == cCapacityBefore );
    CHECK( f.brush.sourceId.value == brushIdBefore );
    CHECK_FALSE( GeometrySourceId_IsValid( f.idAllocator.next ) );
    for ( common::usize i = 0u; i < 6u; ++i ) {
        CHECK( SidesEqual( f.brush.sides.pData[i], sidesBefore[i] ) );
    }
}

TEST_CASE( "BrushVertexOps: scratch limit rejects without mutation",
           "[Gate11][VertexOps][scratch][contract]" )
{
    VertexOpsFixture f;
    brush_solid_side_t *const pSidesBefore = f.brush.sides.pData;
    const common::u64 nextIdBefore = f.idAllocator.next.value;
    geometry_policy_t policy = f.policy;
    policy.limits.cbScratchMax = 1u;

    const auto result = BrushVertexOps_TryMoveVertex(
        &f.brush, &f.boundary, &f.allocator, policy, &f.idAllocator,
        0u, Vec3d_Make( -80.0, -80.0, -80.0 ) );
    CHECK( result.status == geometry_status_t::LIMIT_EXCEEDED );
    CHECK( f.brush.sides.pData == pSidesBefore );
    CHECK( f.idAllocator.next.value == nextIdBefore );
}

TEST_CASE( "BrushVertexOps: every allocation failure preserves brush and ID sequence",
           "[Gate11][VertexOps][allocation][contract]" )
{
    common::usize cOperationAllocations = 0u;
    {
        vertex_ops_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeVertexOpsFailureAllocator( &state );
        geometry_policy_t policy{};
        geometry_source_id_allocator_t ids{};
        brush_solid_t brush{};
        brush_boundary_t boundary{};
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &ids,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 64.0, 64.0, 64.0 ) ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &brush, policy ) == geometry_status_t::OK );
        const common::usize cBefore = state.cAllocationCalls;
        REQUIRE( BrushVertexOps_TryMoveVertex(
                     &brush, &boundary, &allocator, policy, &ids, 0u,
                     Vec3d_Make( -80.0, -80.0, -80.0 ) ).status ==
                 geometry_status_t::OK );
        cOperationAllocations = state.cAllocationCalls - cBefore;
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
        REQUIRE( state.cSuccessfulAllocations == state.cFreeCalls );
    }
    REQUIRE( cOperationAllocations > 0u );

    for ( common::usize iFailure = 0u;
          iFailure < cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        vertex_ops_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeVertexOpsFailureAllocator( &state );
        geometry_policy_t policy{};
        geometry_source_id_allocator_t ids{};
        brush_solid_t brush{};
        brush_boundary_t boundary{};
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &ids,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 64.0, 64.0, 64.0 ) ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &brush, policy ) == geometry_status_t::OK );

        brush_solid_side_t *const pSidesBefore = brush.sides.pData;
        const common::usize cSidesBefore = brush.sides.nCount;
        const common::usize cCapacityBefore = brush.sides.nCapacity;
        const common::u64 brushIdBefore = brush.sourceId.value;
        const common::u64 nextIdBefore = ids.next.value;
        brush_solid_side_t sidesBefore[6]{};
        std::copy_n( brush.sides.pData, 6u, sidesBefore );
        state.iFailure = state.cAllocationCalls + iFailure;

        const auto result = BrushVertexOps_TryMoveVertex(
            &brush, &boundary, &allocator, policy, &ids, 0u,
            Vec3d_Make( -80.0, -80.0, -80.0 ) );
        CHECK( result.status == geometry_status_t::ALLOCATION_FAILED );
        CHECK( result.cVertices == 0u );
        CHECK( result.cEdges == 0u );
        CHECK( result.cFaces == 0u );
        CHECK( brush.sides.pData == pSidesBefore );
        CHECK( brush.sides.nCount == cSidesBefore );
        CHECK( brush.sides.nCapacity == cCapacityBefore );
        CHECK( brush.sourceId.value == brushIdBefore );
        CHECK( ids.next.value == nextIdBefore );
        for ( common::usize i = 0u; i < 6u; ++i ) {
            CHECK( SidesEqual( brush.sides.pData[i], sidesBefore[i] ) );
        }

        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

} // namespace cypher::editor::geometry
