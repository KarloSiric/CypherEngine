//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushValidation_Tests.cpp
//  Purpose: Verifies defensive quick and deep brush validation contracts.
//  Details: Covers storage, policy, identity, numerical, topology, result
//           publication, allocator-failure, and leak-free unwind behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace cypher::editor::geometry
{

namespace
{

struct validation_box_fixture_t {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    brush_solid_t brush{};

    validation_box_fixture_t()
    {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush,
                     &allocator,
                     policy,
                     &ids,
                     math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                     math::Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );
    }

    ~validation_box_fixture_t()
    {
        BrushSolid_Shutdown( &brush );
    }
};

struct validation_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFreeCalls{ 0u };
    common::usize cLiveAllocations{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *ValidationFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<validation_failure_allocator_state_t *>( pUserData );
    const common::usize iAllocation = pState->cAllocationCalls++;
    if ( iAllocation == pState->iFailure ) {
        return nullptr;
    }

    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
        ++pState->cLiveAllocations;
    }
    return pMemory;
}

void ValidationFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<validation_failure_allocator_state_t *>( pUserData );
    ++pState->cFreeCalls;
    --pState->cLiveAllocations;
    common::Allocator_Free(
        common::Allocator_GetSystem(),
        pMemory,
        cbSize,
        nAlignment );
}

common::allocator_t MakeValidationFailureAllocator(
    validation_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        ValidationFailureAllocate,
        nullptr,
        ValidationFailureFree,
        pState
    };
}

void CheckUnpublishedFailure(
    const brush_validation_result_t &result,
    geometry_status_t expectedStatus )
{
    CHECK( result.status == expectedStatus );
    CHECK_FALSE( result.bWatertight );
    CHECK( result.cVertices == 0u );
    CHECK( result.cEdges == 0u );
    CHECK( result.cFaces == 0u );
    CHECK( result.eulerCharacteristic == 0 );
}

} // namespace

TEST_CASE( "BrushValidation Quick rejects invalid lifecycle and storage states",
           "[editor][geometry][brush][validation][contract]" )
{
    const geometry_policy_t policy{};

    CHECK( BrushValidation_Quick( nullptr, policy ) ==
           geometry_status_t::NOT_INITIALIZED );

    brush_solid_t empty{};
    CHECK( BrushValidation_Quick( &empty, policy ) ==
           geometry_status_t::NOT_INITIALIZED );

    empty.sourceId = geometry_source_id_t{ 1u };
    CHECK( BrushValidation_Quick( &empty, policy ) ==
           geometry_status_t::CORRUPT_STATE );

    validation_box_fixture_t fixture;
    const common::usize cCountBefore = fixture.brush.sides.nCount;
    fixture.brush.sides.nCount = fixture.brush.sides.nCapacity + 1u;
    const geometry_status_t badCountStatus =
        BrushValidation_Quick( &fixture.brush, fixture.policy );
    fixture.brush.sides.nCount = cCountBefore;
    CHECK( badCountStatus == geometry_status_t::CORRUPT_STATE );

    const common::allocator_t allocatorBefore = fixture.allocator;
    fixture.allocator.pfnFree = nullptr;
    const geometry_status_t badAllocatorStatus =
        BrushValidation_Quick( &fixture.brush, fixture.policy );
    fixture.allocator = allocatorBefore;
    CHECK( badAllocatorStatus == geometry_status_t::CORRUPT_STATE );
}

TEST_CASE( "BrushValidation Quick validates policy before inspecting sides",
           "[editor][geometry][brush][validation][contract]" )
{
    validation_box_fixture_t fixture;

    geometry_policy_t invalidNumerical = fixture.policy;
    invalidNumerical.numerical.fAbsoluteDistanceTolerance = 0.0;
    CHECK( BrushValidation_Quick( &fixture.brush, invalidNumerical ) ==
           geometry_status_t::INVALID_ARGUMENT );

    geometry_policy_t invalidLimits = fixture.policy;
    invalidLimits.limits.cBrushSidesPerBrushMax = 0u;
    CHECK( BrushValidation_Quick( &fixture.brush, invalidLimits ) ==
           geometry_status_t::INVALID_ARGUMENT );

    geometry_policy_t bounded = fixture.policy;
    bounded.limits.cBrushSidesPerBrushMax = 5u;
    REQUIRE( GeometryPolicy_IsValid( bounded ) );
    CHECK( BrushValidation_Quick( &fixture.brush, bounded ) ==
           geometry_status_t::LIMIT_EXCEEDED );
}

TEST_CASE( "BrushValidation Quick validates canonical source identities",
           "[editor][geometry][brush][validation][identity]" )
{
    SECTION( "invalid brush identity is corrupt state" )
    {
        validation_box_fixture_t fixture;
        fixture.brush.sourceId = GEOMETRY_SOURCE_ID_INVALID;
        CHECK( BrushValidation_Quick( &fixture.brush, fixture.policy ) ==
               geometry_status_t::CORRUPT_STATE );
    }

    SECTION( "invalid side identity is corrupt state" )
    {
        validation_box_fixture_t fixture;
        fixture.brush.sides.pData[2u].sourceId =
            GEOMETRY_SOURCE_ID_INVALID;
        CHECK( BrushValidation_Quick( &fixture.brush, fixture.policy ) ==
               geometry_status_t::CORRUPT_STATE );
    }

    SECTION( "side identity cannot equal brush identity" )
    {
        validation_box_fixture_t fixture;
        fixture.brush.sides.pData[2u].sourceId =
            fixture.brush.sourceId;
        CHECK( BrushValidation_Quick( &fixture.brush, fixture.policy ) ==
               geometry_status_t::IDENTITY_CONFLICT );
    }

    SECTION( "side identities are unique and precede plane diagnostics" )
    {
        validation_box_fixture_t fixture;
        fixture.brush.sides.pData[3u].sourceId =
            fixture.brush.sides.pData[1u].sourceId;
        fixture.brush.sides.pData[3u].plane.d =
            std::numeric_limits<math::f64>::quiet_NaN();
        CHECK( BrushValidation_Quick( &fixture.brush, fixture.policy ) ==
               geometry_status_t::IDENTITY_CONFLICT );
    }
}

TEST_CASE( "BrushValidation Quick validates finite normalized bounded planes",
           "[editor][geometry][brush][validation][numeric]" )
{
    SECTION( "non-finite plane" )
    {
        validation_box_fixture_t fixture;
        fixture.brush.sides.pData[0u].plane.normal.x =
            std::numeric_limits<math::f64>::infinity();
        CHECK( BrushValidation_Quick( &fixture.brush, fixture.policy ) ==
               geometry_status_t::NUMERIC_FAILURE );
    }

    SECTION( "non-unit plane normal" )
    {
        validation_box_fixture_t fixture;
        fixture.brush.sides.pData[0u].plane.normal.x = 2.0;
        CHECK( BrushValidation_Quick( &fixture.brush, fixture.policy ) ==
               geometry_status_t::DEGENERATE );
    }

    SECTION( "plane distance beyond coordinate limit" )
    {
        validation_box_fixture_t fixture;
        fixture.brush.sides.pData[0u].plane.d =
            fixture.policy.numerical.fCoordinateMagnitudeLimit + 1.0;
        CHECK( BrushValidation_Quick( &fixture.brush, fixture.policy ) ==
               geometry_status_t::LIMIT_EXCEEDED );
    }
}

TEST_CASE( "BrushValidation preserves Quick plausibility and Deep strictness",
           "[editor][geometry][brush][validation][contract]" )
{
    validation_box_fixture_t fixture;
    const geometry_source_id_result_t sideId =
        GeometrySourceIdAllocator_Allocate( &fixture.ids );
    REQUIRE( sideId.status == geometry_status_t::OK );

    // x <= 0 is a valid new constraint, but makes the original x <= 1 side
    // redundant. Quick accepts the plausible plane set; Deep requires every
    // authored side to contribute exactly one face.
    const brush_solid_side_t redundant{
        math::Planed_Make(
            math::Vec3d_Make( 1.0, 0.0, 0.0 ),
            0.0 ),
        sideId.id,
        0u
    };
    REQUIRE( BrushSolid_TryAddSide(
                 &fixture.brush,
                 fixture.policy.limits,
                 redundant,
                 nullptr ) == geometry_status_t::OK );

    CHECK( BrushValidation_Quick( &fixture.brush, fixture.policy ) ==
           geometry_status_t::OK );
    const brush_validation_result_t result = BrushValidation_Deep(
        &fixture.brush, fixture.policy, &fixture.allocator );
    CheckUnpublishedFailure( result, geometry_status_t::DEGENERATE );
}

TEST_CASE( "BrushValidation Deep validates allocator and clears failure output",
           "[editor][geometry][brush][validation][contract]" )
{
    validation_box_fixture_t fixture;

    CheckUnpublishedFailure(
        BrushValidation_Deep(
            &fixture.brush, fixture.policy, nullptr ),
        geometry_status_t::INVALID_ARGUMENT );

    common::allocator_t invalidAllocator{};
    CheckUnpublishedFailure(
        BrushValidation_Deep(
            &fixture.brush, fixture.policy, &invalidAllocator ),
        geometry_status_t::INVALID_ARGUMENT );

    geometry_policy_t invalidPolicy = fixture.policy;
    invalidPolicy.numerical.fAbsoluteDistanceTolerance = 0.0;
    CheckUnpublishedFailure(
        BrushValidation_Deep(
            &fixture.brush, invalidPolicy, &fixture.allocator ),
        geometry_status_t::INVALID_ARGUMENT );

    const brush_validation_result_t success = BrushValidation_Deep(
        &fixture.brush, fixture.policy, &fixture.allocator );
    REQUIRE( success.status == geometry_status_t::OK );
    REQUIRE( success.bWatertight );
    REQUIRE( success.cVertices == 8u );
    REQUIRE( success.cEdges == 12u );
    REQUIRE( success.cFaces == 6u );
    REQUIRE( success.eulerCharacteristic == 2 );

    fixture.brush.sides.pData[0u].plane.normal.x = 2.0;
    CheckUnpublishedFailure(
        BrushValidation_Deep(
            &fixture.brush, fixture.policy, &fixture.allocator ),
        geometry_status_t::DEGENERATE );
}

TEST_CASE( "BrushValidation Deep unwinds every allocation failure",
           "[editor][geometry][brush][validation][allocation]" )
{
    validation_box_fixture_t fixture;

    validation_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeValidationFailureAllocator( &baselineState );
    const brush_validation_result_t baseline = BrushValidation_Deep(
        &fixture.brush, fixture.policy, &baselineAllocator );
    REQUIRE( baseline.status == geometry_status_t::OK );
    REQUIRE( baseline.bWatertight );
    REQUIRE( baselineState.cAllocationCalls > 0u );
    REQUIRE( baselineState.cLiveAllocations == 0u );
    REQUIRE( baselineState.cSuccessfulAllocations ==
             baselineState.cFreeCalls );

    const common::usize cAllocationAttempts =
        baselineState.cAllocationCalls;
    for ( common::usize iFailure = 0u;
          iFailure < cAllocationAttempts;
          ++iFailure ) {
        CAPTURE( iFailure, cAllocationAttempts );

        validation_failure_allocator_state_t state{};
        state.iFailure = iFailure;
        common::allocator_t allocator =
            MakeValidationFailureAllocator( &state );
        const brush_validation_result_t result = BrushValidation_Deep(
            &fixture.brush, fixture.policy, &allocator );

        CheckUnpublishedFailure(
            result, geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cAllocationCalls == iFailure + 1u );
        CHECK( state.cLiveAllocations == 0u );
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

} // namespace cypher::editor::geometry
