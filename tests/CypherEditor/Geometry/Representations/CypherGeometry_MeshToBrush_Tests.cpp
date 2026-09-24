//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshToBrush_Tests.cpp
//  Purpose: Gate 13 contract tests for the mesh-to-brush round-trip.
//  Details: Verifies that converting a mesh back to a brush_solid_t
//           produces valid plane sets that reconstruct to the same
//           geometry. The canonical test path is:
//             brush → boundary → mesh → MeshToBrush → brush₂ → boundary₂
//           then compare the two boundaries for geometric equivalence.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshToBrush.h"
#include "CypherGeometry_MeshBuilder.h"
#include "CypherGeometry_EditableMesh.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Vec3d_Dot;
using cypher::math::Vec3d_LengthSquared;
using Catch::Approx;

namespace {

// Builds the full round-trip pipeline: brush → boundary → mesh.
// Caller owns all three output objects and must shut them down.
struct RoundTripFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};

    brush_solid_t brushSrc{};
    brush_boundary_t boundarySrc{};
    editable_mesh_t mesh{};

    // The converted-back objects.
    brush_solid_t brushDst{};
    brush_boundary_t boundaryDst{};

    void BuildBox( math::vec3d_t origin, math::vec3d_t halfExtent ) {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brushSrc, &allocator, policy, &idAllocator,
                     origin, halfExtent ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundarySrc, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundarySrc, &brushSrc, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshBuilder_TryBuildFromBoundary( &mesh, &boundarySrc ) ==
                 geometry_status_t::OK );
    }

    // Convert mesh back to a brush and reconstruct its boundary.
    geometry_status_t ConvertBack() {
        const geometry_status_t s = MeshToBrush_TryConvert(
            &mesh, &brushDst, &allocator, policy, &idAllocator );
        if ( s != geometry_status_t::OK ) { return s; }

        const geometry_status_t s2 = BrushBoundary_Init(
            &boundaryDst, &allocator );
        if ( s2 != geometry_status_t::OK ) { return s2; }

        return BrushBoundary_TryReconstruct(
            &boundaryDst, &brushDst, policy );
    }

    ~RoundTripFixture() {
        BrushBoundary_Shutdown( &boundaryDst );
        BrushSolid_Shutdown( &brushDst );
        EditableMesh_Shutdown( &mesh );
        BrushBoundary_Shutdown( &boundarySrc );
        BrushSolid_Shutdown( &brushSrc );
    }
};

struct conversion_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ 0u };
    common::usize cLiveAllocations{ 0u };
};

void *ConversionFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<conversion_failure_allocator_state_t *>(
        pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) {
        return nullptr;
    }
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    void *pMemory = pSystem->pfnAllocate(
        pSystem->pUserData, cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cLiveAllocations;
    }
    return pMemory;
}

void ConversionFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<conversion_failure_allocator_state_t *>(
        pUserData );
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    pSystem->pfnFree(
        pSystem->pUserData, pMemory, cbSize, nAlignment );
    --pState->cLiveAllocations;
}

common::allocator_t MakeConversionFailureAllocator(
    conversion_failure_allocator_state_t *pState ) noexcept
{
    return {
        &ConversionFailureAllocate,
        nullptr,
        &ConversionFailureFree,
        pState
    };
}

bool BrushIsCanonicalEmpty( const brush_solid_t &brush ) noexcept
{
    return brush.sides.pData == nullptr &&
           brush.sides.nCount == 0u &&
           brush.sides.nCapacity == 0u &&
           brush.sides.pAllocator == nullptr &&
           !GeometrySourceId_IsValid( brush.sourceId );
}

} // anon namespace

// ---------------------------------------------------------------------------
// MeshToBrush round-trip (Gate 13)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshToBrush: box round-trip preserves side count",
           "[Gate13][MeshToBrush]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( f.ConvertBack() == geometry_status_t::OK );

    // A box has 6 sides — the round-trip must produce exactly 6 planes.
    CHECK( BrushSolid_SideCount( &f.brushDst ) == 6u );
}

TEST_CASE( "MeshToBrush: round-trip boundary has same vertex count",
           "[Gate13][MeshToBrush]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 2.0, 1.0, 0.5 ) );

    REQUIRE( f.ConvertBack() == geometry_status_t::OK );

    // Both boundaries should have 8 vertices for a box.
    CHECK( BrushBoundary_VertexCount( &f.boundaryDst ) ==
           BrushBoundary_VertexCount( &f.boundarySrc ) );
}

TEST_CASE( "MeshToBrush: round-trip boundary has same edge count",
           "[Gate13][MeshToBrush]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( f.ConvertBack() == geometry_status_t::OK );

    CHECK( BrushBoundary_EdgeCount( &f.boundaryDst ) ==
           BrushBoundary_EdgeCount( &f.boundarySrc ) );
}

TEST_CASE( "MeshToBrush: round-trip boundary has same face count",
           "[Gate13][MeshToBrush]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( f.ConvertBack() == geometry_status_t::OK );

    CHECK( BrushBoundary_FaceCount( &f.boundaryDst ) ==
           BrushBoundary_FaceCount( &f.boundarySrc ) );
}

TEST_CASE( "MeshToBrush: round-trip planes are unit-normal",
           "[Gate13][MeshToBrush]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( f.ConvertBack() == geometry_status_t::OK );

    // Every side plane should have a unit-length normal.
    const common::usize cSides = BrushSolid_SideCount( &f.brushDst );
    for ( common::usize i = 0u; i < cSides; ++i ) {
        brush_solid_side_t side{};
        REQUIRE( BrushSolid_TryGetSide( &f.brushDst, i, &side ) ==
                 geometry_status_t::OK );
        const common::f64 lenSq = Vec3d_LengthSquared( side.plane.normal );
        CHECK( lenSq == Approx( 1.0 ).epsilon( 1e-10 ) );
    }
}

TEST_CASE( "MeshToBrush: round-trip passes quick validation",
           "[Gate13][MeshToBrush]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( f.ConvertBack() == geometry_status_t::OK );

    CHECK( BrushValidation_Quick( &f.brushDst, f.policy ) ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshToBrush: non-cube box round-trip preserves geometry",
           "[Gate13][MeshToBrush]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 5.0, -3.0, 2.0 ),
                Vec3d_Make( 4.0, 2.0, 1.0 ) );

    REQUIRE( f.ConvertBack() == geometry_status_t::OK );

    CHECK( BrushSolid_SideCount( &f.brushDst ) == 6u );
    CHECK( BrushBoundary_VertexCount( &f.boundaryDst ) == 8u );
    CHECK( BrushBoundary_EdgeCount( &f.boundaryDst ) == 12u );
    CHECK( BrushBoundary_FaceCount( &f.boundaryDst ) == 6u );
}

TEST_CASE( "MeshToBrush: null mesh rejected",
           "[Gate13][MeshToBrush]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};

    CHECK( MeshToBrush_TryConvert(
               nullptr, &brush, &allocator, policy, &idAllocator ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshToBrush: null brush output rejected",
           "[Gate13][MeshToBrush]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );

    CHECK( MeshToBrush_TryConvert(
               &f.mesh, nullptr, &f.allocator, f.policy, &f.idAllocator ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshToBrush: null allocator rejected",
           "[Gate13][MeshToBrush]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_solid_t brushOut{};
    CHECK( MeshToBrush_TryConvert(
               &f.mesh, &brushOut, nullptr, f.policy, &f.idAllocator ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshToBrush: null ID allocator rejected",
           "[Gate13][MeshToBrush]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_solid_t brushOut{};
    CHECK( MeshToBrush_TryConvert(
               &f.mesh, &brushOut, &f.allocator, f.policy, nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshToBrush: uninitialized mesh rejected",
           "[Gate13][MeshToBrush]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    editable_mesh_t emptyMesh{};
    brush_solid_t brushOut{};

    CHECK( MeshToBrush_TryConvert(
               &emptyMesh, &brushOut, &allocator, policy, &idAllocator ) ==
           geometry_status_t::NOT_INITIALIZED );
}

TEST_CASE( "MeshToBrush: allocation failure leaves output and IDs unchanged",
           "[Gate13][MeshToBrush][allocation][contract]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );
    const geometry_source_id_t nextBefore = f.idAllocator.next;

    bool bObservedFailure = false;
    bool bReachedSuccess = false;
    for ( common::usize iFail = 1u; iFail <= 8u; ++iFail ) {
        conversion_failure_allocator_state_t state{};
        state.iFailOnCall = iFail;
        common::allocator_t allocator =
            MakeConversionFailureAllocator( &state );
        geometry_source_id_allocator_t ids = f.idAllocator;
        brush_solid_t output{};

        const geometry_status_t status = MeshToBrush_TryConvert(
            &f.mesh, &output, &allocator, f.policy, &ids );
        if ( status == geometry_status_t::ALLOCATION_FAILED ) {
            bObservedFailure = true;
            CHECK( BrushIsCanonicalEmpty( output ) );
            CHECK( ids.next.value == nextBefore.value );
            CHECK( state.cLiveAllocations == 0u );
        } else {
            REQUIRE( status == geometry_status_t::OK );
            CHECK( BrushSolid_SideCount( &output ) == 6u );
            CHECK( ids.next.value == nextBefore.value + 7u );
            bReachedSuccess = true;
        }

        BrushSolid_Shutdown( &output );
        CHECK( state.cLiveAllocations == 0u );
        if ( bReachedSuccess ) {
            break;
        }
    }

    CHECK( bObservedFailure );
    CHECK( bReachedSuccess );
}

TEST_CASE( "MeshToBrush: ID exhaustion is failure-atomic",
           "[Gate13][MeshToBrush][identity][contract]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );

    geometry_source_id_allocator_t ids{};
    ids.next = geometry_source_id_t{ common::CY_U64_MAX - 2u };
    const geometry_source_id_t nextBefore = ids.next;
    brush_solid_t output{};

    CHECK( MeshToBrush_TryConvert(
               &f.mesh, &output, &f.allocator, f.policy, &ids ) ==
           geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( BrushIsCanonicalEmpty( output ) );
    CHECK( ids.next.value == nextBefore.value );
}

TEST_CASE( "MeshToBrush: initialized output is rejected unchanged",
           "[Gate13][MeshToBrush][contract]" )
{
    RoundTripFixture f;
    f.BuildBox( Vec3d_Make( 0.0, 0.0, 0.0 ),
                Vec3d_Make( 1.0, 1.0, 1.0 ) );
    brush_solid_t output{};
    REQUIRE( BrushSolid_Init(
                 &output, &f.allocator, geometry_source_id_t{ 900u } ) ==
             geometry_status_t::OK );
    const geometry_source_id_t nextBefore = f.idAllocator.next;

    CHECK( MeshToBrush_TryConvert(
               &f.mesh, &output, &f.allocator,
               f.policy, &f.idAllocator ) ==
           geometry_status_t::ALREADY_INITIALIZED );
    CHECK( output.sourceId.value == 900u );
    CHECK( BrushSolid_SideCount( &output ) == 0u );
    CHECK( f.idAllocator.next.value == nextBefore.value );

    BrushSolid_Shutdown( &output );
}

} // namespace cypher::editor::geometry
