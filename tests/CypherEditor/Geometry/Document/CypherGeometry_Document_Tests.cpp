//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Document_Tests.cpp
//  Purpose: Verifies geometry document lifecycle, brush pool operations,
//           and snapshot immutability.
//  Details: Covers Gate 3 document and snapshot acceptance: init/shutdown,
//           add/remove/find brushes, revision counter, deep-copy semantics,
//           snapshot independence from subsequent mutations, and error paths
//           for double-init, null arguments, identity conflicts, and
//           capacity limits.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Document.h"
#include "CypherGeometry_Snapshot.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Planed_Make;
using Catch::Approx;

namespace {

// Shared fixture: system allocator + default policy + ID allocator for
// generating brushes outside the document (simulating a creation command).
struct DocumentFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t externalIdAlloc{};
    geometry_document_t document{};

    DocumentFixture()
    {
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) ==
                 geometry_status_t::OK );
    }

    ~DocumentFixture()
    {
        GeometryDocument_Shutdown( &document );
    }

    // Builds a unit box using an external ID allocator, simulating how a
    // creation command produces a brush before handing it to the document.
    void MakeBox( brush_solid_t *pBrushOut )
    {
        REQUIRE( BrushGenerator_TryMakeBox(
                     pBrushOut, &allocator, policy, &externalIdAlloc,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );
    }
};

struct document_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFreeCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *DocumentFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<document_failure_allocator_state_t *>(
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

void DocumentFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<document_failure_allocator_state_t *>(
        pUserData );
    ++pState->cFreeCalls;
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeDocumentFailureAllocator(
    document_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        DocumentFailureAllocate,
        nullptr,
        DocumentFailureFree,
        pState
    };
}

} // namespace

// ---------------------------------------------------------------------------
// Document lifecycle
// ---------------------------------------------------------------------------

TEST_CASE( "Document: init sets revision to zero", "[Gate3][Document]" )
{
    DocumentFixture f;
    REQUIRE( GeometryDocument_GetRevision( &f.document ) ==
             GEOMETRY_REVISION_INITIAL );
    REQUIRE( GeometryDocument_IsInitialized( &f.document ) );
    REQUIRE( GeometryDocument_BrushCount( &f.document ) == 0u );
}

TEST_CASE( "Document: double init returns ALREADY_INITIALIZED",
           "[Gate3][Document]" )
{
    DocumentFixture f;
    REQUIRE( GeometryDocument_Init(
                 &f.document, &f.allocator, f.policy ) ==
             geometry_status_t::ALREADY_INITIALIZED );
}

TEST_CASE( "Document: null arguments rejected", "[Gate3][Document]" )
{
    geometry_policy_t policy{};
    REQUIRE( GeometryDocument_Init( nullptr, nullptr, policy ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Document: shutdown is safe on null", "[Gate3][Document]" )
{
    GeometryDocument_Shutdown( nullptr );
}

TEST_CASE( "Document: IsInitialized returns false for null",
           "[Gate3][Document]" )
{
    REQUIRE_FALSE( GeometryDocument_IsInitialized( nullptr ) );
}

// ---------------------------------------------------------------------------
// Brush add / remove / find
// ---------------------------------------------------------------------------

TEST_CASE( "Document: add brush increases count",
           "[Gate3][Document]" )
{
    DocumentFixture f;
    brush_solid_t box{};
    f.MakeBox( &box );

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &f.document ) == 1u );

    // The document deep-copied the brush, so we still own and must
    // shut down our local copy.
    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document: find brush returns correct brush",
           "[Gate3][Document]" )
{
    DocumentFixture f;
    brush_solid_t box{};
    f.MakeBox( &box );
    const geometry_source_id_t boxId = box.sourceId;

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::OK );

    const brush_solid_t *pFound =
        GeometryDocument_FindBrush( &f.document, boxId );
    REQUIRE( pFound != nullptr );
    REQUIRE( pFound->sourceId.value == boxId.value );
    REQUIRE( BrushSolid_SideCount( pFound ) == 6u );

    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document: find nonexistent brush returns null",
           "[Gate3][Document]" )
{
    DocumentFixture f;
    REQUIRE( GeometryDocument_FindBrush(
                 &f.document, geometry_source_id_t{ 9999u } ) == nullptr );
}

TEST_CASE( "Document: remove brush decreases count",
           "[Gate3][Document]" )
{
    DocumentFixture f;
    brush_solid_t box{};
    f.MakeBox( &box );
    const geometry_source_id_t boxId = box.sourceId;

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &f.document ) == 1u );

    REQUIRE( GeometryDocument_TryRemoveBrush( &f.document, boxId ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &f.document ) == 0u );

    // Cannot find it after removal.
    REQUIRE( GeometryDocument_FindBrush( &f.document, boxId ) == nullptr );

    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document: remove nonexistent brush fails",
           "[Gate3][Document]" )
{
    DocumentFixture f;
    REQUIRE( GeometryDocument_TryRemoveBrush(
                 &f.document, geometry_source_id_t{ 9999u } ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Document: duplicate brush ID rejected",
           "[Gate3][Document]" )
{
    DocumentFixture f;
    brush_solid_t box{};
    f.MakeBox( &box );

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::OK );

    // Same brush (same source ID) should be rejected.
    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::IDENTITY_CONFLICT );

    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document: duplicate IDs inside an incoming brush are atomic",
           "[Gate3][Document][identity][contract]" )
{
    common::allocator_t sourceAllocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t box{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &box, &sourceAllocator, policy, &idAllocator,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );

    document_failure_allocator_state_t state{};
    state.iFailure = 0u;
    common::allocator_t allocator = MakeDocumentFailureAllocator( &state );
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init(
                 &document, &allocator, policy ) ==
             geometry_status_t::OK );

    REQUIRE( box.sides.nCount == 6u );
    box.sides.pData[1u].sourceId = box.sides.pData[0u].sourceId;

    REQUIRE( GeometryDocument_TryAddBrush( &document, &box ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    CHECK( state.cAllocationCalls == 0u );
    CHECK( GeometryDocument_BrushCount( &document ) == 0u );
    CHECK( GeometrySourceIdRegistry_Count( &document.sourceIds ) == 0u );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
               &document.sourceIds ) == 0u );
    CHECK( document.sourceIds.allocator.next.value == 1u );
    CHECK( GeometryDocument_GetRevision( &document ) ==
           GEOMETRY_REVISION_INITIAL );
    CHECK( GeometrySourceIdRegistry_ValidateDeep(
        &document.sourceIds ) );

    GeometryDocument_Shutdown( &document );
    CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document: side ID collision with live state is atomic",
           "[Gate3][Document][identity][contract]" )
{
    DocumentFixture f;
    brush_solid_t first{};
    brush_solid_t second{};
    f.MakeBox( &first );
    f.MakeBox( &second );

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &first ) ==
             geometry_status_t::OK );
    REQUIRE( first.sides.nCount == 6u );
    REQUIRE( second.sides.nCount == 6u );
    second.sides.pData[0u].sourceId = first.sides.pData[3u].sourceId;

    const geometry_revision_t revisionBefore =
        GeometryDocument_GetRevision( &f.document );
    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &second ) ==
             geometry_status_t::IDENTITY_CONFLICT );

    CHECK( GeometryDocument_BrushCount( &f.document ) == 1u );
    CHECK( GeometryDocument_FindBrush(
               &f.document, first.sourceId ) != nullptr );
    CHECK( GeometryDocument_FindBrush(
               &f.document, second.sourceId ) == nullptr );
    CHECK( GeometrySourceIdRegistry_Count( &f.document.sourceIds ) == 7u );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
               &f.document.sourceIds ) == 7u );
    CHECK( f.document.sourceIds.allocator.next.value == 8u );
    CHECK( GeometryDocument_GetRevision( &f.document ) == revisionBefore );
    CHECK( GeometrySourceIdRegistry_ValidateDeep(
        &f.document.sourceIds ) );

    BrushSolid_Shutdown( &second );
    BrushSolid_Shutdown( &first );
}

TEST_CASE( "Document: every add allocation failure preserves state",
           "[Gate3][Document][allocation][contract]" )
{
    common::allocator_t sourceAllocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t box{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &box, &sourceAllocator, policy, &idAllocator,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );

    document_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeDocumentFailureAllocator( &baselineState );
    geometry_document_t baseline{};
    REQUIRE( GeometryDocument_Init(
                 &baseline, &baselineAllocator, policy ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush( &baseline, &box ) ==
             geometry_status_t::OK );
    const common::usize cAllocationAttempts =
        baselineState.cAllocationCalls;
    REQUIRE( cAllocationAttempts > 0u );
    CHECK( GeometryDocument_BrushCount( &baseline ) == 1u );
    CHECK( GeometrySourceIdRegistry_Count( &baseline.sourceIds ) == 7u );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
               &baseline.sourceIds ) == 7u );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &baseline.sourceIds ) );
    GeometryDocument_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations ==
             baselineState.cFreeCalls );

    for ( common::usize iFailure = 0u;
          iFailure < cAllocationAttempts;
          ++iFailure ) {
        CAPTURE( iFailure, cAllocationAttempts );

        document_failure_allocator_state_t state{};
        state.iFailure = iFailure;
        common::allocator_t allocator =
            MakeDocumentFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) ==
                 geometry_status_t::OK );

        REQUIRE( GeometryDocument_TryAddBrush( &document, &box ) ==
                 geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cAllocationCalls == iFailure + 1u );
        CHECK( GeometryDocument_BrushCount( &document ) == 0u );
        CHECK( GeometryDocument_FindBrush(
                   &document, box.sourceId ) == nullptr );
        CHECK( GeometryDocument_GetRevision( &document ) ==
               GEOMETRY_REVISION_INITIAL );
        CHECK( GeometrySourceIdRegistry_Count(
                   &document.sourceIds ) == 0u );
        CHECK( GeometrySourceIdRegistry_ClaimedCount(
                   &document.sourceIds ) == 0u );
        CHECK( document.sourceIds.allocator.next.value == 1u );
        CHECK( GeometrySourceIdRegistry_ValidateDeep(
            &document.sourceIds ) );

        GeometryDocument_Shutdown( &document );
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }

    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document: every retired-ID re-add allocation failure is atomic",
           "[Gate3][Document][allocation][identity][contract]" )
{
    common::allocator_t sourceAllocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t box{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &box, &sourceAllocator, policy, &idAllocator,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );

    document_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeDocumentFailureAllocator( &baselineState );
    geometry_document_t baseline{};
    REQUIRE( GeometryDocument_Init(
                 &baseline, &baselineAllocator, policy ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush( &baseline, &box ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryRemoveBrush(
                 &baseline, box.sourceId ) == geometry_status_t::OK );
    const common::usize iFirstReAddAllocation =
        baselineState.cAllocationCalls;
    REQUIRE( GeometryDocument_TryAddBrush( &baseline, &box ) ==
             geometry_status_t::OK );
    const common::usize cReAddAllocationAttempts =
        baselineState.cAllocationCalls - iFirstReAddAllocation;
    REQUIRE( cReAddAllocationAttempts > 0u );
    GeometryDocument_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations ==
             baselineState.cFreeCalls );

    for ( common::usize iFailure = 0u;
          iFailure < cReAddAllocationAttempts;
          ++iFailure ) {
        CAPTURE( iFailure, cReAddAllocationAttempts );

        document_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeDocumentFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrush( &document, &box ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryRemoveBrush(
                     &document, box.sourceId ) ==
                 geometry_status_t::OK );

        const common::usize iFailureCall =
            state.cAllocationCalls + iFailure;
        state.iFailure = iFailureCall;
        REQUIRE( GeometryDocument_TryAddBrush( &document, &box ) ==
                 geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cAllocationCalls == iFailureCall + 1u );
        CHECK( GeometryDocument_BrushCount( &document ) == 0u );
        CHECK( GeometryDocument_FindBrush(
                   &document, box.sourceId ) == nullptr );
        CHECK( GeometryDocument_GetRevision( &document ) ==
               GEOMETRY_REVISION_INITIAL );
        CHECK( GeometrySourceIdRegistry_Count(
                   &document.sourceIds ) == 0u );
        CHECK( GeometrySourceIdRegistry_ClaimedCount(
                   &document.sourceIds ) == 7u );
        CHECK( document.sourceIds.allocator.next.value == 8u );
        CHECK( GeometrySourceIdRegistry_ValidateDeep(
            &document.sourceIds ) );

        GeometryDocument_Shutdown( &document );
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }

    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document: removed identities restore without new claims",
           "[Gate3][Document][identity][contract]" )
{
    DocumentFixture f;
    brush_solid_t box{};
    f.MakeBox( &box );
    const geometry_source_id_t brushId = box.sourceId;

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryRemoveBrush(
                 &f.document, brushId ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Count(
                 &f.document.sourceIds ) == 0u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount(
                 &f.document.sourceIds ) == 7u );
    REQUIRE( f.document.sourceIds.allocator.next.value == 8u );

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::OK );
    CHECK( GeometryDocument_BrushCount( &f.document ) == 1u );
    CHECK( GeometrySourceIdRegistry_Count( &f.document.sourceIds ) == 7u );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
               &f.document.sourceIds ) == 7u );
    CHECK( f.document.sourceIds.allocator.next.value == 8u );
    CHECK( GeometrySourceIdRegistry_ValidateDeep(
        &f.document.sourceIds ) );

    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document: remove preflight prevents partial ID release",
           "[Gate3][Document][identity][contract]" )
{
    DocumentFixture f;
    brush_solid_t box{};
    f.MakeBox( &box );
    const geometry_source_id_t brushId = box.sourceId;
    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::OK );

    brush_solid_side_t missingSide{};
    REQUIRE( BrushSolid_TryGetSide( &box, 3u, &missingSide ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Release(
                 &f.document.sourceIds, missingSide.sourceId ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Count(
                 &f.document.sourceIds ) == 6u );

    const geometry_revision_t revisionBefore =
        GeometryDocument_GetRevision( &f.document );
    REQUIRE( GeometryDocument_TryRemoveBrush(
                 &f.document, brushId ) ==
             geometry_status_t::CORRUPT_STATE );

    CHECK( GeometryDocument_BrushCount( &f.document ) == 1u );
    CHECK( GeometryDocument_FindBrush(
               &f.document, brushId ) != nullptr );
    CHECK( GeometrySourceIdRegistry_Contains(
        &f.document.sourceIds, brushId ) );
    for ( common::usize iSide = 0u; iSide < box.sides.nCount; ++iSide ) {
        const geometry_source_id_t sideId =
            box.sides.pData[iSide].sourceId;
        CHECK( GeometrySourceIdRegistry_Contains(
                   &f.document.sourceIds, sideId ) ==
               ( sideId.value != missingSide.sourceId.value ) );
    }
    CHECK( GeometrySourceIdRegistry_Count( &f.document.sourceIds ) == 6u );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
               &f.document.sourceIds ) == 7u );
    CHECK( GeometryDocument_GetRevision( &f.document ) == revisionBefore );
    CHECK( GeometrySourceIdRegistry_ValidateDeep(
        &f.document.sourceIds ) );

    REQUIRE( GeometrySourceIdRegistry_RestoreRetired(
                 &f.document.sourceIds, missingSide.sourceId ) ==
             geometry_status_t::OK );
    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document: deep copy semantics — modifying original does "
           "not affect document",
           "[Gate3][Document]" )
{
    DocumentFixture f;
    brush_solid_t box{};
    f.MakeBox( &box );
    const geometry_source_id_t boxId = box.sourceId;

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::OK );

    // Modify the original brush's first side plane.
    (void)BrushSolid_TrySetSidePlane(
        &box, 0u,
        Planed_Make( Vec3d_Make( 0.0, 1.0, 0.0 ), 999.0 ) );

    // The document's copy should be unaffected.
    const brush_solid_t *pDoc =
        GeometryDocument_FindBrush( &f.document, boxId );
    REQUIRE( pDoc != nullptr );
    brush_solid_side_t docSide{};
    REQUIRE( BrushSolid_TryGetSide( pDoc, 0u, &docSide ) ==
             geometry_status_t::OK );
    // Original box has +X normal on side 0; the document copy must still
    // have it, not the (0,1,0) we set on the local copy.
    REQUIRE( docSide.plane.normal.x == Approx( 1.0 ) );

    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document: multiple brushes", "[Gate3][Document]" )
{
    DocumentFixture f;

    brush_solid_t box1{};
    f.MakeBox( &box1 );
    brush_solid_t box2{};
    f.MakeBox( &box2 );
    const geometry_source_id_t id1 = box1.sourceId;
    const geometry_source_id_t id2 = box2.sourceId;

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box1 ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box2 ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &f.document ) == 2u );

    // Both findable.
    REQUIRE( GeometryDocument_FindBrush( &f.document, id1 ) != nullptr );
    REQUIRE( GeometryDocument_FindBrush( &f.document, id2 ) != nullptr );

    // Remove first, second still findable.
    REQUIRE( GeometryDocument_TryRemoveBrush( &f.document, id1 ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &f.document ) == 1u );
    REQUIRE( GeometryDocument_FindBrush( &f.document, id1 ) == nullptr );
    REQUIRE( GeometryDocument_FindBrush( &f.document, id2 ) != nullptr );

    BrushSolid_Shutdown( &box1 );
    BrushSolid_Shutdown( &box2 );
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

TEST_CASE( "Snapshot: captures document state", "[Gate3][Snapshot]" )
{
    DocumentFixture f;
    brush_solid_t box{};
    f.MakeBox( &box );
    const geometry_source_id_t boxId = box.sourceId;

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::OK );

    geometry_snapshot_t snapshot{};
    REQUIRE( GeometrySnapshot_TakeFromDocument(
                 &snapshot, &f.document ) ==
             geometry_status_t::OK );

    REQUIRE( GeometrySnapshot_IsInitialized( &snapshot ) );
    REQUIRE( GeometrySnapshot_GetRevision( &snapshot ) ==
             GEOMETRY_REVISION_INITIAL );
    REQUIRE( GeometrySnapshot_GetPolicy( &snapshot ) != nullptr );
    REQUIRE( GeometrySnapshot_GetPolicy(
                 &snapshot )->numerical.fSnapDistance ==
             f.document.policy.numerical.fSnapDistance );
    REQUIRE( GeometrySnapshot_BrushCount( &snapshot ) == 1u );
    REQUIRE( GeometrySnapshot_FindBrush( &snapshot, boxId ) != nullptr );

    GeometrySnapshot_Shutdown( &snapshot );
    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Snapshot: immutable after document mutation",
           "[Gate3][Snapshot]" )
{
    DocumentFixture f;
    brush_solid_t box1{};
    f.MakeBox( &box1 );
    const geometry_source_id_t id1 = box1.sourceId;

    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box1 ) ==
             geometry_status_t::OK );

    // Take a snapshot with one brush.
    geometry_snapshot_t snapshot{};
    REQUIRE( GeometrySnapshot_TakeFromDocument(
                 &snapshot, &f.document ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySnapshot_BrushCount( &snapshot ) == 1u );

    // Add a second brush to the document.
    brush_solid_t box2{};
    f.MakeBox( &box2 );
    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box2 ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &f.document ) == 2u );

    // Snapshot must still show only one brush.
    REQUIRE( GeometrySnapshot_BrushCount( &snapshot ) == 1u );

    // Remove the first brush from the document.
    REQUIRE( GeometryDocument_TryRemoveBrush( &f.document, id1 ) ==
             geometry_status_t::OK );

    // Snapshot still finds the removed brush.
    REQUIRE( GeometrySnapshot_FindBrush( &snapshot, id1 ) != nullptr );

    GeometrySnapshot_Shutdown( &snapshot );
    BrushSolid_Shutdown( &box1 );
    BrushSolid_Shutdown( &box2 );
}

TEST_CASE( "Snapshot: double init rejected", "[Gate3][Snapshot]" )
{
    DocumentFixture f;
    geometry_snapshot_t snapshot{};
    REQUIRE( GeometrySnapshot_TakeFromDocument(
                 &snapshot, &f.document ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySnapshot_TakeFromDocument(
                 &snapshot, &f.document ) ==
             geometry_status_t::ALREADY_INITIALIZED );
    GeometrySnapshot_Shutdown( &snapshot );
}

TEST_CASE( "Snapshot: requires an exact default destination",
           "[Gate3][Snapshot][FailureAtomic]" )
{
    DocumentFixture f;
    geometry_snapshot_t snapshot{};
    snapshot.policy.numerical.fSnapDistance = 0.25;

    REQUIRE( GeometrySnapshot_TakeFromDocument(
                 &snapshot, &f.document ) ==
             geometry_status_t::ALREADY_INITIALIZED );
    REQUIRE( snapshot.policy.numerical.fSnapDistance == 0.25 );
    REQUIRE_FALSE( GeometrySnapshot_IsInitialized( &snapshot ) );
}

TEST_CASE( "Snapshot: null arguments rejected", "[Gate3][Snapshot]" )
{
    REQUIRE( GeometrySnapshot_TakeFromDocument( nullptr, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Snapshot: shutdown safe on null", "[Gate3][Snapshot]" )
{
    GeometrySnapshot_Shutdown( nullptr );
}

TEST_CASE( "Snapshot: corrupt document is rejected without publishing output",
           "[Gate3][Snapshot][FailureAtomic]" )
{
    DocumentFixture f;
    brush_solid_t box{};
    f.MakeBox( &box );
    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &box ) ==
             geometry_status_t::OK );
    BrushSolid_Shutdown( &box );

    brush_solid_t *pStored = f.document.brushes.pData[0];
    f.document.brushes.pData[0] = nullptr;
    geometry_snapshot_t snapshot{};
    REQUIRE( GeometrySnapshot_TakeFromDocument(
                 &snapshot, &f.document ) ==
             geometry_status_t::CORRUPT_STATE );
    REQUIRE_FALSE( GeometrySnapshot_IsInitialized( &snapshot ) );
    REQUIRE( snapshot.brushes.pData == nullptr );
    REQUIRE( snapshot.brushes.pAllocator == nullptr );
    REQUIRE( GeometrySnapshot_GetPolicy( &snapshot ) == nullptr );

    f.document.brushes.pData[0] = pStored;
}

TEST_CASE( "Snapshot: every allocation failure leaves canonical output and no leak",
           "[Gate3][Snapshot][FailureAtomic][allocation]" )
{
    geometry_policy_t policy{};

    document_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeDocumentFailureAllocator( &baselineState );
    geometry_document_t baselineDocument{};
    REQUIRE( GeometryDocument_Init(
                 &baselineDocument, &baselineAllocator, policy ) ==
             geometry_status_t::OK );
    geometry_source_id_allocator_t baselineIds{};
    brush_solid_t baselineBox{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &baselineBox,
                 &baselineAllocator,
                 policy,
                 &baselineIds,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush(
                 &baselineDocument, &baselineBox ) ==
             geometry_status_t::OK );
    BrushSolid_Shutdown( &baselineBox );
    const common::usize iSnapshotBegin =
        baselineState.cAllocationCalls;
    geometry_snapshot_t baselineSnapshot{};
    REQUIRE( GeometrySnapshot_TakeFromDocument(
                 &baselineSnapshot, &baselineDocument ) ==
             geometry_status_t::OK );
    const common::usize cSnapshotAllocations =
        baselineState.cAllocationCalls - iSnapshotBegin;
    REQUIRE( cSnapshotAllocations > 0u );
    GeometrySnapshot_Shutdown( &baselineSnapshot );
    GeometryDocument_Shutdown( &baselineDocument );
    REQUIRE( baselineState.cSuccessfulAllocations ==
             baselineState.cFreeCalls );

    for ( common::usize iFailure = 0u;
          iFailure < cSnapshotAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cSnapshotAllocations );
        document_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeDocumentFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) ==
                 geometry_status_t::OK );
        geometry_source_id_allocator_t ids{};
        brush_solid_t box{};
        REQUIRE( BrushGenerator_TryMakeBox(
                     &box,
                     &allocator,
                     policy,
                     &ids,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrush(
                     &document, &box ) == geometry_status_t::OK );
        BrushSolid_Shutdown( &box );

        state.iFailure = state.cAllocationCalls + iFailure;
        geometry_snapshot_t snapshot{};
        REQUIRE( GeometrySnapshot_TakeFromDocument(
                     &snapshot, &document ) ==
                 geometry_status_t::ALLOCATION_FAILED );
        REQUIRE_FALSE( GeometrySnapshot_IsInitialized( &snapshot ) );
        REQUIRE( snapshot.brushes.pData == nullptr );
        REQUIRE( snapshot.brushes.nCount == 0u );
        REQUIRE( snapshot.brushes.nCapacity == 0u );
        REQUIRE( snapshot.brushes.pAllocator == nullptr );
        REQUIRE( GeometrySnapshot_GetPolicy( &snapshot ) == nullptr );

        state.iFailure = common::CY_USIZE_MAX;
        GeometryDocument_Shutdown( &document );
        REQUIRE( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

// ---------------------------------------------------------------------------
// BrushSolid_DeepCopy
// ---------------------------------------------------------------------------

TEST_CASE( "BrushSolid_DeepCopy: produces independent copy",
           "[Gate3][Document]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};

    brush_solid_t src{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &src, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );

    brush_solid_t copy{};
    REQUIRE( BrushSolid_DeepCopy(
                 &copy, &src, &allocator, policy.limits ) ==
             geometry_status_t::OK );

    REQUIRE( copy.sourceId.value == src.sourceId.value );
    REQUIRE( BrushSolid_SideCount( &copy ) == 6u );

    // Modify the source — copy must be unaffected.
    (void)BrushSolid_TrySetSidePlane(
        &src, 0u,
        Planed_Make( Vec3d_Make( 0.0, 0.0, 1.0 ), 100.0 ) );

    brush_solid_side_t copySide{};
    REQUIRE( BrushSolid_TryGetSide( &copy, 0u, &copySide ) ==
             geometry_status_t::OK );
    REQUIRE( copySide.plane.normal.x == Approx( 1.0 ) );

    BrushSolid_Shutdown( &src );
    BrushSolid_Shutdown( &copy );
}

} // namespace cypher::editor::geometry
