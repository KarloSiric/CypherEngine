//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Transaction_Tests.cpp
//  Purpose: Verifies the begin/preview/commit/cancel transaction protocol,
//           typed deltas, inverse computation, and change sets.
//  Details: Directly targets the Gate 3 closing criterion: "Repeated
//           preview replacement of one canonical brush value creates one
//           committed revision and one inverse delta; cancel, invalid
//           input, stale revision, allocation failure, and cancellation
//           restore exact authored state; previously published snapshots
//           remain unchanged."
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Transaction.h"
#include "CypherGeometry_Delta.h"
#include "CypherGeometry_Document.h"
#include "CypherGeometry_Snapshot.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Planed_Make;
using cypher::math::planed_t;
using Catch::Approx;

namespace {

constexpr common::usize TRANSACTION_TEST_SIDE_CAPACITY = 16u;

struct captured_brush_t {
    geometry_source_id_t sourceId{};
    brush_solid_side_t sides[TRANSACTION_TEST_SIDE_CAPACITY]{};
    common::usize cSides{ 0u };
};

struct captured_registry_t {
    const void *pClaimedSlots{ nullptr };
    const void *pLiveSlots{ nullptr };
    common::usize cClaimed{ 0u };
    common::usize cLive{ 0u };
    common::usize cClaimedCapacity{ 0u };
    common::usize cLiveCapacity{ 0u };
    geometry_source_id_t next{};
    common::usize cEntriesMax{ 0u };
    const common::allocator_t *pAllocator{ nullptr };
    bool bLoadRegistrationOpen{ false };
};

bool PlaneExactlyEqual(
    const planed_t &a,
    const planed_t &b ) noexcept
{
    return a.normal.x == b.normal.x &&
           a.normal.y == b.normal.y &&
           a.normal.z == b.normal.z &&
           a.d == b.d;
}

bool SideExactlyEqual(
    const brush_solid_side_t &a,
    const brush_solid_side_t &b ) noexcept
{
    return PlaneExactlyEqual( a.plane, b.plane ) &&
           a.sourceId.value == b.sourceId.value &&
           a.iAttributeIndex == b.iAttributeIndex;
}

captured_brush_t CaptureBrush( const brush_solid_t *pBrush )
{
    REQUIRE( pBrush != nullptr );

    captured_brush_t captured{};
    captured.sourceId = pBrush->sourceId;
    captured.cSides = BrushSolid_SideCount( pBrush );
    REQUIRE( captured.cSides <= TRANSACTION_TEST_SIDE_CAPACITY );

    for ( common::usize i = 0u; i < captured.cSides; ++i ) {
        REQUIRE( BrushSolid_TryGetSide(
                     pBrush, i, &captured.sides[i] ) ==
                 geometry_status_t::OK );
    }
    return captured;
}

void CheckBrushMatches(
    const brush_solid_t *pBrush,
    const captured_brush_t &expected )
{
    REQUIRE( pBrush != nullptr );
    CHECK( pBrush->sourceId.value == expected.sourceId.value );
    REQUIRE( BrushSolid_SideCount( pBrush ) == expected.cSides );

    for ( common::usize i = 0u; i < expected.cSides; ++i ) {
        brush_solid_side_t actual{};
        REQUIRE( BrushSolid_TryGetSide( pBrush, i, &actual ) ==
                 geometry_status_t::OK );
        CHECK( SideExactlyEqual( actual, expected.sides[i] ) );
    }
}

captured_registry_t CaptureRegistry(
    const geometry_source_id_registry_t &registry ) noexcept
{
    return captured_registry_t{
        registry.claimedIds.pSlots,
        registry.liveIds.pSlots,
        common::HashSet_Count( &registry.claimedIds ),
        common::HashSet_Count( &registry.liveIds ),
        common::HashSet_Capacity( &registry.claimedIds ),
        common::HashSet_Capacity( &registry.liveIds ),
        registry.allocator.next,
        registry.cEntriesMax,
        registry.pAllocator,
        registry.bLoadRegistrationOpen
    };
}

void CheckRegistryExactlyMatches(
    const geometry_source_id_registry_t &registry,
    const captured_registry_t &expected )
{
    CHECK( registry.claimedIds.pSlots == expected.pClaimedSlots );
    CHECK( registry.liveIds.pSlots == expected.pLiveSlots );
    CHECK( common::HashSet_Count( &registry.claimedIds ) ==
           expected.cClaimed );
    CHECK( common::HashSet_Count( &registry.liveIds ) == expected.cLive );
    CHECK( common::HashSet_Capacity( &registry.claimedIds ) ==
           expected.cClaimedCapacity );
    CHECK( common::HashSet_Capacity( &registry.liveIds ) ==
           expected.cLiveCapacity );
    CHECK( registry.allocator.next.value == expected.next.value );
    CHECK( registry.cEntriesMax == expected.cEntriesMax );
    CHECK( registry.pAllocator == expected.pAllocator );
    CHECK( registry.bLoadRegistrationOpen ==
           expected.bLoadRegistrationOpen );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &registry ) );
}

brush_solid_side_t MakeTransactionTestSide() noexcept
{
    brush_solid_side_t side{};
    side.plane = Planed_Make( Vec3d_Make( 0.0, 0.0, 1.0 ), -2.0 );
    side.sourceId = geometry_source_id_t{ 900001u };
    side.iAttributeIndex = 73u;
    return side;
}

struct transaction_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFreeCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *TransactionFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<transaction_failure_allocator_state_t *>(
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

void TransactionFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<transaction_failure_allocator_state_t *>(
        pUserData );
    ++pState->cFreeCalls;
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeTransactionFailureAllocator(
    transaction_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        TransactionFailureAllocate,
        nullptr,
        TransactionFailureFree,
        pState
    };
}

common::usize OutstandingAllocationCount(
    const transaction_failure_allocator_state_t &state ) noexcept
{
    return state.cSuccessfulAllocations - state.cFreeCalls;
}

// Shared fixture for transaction tests: document with one box brush.
struct TransactionFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const common::allocator_t *pAllocator{ &allocator };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t externalIdAlloc{};
    geometry_document_t document{};
    geometry_source_id_t brushId{};

    TransactionFixture()
    {
        Initialize();
    }

    explicit TransactionFixture( const common::allocator_t *pExternalAllocator )
        : pAllocator( pExternalAllocator )
    {
        Initialize();
    }

    void Initialize()
    {
        REQUIRE( GeometryDocument_Init(
                     &document, pAllocator, policy ) ==
                 geometry_status_t::OK );

        brush_solid_t box{};
        REQUIRE( BrushGenerator_TryMakeBox(
                     &box, pAllocator, policy, &externalIdAlloc,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );
        brushId = box.sourceId;

        REQUIRE( GeometryDocument_TryAddBrush( &document, &box ) ==
                 geometry_status_t::OK );
        BrushSolid_Shutdown( &box );
    }

    ~TransactionFixture()
    {
        GeometryDocument_Shutdown( &document );
    }
};

void MakeStructuralReplacementDelta(
    const TransactionFixture &fixture,
    geometry_source_id_t addedSideId,
    geometry_delta_t *pDeltaOut )
{
    REQUIRE( pDeltaOut != nullptr );
    const brush_solid_t *pBrush = GeometryDocument_FindBrush(
        &fixture.document, fixture.brushId );
    REQUIRE( pBrush != nullptr );

    pDeltaOut->kind = geometry_delta_kind_t::BRUSH_REPLACED;
    pDeltaOut->brushId = fixture.brushId;
    REQUIRE( BrushSolid_DeepCopy(
                 &pDeltaOut->brushData,
                 pBrush,
                 fixture.pAllocator,
                 fixture.policy.limits ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_DeepCopy(
                 &pDeltaOut->newBrushData,
                 pBrush,
                 fixture.pAllocator,
                 fixture.policy.limits ) ==
             geometry_status_t::OK );

    brush_solid_side_t addedSide = MakeTransactionTestSide();
    addedSide.sourceId = addedSideId;
    REQUIRE( BrushSolid_TryAddSide(
                 &pDeltaOut->newBrushData,
                 fixture.policy.limits,
                 addedSide,
                 nullptr ) ==
             geometry_status_t::OK );
}

} // namespace

// ---------------------------------------------------------------------------
// Transaction begin
// ---------------------------------------------------------------------------

TEST_CASE( "Transaction: begin succeeds on valid brush",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;
    geometry_transaction_t txn{};

    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryTransaction_IsActive( &txn ) );

    (void)GeometryTransaction_Cancel( &txn );
}

TEST_CASE( "Transaction: begin on nonexistent brush fails",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;
    geometry_transaction_t txn{};

    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document,
                 geometry_source_id_t{ 9999u } ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( GeometryTransaction_IsActive( &txn ) );
}

TEST_CASE( "Transaction: double begin returns TRANSACTION_ACTIVE",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;
    geometry_transaction_t txn{};

    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::TRANSACTION_ACTIVE );

    (void)GeometryTransaction_Cancel( &txn );
}

TEST_CASE( "Transaction: begin with null args fails",
           "[Gate3][Transaction]" )
{
    REQUIRE( GeometryTransaction_Begin( nullptr, nullptr, {} ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Transaction: begin allocation failure leaves transaction and brush unchanged",
           "[Gate3][Transaction][allocation][contract]" )
{
    transaction_failure_allocator_state_t state{};
    common::allocator_t allocator =
        MakeTransactionFailureAllocator( &state );

    {
        TransactionFixture f{ &allocator };
        const brush_solid_t *pBrush =
            GeometryDocument_FindBrush( &f.document, f.brushId );
        const captured_brush_t baseline = CaptureBrush( pBrush );
        const common::usize cOutstandingBefore =
            OutstandingAllocationCount( state );

        state.iFailure = state.cAllocationCalls;

        geometry_transaction_t txn{};
        REQUIRE( GeometryTransaction_Begin(
                     &txn, &f.document, f.brushId ) ==
                 geometry_status_t::ALLOCATION_FAILED );

        CHECK_FALSE( GeometryTransaction_IsActive( &txn ) );
        CHECK( txn.baselineBrush.sides.pAllocator == nullptr );
        CheckBrushMatches(
            GeometryDocument_FindBrush( &f.document, f.brushId ),
            baseline );
        CHECK( OutstandingAllocationCount( state ) ==
               cOutstandingBefore );
    }

    CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------

TEST_CASE( "Transaction: preview updates live brush",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;
    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    const planed_t newPlane =
        Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -5.0 );
    REQUIRE( GeometryTransaction_TryPreviewSidePlane(
                 &txn, 0u, newPlane ) ==
             geometry_status_t::OK );

    const brush_solid_t *pLive =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    REQUIRE( pLive != nullptr );
    brush_solid_side_t side{};
    REQUIRE( BrushSolid_TryGetSide( pLive, 0u, &side ) ==
             geometry_status_t::OK );
    REQUIRE( side.plane.d == Approx( -5.0 ) );

    // Revision must NOT have advanced.
    REQUIRE( GeometryDocument_GetRevision( &f.document ) ==
             GEOMETRY_REVISION_INITIAL );

    (void)GeometryTransaction_Cancel( &txn );
}

TEST_CASE( "Transaction: multiple previews overwrite each other",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;
    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    for ( int i = 1; i <= 60; ++i ) {
        const planed_t plane = Planed_Make(
            Vec3d_Make( 1.0, 0.0, 0.0 ),
            -static_cast<math::f64>( i ) );
        REQUIRE( GeometryTransaction_TryPreviewSidePlane(
                     &txn, 0u, plane ) ==
                 geometry_status_t::OK );
    }

    const brush_solid_t *pLive =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    brush_solid_side_t side{};
    (void)BrushSolid_TryGetSide( pLive, 0u, &side );
    REQUIRE( side.plane.d == Approx( -60.0 ) );

    (void)GeometryTransaction_Cancel( &txn );
}

TEST_CASE( "Transaction: preview without active transaction fails",
           "[Gate3][Transaction]" )
{
    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_TryPreviewSidePlane(
                 &txn, 0u, {} ) ==
             geometry_status_t::NO_ACTIVE_TRANSACTION );
}

TEST_CASE( "Transaction: preview invalid side index fails",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;
    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    REQUIRE( GeometryTransaction_TryPreviewSidePlane(
                 &txn, 999u, {} ) ==
             geometry_status_t::INVALID_ARGUMENT );

    (void)GeometryTransaction_Cancel( &txn );
}

TEST_CASE( "Transaction: preview-all invalid plane is all-or-nothing",
           "[Gate3][Transaction][contract]" )
{
    TransactionFixture f;
    const captured_brush_t original = CaptureBrush(
        GeometryDocument_FindBrush( &f.document, f.brushId ) );

    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    // Establish a valid preview first. A failed replacement must preserve
    // this immediate pre-call state rather than jumping back to Begin.
    planed_t firstPreview = original.sides[0u].plane;
    firstPreview.d -= 3.0;
    REQUIRE( GeometryTransaction_TryPreviewSidePlane(
                 &txn, 0u, firstPreview ) ==
             geometry_status_t::OK );

    const captured_brush_t beforeFailedPreview = CaptureBrush(
        GeometryDocument_FindBrush( &f.document, f.brushId ) );
    planed_t replacement[TRANSACTION_TEST_SIDE_CAPACITY]{};
    for ( common::usize i = 0u;
          i < beforeFailedPreview.cSides;
          ++i ) {
        replacement[i] = beforeFailedPreview.sides[i].plane;
    }
    replacement[0u].d -= 7.0;
    replacement[1u].d = std::numeric_limits<math::f64>::quiet_NaN();

    REQUIRE( GeometryTransaction_TryPreviewAllPlanes(
                 &txn, replacement, beforeFailedPreview.cSides ) ==
             geometry_status_t::NUMERIC_FAILURE );
    CheckBrushMatches(
        GeometryDocument_FindBrush( &f.document, f.brushId ),
        beforeFailedPreview );

    REQUIRE( GeometryTransaction_Cancel( &txn ) ==
             geometry_status_t::OK );
    CheckBrushMatches(
        GeometryDocument_FindBrush( &f.document, f.brushId ),
        original );
}

TEST_CASE( "Transaction: preview-add limit failure changes nothing",
           "[Gate3][Transaction][limits][contract]" )
{
    TransactionFixture f;
    const captured_brush_t baseline = CaptureBrush(
        GeometryDocument_FindBrush( &f.document, f.brushId ) );

    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    geometry_limit_policy_t limits = f.policy.limits;
    limits.cBrushSidesPerBrushMax = baseline.cSides;
    REQUIRE( GeometryTransaction_TryPreviewAddSide(
                 &txn, MakeTransactionTestSide(), limits ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    CHECK( GeometryTransaction_IsActive( &txn ) );
    CheckBrushMatches(
        GeometryDocument_FindBrush( &f.document, f.brushId ),
        baseline );

    REQUIRE( GeometryTransaction_Cancel( &txn ) ==
             geometry_status_t::OK );
    CheckBrushMatches(
        GeometryDocument_FindBrush( &f.document, f.brushId ),
        baseline );
}

TEST_CASE( "Transaction: preview-add allocation failure changes nothing",
           "[Gate3][Transaction][allocation][contract]" )
{
    transaction_failure_allocator_state_t state{};
    common::allocator_t allocator =
        MakeTransactionFailureAllocator( &state );

    {
        TransactionFixture f{ &allocator };
        const captured_brush_t baseline = CaptureBrush(
            GeometryDocument_FindBrush( &f.document, f.brushId ) );
        const common::usize cOutstandingBeforeBegin =
            OutstandingAllocationCount( state );

        geometry_transaction_t txn{};
        REQUIRE( GeometryTransaction_Begin(
                     &txn, &f.document, f.brushId ) ==
                 geometry_status_t::OK );

        const common::usize cOutstandingBeforeAdd =
            OutstandingAllocationCount( state );
        state.iFailure = state.cAllocationCalls;

        REQUIRE( GeometryTransaction_TryPreviewAddSide(
                     &txn, MakeTransactionTestSide(), f.policy.limits ) ==
                 geometry_status_t::ALLOCATION_FAILED );
        CHECK( GeometryTransaction_IsActive( &txn ) );
        CheckBrushMatches(
            GeometryDocument_FindBrush( &f.document, f.brushId ),
            baseline );
        CHECK( OutstandingAllocationCount( state ) ==
               cOutstandingBeforeAdd );

        REQUIRE( GeometryTransaction_Cancel( &txn ) ==
                 geometry_status_t::OK );
        CheckBrushMatches(
            GeometryDocument_FindBrush( &f.document, f.brushId ),
            baseline );
        CHECK( OutstandingAllocationCount( state ) ==
               cOutstandingBeforeBegin );
    }

    CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
}

// ---------------------------------------------------------------------------
// Commit
// ---------------------------------------------------------------------------

TEST_CASE( "Transaction: commit produces delta and advances revision",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;
    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    const planed_t newPlane =
        Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -5.0 );
    REQUIRE( GeometryTransaction_TryPreviewSidePlane(
                 &txn, 0u, newPlane ) ==
             geometry_status_t::OK );

    geometry_delta_t delta{};
    geometry_changeset_t changeset{};
    geometry_revision_t newRevision = 0u;

    REQUIRE( GeometryTransaction_Commit(
                 &txn, &delta, &changeset, &newRevision ) ==
             geometry_status_t::OK );

    // Revision advanced from 0 to 1.
    REQUIRE( newRevision == 1u );
    REQUIRE( GeometryDocument_GetRevision( &f.document ) == 1u );

    // Delta describes the change.
    REQUIRE( delta.kind ==
             geometry_delta_kind_t::BRUSH_SIDE_PLANE_CHANGED );
    REQUIRE( delta.brushId.value == f.brushId.value );
    REQUIRE( delta.sideIndex == 0u );
    REQUIRE( delta.newPlane.d == Approx( -5.0 ) );

    // Changeset contains the affected brush.
    REQUIRE( GeometryChangeset_Count( &changeset ) == 1u );
    REQUIRE( GeometryChangeset_Contains( &changeset, f.brushId ) );

    REQUIRE_FALSE( GeometryTransaction_IsActive( &txn ) );

    GeometryDelta_Shutdown( &delta );
    GeometryChangeset_Shutdown( &changeset );
}

TEST_CASE( "Transaction: repeated preview then commit produces one "
           "delta from baseline",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;

    const brush_solid_t *pBefore =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    brush_solid_side_t baselineSide{};
    (void)BrushSolid_TryGetSide( pBefore, 0u, &baselineSide );
    const planed_t baselinePlane = baselineSide.plane;

    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    for ( int i = 1; i <= 60; ++i ) {
        const planed_t plane = Planed_Make(
            Vec3d_Make( 1.0, 0.0, 0.0 ),
            -static_cast<math::f64>( i ) );
        (void)GeometryTransaction_TryPreviewSidePlane(
            &txn, 0u, plane );
    }

    geometry_delta_t delta{};
    geometry_changeset_t changeset{};
    geometry_revision_t newRevision = 0u;

    REQUIRE( GeometryTransaction_Commit(
                 &txn, &delta, &changeset, &newRevision ) ==
             geometry_status_t::OK );

    // The delta's oldPlane must be the ORIGINAL baseline.
    REQUIRE( delta.oldPlane.d == Approx( baselinePlane.d ) );
    REQUIRE( delta.newPlane.d == Approx( -60.0 ) );

    GeometryDelta_Shutdown( &delta );
    GeometryChangeset_Shutdown( &changeset );
}

TEST_CASE( "Transaction: no-op commit does not advance revision",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;
    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    geometry_delta_t delta{};
    geometry_changeset_t changeset{};
    geometry_revision_t newRevision = 0u;

    REQUIRE( GeometryTransaction_Commit(
                 &txn, &delta, &changeset, &newRevision ) ==
             geometry_status_t::OK );
    REQUIRE( delta.kind == geometry_delta_kind_t::INVALID );
    REQUIRE( GeometryDocument_GetRevision( &f.document ) ==
             GEOMETRY_REVISION_INITIAL );
}

TEST_CASE( "Transaction: commit without active transaction fails",
           "[Gate3][Transaction]" )
{
    geometry_transaction_t txn{};
    geometry_delta_t delta{};
    geometry_changeset_t changeset{};
    geometry_revision_t newRevision = 0u;

    REQUIRE( GeometryTransaction_Commit(
                 &txn, &delta, &changeset, &newRevision ) ==
             geometry_status_t::NO_ACTIVE_TRANSACTION );
}

TEST_CASE( "Transaction: structural add commit publishes a complete replacement delta",
           "[Gate3][Transaction][contract]" )
{
    TransactionFixture f;
    const captured_brush_t baseline = CaptureBrush(
        GeometryDocument_FindBrush( &f.document, f.brushId ) );
    const brush_solid_side_t addedSide = MakeTransactionTestSide();
    const common::usize cClaimedBefore =
        GeometrySourceIdRegistry_ClaimedCount( &f.document.sourceIds );
    const common::usize cLiveBefore =
        GeometrySourceIdRegistry_Count( &f.document.sourceIds );

    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewAddSide(
                 &txn, addedSide, f.policy.limits ) ==
             geometry_status_t::OK );

    geometry_delta_t delta{};
    geometry_changeset_t changeset{};
    geometry_revision_t newRevision = 0u;
    REQUIRE( GeometryTransaction_Commit(
                 &txn, &delta, &changeset, &newRevision ) ==
             geometry_status_t::OK );

    CHECK_FALSE( GeometryTransaction_IsActive( &txn ) );
    CHECK( newRevision == GEOMETRY_REVISION_INITIAL + 1u );
    CHECK( delta.kind == geometry_delta_kind_t::BRUSH_REPLACED );
    CHECK( delta.brushId.value == f.brushId.value );
    CHECK( BrushSolid_SideCount( &delta.brushData ) == baseline.cSides );
    REQUIRE( BrushSolid_SideCount( &delta.newBrushData ) ==
             baseline.cSides + 1u );

    brush_solid_side_t deltaAddedSide{};
    REQUIRE( BrushSolid_TryGetSide(
                 &delta.newBrushData,
                 baseline.cSides,
                 &deltaAddedSide ) ==
             geometry_status_t::OK );
    CHECK( SideExactlyEqual( deltaAddedSide, addedSide ) );
    CHECK( GeometryChangeset_Count( &changeset ) == 1u );
    CHECK( GeometryChangeset_Contains( &changeset, f.brushId ) );

    const brush_solid_t *pLive =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    REQUIRE( BrushSolid_SideCount( pLive ) == baseline.cSides + 1u );
    brush_solid_side_t liveAddedSide{};
    REQUIRE( BrushSolid_TryGetSide(
                 pLive, baseline.cSides, &liveAddedSide ) ==
             geometry_status_t::OK );
    CHECK( SideExactlyEqual( liveAddedSide, addedSide ) );
    CHECK( GeometrySourceIdRegistry_Contains(
        &f.document.sourceIds, addedSide.sourceId ) );
    CHECK( common::HashSet_Contains(
        &f.document.sourceIds.claimedIds, addedSide.sourceId ) );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
        &f.document.sourceIds ) == cClaimedBefore + 1u );
    CHECK( GeometrySourceIdRegistry_Count(
        &f.document.sourceIds ) == cLiveBefore + 1u );
    CHECK( f.document.sourceIds.allocator.next.value >
           addedSide.sourceId.value );
    CHECK( GeometrySourceIdRegistry_ValidateDeep(
        &f.document.sourceIds ) );

    GeometryChangeset_Shutdown( &changeset );
    GeometryDelta_Shutdown( &delta );
}

TEST_CASE( "Transaction: structural commit allocation failures restore brush, registry, and outputs exactly",
           "[Gate3][Transaction][allocation][identity][contract]" )
{
    // Measure the implementation's successful commit allocation count instead
    // of baking in vector/hash-table growth details.
    common::usize cCommitAllocations = 0u;
    {
        transaction_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeTransactionFailureAllocator( &state );
        TransactionFixture f{ &allocator };

        geometry_transaction_t txn{};
        REQUIRE( GeometryTransaction_Begin(
                     &txn, &f.document, f.brushId ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryTransaction_TryPreviewAddSide(
                     &txn, MakeTransactionTestSide(),
                     f.policy.limits ) == geometry_status_t::OK );

        const common::usize cCallsBeforeCommit =
            state.cAllocationCalls;
        geometry_delta_t delta{};
        geometry_changeset_t changeset{};
        geometry_revision_t newRevision = 0u;
        REQUIRE( GeometryTransaction_Commit(
                     &txn, &delta, &changeset, &newRevision ) ==
                 geometry_status_t::OK );
        cCommitAllocations =
            state.cAllocationCalls - cCallsBeforeCommit;
        REQUIRE( cCommitAllocations > 0u );

        GeometryChangeset_Shutdown( &changeset );
        GeometryDelta_Shutdown( &delta );
    }

    for ( common::usize iFailureOffset = 0u;
          iFailureOffset < cCommitAllocations;
          ++iFailureOffset ) {
        CAPTURE( iFailureOffset, cCommitAllocations );

        transaction_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeTransactionFailureAllocator( &state );
        {
            TransactionFixture f{ &allocator };
            const captured_brush_t baseline = CaptureBrush(
                GeometryDocument_FindBrush(
                    &f.document, f.brushId ) );
            const captured_registry_t registryBefore =
                CaptureRegistry( f.document.sourceIds );
            const common::usize cOutstandingBeforeBegin =
                OutstandingAllocationCount( state );

            geometry_transaction_t txn{};
            REQUIRE( GeometryTransaction_Begin(
                         &txn, &f.document, f.brushId ) ==
                     geometry_status_t::OK );
            REQUIRE( GeometryTransaction_TryPreviewAddSide(
                         &txn, MakeTransactionTestSide(),
                         f.policy.limits ) == geometry_status_t::OK );

            state.iFailure =
                state.cAllocationCalls + iFailureOffset;

            geometry_delta_t delta{};
            geometry_changeset_t changeset{};
            geometry_revision_t newRevision = 12345u;
            REQUIRE( GeometryTransaction_Commit(
                         &txn, &delta, &changeset,
                         &newRevision ) ==
                     geometry_status_t::ALLOCATION_FAILED );

            CHECK_FALSE( GeometryTransaction_IsActive( &txn ) );
            CheckBrushMatches(
                GeometryDocument_FindBrush(
                    &f.document, f.brushId ),
                baseline );
            CheckRegistryExactlyMatches(
                f.document.sourceIds, registryBefore );
            CHECK( GeometryDocument_GetRevision( &f.document ) ==
                   GEOMETRY_REVISION_INITIAL );
            CHECK( delta.kind == geometry_delta_kind_t::INVALID );
            CHECK( delta.brushData.sides.pAllocator == nullptr );
            CHECK( delta.newBrushData.sides.pAllocator == nullptr );
            CHECK( changeset.affectedBrushIds.pAllocator == nullptr );
            CHECK( newRevision == 12345u );
            CHECK( OutstandingAllocationCount( state ) ==
                   cOutstandingBeforeBegin );
        }
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

TEST_CASE( "Transaction: every replaced-delta commit allocation failure rolls back cleanly",
           "[Gate3][Transaction][allocation][contract]" )
{
    // A multi-plane commit allocates for the old brush copy, the new brush
    // copy, and the changeset ID. Fail each allocation in turn.
    for ( common::usize iFailureOffset = 0u;
          iFailureOffset < 3u;
          ++iFailureOffset ) {
        CAPTURE( iFailureOffset );

        transaction_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeTransactionFailureAllocator( &state );

        {
            TransactionFixture f{ &allocator };
            const captured_brush_t baseline = CaptureBrush(
                GeometryDocument_FindBrush( &f.document, f.brushId ) );
            const common::usize cOutstandingBeforeBegin =
                OutstandingAllocationCount( state );

            geometry_transaction_t txn{};
            REQUIRE( GeometryTransaction_Begin(
                         &txn, &f.document, f.brushId ) ==
                     geometry_status_t::OK );

            planed_t replacement[TRANSACTION_TEST_SIDE_CAPACITY]{};
            for ( common::usize i = 0u; i < baseline.cSides; ++i ) {
                replacement[i] = baseline.sides[i].plane;
            }
            replacement[0u].d -= 2.0;
            replacement[1u].d -= 4.0;
            REQUIRE( GeometryTransaction_TryPreviewAllPlanes(
                         &txn, replacement, baseline.cSides ) ==
                     geometry_status_t::OK );

            state.iFailure =
                state.cAllocationCalls + iFailureOffset;

            geometry_delta_t delta{};
            geometry_changeset_t changeset{};
            geometry_revision_t newRevision = 12345u;
            REQUIRE( GeometryTransaction_Commit(
                         &txn, &delta, &changeset, &newRevision ) ==
                     geometry_status_t::ALLOCATION_FAILED );

            CHECK_FALSE( GeometryTransaction_IsActive( &txn ) );
            CheckBrushMatches(
                GeometryDocument_FindBrush( &f.document, f.brushId ),
                baseline );
            CHECK( GeometryDocument_GetRevision( &f.document ) ==
                   GEOMETRY_REVISION_INITIAL );

            // Nothing was published before the failed operation completed.
            CHECK( delta.kind == geometry_delta_kind_t::INVALID );
            CHECK( delta.brushData.sides.pAllocator == nullptr );
            CHECK( delta.newBrushData.sides.pAllocator == nullptr );
            CHECK( changeset.affectedBrushIds.pAllocator == nullptr );
            CHECK( newRevision == 12345u );
            CHECK( OutstandingAllocationCount( state ) ==
                   cOutstandingBeforeBegin );
        }

        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

// ---------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------

TEST_CASE( "Transaction: cancel restores exact baseline",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;

    const brush_solid_t *pBefore =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    brush_solid_side_t baselineSide{};
    (void)BrushSolid_TryGetSide( pBefore, 0u, &baselineSide );

    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    const planed_t newPlane =
        Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -999.0 );
    (void)GeometryTransaction_TryPreviewSidePlane( &txn, 0u, newPlane );

    REQUIRE( GeometryTransaction_Cancel( &txn ) ==
             geometry_status_t::OK );
    REQUIRE_FALSE( GeometryTransaction_IsActive( &txn ) );

    const brush_solid_t *pAfter =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    brush_solid_side_t restoredSide{};
    (void)BrushSolid_TryGetSide( pAfter, 0u, &restoredSide );
    REQUIRE( restoredSide.plane.d == Approx( baselineSide.plane.d ) );

    REQUIRE( GeometryDocument_GetRevision( &f.document ) ==
             GEOMETRY_REVISION_INITIAL );
}

TEST_CASE( "Transaction: cancel removes preview-added sides and restores every side field",
           "[Gate3][Transaction][contract]" )
{
    TransactionFixture f;
    const captured_brush_t baseline = CaptureBrush(
        GeometryDocument_FindBrush( &f.document, f.brushId ) );
    const captured_registry_t registryBefore =
        CaptureRegistry( f.document.sourceIds );

    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    planed_t changedPlane = baseline.sides[0u].plane;
    changedPlane.d -= 9.0;
    REQUIRE( GeometryTransaction_TryPreviewSidePlane(
                 &txn, 0u, changedPlane ) ==
             geometry_status_t::OK );

    const brush_solid_side_t addedSide = MakeTransactionTestSide();
    REQUIRE( GeometryTransaction_TryPreviewAddSide(
                 &txn, addedSide, f.policy.limits ) ==
             geometry_status_t::OK );

    const brush_solid_t *pPreview =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    REQUIRE( BrushSolid_SideCount( pPreview ) == baseline.cSides + 1u );
    brush_solid_side_t actualAddedSide{};
    REQUIRE( BrushSolid_TryGetSide(
                 pPreview, baseline.cSides, &actualAddedSide ) ==
             geometry_status_t::OK );
    CHECK( SideExactlyEqual( actualAddedSide, addedSide ) );

    REQUIRE( GeometryTransaction_Cancel( &txn ) ==
             geometry_status_t::OK );
    CHECK_FALSE( GeometryTransaction_IsActive( &txn ) );
    CheckBrushMatches(
        GeometryDocument_FindBrush( &f.document, f.brushId ),
        baseline );
    CheckRegistryExactlyMatches(
        f.document.sourceIds, registryBefore );
    CHECK( GeometryDocument_GetRevision( &f.document ) ==
           GEOMETRY_REVISION_INITIAL );
}

TEST_CASE( "Transaction: cancel without active transaction fails",
           "[Gate3][Transaction]" )
{
    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Cancel( &txn ) ==
             geometry_status_t::NO_ACTIVE_TRANSACTION );
}

// ---------------------------------------------------------------------------
// Stale revision
// ---------------------------------------------------------------------------

TEST_CASE( "Transaction: stale revision rejected on commit",
           "[Gate3][Transaction]" )
{
    TransactionFixture f;
    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    // Simulate another commit advancing the revision.
    f.document.revision += 1u;

    geometry_delta_t delta{};
    geometry_changeset_t changeset{};
    geometry_revision_t newRevision = 0u;

    REQUIRE( GeometryTransaction_Commit(
                 &txn, &delta, &changeset, &newRevision ) ==
             geometry_status_t::STALE_REVISION );

    // Transaction stays active so the caller can cancel.
    REQUIRE( GeometryTransaction_IsActive( &txn ) );

    (void)GeometryTransaction_Cancel( &txn );
}

// ---------------------------------------------------------------------------
// Delta inverse
// ---------------------------------------------------------------------------

TEST_CASE( "Delta: inverse of BRUSH_SIDE_PLANE_CHANGED swaps planes",
           "[Gate3][Delta]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};

    geometry_delta_t original{};
    original.kind = geometry_delta_kind_t::BRUSH_SIDE_PLANE_CHANGED;
    original.brushId = geometry_source_id_t{ 42u };
    original.sideIndex = 3u;
    original.oldPlane = Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -1.0 );
    original.newPlane = Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -5.0 );

    geometry_delta_t inverse{};
    REQUIRE( GeometryDelta_TryComputeInverse(
                 &original, &allocator, policy.limits, &inverse ) ==
             geometry_status_t::OK );

    REQUIRE( inverse.kind ==
             geometry_delta_kind_t::BRUSH_SIDE_PLANE_CHANGED );
    REQUIRE( inverse.brushId.value == 42u );
    REQUIRE( inverse.sideIndex == 3u );
    REQUIRE( inverse.oldPlane.d == Approx( -5.0 ) );
    REQUIRE( inverse.newPlane.d == Approx( -1.0 ) );

    GeometryDelta_Shutdown( &inverse );
}

TEST_CASE( "Delta: inverse applied undoes original change",
           "[Gate3][Delta]" )
{
    TransactionFixture f;
    geometry_transaction_t txn{};

    const brush_solid_t *pBefore =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    brush_solid_side_t originalSide{};
    (void)BrushSolid_TryGetSide( pBefore, 0u, &originalSide );

    // Commit a change.
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );
    const planed_t newPlane =
        Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -5.0 );
    (void)GeometryTransaction_TryPreviewSidePlane( &txn, 0u, newPlane );

    geometry_delta_t delta{};
    geometry_changeset_t changeset{};
    geometry_revision_t newRevision = 0u;
    REQUIRE( GeometryTransaction_Commit(
                 &txn, &delta, &changeset, &newRevision ) ==
             geometry_status_t::OK );

    // Compute the inverse delta.
    geometry_delta_t inverse{};
    REQUIRE( GeometryDelta_TryComputeInverse(
                 &delta, f.document.pAllocator,
                 f.policy.limits, &inverse ) ==
             geometry_status_t::OK );

    // Apply the inverse to undo.
    REQUIRE( GeometryDelta_TryApplyToDocument(
                 &inverse, &f.document ) ==
             geometry_status_t::OK );

    // The brush should be back to the original plane.
    const brush_solid_t *pAfter =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    brush_solid_side_t restoredSide{};
    (void)BrushSolid_TryGetSide( pAfter, 0u, &restoredSide );
    REQUIRE( restoredSide.plane.d == Approx( originalSide.plane.d ) );

    GeometryDelta_Shutdown( &delta );
    GeometryChangeset_Shutdown( &changeset );
    GeometryDelta_Shutdown( &inverse );
}

// ---------------------------------------------------------------------------
// Snapshot immutability across transactions
// ---------------------------------------------------------------------------

TEST_CASE( "Snapshot: remains unchanged after transaction commit",
           "[Gate3][Snapshot][Transaction]" )
{
    TransactionFixture f;

    geometry_snapshot_t snapshot{};
    REQUIRE( GeometrySnapshot_TakeFromDocument(
                 &snapshot, &f.document ) ==
             geometry_status_t::OK );

    const brush_solid_t *pSnapBrush =
        GeometrySnapshot_FindBrush( &snapshot, f.brushId );
    REQUIRE( pSnapBrush != nullptr );
    brush_solid_side_t snapSide{};
    (void)BrushSolid_TryGetSide( pSnapBrush, 0u, &snapSide );
    const math::f64 originalD = snapSide.plane.d;

    // Commit a transaction that changes the plane.
    geometry_transaction_t txn{};
    REQUIRE( GeometryTransaction_Begin(
                 &txn, &f.document, f.brushId ) ==
             geometry_status_t::OK );
    const planed_t newPlane =
        Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -999.0 );
    (void)GeometryTransaction_TryPreviewSidePlane( &txn, 0u, newPlane );

    geometry_delta_t delta{};
    geometry_changeset_t changeset{};
    geometry_revision_t newRevision = 0u;
    REQUIRE( GeometryTransaction_Commit(
                 &txn, &delta, &changeset, &newRevision ) ==
             geometry_status_t::OK );

    // The snapshot must still show the original value.
    const brush_solid_t *pSnapAfter =
        GeometrySnapshot_FindBrush( &snapshot, f.brushId );
    brush_solid_side_t snapSideAfter{};
    (void)BrushSolid_TryGetSide( pSnapAfter, 0u, &snapSideAfter );
    REQUIRE( snapSideAfter.plane.d == Approx( originalD ) );

    GeometryDelta_Shutdown( &delta );
    GeometryChangeset_Shutdown( &changeset );
    GeometrySnapshot_Shutdown( &snapshot );
}

// ---------------------------------------------------------------------------
// Change set
// ---------------------------------------------------------------------------

TEST_CASE( "Changeset: init/shutdown/add/contains",
           "[Gate3][Changeset]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_changeset_t cs{};

    REQUIRE( GeometryChangeset_Init( &cs, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryChangeset_Count( &cs ) == 0u );

    const geometry_source_id_t id1{ 10u };
    const geometry_source_id_t id2{ 20u };

    REQUIRE( GeometryChangeset_TryAddBrush( &cs, id1 ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryChangeset_Count( &cs ) == 1u );
    REQUIRE( GeometryChangeset_Contains( &cs, id1 ) );
    REQUIRE_FALSE( GeometryChangeset_Contains( &cs, id2 ) );

    // Duplicate is silently accepted.
    REQUIRE( GeometryChangeset_TryAddBrush( &cs, id1 ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryChangeset_Count( &cs ) == 1u );

    REQUIRE( GeometryChangeset_TryAddBrush( &cs, id2 ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryChangeset_Count( &cs ) == 2u );

    GeometryChangeset_Shutdown( &cs );
}

// ---------------------------------------------------------------------------
// Delta apply: BRUSH_ADDED / BRUSH_REMOVED
// ---------------------------------------------------------------------------

TEST_CASE( "Delta: apply BRUSH_ADDED adds brush to document",
           "[Gate3][Delta]" )
{
    TransactionFixture f;

    brush_solid_t box{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &box, &f.allocator, f.policy, &f.externalIdAlloc,
                 Vec3d_Make( 5.0, 5.0, 5.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );

    // Create a BRUSH_ADDED delta with deep-copied brush data.
    geometry_delta_t delta{};
    delta.kind = geometry_delta_kind_t::BRUSH_ADDED;
    delta.brushId = box.sourceId;
    REQUIRE( BrushSolid_DeepCopy(
                 &delta.brushData, &box, &f.allocator,
                 f.policy.limits ) ==
             geometry_status_t::OK );

    const common::usize countBefore =
        GeometryDocument_BrushCount( &f.document );

    REQUIRE( GeometryDelta_TryApplyToDocument(
                 &delta, &f.document ) ==
             geometry_status_t::OK );

    REQUIRE( GeometryDocument_BrushCount( &f.document ) ==
             countBefore + 1u );
    REQUIRE( GeometryDocument_FindBrush(
                 &f.document, delta.brushId ) != nullptr );

    GeometryDelta_Shutdown( &delta );
    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Delta: apply BRUSH_REMOVED removes brush from document",
           "[Gate3][Delta]" )
{
    TransactionFixture f;

    geometry_delta_t delta{};
    delta.kind = geometry_delta_kind_t::BRUSH_REMOVED;
    delta.brushId = f.brushId;

    REQUIRE( GeometryDelta_TryApplyToDocument(
                 &delta, &f.document ) ==
             geometry_status_t::OK );

    REQUIRE( GeometryDocument_BrushCount( &f.document ) == 0u );
    REQUIRE( GeometryDocument_FindBrush(
                 &f.document, f.brushId ) == nullptr );
}

TEST_CASE( "Delta: BRUSH_ADDED/REMOVED inverse round-trips",
           "[Gate3][Delta]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};

    brush_solid_t box{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &box, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );

    // Create BRUSH_ADDED delta.
    geometry_delta_t addDelta{};
    addDelta.kind = geometry_delta_kind_t::BRUSH_ADDED;
    addDelta.brushId = box.sourceId;
    REQUIRE( BrushSolid_DeepCopy(
                 &addDelta.brushData, &box, &allocator,
                 policy.limits ) ==
             geometry_status_t::OK );

    // Inverse should be BRUSH_REMOVED.
    geometry_delta_t removeDelta{};
    REQUIRE( GeometryDelta_TryComputeInverse(
                 &addDelta, &allocator, policy.limits,
                 &removeDelta ) ==
             geometry_status_t::OK );
    REQUIRE( removeDelta.kind == geometry_delta_kind_t::BRUSH_REMOVED );
    REQUIRE( removeDelta.brushId.value == box.sourceId.value );

    // Inverse of inverse should be BRUSH_ADDED again.
    geometry_delta_t addAgain{};
    REQUIRE( GeometryDelta_TryComputeInverse(
                 &removeDelta, &allocator, policy.limits,
                 &addAgain ) ==
             geometry_status_t::OK );
    REQUIRE( addAgain.kind == geometry_delta_kind_t::BRUSH_ADDED );
    REQUIRE( addAgain.brushId.value == box.sourceId.value );
    REQUIRE( BrushSolid_SideCount( &addAgain.brushData ) == 6u );

    GeometryDelta_Shutdown( &addAgain );
    GeometryDelta_Shutdown( &removeDelta );
    GeometryDelta_Shutdown( &addDelta );
    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Delta: same-count replacement restores complete side records",
           "[Gate3][Delta][contract]" )
{
    TransactionFixture f;
    const brush_solid_t *pOriginal = GeometryDocument_FindBrush(
        &f.document, f.brushId );
    const captured_brush_t baseline = CaptureBrush( pOriginal );
    const geometry_source_id_t oldSideId = baseline.sides[0u].sourceId;
    const geometry_source_id_t replacementSideId{ 900010u };

    geometry_delta_t delta{};
    delta.kind = geometry_delta_kind_t::BRUSH_REPLACED;
    delta.brushId = f.brushId;
    REQUIRE( BrushSolid_DeepCopy(
                 &delta.brushData,
                 pOriginal,
                 f.pAllocator,
                 f.policy.limits ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_DeepCopy(
                 &delta.newBrushData,
                 pOriginal,
                 f.pAllocator,
                 f.policy.limits ) ==
             geometry_status_t::OK );

    brush_solid_side_t expected =
        delta.newBrushData.sides.pData[0u];
    expected.plane.d -= 3.25;
    expected.sourceId = replacementSideId;
    expected.iAttributeIndex = 8128u;
    delta.newBrushData.sides.pData[0u] = expected;

    f.document.revision = 27u;
    REQUIRE( GeometryDelta_TryApplyToDocument(
                 &delta, &f.document ) ==
             geometry_status_t::OK );

    const brush_solid_t *pReplaced = GeometryDocument_FindBrush(
        &f.document, f.brushId );
    REQUIRE( pReplaced != nullptr );
    REQUIRE( BrushSolid_SideCount( pReplaced ) == baseline.cSides );
    brush_solid_side_t actual{};
    REQUIRE( BrushSolid_TryGetSide( pReplaced, 0u, &actual ) ==
             geometry_status_t::OK );
    CHECK( SideExactlyEqual( actual, expected ) );
    CHECK( GeometrySourceIdRegistry_Contains(
        &f.document.sourceIds, replacementSideId ) );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains(
        &f.document.sourceIds, oldSideId ) );
    CHECK( common::HashSet_Contains(
        &f.document.sourceIds.claimedIds, oldSideId ) );
    CHECK( GeometrySourceIdRegistry_ValidateDeep(
        &f.document.sourceIds ) );
    CHECK( GeometryDocument_GetRevision( &f.document ) == 27u );

    GeometryDelta_Shutdown( &delta );
}

TEST_CASE( "Delta: structural replacement undo and redo preserve identity ownership",
           "[Gate3][Delta][contract]" )
{
    TransactionFixture f;
    const captured_brush_t baseline = CaptureBrush(
        GeometryDocument_FindBrush( &f.document, f.brushId ) );
    const geometry_source_id_t addedSideId{ 900015u };
    const common::usize cClaimedBefore =
        GeometrySourceIdRegistry_ClaimedCount( &f.document.sourceIds );

    geometry_delta_t delta{};
    MakeStructuralReplacementDelta( f, addedSideId, &delta );
    geometry_delta_t inverse{};
    REQUIRE( GeometryDelta_TryComputeInverse(
                 &delta,
                 f.pAllocator,
                 f.policy.limits,
                 &inverse ) ==
             geometry_status_t::OK );

    f.document.revision = 44u;
    REQUIRE( GeometryDelta_TryApplyToDocument(
                 &delta, &f.document ) ==
             geometry_status_t::OK );
    CHECK( GeometrySourceIdRegistry_Contains(
        &f.document.sourceIds, addedSideId ) );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
        &f.document.sourceIds ) == cClaimedBefore + 1u );

    REQUIRE( GeometryDelta_TryApplyToDocument(
                 &inverse, &f.document ) ==
             geometry_status_t::OK );
    CheckBrushMatches(
        GeometryDocument_FindBrush( &f.document, f.brushId ),
        baseline );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains(
        &f.document.sourceIds, addedSideId ) );
    CHECK( common::HashSet_Contains(
        &f.document.sourceIds.claimedIds, addedSideId ) );

    REQUIRE( GeometryDelta_TryApplyToDocument(
                 &delta, &f.document ) ==
             geometry_status_t::OK );
    CHECK( GeometrySourceIdRegistry_Contains(
        &f.document.sourceIds, addedSideId ) );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
        &f.document.sourceIds ) == cClaimedBefore + 1u );
    CHECK( GeometrySourceIdRegistry_ValidateDeep(
        &f.document.sourceIds ) );
    CHECK( GeometryDocument_GetRevision( &f.document ) == 44u );

    GeometryDelta_Shutdown( &inverse );
    GeometryDelta_Shutdown( &delta );
}

TEST_CASE( "Delta: structural replacement allocation failures are exact no-ops",
           "[Gate3][Delta][allocation][contract]" )
{
    const geometry_source_id_t addedSideId{ 900020u };
    common::usize cReplayAllocations = 0u;

    // Measure the complete successful replay path so the failure sweep covers
    // every allocation point without guessing container growth behavior.
    {
        transaction_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeTransactionFailureAllocator( &state );
        {
            TransactionFixture f{ &allocator };
            geometry_delta_t delta{};
            MakeStructuralReplacementDelta( f, addedSideId, &delta );

            const common::usize cCallsBefore = state.cAllocationCalls;
            REQUIRE( GeometryDelta_TryApplyToDocument(
                         &delta, &f.document ) ==
                     geometry_status_t::OK );
            cReplayAllocations =
                state.cAllocationCalls - cCallsBefore;
            REQUIRE( cReplayAllocations > 0u );
            CHECK( GeometrySourceIdRegistry_Contains(
                &f.document.sourceIds, addedSideId ) );
            CHECK( GeometrySourceIdRegistry_ValidateDeep(
                &f.document.sourceIds ) );

            GeometryDelta_Shutdown( &delta );
        }
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }

    for ( common::usize iFailureOffset = 0u;
          iFailureOffset < cReplayAllocations;
          ++iFailureOffset ) {
        CAPTURE( iFailureOffset, cReplayAllocations );

        transaction_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeTransactionFailureAllocator( &state );
        {
            TransactionFixture f{ &allocator };
            geometry_delta_t delta{};
            MakeStructuralReplacementDelta( f, addedSideId, &delta );

            f.document.revision = 91u;
            const brush_solid_t *pBefore = GeometryDocument_FindBrush(
                &f.document, f.brushId );
            const captured_brush_t baseline = CaptureBrush( pBefore );
            const captured_registry_t registryBefore =
                CaptureRegistry( f.document.sourceIds );
            const brush_solid_t *const pBrushBefore = pBefore;
            const brush_solid_side_t *const pSidesBefore =
                pBefore->sides.pData;
            const common::usize cSideCapacityBefore =
                pBefore->sides.nCapacity;
            const common::usize cOutstandingBefore =
                OutstandingAllocationCount( state );

            state.iFailure =
                state.cAllocationCalls + iFailureOffset;
            REQUIRE( GeometryDelta_TryApplyToDocument(
                         &delta, &f.document ) ==
                     geometry_status_t::ALLOCATION_FAILED );

            const brush_solid_t *pAfter = GeometryDocument_FindBrush(
                &f.document, f.brushId );
            CHECK( pAfter == pBrushBefore );
            CHECK( pAfter->sides.pData == pSidesBefore );
            CHECK( pAfter->sides.nCapacity == cSideCapacityBefore );
            CheckBrushMatches( pAfter, baseline );
            CheckRegistryExactlyMatches(
                f.document.sourceIds, registryBefore );
            CHECK( GeometryDocument_GetRevision( &f.document ) == 91u );
            CHECK( OutstandingAllocationCount( state ) ==
                   cOutstandingBefore );

            GeometryDelta_Shutdown( &delta );
        }
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

} // namespace cypher::editor::geometry
