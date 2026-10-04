//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_History.cpp
//  Purpose: Implements the editor document history over CypherCommon's undo
//           stack.
//  Details: The clean check compares state tokens. Group IDs are never
//           reused, so a saved state whose group was evicted can never
//           match again - except the empty state (nothing applied), which
//           every history can return to until its oldest steps are evicted;
//           the eviction count taken at the save covers that case.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_History.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <algorithm>

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

void Notify( editor_history_t *pHistory ) noexcept
{
    // A listener may unsubscribe while being told (a panel closing).
    editor_history_listener_t snapshot[EDITOR_HISTORY_MAX_LISTENERS];
    const usize nListeners = pHistory->nListeners;
    std::copy( pHistory->listeners, pHistory->listeners + nListeners, snapshot );
    for ( usize i = 0u; i < nListeners; ++i ) {
        const auto &listener = snapshot[i];
        const bool subscribed = std::any_of( pHistory->listeners, pHistory->listeners + pHistory->nListeners,
            [&]( const editor_history_listener_t &live ) { return live.pfnChanged == listener.pfnChanged && live.pContext == listener.pContext; } );
        if ( subscribed ) { listener.pfnChanged( listener.pContext ); }
    }
}

CYPHER_NODISCARD editor_history_status_t ApplyStatus( error_code_t error ) noexcept
{
    if ( Cy_ErrorSucceeded( error ) ) { return editor_history_status_t::OK; }
    CY_LOG_WRITE( Error, Editor, "An undo or redo step failed; the document may be partly changed" );
    return editor_history_status_t::APPLY_FAILED;
}

} // namespace

editor_history_t::~editor_history_t() noexcept
{
    EditorHistory_Shutdown( this );
}

editor_history_status_t EditorHistory_Init( editor_history_t *pHistory, const allocator_t *pAllocator, usize nMaxOperations, usize cbMaxPayloads ) noexcept
{
    if ( pHistory == nullptr || pAllocator == nullptr || nMaxOperations == 0u || pHistory->pUndo != nullptr ) {
        return editor_history_status_t::INVALID_ARGUMENT;
    }
    undo_history_desc_t desc{};
    desc.pAllocator = pAllocator;
    desc.nMaxOperations = nMaxOperations;
    desc.cbMaxPayloads = cbMaxPayloads;
    pHistory->pUndo = UndoRedo_Create( desc );
    if ( pHistory->pUndo == nullptr ) { return editor_history_status_t::OUT_OF_MEMORY; }
    pHistory->cleanToken = UndoRedo_StateToken( pHistory->pUndo );
    pHistory->nEvictedAtClean = 0u;
    pHistory->bCleanReachable = CY_TRUE;
    return editor_history_status_t::OK;
}

void EditorHistory_Shutdown( editor_history_t *pHistory ) noexcept
{
    if ( pHistory == nullptr ) { return; }
    UndoRedo_Destroy( pHistory->pUndo );
    pHistory->pUndo = nullptr;
    pHistory->nListeners = 0u;
}

bool_t EditorHistory_IsInitialized( const editor_history_t *pHistory ) noexcept
{
    return pHistory != nullptr && pHistory->pUndo != nullptr;
}

editor_history_status_t EditorHistory_Begin( editor_history_t *pHistory, string_view_t label ) noexcept
{
    if ( !EditorHistory_IsInitialized( pHistory ) ) { return editor_history_status_t::INVALID_ARGUMENT; }
    if ( UndoRedo_IsTransactionOpen( pHistory->pUndo ) ) { return editor_history_status_t::TRANSACTION_OPEN; }
    if ( !UndoRedo_BeginTransaction( pHistory->pUndo, label ) ) { return editor_history_status_t::OUT_OF_MEMORY; }
    Notify( pHistory ); // Disable history navigation while the gesture is open.
    return editor_history_status_t::OK;
}

editor_history_status_t EditorHistory_Commit( editor_history_t *pHistory ) noexcept
{
    if ( !EditorHistory_IsInitialized( pHistory ) || !UndoRedo_IsTransactionOpen( pHistory->pUndo ) ) {
        return editor_history_status_t::INVALID_ARGUMENT;
    }
    if ( !UndoRedo_CommitTransaction( pHistory->pUndo ) ) { return editor_history_status_t::OUT_OF_MEMORY; }
    Notify( pHistory );
    return editor_history_status_t::OK;
}

void EditorHistory_Cancel( editor_history_t *pHistory ) noexcept
{
    if ( !EditorHistory_IsInitialized( pHistory ) || !UndoRedo_IsTransactionOpen( pHistory->pUndo ) ) { return; }
    UndoRedo_CancelTransaction( pHistory->pUndo );
    Notify( pHistory ); // Restore navigation even when no edit was committed.
}

editor_history_status_t EditorHistory_Push( editor_history_t *pHistory, const undo_operation_desc_t &operation, bool notify ) noexcept
{
    if ( !EditorHistory_IsInitialized( pHistory ) || operation.pfnUndo == nullptr || operation.pfnRedo == nullptr ) {
        return editor_history_status_t::INVALID_ARGUMENT;
    }
    if ( !UndoRedo_Push( pHistory->pUndo, operation ) ) {
        // The caller retains payload ownership and decides whether to publish
        // or roll back its edit when the journal cannot accept the step.
        CY_LOG_WRITE( Warning, Editor, "An edit could not be recorded for undo" );
        return editor_history_status_t::OUT_OF_MEMORY;
    }
    // Inside a gesture the step is not finished; listeners hear on Commit.
    if ( notify && !UndoRedo_IsTransactionOpen( pHistory->pUndo ) ) { Notify( pHistory ); }
    return editor_history_status_t::OK;
}

void EditorHistory_NotifyChanged( editor_history_t *pHistory ) noexcept
{
    if ( EditorHistory_IsInitialized( pHistory ) && !EditorHistory_IsTransactionOpen( pHistory ) ) { Notify( pHistory ); }
}

editor_history_status_t EditorHistory_Undo( editor_history_t *pHistory ) noexcept
{
    if ( !EditorHistory_IsInitialized( pHistory ) ) { return editor_history_status_t::INVALID_ARGUMENT; }
    if ( UndoRedo_IsTransactionOpen( pHistory->pUndo ) ) { return editor_history_status_t::TRANSACTION_OPEN; }
    if ( !UndoRedo_CanUndo( pHistory->pUndo ) ) { return editor_history_status_t::NOTHING_TO_DO; }
    const editor_history_status_t status = ApplyStatus( UndoRedo_Undo( pHistory->pUndo ) );
    Notify( pHistory );
    return status;
}

editor_history_status_t EditorHistory_Redo( editor_history_t *pHistory ) noexcept
{
    if ( !EditorHistory_IsInitialized( pHistory ) ) { return editor_history_status_t::INVALID_ARGUMENT; }
    if ( UndoRedo_IsTransactionOpen( pHistory->pUndo ) ) { return editor_history_status_t::TRANSACTION_OPEN; }
    if ( !UndoRedo_CanRedo( pHistory->pUndo ) ) { return editor_history_status_t::NOTHING_TO_DO; }
    const editor_history_status_t status = ApplyStatus( UndoRedo_Redo( pHistory->pUndo ) );
    Notify( pHistory );
    return status;
}

editor_history_status_t EditorHistory_StepTo( editor_history_t *pHistory, usize nAppliedSteps ) noexcept
{
    if ( !EditorHistory_IsInitialized( pHistory ) || nAppliedSteps > UndoRedo_GroupCount( pHistory->pUndo ) ) {
        return editor_history_status_t::INVALID_ARGUMENT;
    }
    if ( UndoRedo_IsTransactionOpen( pHistory->pUndo ) ) { return editor_history_status_t::TRANSACTION_OPEN; }
    editor_history_status_t status = editor_history_status_t::OK;
    // One notification for the whole walk: a panel should redraw once.
    while ( status == editor_history_status_t::OK && UndoRedo_AppliedGroupCount( pHistory->pUndo ) > nAppliedSteps ) {
        status = ApplyStatus( UndoRedo_Undo( pHistory->pUndo ) );
    }
    while ( status == editor_history_status_t::OK && UndoRedo_AppliedGroupCount( pHistory->pUndo ) < nAppliedSteps ) {
        status = ApplyStatus( UndoRedo_Redo( pHistory->pUndo ) );
    }
    Notify( pHistory );
    return status;
}

void EditorHistory_Clear( editor_history_t *pHistory ) noexcept
{
    if ( !EditorHistory_IsInitialized( pHistory ) ) { return; }
    const bool_t bWasClean = EditorHistory_IsClean( pHistory );
    UndoRedo_Clear( pHistory->pUndo );
    // After clearing, the state token is the empty one whatever the
    // document holds, so only a clean document may keep a reachable mark.
    pHistory->cleanToken = UndoRedo_StateToken( pHistory->pUndo );
    pHistory->nEvictedAtClean = UndoRedo_EvictedGroupCount( pHistory->pUndo );
    pHistory->bCleanReachable = bWasClean;
    Notify( pHistory );
}

bool_t EditorHistory_CanUndo( const editor_history_t *pHistory ) noexcept
{
    return EditorHistory_IsInitialized( pHistory ) && UndoRedo_CanUndo( pHistory->pUndo );
}

bool_t EditorHistory_CanRedo( const editor_history_t *pHistory ) noexcept
{
    return EditorHistory_IsInitialized( pHistory ) && UndoRedo_CanRedo( pHistory->pUndo );
}

string_view_t EditorHistory_UndoLabel( const editor_history_t *pHistory ) noexcept
{
    return EditorHistory_IsInitialized( pHistory ) ? UndoRedo_UndoLabel( pHistory->pUndo ) : string_view_t{};
}

string_view_t EditorHistory_RedoLabel( const editor_history_t *pHistory ) noexcept
{
    return EditorHistory_IsInitialized( pHistory ) ? UndoRedo_RedoLabel( pHistory->pUndo ) : string_view_t{};
}

usize EditorHistory_StepCount( const editor_history_t *pHistory ) noexcept
{
    return EditorHistory_IsInitialized( pHistory ) ? UndoRedo_GroupCount( pHistory->pUndo ) : 0u;
}

usize EditorHistory_AppliedStepCount( const editor_history_t *pHistory ) noexcept
{
    return EditorHistory_IsInitialized( pHistory ) ? UndoRedo_AppliedGroupCount( pHistory->pUndo ) : 0u;
}

string_view_t EditorHistory_StepLabel( const editor_history_t *pHistory, usize iStep ) noexcept
{
    return EditorHistory_IsInitialized( pHistory ) ? UndoRedo_GroupLabelAt( pHistory->pUndo, iStep ) : string_view_t{};
}

bool_t EditorHistory_IsTransactionOpen( const editor_history_t *pHistory ) noexcept
{
    return EditorHistory_IsInitialized( pHistory ) && UndoRedo_IsTransactionOpen( pHistory->pUndo );
}

void EditorHistory_MarkClean( editor_history_t *pHistory ) noexcept
{
    if ( !EditorHistory_IsInitialized( pHistory ) ) { return; }
    pHistory->cleanToken = UndoRedo_StateToken( pHistory->pUndo );
    pHistory->nEvictedAtClean = UndoRedo_EvictedGroupCount( pHistory->pUndo );
    pHistory->bCleanReachable = CY_TRUE;
    Notify( pHistory );
}

bool_t EditorHistory_IsClean( const editor_history_t *pHistory ) noexcept
{
    if ( !EditorHistory_IsInitialized( pHistory ) || !pHistory->bCleanReachable ) { return CY_FALSE; }
    if ( !UndoRedo_StateTokenEquals( pHistory->cleanToken, UndoRedo_StateToken( pHistory->pUndo ) ) ) { return CY_FALSE; }
    // The empty state stops being the saved one once steps applied after
    // the save have been evicted: undoing everything left no longer undoes
    // those.
    return pHistory->cleanToken.nGroup != 0u || UndoRedo_EvictedGroupCount( pHistory->pUndo ) == pHistory->nEvictedAtClean;
}

bool_t EditorHistory_AddListener( editor_history_t *pHistory, editor_history_listener_fn pfnChanged, void *pContext ) noexcept
{
    CY_ASSERT( pHistory != nullptr && pfnChanged != nullptr );
    if ( pHistory == nullptr || pfnChanged == nullptr || pHistory->nListeners >= EDITOR_HISTORY_MAX_LISTENERS ) {
        CY_LOG_WRITE( Error, Editor, "History listener table is full" );
        return CY_FALSE;
    }
    pHistory->listeners[pHistory->nListeners++] = editor_history_listener_t{ pfnChanged, pContext };
    return CY_TRUE;
}

void EditorHistory_RemoveListener( editor_history_t *pHistory, editor_history_listener_fn pfnChanged, void *pContext ) noexcept
{
    if ( pHistory == nullptr ) { return; }
    for ( usize i = 0u; i < pHistory->nListeners; ++i ) {
        if ( pHistory->listeners[i].pfnChanged != pfnChanged || pHistory->listeners[i].pContext != pContext ) { continue; }
        for ( usize j = i + 1u; j < pHistory->nListeners; ++j ) { pHistory->listeners[j - 1u] = pHistory->listeners[j]; }
        --pHistory->nListeners;
        return;
    }
}

const char *EditorHistory_StatusName( editor_history_status_t status ) noexcept
{
    switch ( status ) {
        case editor_history_status_t::OK: return "OK";
        case editor_history_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case editor_history_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case editor_history_status_t::NOTHING_TO_DO: return "NOTHING_TO_DO";
        case editor_history_status_t::TRANSACTION_OPEN: return "TRANSACTION_OPEN";
        case editor_history_status_t::APPLY_FAILED: return "APPLY_FAILED";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor
