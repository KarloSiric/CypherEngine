//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Stack.h
//  Purpose: Declares a last-in-first-out adapter over vector storage.
//  Details: Stack owns elements through vector_t and preserves the same explicit
//           allocator and failure behavior.
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

#include "CypherCommon_Vector.h"

namespace cypher::common
{

template <typename type_t>
struct stack_t {
    vector_t<type_t> storage{};
};

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_Init(
    stack_t<type_t> *pStack,
    const allocator_t *pAllocator,
    usize nInitialCapacity = 0u ) noexcept;

template <typename type_t>
void Stack_Shutdown( stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
void Stack_Clear( stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_Push(
    stack_t<type_t> *pStack,
    const type_t &value ) noexcept;

template <typename type_t, typename... args_t>
CYPHER_NODISCARD type_t *Stack_Emplace(
    stack_t<type_t> *pStack,
    args_t &&... args ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_Pop(
    stack_t<type_t> *pStack,
    type_t *pValueOut = nullptr ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Stack_Top( stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Stack_Top(
    const stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize Stack_Count( const stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_IsEmpty( const stack_t<type_t> *pStack ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STACK_H
