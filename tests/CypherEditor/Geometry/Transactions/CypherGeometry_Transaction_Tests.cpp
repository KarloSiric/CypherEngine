//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Transaction_Tests.cpp
//  Purpose: Verifies preview/commit/cancel transactions, invertible change
//           records, and the linear undo/redo history.
//  Details: Covers the Gate 3 closing criterion: repeated preview
//           replacement of one canonical brush value creates one committed
//           revision and one inverse delta; cancel, invalid input, stale
//           revision, allocation failure, and cancellation restore exact
//           authored state; previously published snapshots stay unchanged.
//
//           "Exact state" is checked by value identity: the document must
//           point at the very same immutable value objects, with the same
//           registry membership, as before.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_Document.h"
#include "CypherGeometry_History.h"
#include "CypherGeometry_Snapshot.h"
#include "CypherGeometry_Transaction.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::planed_t;
using cypher::math::vec3d_t;

namespace {

struct failing_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *FailingAllocate(
    void *pUserData, common::usize cbSize, common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failing_allocator_state_t *>( pUserData );
    const common::usize iAllocation = pState->cAllocationCalls++;
    if ( iAllocation == pState->iFailure ) {
        return nullptr;
    }
    return common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
}

void FailingFree(
    void *, void *pMemory, common::usize cbSize, common::usize nAlignment ) noexcept
{
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

struct document_holder_t {
    common::allocator_t custom{};
    geometry_document_t document{};

    document_holder_t()
    {
        geometry_document_desc_t desc{};
        desc.pAllocator = common::Allocator_GetSystem();
        REQUIRE( GeometryDocument_Init( &document, desc ) == geometry_status_t::OK );
    }
    explicit document_holder_t( failing_allocator_state_t *pState )
        : custom{ FailingAllocate, nullptr, FailingFree, pState }
    {
        geometry_document_desc_t desc{};
        desc.pAllocator = &custom;
        REQUIRE( GeometryDocument_Init( &document, desc ) == geometry_status_t::OK );
    }
    ~document_holder_t() { GeometryDocument_Shutdown( &document ); }
};

struct value_ref_t {
    const geometry_brush_value_t *p{ nullptr };
    value_ref_t() = default;
    value_ref_t( const value_ref_t & ) = delete;
    value_ref_t &operator=( const value_ref_t & ) = delete;
    value_ref_t( value_ref_t &&other ) noexcept : p( other.p ) { other.p = nullptr; }
    value_ref_t &operator=( value_ref_t &&other ) noexcept
    {
        if ( this != &other ) {
            BrushValue_Release( p );
            p = std::exchange( other.p, nullptr );
        }
        return *this;
    }
    ~value_ref_t() { BrushValue_Release( p ); }
};

struct snapshot_ref_t {
    const geometry_document_snapshot_t *p{ nullptr };
    snapshot_ref_t() = default;
    snapshot_ref_t( const snapshot_ref_t & ) = delete;
    snapshot_ref_t &operator=( const snapshot_ref_t & ) = delete;
    ~snapshot_ref_t() { GeometrySnapshot_Release( p ); }
};

struct record_ref_t {
    geometry_change_record_t *p{ nullptr };
    record_ref_t() = default;
    record_ref_t( const record_ref_t & ) = delete;
    record_ref_t &operator=( const record_ref_t & ) = delete;
    ~record_ref_t() { GeometryChangeRecord_Destroy( p ); }
    geometry_change_record_t *Release() noexcept { return std::exchange( p, nullptr ); }
};

// Builds a box value inside a transaction, drawing its IDs through the
// transaction so a cancel retires them.
void MakeBoxValue(
    geometry_transaction_t *pTransaction,
    vec3d_t center,
    vec3d_t halfExtents,
    value_ref_t *pOut )
{
    geometry_document_t *pDocument = pTransaction->pDocument;
    geometry_source_id_t ids[BRUSH_GENERATOR_BOX_ID_COUNT]{};
    REQUIRE( GeometryTransaction_TryAllocateSourceIds(
                 pTransaction,
                 common::span_t<geometry_source_id_t>{ ids, BRUSH_GENERATOR_BOX_ID_COUNT } ) ==
             geometry_status_t::OK );
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeBoxWithIds(
                 &brush, common::Allocator_GetSystem(), pDocument->policy, ids,
                 center, halfExtents ) == geometry_status_t::OK );
    geometry_brush_side_attribute_store_t attributes{};
    REQUIRE( BrushSideAttributeStore_Init( &attributes, common::Allocator_GetSystem() ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppendDefaults( &attributes, pDocument->policy, 6u ) ==
             geometry_status_t::OK );
    const geometry_status_t status = GeometryDocument_TryCreateBrushValue(
        pDocument, &brush, &attributes, &pOut->p, nullptr );
    BrushSideAttributeStore_Shutdown( &attributes );
    BrushSolid_Shutdown( &brush );
    REQUIRE( status == geometry_status_t::OK );
}

// A drag of the +X side to x = position, as a fresh value.
geometry_status_t MakeDraggedValue(
    geometry_document_t *pDocument,
    const geometry_brush_value_t *pSource,
    double position,
    value_ref_t *pOut )
{
    brush_solid_t brush{};
    REQUIRE( BrushSolid_Init( &brush, common::Allocator_GetSystem(), pSource->brush.sourceId ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_TryCopyFrom( &brush, &pSource->brush, pDocument->policy.limits ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_TrySetSidePlane(
                 &brush, 0u,
                 cypher::math::Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -position ) ) ==
             geometry_status_t::OK );
    const geometry_status_t status = GeometryDocument_TryCreateBrushValue(
        pDocument, &brush, &pSource->attributes, &pOut->p, nullptr );
    BrushSolid_Shutdown( &brush );
    return status;
}

// Commits a set of boxes and returns their values (caller keeps a ref).
std::vector<value_ref_t> SeedBoxes( geometry_document_t *pDocument, common::usize cBoxes )
{
    std::vector<value_ref_t> values( cBoxes );
    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
    for ( common::usize i = 0u; i < cBoxes; ++i ) {
        MakeBoxValue( &transaction, Vec3d_Make( 4.0 * static_cast<double>( i ), 0.0, 0.0 ),
                      Vec3d_Make( 1.0, 1.0, 1.0 ), &values[i] );
        REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, values[i].p ) ==
                 geometry_status_t::OK );
    }
    record_ref_t record{};
    REQUIRE( GeometryTransaction_TryCommit( &transaction, &record.p ) == geometry_status_t::OK );
    REQUIRE( record.p != nullptr );
    return values;
}

// Every externally observable property of the committed state, compared
// by value identity.
struct authored_state_t {
    std::vector<std::pair<common::u64, const geometry_brush_value_t *>> brushes;
    common::usize cLive;
    common::usize cOwners;
    common::u64 cSides;
};

authored_state_t CaptureState( geometry_document_t *pDocument )
{
    authored_state_t state{};
    snapshot_ref_t snapshot{};
    REQUIRE( GeometryDocument_TryAcquireSnapshot( pDocument, &snapshot.p ) ==
             geometry_status_t::OK );
    for ( common::usize i = 0u; i < GeometrySnapshot_BrushCount( snapshot.p ); ++i ) {
        geometry_snapshot_brush_t entry{};
        REQUIRE( GeometrySnapshot_TryGetBrush( snapshot.p, i, &entry ) == geometry_status_t::OK );
        state.brushes.emplace_back( entry.brushId.value, entry.pValue );
    }
    state.cLive = GeometrySourceIdRegistry_Count( &pDocument->registry );
    state.cOwners = common::HashMap_Count( &pDocument->owners );
    state.cSides = pDocument->cSides;
    REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );
    return state;
}

// Committed state only. While a transaction is active its pending source
// IDs are legitimately live in the registry, so the live count is not part
// of this comparison.
void RequireSameCommitted( const authored_state_t &a, const authored_state_t &b )
{
    REQUIRE( a.brushes == b.brushes );
    REQUIRE( a.cOwners == b.cOwners );
    REQUIRE( a.cSides == b.cSides );
}

// Committed state plus registry live membership; valid only when no
// transaction on the document is active.
void RequireSameState( const authored_state_t &a, const authored_state_t &b )
{
    RequireSameCommitted( a, b );
    REQUIRE( a.cLive == b.cLive );
}

} // namespace

//==========================================================================
// Gate 3 closing criterion
//==========================================================================

TEST_CASE( "repeated preview replacement commits one revision and one inverse delta",
           "[editor][geometry][transactions]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 1u );
    const geometry_brush_value_t *pOriginal = seeded[0].p;
    const geometry_source_id_t brushId = pOriginal->brush.sourceId;
    const common::u64 baseRevision = GeometryDocument_Revision( pDocument );

    snapshot_ref_t published{};
    REQUIRE( GeometryDocument_TryAcquireSnapshot( pDocument, &published.p ) ==
             geometry_status_t::OK );
    const authored_state_t before = CaptureState( pDocument );

    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );

    // Sixty drag updates of the +X face. Nothing reaches the document.
    value_ref_t last{};
    for ( int step = 1; step <= 60; ++step ) {
        value_ref_t dragged{};
        REQUIRE( MakeDraggedValue( pDocument, pOriginal, 1.0 + 0.05 * step, &dragged ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryTransaction_TryPreviewReplace( &transaction, dragged.p ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryDocument_Revision( pDocument ) == baseRevision );
        const geometry_brush_value_t *pPreview = nullptr;
        REQUIRE( GeometryTransaction_TryGetPreview( &transaction, brushId, &pPreview ) ==
                 geometry_status_t::OK );
        REQUIRE( pPreview == dragged.p );
        last = std::move( dragged );
    }
    REQUIRE( GeometryTransaction_ChangeCount( &transaction ) == 1u );
    RequireSameState( CaptureState( pDocument ), before );

    record_ref_t record{};
    REQUIRE( GeometryTransaction_TryCommit( &transaction, &record.p ) == geometry_status_t::OK );
    REQUIRE_FALSE( GeometryTransaction_IsActive( &transaction ) );
    REQUIRE( GeometryDocument_Revision( pDocument ) == baseRevision + 1u );

    // One inverse delta: one MODIFIED entry from the original to the last preview.
    REQUIRE( GeometryChangeRecord_Count( record.p ) == 1u );
    geometry_change_entry_t entry{};
    REQUIRE( GeometryChangeRecord_TryGetEntry( record.p, 0u, &entry ) == geometry_status_t::OK );
    REQUIRE( GeometryChangeEntry_Kind( entry ) == geometry_change_kind_t::MODIFIED );
    REQUIRE( entry.brushId.value == brushId.value );
    REQUIRE( entry.pBefore == pOriginal );
    REQUIRE( entry.pAfter == last.p );
    REQUIRE( record.p->revisionBefore == baseRevision );
    REQUIRE( record.p->revisionAfter == baseRevision + 1u );
    REQUIRE( last.p->bounds.maximum.x == 4.0 );

    // The snapshot published before the edit is untouched.
    const geometry_brush_value_t *pSnapshotValue = nullptr;
    REQUIRE( GeometrySnapshot_TryFindBrush( published.p, brushId, &pSnapshotValue ) ==
             geometry_status_t::OK );
    REQUIRE( pSnapshotValue == pOriginal );
    REQUIRE( pSnapshotValue->bounds.maximum.x == 1.0 );

    // The inverse delta restores exact authored state.
    common::u64 undoRevision = 0u;
    REQUIRE( GeometryChangeRecord_TryApply(
                 record.p, pDocument, geometry_change_direction_t::INVERSE,
                 GeometryDocument_Revision( pDocument ), &undoRevision ) ==
             geometry_status_t::OK );
    REQUIRE( undoRevision == baseRevision + 2u );
    RequireSameState( CaptureState( pDocument ), before );
}

//==========================================================================
// Cancel
//==========================================================================

TEST_CASE( "cancel restores exact authored state and retires pending IDs",
           "[editor][geometry][transactions]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 3u );
    const authored_state_t before = CaptureState( pDocument );
    const common::u64 revision = GeometryDocument_Revision( pDocument );
    const common::usize cClaimed = GeometrySourceIdRegistry_ClaimedCount( &pDocument->registry );

    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
    value_ref_t inserted{};
    MakeBoxValue( &transaction, Vec3d_Make( 0.0, 8.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &inserted );
    value_ref_t dragged{};
    REQUIRE( MakeDraggedValue( pDocument, seeded[0].p, 2.0, &dragged ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, inserted.p ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewReplace( &transaction, dragged.p ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, seeded[1].p->brush.sourceId ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryTransaction_ChangeCount( &transaction ) == 3u );
    REQUIRE( GeometrySourceIdRegistry_Count( &pDocument->registry ) == before.cLive + 7u );

    GeometryTransaction_Cancel( &transaction );
    REQUIRE_FALSE( GeometryTransaction_IsActive( &transaction ) );
    REQUIRE( GeometryDocument_Revision( pDocument ) == revision );
    RequireSameState( CaptureState( pDocument ), before );
    // The seven IDs stay claimed (never reused) but are no longer live.
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &pDocument->registry ) == cClaimed + 7u );
    for ( common::usize i = 0u; i < 7u; ++i ) {
        const geometry_source_id_t id =
            i == 0u ? inserted.p->brush.sourceId : inserted.p->brush.sides.pData[i - 1u].sourceId;
        REQUIRE_FALSE( GeometrySourceIdRegistry_Contains( &pDocument->registry, id ) );
    }
    // Preview references were dropped: only the test holds these values.
    REQUIRE( BrushValue_RefCount( inserted.p ) == 1u );
    REQUIRE( BrushValue_RefCount( dragged.p ) == 1u );

    GeometryTransaction_Cancel( &transaction ); // idempotent
}

TEST_CASE( "destroying an active transaction cancels it",
           "[editor][geometry][transactions]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 1u );
    const authored_state_t before = CaptureState( pDocument );
    value_ref_t dragged{};
    REQUIRE( MakeDraggedValue( pDocument, seeded[0].p, 3.0, &dragged ) == geometry_status_t::OK );
    {
        geometry_transaction_t transaction{};
        REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
        REQUIRE( GeometryTransaction_TryPreviewReplace( &transaction, dragged.p ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushValue_RefCount( dragged.p ) == 2u );
    }
    REQUIRE( BrushValue_RefCount( dragged.p ) == 1u );
    RequireSameState( CaptureState( pDocument ), before );
}

//==========================================================================
// Preview semantics
//==========================================================================

TEST_CASE( "preview operations follow the brush's preview existence",
           "[editor][geometry][transactions]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 1u );
    const geometry_brush_value_t *pCommitted = seeded[0].p;
    const geometry_source_id_t brushId = pCommitted->brush.sourceId;

    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) ==
             geometry_status_t::ALREADY_INITIALIZED );

    // A committed brush cannot be inserted again.
    REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, pCommitted ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    // Untouched brushes preview as their committed value.
    const geometry_brush_value_t *pPreview = nullptr;
    REQUIRE( GeometryTransaction_TryGetPreview( &transaction, brushId, &pPreview ) ==
             geometry_status_t::OK );
    REQUIRE( pPreview == pCommitted );

    REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, brushId ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryGetPreview( &transaction, brushId, &pPreview ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, brushId ) ==
             geometry_status_t::INVALID_HANDLE );
    value_ref_t dragged{};
    REQUIRE( MakeDraggedValue( pDocument, pCommitted, 2.0, &dragged ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewReplace( &transaction, dragged.p ) ==
             geometry_status_t::INVALID_HANDLE );

    // Re-inserting the removed brush turns the entry into a replace...
    REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, dragged.p ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_ChangeCount( &transaction ) == 1u );
    // ...and re-inserting the original value makes it a no-op.
    REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, brushId ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, pCommitted ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_ChangeCount( &transaction ) == 0u );

    // Unknown brushes cannot be replaced or removed.
    REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, geometry_source_id_t{ 999u } ) ==
             geometry_status_t::INVALID_HANDLE );
    REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, geometry_source_id_t{} ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometryTransaction_TryPreviewReplace( &transaction, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );

    // A no-op commit publishes nothing.
    const common::u64 revision = GeometryDocument_Revision( pDocument );
    record_ref_t record{};
    REQUIRE( GeometryTransaction_TryCommit( &transaction, &record.p ) == geometry_status_t::OK );
    REQUIRE( record.p == nullptr );
    REQUIRE( GeometryDocument_Revision( pDocument ) == revision );
    REQUIRE_FALSE( GeometryTransaction_IsActive( &transaction ) );
}

TEST_CASE( "insert followed by remove commits nothing and retires its IDs",
           "[editor][geometry][transactions]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    const common::usize cLive = GeometrySourceIdRegistry_Count( &pDocument->registry );

    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
    value_ref_t box{};
    MakeBoxValue( &transaction, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );
    REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, box.p ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, box.p ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, box.p->brush.sourceId ) ==
             geometry_status_t::OK );

    record_ref_t record{};
    REQUIRE( GeometryTransaction_TryCommit( &transaction, &record.p ) == geometry_status_t::OK );
    REQUIRE( record.p == nullptr );
    REQUIRE( GeometryDocument_Revision( pDocument ) == 1u );
    REQUIRE( GeometrySourceIdRegistry_Count( &pDocument->registry ) == cLive );
}

TEST_CASE( "a rejected preview keeps the last valid preview",
           "[editor][geometry][transactions]" ) {
    document_holder_t holder{};
    document_holder_t other{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 1u );
    std::vector<value_ref_t> foreign = SeedBoxes( &other.document, 1u );

    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
    value_ref_t good{};
    REQUIRE( MakeDraggedValue( pDocument, seeded[0].p, 2.0, &good ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewReplace( &transaction, good.p ) == geometry_status_t::OK );

    // The drag goes past the -X face: no valid value can be created.
    value_ref_t inverted{};
    REQUIRE( MakeDraggedValue( pDocument, seeded[0].p, -1.5, &inverted ) ==
             geometry_status_t::DEGENERATE );
    REQUIRE( inverted.p == nullptr );

    // A value from another document is refused outright.
    REQUIRE( GeometryTransaction_TryPreviewReplace( &transaction, foreign[0].p ) ==
             geometry_status_t::INVALID_ARGUMENT );

    const geometry_brush_value_t *pPreview = nullptr;
    REQUIRE( GeometryTransaction_TryGetPreview( &transaction, seeded[0].p->brush.sourceId,
                                                &pPreview ) == geometry_status_t::OK );
    REQUIRE( pPreview == good.p );
    GeometryTransaction_Cancel( &transaction );
}

//==========================================================================
// Stale revision and commit failures
//==========================================================================

TEST_CASE( "a transaction whose base went stale cannot preview or commit",
           "[editor][geometry][transactions]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 2u );

    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
    value_ref_t dragged{};
    REQUIRE( MakeDraggedValue( pDocument, seeded[0].p, 2.0, &dragged ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewReplace( &transaction, dragged.p ) == geometry_status_t::OK );

    // Another writer commits first.
    geometry_transaction_t other{};
    REQUIRE( GeometryTransaction_Begin( &other, pDocument ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewRemove( &other, seeded[1].p->brush.sourceId ) ==
             geometry_status_t::OK );
    record_ref_t otherRecord{};
    REQUIRE( GeometryTransaction_TryCommit( &other, &otherRecord.p ) == geometry_status_t::OK );
    const authored_state_t afterOther = CaptureState( pDocument );

    REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, seeded[0].p->brush.sourceId ) ==
             geometry_status_t::STALE_REVISION );
    geometry_source_id_t id{};
    REQUIRE( GeometryTransaction_TryAllocateSourceIds(
                 &transaction, common::span_t<geometry_source_id_t>{ &id, 1u } ) ==
             geometry_status_t::STALE_REVISION );
    record_ref_t record{};
    REQUIRE( GeometryTransaction_TryCommit( &transaction, &record.p ) ==
             geometry_status_t::STALE_REVISION );
    REQUIRE( record.p == nullptr );
    REQUIRE( GeometryTransaction_IsActive( &transaction ) );
    RequireSameState( CaptureState( pDocument ), afterOther );

    GeometryTransaction_Cancel( &transaction );
    RequireSameState( CaptureState( pDocument ), afterOther );
}

TEST_CASE( "an identity conflict at commit keeps the transaction active",
           "[editor][geometry][transactions]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 1u );
    const authored_state_t before = CaptureState( pDocument );

    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
    value_ref_t box{};
    MakeBoxValue( &transaction, Vec3d_Make( 0.0, 8.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );

    // A second new brush reusing the first one's side ID.
    brush_solid_t brush{};
    REQUIRE( BrushSolid_Init( &brush, common::Allocator_GetSystem(), box.p->brush.sourceId ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_TryCopyFrom( &brush, &box.p->brush, pDocument->policy.limits ) ==
             geometry_status_t::OK );
    geometry_source_id_t freshRoot{};
    REQUIRE( GeometryTransaction_TryAllocateSourceIds(
                 &transaction, common::span_t<geometry_source_id_t>{ &freshRoot, 1u } ) ==
             geometry_status_t::OK );
    brush.sourceId = freshRoot;
    value_ref_t clash{};
    REQUIRE( GeometryDocument_TryCreateBrushValue( pDocument, &brush, &box.p->attributes, &clash.p,
                                                   nullptr ) == geometry_status_t::OK );
    BrushSolid_Shutdown( &brush );

    REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, box.p ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, clash.p ) == geometry_status_t::OK );
    record_ref_t record{};
    REQUIRE( GeometryTransaction_TryCommit( &transaction, &record.p ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    REQUIRE( record.p == nullptr );
    REQUIRE( GeometryTransaction_IsActive( &transaction ) );
    RequireSameCommitted( CaptureState( pDocument ), before );

    // Dropping the clashing brush lets the commit through.
    REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, freshRoot ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryCommit( &transaction, &record.p ) == geometry_status_t::OK );
    REQUIRE( GeometryChangeRecord_Count( record.p ) == 1u );
    // The unused root ID was retired at commit.
    REQUIRE_FALSE( GeometrySourceIdRegistry_Contains( &pDocument->registry, freshRoot ) );
    REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );
}

TEST_CASE( "commit under every allocation failure publishes all or nothing",
           "[editor][geometry][transactions][atomicity]" ) {
    bool bSucceeded = false;
    common::usize cInjected = 0u;
    for ( common::usize iFailure = 0u; iFailure < 128u && !bSucceeded; ++iFailure ) {
        failing_allocator_state_t state{};
        document_holder_t holder{ &state };
        geometry_document_t *pDocument = &holder.document;
        std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 2u );
        const authored_state_t before = CaptureState( pDocument );

        geometry_transaction_t transaction{};
        REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
        value_ref_t dragged{};
        REQUIRE( MakeDraggedValue( pDocument, seeded[0].p, 2.0, &dragged ) == geometry_status_t::OK );
        value_ref_t inserted{};
        MakeBoxValue( &transaction, Vec3d_Make( 0.0, 8.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
                      &inserted );
        REQUIRE( GeometryTransaction_TryPreviewReplace( &transaction, dragged.p ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, inserted.p ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, seeded[1].p->brush.sourceId ) ==
                 geometry_status_t::OK );

        record_ref_t record{};
        state.iFailure = state.cAllocationCalls + iFailure;
        const geometry_status_t status = GeometryTransaction_TryCommit( &transaction, &record.p );
        state.iFailure = common::CY_USIZE_MAX;

        if ( status == geometry_status_t::OK ) {
            bSucceeded = true;
            REQUIRE( GeometryChangeRecord_Count( record.p ) == 3u );
            REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );
            // And undo restores the seeded state exactly.
            REQUIRE( GeometryChangeRecord_TryApply(
                         record.p, pDocument, geometry_change_direction_t::INVERSE,
                         GeometryDocument_Revision( pDocument ), nullptr ) ==
                     geometry_status_t::OK );
            RequireSameState( CaptureState( pDocument ), before );
        } else {
            ++cInjected;
            REQUIRE( status == geometry_status_t::ALLOCATION_FAILED );
            REQUIRE( record.p == nullptr );
            REQUIRE( GeometryTransaction_IsActive( &transaction ) );
            REQUIRE( GeometryTransaction_ChangeCount( &transaction ) == 3u );
            RequireSameCommitted( CaptureState( pDocument ), before );
            GeometryTransaction_Cancel( &transaction );
            RequireSameState( CaptureState( pDocument ), before );
        }
    }
    REQUIRE( bSucceeded );
    REQUIRE( cInjected > 0u );
}

//==========================================================================
// History
//==========================================================================

namespace {

// Runs one committed drag of seeded brush 0 to the given +X position and
// pushes it into the history.
void CommitDrag( geometry_history_t *pHistory, const geometry_brush_value_t *pSource,
                 double position )
{
    geometry_document_t *pDocument = pHistory->pDocument;
    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
    value_ref_t dragged{};
    REQUIRE( MakeDraggedValue( pDocument, pSource, position, &dragged ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewReplace( &transaction, dragged.p ) == geometry_status_t::OK );
    record_ref_t record{};
    REQUIRE( GeometryTransaction_TryCommit( &transaction, &record.p ) == geometry_status_t::OK );
    REQUIRE( GeometryHistory_TryPush( pHistory, record.p ) == geometry_status_t::OK );
    ( void )record.Release();
}

double CommittedMaxX( geometry_document_t *pDocument, geometry_source_id_t brushId )
{
    const geometry_brush_value_t *pValue = nullptr;
    REQUIRE( GeometryDocument_TryGetBrushById( pDocument, brushId, &pValue ) ==
             geometry_status_t::OK );
    return pValue->bounds.maximum.x;
}

} // namespace

TEST_CASE( "undo and redo walk the history and restore exact state",
           "[editor][geometry][transactions][history]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 1u );
    const geometry_source_id_t brushId = seeded[0].p->brush.sourceId;

    geometry_history_t history{};
    REQUIRE( GeometryHistory_Init( &history, pDocument, 8u ) == geometry_status_t::OK );
    const authored_state_t s0 = CaptureState( pDocument );
    CommitDrag( &history, seeded[0].p, 2.0 );
    const authored_state_t s1 = CaptureState( pDocument );
    CommitDrag( &history, seeded[0].p, 3.0 );
    const authored_state_t s2 = CaptureState( pDocument );
    REQUIRE( GeometryHistory_UndoCount( &history ) == 2u );
    REQUIRE( GeometryHistory_RedoCount( &history ) == 0u );
    REQUIRE( CommittedMaxX( pDocument, brushId ) == 3.0 );

    const geometry_change_record_t *pApplied = nullptr;
    REQUIRE( GeometryHistory_TryUndo( &history, &pApplied ) == geometry_status_t::OK );
    REQUIRE( pApplied != nullptr );
    RequireSameState( CaptureState( pDocument ), s1 );
    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::OK );
    RequireSameState( CaptureState( pDocument ), s0 );
    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometryHistory_RedoCount( &history ) == 2u );

    REQUIRE( GeometryHistory_TryRedo( &history, nullptr ) == geometry_status_t::OK );
    RequireSameState( CaptureState( pDocument ), s1 );
    REQUIRE( GeometryHistory_TryRedo( &history, nullptr ) == geometry_status_t::OK );
    RequireSameState( CaptureState( pDocument ), s2 );
    REQUIRE( GeometryHistory_TryRedo( &history, nullptr ) == geometry_status_t::INVALID_ARGUMENT );

    // Revisions only grow: 1 seed + 2 commits + 2 undos + 2 redos after init at 1.
    REQUIRE( GeometryDocument_Revision( pDocument ) == 8u );
}

TEST_CASE( "a new commit after undo discards the redo tail",
           "[editor][geometry][transactions][history]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 1u );
    const geometry_source_id_t brushId = seeded[0].p->brush.sourceId;

    geometry_history_t history{};
    REQUIRE( GeometryHistory_Init( &history, pDocument, 8u ) == geometry_status_t::OK );
    CommitDrag( &history, seeded[0].p, 2.0 );
    CommitDrag( &history, seeded[0].p, 3.0 );
    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::OK );
    REQUIRE( GeometryHistory_RedoCount( &history ) == 1u );

    CommitDrag( &history, seeded[0].p, 5.0 );
    REQUIRE( GeometryHistory_UndoCount( &history ) == 2u );
    REQUIRE( GeometryHistory_RedoCount( &history ) == 0u );
    REQUIRE( CommittedMaxX( pDocument, brushId ) == 5.0 );
    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::OK );
    REQUIRE( CommittedMaxX( pDocument, brushId ) == 2.0 );
}

TEST_CASE( "history depth evicts the oldest record",
           "[editor][geometry][transactions][history]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 1u );
    const geometry_source_id_t brushId = seeded[0].p->brush.sourceId;

    geometry_history_t history{};
    REQUIRE( GeometryHistory_Init( &history, pDocument, 2u ) == geometry_status_t::OK );
    CommitDrag( &history, seeded[0].p, 2.0 );
    CommitDrag( &history, seeded[0].p, 3.0 );
    CommitDrag( &history, seeded[0].p, 4.0 );
    REQUIRE( GeometryHistory_UndoCount( &history ) == 2u );
    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::OK );
    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::OK );
    REQUIRE( CommittedMaxX( pDocument, brushId ) == 2.0 );
    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "history refuses to act after an external commit",
           "[editor][geometry][transactions][history]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 2u );

    geometry_history_t history{};
    REQUIRE( GeometryHistory_Init( &history, pDocument, 8u ) == geometry_status_t::OK );
    CommitDrag( &history, seeded[0].p, 2.0 );

    // A commit that bypasses the history.
    geometry_transaction_t external{};
    REQUIRE( GeometryTransaction_Begin( &external, pDocument ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewRemove( &external, seeded[1].p->brush.sourceId ) ==
             geometry_status_t::OK );
    record_ref_t externalRecord{};
    REQUIRE( GeometryTransaction_TryCommit( &external, &externalRecord.p ) == geometry_status_t::OK );
    const authored_state_t afterExternal = CaptureState( pDocument );

    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::STALE_REVISION );
    REQUIRE( GeometryHistory_TryRedo( &history, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    RequireSameState( CaptureState( pDocument ), afterExternal );

    // A record committed against a base the history never saw is refused,
    // and ownership stays with the caller.
    geometry_transaction_t later{};
    REQUIRE( GeometryTransaction_Begin( &later, pDocument ) == geometry_status_t::OK );
    value_ref_t dragged{};
    REQUIRE( MakeDraggedValue( pDocument, seeded[0].p, 6.0, &dragged ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewReplace( &later, dragged.p ) == geometry_status_t::OK );
    record_ref_t laterRecord{};
    REQUIRE( GeometryTransaction_TryCommit( &later, &laterRecord.p ) == geometry_status_t::OK );
    REQUIRE( GeometryHistory_TryPush( &history, laterRecord.p ) ==
             geometry_status_t::STALE_REVISION );
    REQUIRE( GeometryHistory_UndoCount( &history ) == 1u );
    const authored_state_t afterLater = CaptureState( pDocument );

    GeometryHistory_Clear( &history );
    REQUIRE( GeometryHistory_UndoCount( &history ) == 0u );
    CommitDrag( &history, seeded[0].p, 3.0 );
    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::OK );
    RequireSameState( CaptureState( pDocument ), afterLater );
}

TEST_CASE( "a record committed directly after the history's state may be pushed",
           "[editor][geometry][transactions][history]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    std::vector<value_ref_t> seeded = SeedBoxes( pDocument, 1u );
    geometry_history_t history{};
    REQUIRE( GeometryHistory_Init( &history, pDocument, 8u ) == geometry_status_t::OK );

    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
    REQUIRE( GeometryTransaction_TryPreviewRemove( &transaction, seeded[0].p->brush.sourceId ) ==
             geometry_status_t::OK );
    record_ref_t record{};
    REQUIRE( GeometryTransaction_TryCommit( &transaction, &record.p ) == geometry_status_t::OK );
    REQUIRE( GeometryHistory_TryPush( &history, record.p ) == geometry_status_t::OK );
    ( void )record.Release();
    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( pDocument ) == 1u );
}

TEST_CASE( "undo and redo of insertion and removal round-trip identities",
           "[editor][geometry][transactions][history]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    geometry_history_t history{};
    REQUIRE( GeometryHistory_Init( &history, pDocument, 8u ) == geometry_status_t::OK );
    const authored_state_t empty = CaptureState( pDocument );

    geometry_transaction_t transaction{};
    REQUIRE( GeometryTransaction_Begin( &transaction, pDocument ) == geometry_status_t::OK );
    value_ref_t box{};
    MakeBoxValue( &transaction, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );
    REQUIRE( GeometryTransaction_TryPreviewInsert( &transaction, box.p ) == geometry_status_t::OK );
    record_ref_t record{};
    REQUIRE( GeometryTransaction_TryCommit( &transaction, &record.p ) == geometry_status_t::OK );
    REQUIRE( GeometryHistory_TryPush( &history, record.p ) == geometry_status_t::OK );
    ( void )record.Release();
    const authored_state_t withBox = CaptureState( pDocument );
    const common::usize cClaimed = GeometrySourceIdRegistry_ClaimedCount( &pDocument->registry );

    for ( int round = 0; round < 3; ++round ) {
        REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::OK );
        RequireSameState( CaptureState( pDocument ), empty );
        REQUIRE( GeometryHistory_TryRedo( &history, nullptr ) == geometry_status_t::OK );
        RequireSameState( CaptureState( pDocument ), withBox );
    }
    // Undo/redo never claims new identities.
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &pDocument->registry ) == cClaimed );
}

TEST_CASE( "history argument and lifecycle checks",
           "[editor][geometry][transactions][history]" ) {
    document_holder_t holder{};
    geometry_history_t history{};
    REQUIRE( GeometryHistory_Init( &history, &holder.document, 0u ) ==
             geometry_status_t::INVALID_ARGUMENT );
    geometry_document_t uninitialized{};
    REQUIRE( GeometryHistory_Init( &history, &uninitialized, 4u ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( GeometryHistory_TryUndo( &history, nullptr ) == geometry_status_t::NOT_INITIALIZED );
    REQUIRE( GeometryHistory_Init( &history, &holder.document, 4u ) == geometry_status_t::OK );
    REQUIRE( GeometryHistory_Init( &history, &holder.document, 4u ) ==
             geometry_status_t::ALREADY_INITIALIZED );
    REQUIRE( GeometryHistory_TryPush( &history, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    GeometryHistory_Shutdown( &history );
    GeometryHistory_Shutdown( &history );
}

} // namespace cypher::editor::geometry
