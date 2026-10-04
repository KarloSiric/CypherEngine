//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_History.h
//  Purpose: Declares a document's undo history for the editor: the engine's
//           undo stack (CypherCommon_UndoRedo) plus what editor windows
//           need around it - a saved point, change notification, and
//           stepping to any point in the list.
//  Details: One history per open document. Edits record operations with
//           undo and redo callbacks; a tool that makes several changes in
//           one gesture wraps them in Begin/Commit so they undo as one
//           step. Whether the document differs from its file is decided by
//           comparing the current state with the state marked clean at the
//           last save, so undoing back to the saved state clears the
//           modified mark, as users expect.
//
//           Listeners are told after every change - an edit, undo, redo,
//           clear, or save mark - so the history panel, Undo/Redo menu
//           labels, and the window's modified mark follow without polling.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_HISTORY_H
#define CYPHER_EDITOR_CORE_HISTORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_UndoRedo.h"

namespace cypher::editor
{

inline constexpr common::usize EDITOR_HISTORY_MAX_LISTENERS = 16u;
inline constexpr common::usize EDITOR_HISTORY_DEFAULT_OPERATIONS = 4096u;
inline constexpr common::usize EDITOR_HISTORY_DEFAULT_PAYLOAD_BYTES = 256u * common::CY_MIB;

enum class editor_history_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUT_OF_MEMORY,
    NOTHING_TO_DO,     // No step to undo or redo.
    TRANSACTION_OPEN,  // Undo, redo, and stepping wait until the gesture commits.
    APPLY_FAILED       // An undo or redo callback reported failure; see the log.
};

using editor_history_listener_fn = void ( * )( void *pContext ) noexcept;

struct editor_history_listener_t {
    editor_history_listener_fn pfnChanged{ nullptr };
    void *pContext{ nullptr };
};

struct editor_history_t {
    editor_history_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( editor_history_t );
    ~editor_history_t() noexcept;

    common::undo_history_t *pUndo{ nullptr };
    common::undo_state_token_t cleanToken{};           // State at the last save.
    common::u64 nEvictedAtClean{ 0u };                 // Evictions when it was marked; see IsClean.
    common::bool_t bCleanReachable{ common::CY_TRUE }; // False once no undo can return to it.
    editor_history_listener_t listeners[EDITOR_HISTORY_MAX_LISTENERS]{};
    common::usize nListeners{ 0u };
};

// Starts empty and clean.
CYPHER_NODISCARD editor_history_status_t EditorHistory_Init(
    editor_history_t *pHistory,
    const common::allocator_t *pAllocator,
    common::usize nMaxOperations = EDITOR_HISTORY_DEFAULT_OPERATIONS,
    common::usize cbMaxPayloads = EDITOR_HISTORY_DEFAULT_PAYLOAD_BYTES ) noexcept;

void EditorHistory_Shutdown( editor_history_t *pHistory ) noexcept;

CYPHER_NODISCARD common::bool_t EditorHistory_IsInitialized( const editor_history_t *pHistory ) noexcept;

// Groups the following pushes into one undo step labelled `label`.
CYPHER_NODISCARD editor_history_status_t EditorHistory_Begin( editor_history_t *pHistory, common::string_view_t label ) noexcept;
CYPHER_NODISCARD editor_history_status_t EditorHistory_Commit( editor_history_t *pHistory ) noexcept;

// Drops the open step without running callbacks; the caller restores any
// live state it changed.
void EditorHistory_Cancel( editor_history_t *pHistory ) noexcept;

// Records an edit that has already been applied. Outside a transaction it
// is its own step (or merges with the previous one on an equal merge key).
// When recording before atomic document publication, pass notify=false and
// call NotifyChanged only after the live document is ready for observers.
CYPHER_NODISCARD editor_history_status_t EditorHistory_Push(
    editor_history_t *pHistory,
    const common::undo_operation_desc_t &operation,
    bool notify = true ) noexcept;

void EditorHistory_NotifyChanged( editor_history_t *pHistory ) noexcept;

CYPHER_NODISCARD editor_history_status_t EditorHistory_Undo( editor_history_t *pHistory ) noexcept;
CYPHER_NODISCARD editor_history_status_t EditorHistory_Redo( editor_history_t *pHistory ) noexcept;

// Undoes or redoes until nAppliedSteps steps are applied: clicking a row in
// the history panel.
CYPHER_NODISCARD editor_history_status_t EditorHistory_StepTo( editor_history_t *pHistory, common::usize nAppliedSteps ) noexcept;

// Forgets every step. The document is unchanged, so it stays clean if it
// was.
void EditorHistory_Clear( editor_history_t *pHistory ) noexcept;

CYPHER_NODISCARD common::bool_t EditorHistory_CanUndo( const editor_history_t *pHistory ) noexcept;
CYPHER_NODISCARD common::bool_t EditorHistory_CanRedo( const editor_history_t *pHistory ) noexcept;
CYPHER_NODISCARD common::string_view_t EditorHistory_UndoLabel( const editor_history_t *pHistory ) noexcept;
CYPHER_NODISCARD common::string_view_t EditorHistory_RedoLabel( const editor_history_t *pHistory ) noexcept;
CYPHER_NODISCARD common::usize EditorHistory_StepCount( const editor_history_t *pHistory ) noexcept;
CYPHER_NODISCARD common::usize EditorHistory_AppliedStepCount( const editor_history_t *pHistory ) noexcept;
CYPHER_NODISCARD common::string_view_t EditorHistory_StepLabel( const editor_history_t *pHistory, common::usize iStep ) noexcept;
CYPHER_NODISCARD common::bool_t EditorHistory_IsTransactionOpen( const editor_history_t *pHistory ) noexcept;

// The document was saved: the current state is the clean one.
void EditorHistory_MarkClean( editor_history_t *pHistory ) noexcept;

// The document matches its file.
CYPHER_NODISCARD common::bool_t EditorHistory_IsClean( const editor_history_t *pHistory ) noexcept;

CYPHER_NODISCARD common::bool_t EditorHistory_AddListener(
    editor_history_t *pHistory,
    editor_history_listener_fn pfnChanged,
    void *pContext ) noexcept;

void EditorHistory_RemoveListener(
    editor_history_t *pHistory,
    editor_history_listener_fn pfnChanged,
    void *pContext ) noexcept;

CYPHER_NODISCARD const char *EditorHistory_StatusName( editor_history_status_t status ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_HISTORY_H
