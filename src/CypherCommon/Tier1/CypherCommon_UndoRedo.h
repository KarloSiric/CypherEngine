//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_UndoRedo.h
//  Purpose: Declares CypherCommon Tier1 UndoRedo support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_UNDOREDO_H
#define CYPHER_COMMON_TIER1_UNDOREDO_H
#pragma once

/*
================
CypherCommon Undo Redo

Undo/redo command declarations for future editor and tool workflows.
Runtime systems should not depend on editor-specific state here.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

using undo_op_id_t = u32;

struct undo_operation_t {
    undo_op_id_t id;
    const char *pName;
    void *pUserData;
};

using undo_apply_fn_t = bool_t ( * )( const undo_operation_t &operation );

struct undo_stack_t;

bool_t UndoRedo_Init( undo_stack_t *pStack, u32 maxOperations );
void UndoRedo_Shutdown( undo_stack_t *pStack );
bool_t UndoRedo_Push( undo_stack_t *pStack, const undo_operation_t &operation, undo_apply_fn_t pUndoFn, undo_apply_fn_t pRedoFn );
bool_t UndoRedo_Undo( undo_stack_t *pStack );
bool_t UndoRedo_Redo( undo_stack_t *pStack );
void UndoRedo_Clear( undo_stack_t *pStack );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_UNDOREDO_H
