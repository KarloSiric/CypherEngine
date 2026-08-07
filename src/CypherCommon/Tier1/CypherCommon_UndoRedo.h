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

using undo_operation_id_t = u64;

using undo_apply_fn_t = error_code_t ( * )(
    binary_block_t payload,
    void *pUserData ) noexcept;

// History copies label and payload. Callbacks and pUserData must outlive the entry.
struct undo_operation_desc_t {
    undo_operation_id_t id{ 0u };
    u64 nMergeKey{ 0u };
    string_view_t label{};
    binary_block_t payload{};
    undo_apply_fn_t pfnUndo{ nullptr };
    undo_apply_fn_t pfnRedo{ nullptr };
    void *pUserData{ nullptr };
};

struct undo_history_desc_t {
    const allocator_t *pAllocator{ nullptr };
    usize nMaxOperations{ 1024u };
    usize cbMaxPayloads{ 64u * CY_MIB };
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

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_UNDOREDO_H
