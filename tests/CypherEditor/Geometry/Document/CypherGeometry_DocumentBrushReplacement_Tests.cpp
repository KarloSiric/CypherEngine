//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentBrushReplacement_Tests.cpp
//  Purpose: Verifies atomic exact-ID replacement of document brushes.
//  Details: Covers N-to-M publication, empty-range semantics, document-wide
//           identity conflicts, limits, and every allocation-failure point.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentBrushReplacement.h"
#include "CypherGeometry_BrushGenerator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>

namespace cypher::editor::geometry
{

namespace
{

void MakeBox(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIds,
    common::f64 x )
{
    REQUIRE( BrushGenerator_TryMakeBox(
                 pBrush,
                 pAllocator,
                 policy,
                 pIds,
                 math::Vec3d_Make( x, 0.0, 0.0 ),
                 math::Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
}

void CheckBrushIdsLive(
    const geometry_document_t &document,
    const brush_solid_t &brush,
    bool bExpectedLive )
{
    CHECK( GeometrySourceIdRegistry_Contains(
               &document.sourceIds, brush.sourceId ) == bExpectedLive );
    for ( common::usize iSide = 0u;
          iSide < brush.sides.nCount;
          ++iSide ) {
        CHECK( GeometrySourceIdRegistry_Contains(
                   &document.sourceIds,
                   brush.sides.pData[iSide].sourceId ) == bExpectedLive );
    }
}

void CheckBrushIdsClaimed(
    const geometry_document_t &document,
    const brush_solid_t &brush,
    bool bExpectedClaimed )
{
    CHECK( common::HashSet_Contains(
               &document.sourceIds.claimedIds,
               brush.sourceId ) == bExpectedClaimed );
    for ( common::usize iSide = 0u;
          iSide < brush.sides.nCount;
          ++iSide ) {
        CHECK( common::HashSet_Contains(
                   &document.sourceIds.claimedIds,
                   brush.sides.pData[iSide].sourceId ) ==
               bExpectedClaimed );
    }
}

void CheckExactBrushCopy(
    const brush_solid_t &expected,
    const brush_solid_t &actual )
{
    REQUIRE( actual.sourceId.value == expected.sourceId.value );
    REQUIRE( actual.sides.nCount == expected.sides.nCount );
    for ( common::usize iSide = 0u;
          iSide < expected.sides.nCount;
          ++iSide ) {
        CHECK( actual.sides.pData[iSide].sourceId.value ==
               expected.sides.pData[iSide].sourceId.value );
        CHECK( actual.sides.pData[iSide].plane.normal.x ==
               expected.sides.pData[iSide].plane.normal.x );
        CHECK( actual.sides.pData[iSide].plane.normal.y ==
               expected.sides.pData[iSide].plane.normal.y );
        CHECK( actual.sides.pData[iSide].plane.normal.z ==
               expected.sides.pData[iSide].plane.normal.z );
        CHECK( actual.sides.pData[iSide].plane.d ==
               expected.sides.pData[iSide].plane.d );
        CHECK( actual.sides.pData[iSide].iAttributeIndex ==
               expected.sides.pData[iSide].iAttributeIndex );
    }
}

struct document_observation_t {
    brush_solid_t **pBrushStorage{ nullptr };
    common::usize cBrushes{ 0u };
    common::usize cBrushCapacity{ 0u };
    brush_solid_t *pBrush0{ nullptr };
    brush_solid_t *pBrush1{ nullptr };
    brush_solid_side_t *pBrush0Sides{ nullptr };
    brush_solid_side_t *pBrush1Sides{ nullptr };
    const void *pClaimedSlots{ nullptr };
    const void *pLiveSlots{ nullptr };
    common::usize cClaimed{ 0u };
    common::usize cLive{ 0u };
    common::usize cClaimedCapacity{ 0u };
    common::usize cLiveCapacity{ 0u };
    common::u64 nextId{ 0u };
    bool bLoadRegistrationOpen{ false };
    geometry_revision_t revision{ 0u };
};

document_observation_t ObserveDocument(
    const geometry_document_t &document ) noexcept
{
    document_observation_t result{};
    result.pBrushStorage = document.brushes.pData;
    result.cBrushes = document.brushes.nCount;
    result.cBrushCapacity = document.brushes.nCapacity;
    if ( document.brushes.nCount > 0u ) {
        result.pBrush0 = document.brushes.pData[0u];
        result.pBrush0Sides = result.pBrush0->sides.pData;
    }
    if ( document.brushes.nCount > 1u ) {
        result.pBrush1 = document.brushes.pData[1u];
        result.pBrush1Sides = result.pBrush1->sides.pData;
    }
    result.pClaimedSlots = document.sourceIds.claimedIds.pSlots;
    result.pLiveSlots = document.sourceIds.liveIds.pSlots;
    result.cClaimed = document.sourceIds.claimedIds.nCount;
    result.cLive = document.sourceIds.liveIds.nCount;
    result.cClaimedCapacity = document.sourceIds.claimedIds.nCapacity;
    result.cLiveCapacity = document.sourceIds.liveIds.nCapacity;
    result.nextId = document.sourceIds.allocator.next.value;
    result.bLoadRegistrationOpen =
        document.sourceIds.bLoadRegistrationOpen;
    result.revision = document.revision;
    return result;
}

void CheckDocumentObservation(
    const geometry_document_t &document,
    const document_observation_t &expected )
{
    CHECK( document.brushes.pData == expected.pBrushStorage );
    CHECK( document.brushes.nCount == expected.cBrushes );
    CHECK( document.brushes.nCapacity == expected.cBrushCapacity );
    if ( document.brushes.nCount > 0u ) {
        CHECK( document.brushes.pData[0u] == expected.pBrush0 );
        CHECK( document.brushes.pData[0u]->sides.pData ==
               expected.pBrush0Sides );
    }
    if ( document.brushes.nCount > 1u ) {
        CHECK( document.brushes.pData[1u] == expected.pBrush1 );
        CHECK( document.brushes.pData[1u]->sides.pData ==
               expected.pBrush1Sides );
    }
    CHECK( document.sourceIds.claimedIds.pSlots ==
           expected.pClaimedSlots );
    CHECK( document.sourceIds.liveIds.pSlots == expected.pLiveSlots );
    CHECK( document.sourceIds.claimedIds.nCount == expected.cClaimed );
    CHECK( document.sourceIds.liveIds.nCount == expected.cLive );
    CHECK( document.sourceIds.claimedIds.nCapacity ==
           expected.cClaimedCapacity );
    CHECK( document.sourceIds.liveIds.nCapacity ==
           expected.cLiveCapacity );
    CHECK( document.sourceIds.allocator.next.value == expected.nextId );
    CHECK( document.sourceIds.bLoadRegistrationOpen ==
           expected.bLoadRegistrationOpen );
    CHECK( document.revision == expected.revision );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &document.sourceIds ) );
}

struct replacement_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFreeCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *ReplacementFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<replacement_failure_allocator_state_t *>( pUserData );
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

void ReplacementFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<replacement_failure_allocator_state_t *>( pUserData );
    ++pState->cFreeCalls;
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeReplacementFailureAllocator(
    replacement_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        ReplacementFailureAllocate,
        nullptr,
        ReplacementFailureFree,
        pState
    };
}

} // namespace

TEST_CASE(
    "Document exact brush replacement publishes N-to-M with preserved IDs",
    "[geometry][document][brush-replacement][identity]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init( &document, &allocator, policy ) ==
             geometry_status_t::OK );

    brush_solid_t original[2]{};
    MakeBox( &original[0], &allocator, policy, &ids, -4.0 );
    MakeBox( &original[1], &allocator, policy, &ids, 0.0 );
    REQUIRE( GeometryDocument_TryAddBrush( &document, &original[0] ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush( &document, &original[1] ) ==
             geometry_status_t::OK );

    brush_solid_t outputs[2]{};
    MakeBox( &outputs[0], &allocator, policy, &ids, 4.0 );
    MakeBox( &outputs[1], &allocator, policy, &ids, 8.0 );
    const geometry_source_id_t removals[]{ original[0].sourceId };
    const brush_solid_t *pRetainedBefore = GeometryDocument_FindBrush(
        &document, original[1].sourceId );
    REQUIRE( pRetainedBefore != nullptr );
    document.revision = 41u;

    REQUIRE( GeometryDocument_TryReplaceBrushesExact(
                 &document,
                 common::span_t<const geometry_source_id_t>{ removals, 1u },
                 common::span_t<const brush_solid_t>{ outputs, 2u } ) ==
             geometry_status_t::OK );

    CHECK( GeometryDocument_BrushCount( &document ) == 3u );
    CHECK( GeometryDocument_FindBrush(
               &document, original[0].sourceId ) == nullptr );
    CHECK( GeometryDocument_FindBrush(
               &document, original[1].sourceId ) == pRetainedBefore );
    for ( const brush_solid_t &output : outputs ) {
        const brush_solid_t *pStored = GeometryDocument_FindBrush(
            &document, output.sourceId );
        REQUIRE( pStored != nullptr );
        CHECK( pStored != &output );
        CheckExactBrushCopy( output, *pStored );
        CheckBrushIdsLive( document, output, true );
        CheckBrushIdsClaimed( document, output, true );
    }
    CheckBrushIdsLive( document, original[0], false );
    CheckBrushIdsClaimed( document, original[0], true );
    CheckBrushIdsLive( document, original[1], true );
    CHECK( document.sourceIds.allocator.next.value == ids.next.value );
    CHECK( document.revision == 41u );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &document.sourceIds ) );

    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Document exact brush replacement defines append delete and empty no-op",
    "[geometry][document][brush-replacement][empty-ranges]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init( &document, &allocator, policy ) ==
             geometry_status_t::OK );
    document.revision = 7u;

    const document_observation_t emptyBefore = ObserveDocument( document );
    REQUIRE( GeometryDocument_TryReplaceBrushesExact(
                 &document, {}, {} ) == geometry_status_t::OK );
    CheckDocumentObservation( document, emptyBefore );

    brush_solid_t output{};
    MakeBox( &output, &allocator, policy, &ids, 0.0 );
    REQUIRE( GeometryDocument_TryReplaceBrushesExact(
                 &document,
                 {},
                 common::span_t<const brush_solid_t>{ &output, 1u } ) ==
             geometry_status_t::OK );
    CHECK( GeometryDocument_BrushCount( &document ) == 1u );
    CHECK( GeometryDocument_FindBrush(
               &document, output.sourceId ) != nullptr );
    CHECK( document.revision == 7u );

    const geometry_source_id_t removal[]{ output.sourceId };
    REQUIRE( GeometryDocument_TryReplaceBrushesExact(
                 &document,
                 common::span_t<const geometry_source_id_t>{ removal, 1u },
                 {} ) == geometry_status_t::OK );
    CHECK( GeometryDocument_BrushCount( &document ) == 0u );
    CheckBrushIdsLive( document, output, false );
    CheckBrushIdsClaimed( document, output, true );
    CHECK( document.revision == 7u );

    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Document exact brush replacement rejects invalid sets atomically",
    "[geometry][document][brush-replacement][conflict]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init( &document, &allocator, policy ) ==
             geometry_status_t::OK );

    brush_solid_t original[2]{};
    MakeBox( &original[0], &allocator, policy, &ids, -4.0 );
    MakeBox( &original[1], &allocator, policy, &ids, 0.0 );
    REQUIRE( GeometryDocument_TryAddBrush( &document, &original[0] ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush( &document, &original[1] ) ==
             geometry_status_t::OK );

    brush_solid_t outputs[2]{};
    MakeBox( &outputs[0], &allocator, policy, &ids, 4.0 );
    MakeBox( &outputs[1], &allocator, policy, &ids, 8.0 );
    document.revision = 19u;
    const document_observation_t before = ObserveDocument( document );

    SECTION( "duplicate removals" ) {
        const geometry_source_id_t removals[]{
            original[0].sourceId,
            original[0].sourceId
        };
        CHECK( GeometryDocument_TryReplaceBrushesExact(
                   &document,
                   common::span_t<const geometry_source_id_t>{ removals, 2u },
                   {} ) == geometry_status_t::IDENTITY_CONFLICT );
    }

    SECTION( "nonexistent removal" ) {
        const geometry_source_id_t removal[]{
            geometry_source_id_t{ 999'999u }
        };
        CHECK( GeometryDocument_TryReplaceBrushesExact(
                   &document,
                   common::span_t<const geometry_source_id_t>{ removal, 1u },
                   {} ) == geometry_status_t::INVALID_ARGUMENT );
    }

    SECTION( "invalid output" ) {
        outputs[0].sides.pData[0u].sourceId =
            GEOMETRY_SOURCE_ID_INVALID;
        CHECK( GeometryDocument_TryReplaceBrushesExact(
                   &document,
                   {},
                   common::span_t<const brush_solid_t>{ &outputs[0], 1u } ) ==
               geometry_status_t::INVALID_ARGUMENT );
    }

    SECTION( "output side collides with retained brush root" ) {
        outputs[0].sides.pData[0u].sourceId =
            original[1].sourceId;
        const geometry_source_id_t removal[]{ original[0].sourceId };
        CHECK( GeometryDocument_TryReplaceBrushesExact(
                   &document,
                   common::span_t<const geometry_source_id_t>{ removal, 1u },
                   common::span_t<const brush_solid_t>{ &outputs[0], 1u } ) ==
               geometry_status_t::IDENTITY_CONFLICT );
    }

    SECTION( "output side collides with another output root" ) {
        outputs[1].sides.pData[0u].sourceId = outputs[0].sourceId;
        CHECK( GeometryDocument_TryReplaceBrushesExact(
                   &document,
                   {},
                   common::span_t<const brush_solid_t>{ outputs, 2u } ) ==
               geometry_status_t::IDENTITY_CONFLICT );
    }

    CheckDocumentObservation( document, before );
    CheckBrushIdsLive( document, original[0], true );
    CheckBrushIdsLive( document, original[1], true );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains(
        &document.sourceIds, outputs[0].sourceId ) );
    CHECK_FALSE( common::HashSet_Contains(
        &document.sourceIds.claimedIds, outputs[0].sourceId ) );

    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Document exact brush replacement enforces aggregate brush limits",
    "[geometry][document][brush-replacement][limits]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    policy.limits.cBrushesMax = 2u;
    REQUIRE( GeometryPolicy_IsValid( policy ) );
    geometry_source_id_allocator_t ids{};
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init( &document, &allocator, policy ) ==
             geometry_status_t::OK );

    brush_solid_t brushes[3]{};
    MakeBox( &brushes[0], &allocator, policy, &ids, -4.0 );
    MakeBox( &brushes[1], &allocator, policy, &ids, 0.0 );
    MakeBox( &brushes[2], &allocator, policy, &ids, 4.0 );
    REQUIRE( GeometryDocument_TryAddBrush( &document, &brushes[0] ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush( &document, &brushes[1] ) ==
             geometry_status_t::OK );
    const document_observation_t before = ObserveDocument( document );

    CHECK( GeometryDocument_TryReplaceBrushesExact(
               &document,
               {},
               common::span_t<const brush_solid_t>{ &brushes[2], 1u } ) ==
           geometry_status_t::LIMIT_EXCEEDED );
    CheckDocumentObservation( document, before );
    CheckBrushIdsLive( document, brushes[2], false );

    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Every exact brush replacement allocation failure preserves document bytes",
    "[geometry][document][brush-replacement][allocation]" )
{
    common::allocator_t sourceAllocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    brush_solid_t original[2]{};
    brush_solid_t outputs[2]{};
    MakeBox( &original[0], &sourceAllocator, policy, &ids, -4.0 );
    MakeBox( &original[1], &sourceAllocator, policy, &ids, 0.0 );
    MakeBox( &outputs[0], &sourceAllocator, policy, &ids, 4.0 );
    MakeBox( &outputs[1], &sourceAllocator, policy, &ids, 8.0 );
    const geometry_source_id_t removal[]{ original[0].sourceId };
    const auto removalSpan =
        common::span_t<const geometry_source_id_t>{ removal, 1u };
    const auto outputSpan =
        common::span_t<const brush_solid_t>{ outputs, 2u };

    replacement_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeReplacementFailureAllocator( &baselineState );
    geometry_document_t baseline{};
    REQUIRE( GeometryDocument_Init(
                 &baseline, &baselineAllocator, policy ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush(
                 &baseline, &original[0] ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush(
                 &baseline, &original[1] ) == geometry_status_t::OK );
    const common::usize iReplacementBegin =
        baselineState.cAllocationCalls;
    REQUIRE( GeometryDocument_TryReplaceBrushesExact(
                 &baseline, removalSpan, outputSpan ) ==
             geometry_status_t::OK );
    const common::usize cReplacementAllocations =
        baselineState.cAllocationCalls - iReplacementBegin;
    REQUIRE( cReplacementAllocations > 0u );
    GeometryDocument_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations ==
             baselineState.cFreeCalls );

    for ( common::usize iFailure = 0u;
          iFailure < cReplacementAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cReplacementAllocations );
        replacement_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeReplacementFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init( &document, &allocator, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrush(
                     &document, &original[0] ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrush(
                     &document, &original[1] ) == geometry_status_t::OK );
        document.revision = 73u;
        const document_observation_t before = ObserveDocument( document );

        const common::usize iFailureCall =
            state.cAllocationCalls + iFailure;
        state.iFailure = iFailureCall;
        CHECK( GeometryDocument_TryReplaceBrushesExact(
                   &document, removalSpan, outputSpan ) ==
               geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cAllocationCalls == iFailureCall + 1u );
        CheckDocumentObservation( document, before );
        CheckBrushIdsLive( document, original[0], true );
        CheckBrushIdsLive( document, original[1], true );
        CheckBrushIdsLive( document, outputs[0], false );
        CheckBrushIdsClaimed( document, outputs[0], false );
        CheckBrushIdsLive( document, outputs[1], false );
        CheckBrushIdsClaimed( document, outputs[1], false );

        state.iFailure = common::CY_USIZE_MAX;
        GeometryDocument_Shutdown( &document );
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

} // namespace cypher::editor::geometry
