//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_UndoRedo.h
//  Purpose: Declares bounded command-based undo and redo history.
//  Details: History copies opaque operation payload bytes, invokes caller callbacks,
//           supports grouped transactions, and enforces operation/byte budgets.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_UNDOREDO_H
#define CYPHER_COMMON_TIER1_UNDOREDO_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

using undo_operation_id_t = u64; // Stable caller-assigned operation identifier.

using undo_apply_fn_t = error_code_t ( * )(
    binary_block_t payload,
    void *pUserData ) noexcept;

// Releases resources referenced by a copied payload when its journal entry
// is discarded. Ownership transfers only after Push succeeds. This callback
// must not re-enter the history; it never applies an edit to the live document.
using undo_dispose_fn_t = void ( * )( binary_block_t payload, void *pUserData ) noexcept;

// History copies label and payload. Callbacks and pUserData must outlive the entry.
struct undo_operation_desc_t {
    undo_operation_id_t id{ 0u };       // Nonzero identity used by tools and diagnostics.
    u64 nMergeKey{ 0u };                // Adjacent equal nonzero keys form one undo group.
    string_view_t label{};              // Human-readable label copied into the history.
    binary_block_t payload{};           // Opaque callback payload copied into the history.
    undo_apply_fn_t pfnUndo{ nullptr }; // Applies the inverse operation.
    undo_apply_fn_t pfnRedo{ nullptr }; // Reapplies the original operation.
    void *pUserData{ nullptr };          // Borrowed callback context; never owned here.
    undo_dispose_fn_t pfnDispose{ nullptr };
    usize cbOwnedBytes{ 0u };            // Referenced owned memory, also charged to the payload budget.
};

struct undo_history_desc_t {
    const allocator_t *pAllocator{ nullptr }; // Allocator for history, labels, and payloads.
    usize nMaxOperations{ 1024u };             // Hard limit before oldest groups are evicted.
    usize cbMaxPayloads{ 64u * CY_MIB };       // Aggregate payload-byte budget.
};

struct undo_history_t;

CYPHER_NODISCARD CYPHER_COMMON_API
undo_history_t *UndoRedo_Create(
    const undo_history_desc_t &desc ) noexcept;

CYPHER_COMMON_API void UndoRedo_Destroy( undo_history_t *pHistory ) noexcept;
CYPHER_COMMON_API void UndoRedo_Clear( undo_history_t *pHistory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UndoRedo_BeginTransaction(
    undo_history_t *pHistory,
    string_view_t label ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UndoRedo_CommitTransaction( undo_history_t *pHistory ) noexcept;

// Discards operations recorded since BeginTransaction without invoking callbacks.
// The caller remains responsible for restoring any live edited state.
CYPHER_COMMON_API void UndoRedo_CancelTransaction(
    undo_history_t *pHistory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UndoRedo_Push(
    undo_history_t *pHistory,
    const undo_operation_desc_t &operation ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UndoRedo_CanUndo( const undo_history_t *pHistory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UndoRedo_CanRedo( const undo_history_t *pHistory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
error_code_t UndoRedo_Undo( undo_history_t *pHistory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
error_code_t UndoRedo_Redo( undo_history_t *pHistory ) noexcept;

// Returns the display label for the next complete undo or redo group.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t UndoRedo_UndoLabel( const undo_history_t *pHistory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t UndoRedo_RedoLabel( const undo_history_t *pHistory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize UndoRedo_OperationCount( const undo_history_t *pHistory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UndoRedo_IsTransactionOpen( const undo_history_t *pHistory ) noexcept;

// Identifies the applied state: the newest applied group and how many of its
// operations are applied. Undoing back to a state reproduces its token; a
// new edit, or a merge into the newest group, produces a different one. Tools
// compare tokens to know whether a document still matches what was saved.
struct undo_state_token_t {
    u64 nGroup{ 0u };    // 0 when nothing is applied.
    usize nApplied{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
undo_state_token_t UndoRedo_StateToken( const undo_history_t *pHistory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UndoRedo_StateTokenEquals( undo_state_token_t a, undo_state_token_t b ) noexcept;

// Groups the budgets have dropped since creation; never reset. Once the
// oldest groups are gone, the empty state ("nothing applied") can no longer
// be reached by undoing, which matters to anyone who marked it saved.
CYPHER_NODISCARD CYPHER_COMMON_API
u64 UndoRedo_EvictedGroupCount( const undo_history_t *pHistory ) noexcept;

// Groups (undo steps) in the history, applied ones first, for history views.
CYPHER_NODISCARD CYPHER_COMMON_API
usize UndoRedo_GroupCount( const undo_history_t *pHistory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize UndoRedo_AppliedGroupCount( const undo_history_t *pHistory ) noexcept;

// Display label of a group, oldest first; empty when out of range.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t UndoRedo_GroupLabelAt( const undo_history_t *pHistory, usize iGroup ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_UNDOREDO_H
