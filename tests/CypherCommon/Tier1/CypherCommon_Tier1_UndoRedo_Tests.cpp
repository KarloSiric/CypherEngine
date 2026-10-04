//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_UndoRedo_Tests.cpp
//  Purpose: Tests bounded grouped undo and redo history.
//  Details: Covers copied payloads, callback ordering, transactions, merge groups,
//           redo invalidation, cancellation, labels, and operation budgets.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_UndoRedo.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct undo_test_state_t {
    i32 value{ 0 };
    i32 order[16]{};
    usize nOrder{ 0u };
};

error_code_t ApplyUndo( binary_block_t payload, void *pUserData ) noexcept
{
    auto *pState = static_cast<undo_test_state_t *>( pUserData );
    const i8 delta = static_cast<i8>( payload.pData[0] );
    pState->value -= delta;
    pState->order[pState->nOrder++] = -delta;
    return CY_ERROR_OK;
}

error_code_t ApplyRedo( binary_block_t payload, void *pUserData ) noexcept
{
    auto *pState = static_cast<undo_test_state_t *>( pUserData );
    const i8 delta = static_cast<i8>( payload.pData[0] );
    pState->value += delta;
    pState->order[pState->nOrder++] = delta;
    return CY_ERROR_OK;
}

undo_operation_desc_t Operation(
    u64 id,
    i8 *pDelta,
    undo_test_state_t *pState,
    const char *pLabel,
    u64 nMergeKey = 0u ) noexcept
{
    return {
        id,
        nMergeKey,
        StringView_FromCString( pLabel ),
        BinaryBlock_FromData( pDelta, sizeof( *pDelta ) ),
        ApplyUndo,
        ApplyRedo,
        pState
    };
}

} // namespace

TEST_CASE( "UndoRedo copies commands and invalidates redo after a new push",
           "[CypherCommon][Tier1][UndoRedo]" )
{
    undo_history_t *pHistory = UndoRedo_Create(
        { Allocator_GetSystem(), 8u, 64u } );
    REQUIRE( pHistory != nullptr );
    undo_test_state_t state{};
    i8 delta = 3;
    REQUIRE( UndoRedo_Push(
        pHistory,
        Operation( 1u, &delta, &state, "Move" ) ) );
    state.value += delta;
    delta = 99;

    REQUIRE( UndoRedo_Undo( pHistory ) == CY_ERROR_OK );
    REQUIRE( state.value == 0 );
    REQUIRE( UndoRedo_CanRedo( pHistory ) );
    REQUIRE( StringView_Equals(
        UndoRedo_RedoLabel( pHistory ),
        StringView_FromCString( "Move" ) ) );

    i8 replacement = 5;
    REQUIRE( UndoRedo_Push(
        pHistory,
        Operation( 2u, &replacement, &state, "Scale" ) ) );
    state.value += replacement;
    REQUIRE_FALSE( UndoRedo_CanRedo( pHistory ) );
    REQUIRE( UndoRedo_OperationCount( pHistory ) == 1u );
    UndoRedo_Destroy( pHistory );
}

TEST_CASE( "UndoRedo clear removes committed and redo history",
           "[CypherCommon][Tier1][UndoRedo]" )
{
    undo_history_t *pHistory = UndoRedo_Create(
        { Allocator_GetSystem(), 8u, 64u } );
    REQUIRE( pHistory != nullptr );
    undo_test_state_t state{};
    i8 delta = 2;
    REQUIRE( UndoRedo_Push(
        pHistory,
        Operation( 1u, &delta, &state, "Translate" ) ) );
    state.value = 2;
    REQUIRE( UndoRedo_Undo( pHistory ) == CY_ERROR_OK );
    REQUIRE( UndoRedo_CanRedo( pHistory ) );

    UndoRedo_Clear( pHistory );
    REQUIRE_FALSE( UndoRedo_CanUndo( pHistory ) );
    REQUIRE_FALSE( UndoRedo_CanRedo( pHistory ) );
    REQUIRE( UndoRedo_OperationCount( pHistory ) == 0u );
    REQUIRE( UndoRedo_UndoLabel( pHistory ).cchLength == 0u );
    REQUIRE( UndoRedo_RedoLabel( pHistory ).cchLength == 0u );
    UndoRedo_Destroy( pHistory );
}

TEST_CASE( "UndoRedo transactions run reverse undo and forward redo",
           "[CypherCommon][Tier1][UndoRedo]" )
{
    undo_history_t *pHistory = UndoRedo_Create(
        { Allocator_GetSystem(), 8u, 64u } );
    REQUIRE( pHistory != nullptr );
    undo_test_state_t state{};
    i8 first = 2;
    i8 second = 7;
    REQUIRE( UndoRedo_BeginTransaction(
        pHistory,
        StringView_FromCString( "Transform selection" ) ) );
    REQUIRE( UndoRedo_Push(
        pHistory,
        Operation( 1u, &first, &state, "First" ) ) );
    REQUIRE( UndoRedo_Push(
        pHistory,
        Operation( 2u, &second, &state, "Second" ) ) );
    state.value = 9;
    REQUIRE( UndoRedo_CommitTransaction( pHistory ) );
    REQUIRE( StringView_Equals(
        UndoRedo_UndoLabel( pHistory ),
        StringView_FromCString( "Transform selection" ) ) );

    REQUIRE( UndoRedo_Undo( pHistory ) == CY_ERROR_OK );
    REQUIRE( state.value == 0 );
    REQUIRE( state.order[0] == -7 );
    REQUIRE( state.order[1] == -2 );
    state.nOrder = 0u;
    REQUIRE( UndoRedo_Redo( pHistory ) == CY_ERROR_OK );
    REQUIRE( state.value == 9 );
    REQUIRE( state.order[0] == 2 );
    REQUIRE( state.order[1] == 7 );
    UndoRedo_Destroy( pHistory );
}

TEST_CASE( "UndoRedo merge keys group edits and budgets evict whole groups",
           "[CypherCommon][Tier1][UndoRedo]" )
{
    undo_history_t *pHistory = UndoRedo_Create(
        { Allocator_GetSystem(), 2u, 8u } );
    REQUIRE( pHistory != nullptr );
    undo_test_state_t state{};
    i8 one = 1;
    i8 two = 2;
    i8 three = 3;
    REQUIRE( UndoRedo_Push(
        pHistory,
        Operation( 1u, &one, &state, "Drag", 9u ) ) );
    REQUIRE( UndoRedo_Push(
        pHistory,
        Operation( 2u, &two, &state, "Drag", 9u ) ) );
    state.value = 3;
    REQUIRE( UndoRedo_Undo( pHistory ) == CY_ERROR_OK );
    REQUIRE( state.value == 0 );
    REQUIRE( UndoRedo_Redo( pHistory ) == CY_ERROR_OK );

    REQUIRE( UndoRedo_Push(
        pHistory,
        Operation( 3u, &three, &state, "New group" ) ) );
    REQUIRE( UndoRedo_OperationCount( pHistory ) == 1u );
    UndoRedo_Destroy( pHistory );
}

TEST_CASE( "UndoRedo cancellation discards pending transaction history",
           "[CypherCommon][Tier1][UndoRedo]" )
{
    undo_history_t *pHistory = UndoRedo_Create(
        { Allocator_GetSystem(), 4u, 16u } );
    REQUIRE( pHistory != nullptr );
    undo_test_state_t state{};
    i8 delta = 4;
    REQUIRE( UndoRedo_BeginTransaction(
        pHistory,
        StringView_FromCString( "Cancelled" ) ) );
    REQUIRE( UndoRedo_Push(
        pHistory,
        Operation( 1u, &delta, &state, "Operation" ) ) );
    UndoRedo_CancelTransaction( pHistory );
    REQUIRE_FALSE( UndoRedo_IsTransactionOpen( pHistory ) );
    REQUIRE( UndoRedo_OperationCount( pHistory ) == 0u );
    REQUIRE_FALSE( UndoRedo_CanUndo( pHistory ) );
    UndoRedo_Destroy( pHistory );
}

TEST_CASE( "UndoRedo state tokens return with undo and change with merges and new edits",
           "[common][tier1][undoredo]" )
{
    undo_history_t *pHistory = UndoRedo_Create( { Allocator_GetSystem(), 64u, CY_MIB } );
    REQUIRE( pHistory != nullptr );
    undo_test_state_t state{};
    i8 one = 1;
    i8 two = 2;
    const undo_state_token_t empty = UndoRedo_StateToken( pHistory );
    CHECK( empty.nGroup == 0u );

    REQUIRE( UndoRedo_Push( pHistory, Operation( 1u, &one, &state, "Move" ) ) );
    const undo_state_token_t saved = UndoRedo_StateToken( pHistory );
    REQUIRE( UndoRedo_Push( pHistory, Operation( 2u, &two, &state, "Rotate" ) ) );
    CHECK_FALSE( UndoRedo_StateTokenEquals( saved, UndoRedo_StateToken( pHistory ) ) );
    CHECK( UndoRedo_GroupCount( pHistory ) == 2u );
    CHECK( UndoRedo_AppliedGroupCount( pHistory ) == 2u );
    CHECK( StringView_Equals( UndoRedo_GroupLabelAt( pHistory, 1u ), StringView_FromCString( "Rotate" ) ) );
    CHECK( UndoRedo_GroupLabelAt( pHistory, 2u ).cchLength == 0u );

    REQUIRE( Cy_ErrorSucceeded( UndoRedo_Undo( pHistory ) ) );
    CHECK( UndoRedo_StateTokenEquals( saved, UndoRedo_StateToken( pHistory ) ) );
    CHECK( UndoRedo_AppliedGroupCount( pHistory ) == 1u );
    CHECK( UndoRedo_GroupCount( pHistory ) == 2u );
    REQUIRE( Cy_ErrorSucceeded( UndoRedo_Undo( pHistory ) ) );
    CHECK( UndoRedo_StateTokenEquals( empty, UndoRedo_StateToken( pHistory ) ) );
    REQUIRE( Cy_ErrorSucceeded( UndoRedo_Redo( pHistory ) ) );
    CHECK( UndoRedo_StateTokenEquals( saved, UndoRedo_StateToken( pHistory ) ) );

    // A merge into the newest group changes the state even though the group
    // stays the same.
    i8 three = 3;
    undo_history_t *pMerging = UndoRedo_Create( { Allocator_GetSystem(), 64u, CY_MIB } );
    REQUIRE( pMerging != nullptr );
    REQUIRE( UndoRedo_Push( pMerging, Operation( 3u, &three, &state, "Drag", 7u ) ) );
    const undo_state_token_t beforeMerge = UndoRedo_StateToken( pMerging );
    REQUIRE( UndoRedo_Push( pMerging, Operation( 4u, &three, &state, "Drag", 7u ) ) );
    const undo_state_token_t afterMerge = UndoRedo_StateToken( pMerging );
    CHECK( afterMerge.nGroup == beforeMerge.nGroup );
    CHECK_FALSE( UndoRedo_StateTokenEquals( beforeMerge, afterMerge ) );
    CHECK( UndoRedo_GroupCount( pMerging ) == 1u );
    CHECK( UndoRedo_EvictedGroupCount( pMerging ) == 0u );

    // Budgets drop the oldest groups and count them.
    undo_history_t *pSmall = UndoRedo_Create( { Allocator_GetSystem(), 2u, CY_MIB } );
    REQUIRE( pSmall != nullptr );
    for ( u64 i = 0u; i < 4u; ++i ) { REQUIRE( UndoRedo_Push( pSmall, Operation( 10u + i, &one, &state, "Step" ) ) ); }
    CHECK( UndoRedo_GroupCount( pSmall ) == 2u );
    CHECK( UndoRedo_EvictedGroupCount( pSmall ) == 2u );
    UndoRedo_Destroy( pSmall );

    // A new edit after undo is a new state, never the saved one.
    REQUIRE( Cy_ErrorSucceeded( UndoRedo_Undo( pHistory ) ) );
    REQUIRE( UndoRedo_Push( pHistory, Operation( 5u, &two, &state, "Scale" ) ) );
    CHECK_FALSE( UndoRedo_StateTokenEquals( saved, UndoRedo_StateToken( pHistory ) ) );
    UndoRedo_Destroy( pMerging );
    UndoRedo_Destroy( pHistory );
}

TEST_CASE( "UndoRedo disposes referenced ownership and charges it to its budget", "[CypherCommon][Tier1][UndoRedo][ownership]" )
{
    struct ownership_t { int disposed{ 0 }; } state;
    const auto apply = +[]( binary_block_t, void * ) noexcept -> error_code_t { return CY_ERROR_OK; };
    const auto dispose = +[]( binary_block_t, void *context ) noexcept { ++static_cast<ownership_t *>( context )->disposed; };
    auto *history = UndoRedo_Create( { Allocator_GetSystem(), 8, 64 } );
    REQUIRE( history != nullptr );
    u64 marker = 1;
    undo_operation_desc_t op{};
    op.id = 1; op.label = StringView_FromCString( "Owned snapshot" );
    op.payload = BinaryBlock_FromData( &marker, sizeof( marker ) );
    op.pfnUndo = apply; op.pfnRedo = apply; op.pfnDispose = dispose; op.pUserData = &state; op.cbOwnedBytes = 32;
    REQUIRE( UndoRedo_Push( history, op ) );
    op.id = 2;
    REQUIRE( UndoRedo_Push( history, op ) ); // Two entries need 80 bytes: first is evicted.
    CHECK( state.disposed == 1 );
    REQUIRE( UndoRedo_Undo( history ) == CY_ERROR_OK );
    CHECK( state.disposed == 1 ); // Applying changes never releases ownership.
    op.id = 3;
    REQUIRE( UndoRedo_Push( history, op ) ); // Redo invalidation releases the second.
    CHECK( state.disposed == 2 );
    op.cbOwnedBytes = 64;
    CHECK_FALSE( UndoRedo_Push( history, op ) );
    CHECK( state.disposed == 2 ); // Failed push leaves ownership with caller.
    UndoRedo_Clear( history );
    CHECK( state.disposed == 3 );
    op.cbOwnedBytes = 32;
    REQUIRE( UndoRedo_BeginTransaction( history, StringView_FromCString( "Gesture" ) ) );
    REQUIRE( UndoRedo_Push( history, op ) );
    UndoRedo_CancelTransaction( history );
    CHECK( state.disposed == 4 );
    REQUIRE( UndoRedo_Push( history, op ) );
    UndoRedo_Destroy( history );
    CHECK( state.disposed == 5 );
}
