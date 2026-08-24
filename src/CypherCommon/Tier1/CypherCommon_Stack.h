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

/*
================
Stack Contract

Stack deliberately exposes only the back of its vector storage. Push may reallocate and invalidate
the current Top pointer; Pop destroys exactly the last live element.
================
*/

#ifndef CYPHER_COMMON_TIER1_STACK_H
#define CYPHER_COMMON_TIER1_STACK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Vector.h"

namespace cypher::common
{

template <typename type_t>
struct cy_stack_t {
    vector_t<type_t> storage{}; // Bottom at index zero, top at nCount - 1.
};

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_Init(
    cy_stack_t<type_t> *pStack,
    const allocator_t *pAllocator,
    usize nInitialCapacity = 0u ) noexcept;

template <typename type_t>
void Stack_Shutdown( cy_stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
void Stack_Clear( cy_stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_IsValid(
    const cy_stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_Reserve(
    cy_stack_t<type_t> *pStack,
    usize nCapacity ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_Push(
    cy_stack_t<type_t> *pStack,
    const type_t &value ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_PushMove(
    cy_stack_t<type_t> *pStack,
    type_t &&value ) noexcept;

template <typename type_t, typename... args_t>
CYPHER_NODISCARD type_t *Stack_Emplace(
    cy_stack_t<type_t> *pStack,
    args_t &&... args ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_Pop(
    cy_stack_t<type_t> *pStack,
    type_t *pValueOut = nullptr ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Stack_Top( cy_stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Stack_Top(
    const cy_stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize Stack_Count( const cy_stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize Stack_Capacity(
    const cy_stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Stack_IsEmpty( const cy_stack_t<type_t> *pStack ) noexcept;

template <typename type_t>
void Stack_Move(
    cy_stack_t<type_t> *pDest,
    cy_stack_t<type_t> *pSource ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_STACK_INL
    #include "CypherCommon_Stack.inl"
#endif

#endif // CYPHER_COMMON_TIER1_STACK_H
