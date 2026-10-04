//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Selection.h
//  Purpose: Declares a selection: a sorted set of object IDs with the
//           operations every workspace's picking and panels use.
//  Details: Objects are named by their stable IDs, never by pointers, so a
//           selection survives undo, reload, and anything else that
//           rebuilds the objects themselves. Every change bumps a revision
//           number, so a panel can tell cheaply whether what it shows is
//           current.
//
//           Selection is deliberately not part of undo (Hammer's choice):
//           undoing an edit should not also throw away what the user
//           selected since.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_SELECTION_H
#define CYPHER_EDITOR_CORE_SELECTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_Vector.h"

namespace cypher::editor
{

enum editor_select_mode_t : common::u8 {
    EDITOR_SELECT_REPLACE = 0u, // Only this object; ID 0 clears.
    EDITOR_SELECT_TOGGLE,       // Ctrl+click.
    EDITOR_SELECT_ADD,          // Shift+click.
    EDITOR_SELECT_REMOVE        // Alt+click.
};

struct editor_selection_t {
    editor_selection_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( editor_selection_t );
    ~editor_selection_t() noexcept = default;

    common::vector_t<common::u64> ids{}; // Sorted, unique, never 0.
    common::u64 revision{ 0u };          // Bumped on every change.
};

CYPHER_NODISCARD common::bool_t EditorSelection_Init( editor_selection_t *pSelection, const common::allocator_t *pAllocator ) noexcept;
void EditorSelection_Shutdown( editor_selection_t *pSelection ) noexcept;

// Each returns true when the selection changed. Allocation failure leaves
// the selection as it was and returns false.
CYPHER_NODISCARD common::bool_t EditorSelection_Apply( editor_selection_t *pSelection, common::u64 id, editor_select_mode_t mode ) noexcept;
CYPHER_NODISCARD common::bool_t EditorSelection_Set( editor_selection_t *pSelection, const common::u64 *pIds, common::usize nIds ) noexcept;
CYPHER_NODISCARD common::bool_t EditorSelection_Clear( editor_selection_t *pSelection ) noexcept;

// Drops IDs for which pfnExists returns false: after objects are deleted or
// a document is reloaded.
CYPHER_NODISCARD common::bool_t EditorSelection_Prune(
    editor_selection_t *pSelection,
    common::bool_t ( *pfnExists )( void *pContext, common::u64 id ) noexcept,
    void *pContext ) noexcept;

CYPHER_NODISCARD common::bool_t EditorSelection_Contains( const editor_selection_t *pSelection, common::u64 id ) noexcept;
CYPHER_NODISCARD common::usize EditorSelection_Count( const editor_selection_t *pSelection ) noexcept;
CYPHER_NODISCARD common::u64 EditorSelection_At( const editor_selection_t *pSelection, common::usize iIndex ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_SELECTION_H
