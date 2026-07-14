//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Stack.h
//  Purpose: Declares CypherCommon Tier1 Stack support.
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

#ifndef CYPHER_COMMON_TIER1_STACK_H
#define CYPHER_COMMON_TIER1_STACK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Stack

LIFO container declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
struct stack_t;

template <typename type_t>
bool_t Stack_Push( stack_t<type_t> *pStack, const type_t &value );

template <typename type_t>
bool_t Stack_Pop( stack_t<type_t> *pStack, type_t *pOutValue );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STACK_H
