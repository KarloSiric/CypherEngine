//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_History_Tests.cpp
//  Purpose: Contract tests for the editor document history: steps, gestures,
//           stepping through the list, the saved point, and notification.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_History.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

// A document that is one integer; each edit adds a delta.
struct counter_t {
    i32 value{ 0 };
    bool bFailUndo{ false };
};

error_code_t UndoAdd( binary_block_t payload, void *pUserData ) noexcept
{
    auto *pCounter = static_cast<counter_t *>( pUserData );
    if ( pCounter->bFailUndo ) { return Cy_ErrorMake( common_error_t::ERR_INVALID_STATE ); }
    pCounter->value -= static_cast<i8>( payload.pData[0] );
    return CY_ERROR_OK;
}

error_code_t RedoAdd( binary_block_t payload, void *pUserData ) noexcept
{
    static_cast<counter_t *>( pUserData )->value += static_cast<i8>( payload.pData[0] );
    return CY_ERROR_OK;
}

// Applies the edit, then records it, as tools do.
editor_history_status_t Add( editor_history_t *pHistory, counter_t *pCounter, i8 delta, const char *pLabel, u64 mergeKey = 0u )
{
    pCounter->value += delta;
    undo_operation_desc_t op{};
    op.id = 1u;
    op.nMergeKey = mergeKey;
    op.label = StringView_FromCString( pLabel );
    op.payload = BinaryBlock_FromData( &delta, sizeof( delta ) );
    op.pfnUndo = UndoAdd;
    op.pfnRedo = RedoAdd;
    op.pUserData = pCounter;
    return EditorHistory_Push( pHistory, op );
}

std::string Str( string_view_t view )
{
    return std::string( view.pData != nullptr ? view.pData : "", view.cchLength );
}

void Count( void *pContext ) noexcept
{
    ++*static_cast<int *>( pContext );
}

} // namespace

TEST_CASE( "History undoes and redoes steps with their labels", "[editor][core][history]" )
{
    editor_history_t history{};
    REQUIRE( EditorHistory_Init( &history, Allocator_GetSystem() ) == editor_history_status_t::OK );
    counter_t counter{};
    CHECK( EditorHistory_Undo( &history ) == editor_history_status_t::NOTHING_TO_DO );

    REQUIRE( Add( &history, &counter, 5, "Move" ) == editor_history_status_t::OK );
    REQUIRE( Add( &history, &counter, 2, "Rotate" ) == editor_history_status_t::OK );
    CHECK( counter.value == 7 );
    CHECK( Str( EditorHistory_UndoLabel( &history ) ) == "Rotate" );
    CHECK( EditorHistory_StepCount( &history ) == 2u );

    REQUIRE( EditorHistory_Undo( &history ) == editor_history_status_t::OK );
    CHECK( counter.value == 5 );
    CHECK( Str( EditorHistory_RedoLabel( &history ) ) == "Rotate" );
    REQUIRE( EditorHistory_Redo( &history ) == editor_history_status_t::OK );
    CHECK( counter.value == 7 );
    CHECK( EditorHistory_Redo( &history ) == editor_history_status_t::NOTHING_TO_DO );
}

TEST_CASE( "A gesture undoes as one step and blocks undo while open", "[editor][core][history]" )
{
    editor_history_t history{};
    REQUIRE( EditorHistory_Init( &history, Allocator_GetSystem() ) == editor_history_status_t::OK );
    counter_t counter{};
    int nNotified = 0;
    REQUIRE( EditorHistory_AddListener( &history, &Count, &nNotified ) );

    REQUIRE( EditorHistory_Begin( &history, StringView_FromCString( "Drag Vertices" ) ) == editor_history_status_t::OK );
    REQUIRE( Add( &history, &counter, 1, "a" ) == editor_history_status_t::OK );
    REQUIRE( Add( &history, &counter, 1, "b" ) == editor_history_status_t::OK );
    CHECK( nNotified == 1 ); // Begin disables navigation; pushes stay quiet until commit.
    CHECK( EditorHistory_Undo( &history ) == editor_history_status_t::TRANSACTION_OPEN );
    CHECK( EditorHistory_Begin( &history, StringView_FromCString( "nested" ) ) == editor_history_status_t::TRANSACTION_OPEN );
    REQUIRE( EditorHistory_Commit( &history ) == editor_history_status_t::OK );
    CHECK( nNotified == 2 );
    CHECK( EditorHistory_StepCount( &history ) == 1u );
    CHECK( Str( EditorHistory_StepLabel( &history, 0u ) ) == "Drag Vertices" );
    REQUIRE( EditorHistory_Undo( &history ) == editor_history_status_t::OK );
    CHECK( counter.value == 0 );

    // A cancelled gesture leaves nothing behind.
    REQUIRE( EditorHistory_Begin( &history, StringView_FromCString( "Cancelled" ) ) == editor_history_status_t::OK );
    REQUIRE( Add( &history, &counter, 3, "c" ) == editor_history_status_t::OK );
    EditorHistory_Cancel( &history );
    counter.value -= 3; // The tool restores its own live state.
    CHECK_FALSE( EditorHistory_IsTransactionOpen( &history ) );
    CHECK( EditorHistory_StepCount( &history ) == 0u );
    // Starting an edit drops the redo steps, even when the edit is then
    // cancelled: the engine stack discards them at the first recorded change.
    CHECK_FALSE( EditorHistory_CanRedo( &history ) );
    EditorHistory_RemoveListener( &history, &Count, &nNotified );
}

TEST_CASE( "History listeners observe transaction begin and cancel without a committed step", "[editor][core][history]" )
{
    editor_history_t history{};
    REQUIRE( EditorHistory_Init( &history, Allocator_GetSystem() ) == editor_history_status_t::OK );
    struct observer_t {
        editor_history_t *pHistory{};
        int nNotifications{};
        bool bOpen{};
    } observer{ &history };
    const auto changed = []( void *pContext ) noexcept {
        auto &state = *static_cast<observer_t *>( pContext );
        ++state.nNotifications;
        state.bOpen = EditorHistory_IsTransactionOpen( state.pHistory );
    };
    REQUIRE( EditorHistory_AddListener( &history, changed, &observer ) );
    REQUIRE( EditorHistory_Begin( &history, StringView_FromCString( "Preview" ) ) == editor_history_status_t::OK );
    CHECK( observer.nNotifications == 1 );
    CHECK( observer.bOpen );
    CHECK( EditorHistory_Begin( &history, StringView_FromCString( "Nested" ) ) == editor_history_status_t::TRANSACTION_OPEN );
    CHECK( observer.nNotifications == 1 );
    EditorHistory_Cancel( &history );
    CHECK( observer.nNotifications == 2 );
    CHECK_FALSE( observer.bOpen );
    CHECK( EditorHistory_StepCount( &history ) == 0u );
    EditorHistory_Cancel( &history ); // A no-op must not emit another change.
    CHECK( observer.nNotifications == 2 );
    EditorHistory_RemoveListener( &history, changed, &observer );
}

TEST_CASE( "Stepping to a point in the list undoes or redoes as needed", "[editor][core][history]" )
{
    editor_history_t history{};
    REQUIRE( EditorHistory_Init( &history, Allocator_GetSystem() ) == editor_history_status_t::OK );
    counter_t counter{};
    int nNotified = 0;
    for ( i8 i = 1; i <= 4; ++i ) { REQUIRE( Add( &history, &counter, i, "Step" ) == editor_history_status_t::OK ); }
    REQUIRE( EditorHistory_AddListener( &history, &Count, &nNotified ) );
    CHECK( counter.value == 10 );
    REQUIRE( EditorHistory_StepTo( &history, 1u ) == editor_history_status_t::OK );
    CHECK( counter.value == 1 );
    CHECK( EditorHistory_AppliedStepCount( &history ) == 1u );
    CHECK( nNotified == 1 ); // One notification for the whole walk.
    REQUIRE( EditorHistory_StepTo( &history, 3u ) == editor_history_status_t::OK );
    CHECK( counter.value == 6 );
    CHECK( EditorHistory_StepTo( &history, 9u ) == editor_history_status_t::INVALID_ARGUMENT );
    EditorHistory_RemoveListener( &history, &Count, &nNotified );
}

TEST_CASE( "The saved point follows undo and redo and survives clearing", "[editor][core][history]" )
{
    editor_history_t history{};
    REQUIRE( EditorHistory_Init( &history, Allocator_GetSystem() ) == editor_history_status_t::OK );
    counter_t counter{};
    CHECK( EditorHistory_IsClean( &history ) );

    REQUIRE( Add( &history, &counter, 1, "One" ) == editor_history_status_t::OK );
    CHECK_FALSE( EditorHistory_IsClean( &history ) );
    EditorHistory_MarkClean( &history ); // Saved.
    CHECK( EditorHistory_IsClean( &history ) );
    REQUIRE( Add( &history, &counter, 1, "Two" ) == editor_history_status_t::OK );
    CHECK_FALSE( EditorHistory_IsClean( &history ) );
    REQUIRE( EditorHistory_Undo( &history ) == editor_history_status_t::OK );
    CHECK( EditorHistory_IsClean( &history ) ); // Back at the saved state.
    REQUIRE( EditorHistory_Undo( &history ) == editor_history_status_t::OK );
    CHECK_FALSE( EditorHistory_IsClean( &history ) );
    REQUIRE( EditorHistory_Redo( &history ) == editor_history_status_t::OK );
    CHECK( EditorHistory_IsClean( &history ) );

    // A merged drag after the save is a change, even in the same step.
    REQUIRE( Add( &history, &counter, 1, "Drag", 9u ) == editor_history_status_t::OK );
    EditorHistory_MarkClean( &history );
    REQUIRE( Add( &history, &counter, 1, "Drag", 9u ) == editor_history_status_t::OK );
    CHECK_FALSE( EditorHistory_IsClean( &history ) );

    // Clearing a modified document keeps it modified; a clean one stays clean.
    EditorHistory_Clear( &history );
    CHECK_FALSE( EditorHistory_IsClean( &history ) );
    EditorHistory_MarkClean( &history );
    EditorHistory_Clear( &history );
    CHECK( EditorHistory_IsClean( &history ) );
}

TEST_CASE( "A saved empty state is lost once steps after it are evicted", "[editor][core][history]" )
{
    editor_history_t history{};
    REQUIRE( EditorHistory_Init( &history, Allocator_GetSystem(), 2u ) == editor_history_status_t::OK );
    counter_t counter{};
    // Saved at "nothing applied"; three edits with room for two.
    for ( int i = 0; i < 3; ++i ) { REQUIRE( Add( &history, &counter, 1, "Step" ) == editor_history_status_t::OK ); }
    REQUIRE( EditorHistory_StepTo( &history, 0u ) == editor_history_status_t::OK );
    CHECK( counter.value == 1 ); // The evicted edit can no longer be undone...
    CHECK_FALSE( EditorHistory_IsClean( &history ) ); // ...so this is not the saved state.
}

TEST_CASE( "A failing undo is reported", "[editor][core][history]" )
{
    editor_history_t history{};
    REQUIRE( EditorHistory_Init( &history, Allocator_GetSystem() ) == editor_history_status_t::OK );
    counter_t counter{};
    REQUIRE( Add( &history, &counter, 1, "One" ) == editor_history_status_t::OK );
    counter.bFailUndo = true;
    CHECK( EditorHistory_Undo( &history ) == editor_history_status_t::APPLY_FAILED );
    CHECK( std::string( EditorHistory_StatusName( editor_history_status_t::APPLY_FAILED ) ) == "APPLY_FAILED" );
}

TEST_CASE( "History notifications can be deferred until publication and skip removed listeners", "[editor][core][history][lifetime]" )
{
    editor_history_t history{};
    REQUIRE( EditorHistory_Init( &history, Allocator_GetSystem() ) == editor_history_status_t::OK );
    int removed = 0;
    struct removal_t { editor_history_t *history; int *removed; int calls{ 0 }; } removal{ &history, &removed };
    const auto unsubscribe = +[]( void *context ) noexcept {
        auto &state = *static_cast<removal_t *>( context );
        ++state.calls;
        EditorHistory_RemoveListener( state.history, &Count, state.removed );
    };
    REQUIRE( EditorHistory_AddListener( &history, unsubscribe, &removal ) );
    REQUIRE( EditorHistory_AddListener( &history, &Count, &removed ) );
    counter_t counter{};
    i8 delta = 3;
    undo_operation_desc_t op{};
    op.id = 1;
    op.payload = BinaryBlock_FromData( &delta, sizeof( delta ) );
    op.pfnUndo = &UndoAdd;
    op.pfnRedo = &RedoAdd;
    op.pUserData = &counter;
    REQUIRE( EditorHistory_Push( &history, op, false ) == editor_history_status_t::OK );
    CHECK( removal.calls == 0 );
    counter.value = 3; // The owning document publishes its prepared edit here.
    EditorHistory_NotifyChanged( &history );
    CHECK( removal.calls == 1 );
    CHECK( removed == 0 );
    REQUIRE( EditorHistory_Undo( &history ) == editor_history_status_t::OK );
    CHECK( counter.value == 0 );
}
