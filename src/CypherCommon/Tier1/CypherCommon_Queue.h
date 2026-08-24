//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Queue.h
//  Purpose: Declares an allocator-backed first-in-first-out circular queue.
//  Details: Queue grows geometrically while preserving logical order. It provides no
//           synchronization; concurrent producers require a dedicated queue type.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Queue Contract

Container mutations must preserve structural invariants and element lifetime. Iterators or
handles are invalidated only according to the rules stated by the public API.
================
*/

#ifndef CYPHER_COMMON_TIER1_QUEUE_H
#define CYPHER_COMMON_TIER1_QUEUE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"

namespace cypher::common
{

// Queue stores live objects in a circular allocation. Logical index zero is iHead; the physical
// tail is (iHead + nCount) modulo nCapacity. Growth linearizes the sequence and invalidates every
// pointer previously returned by Front, Back, or At.

template <typename type_t>
struct queue_t {
    static_assert( is_object_v<type_t>, "queue_t requires an object type." );
    static_assert( !is_array_v<type_t>, "queue_t does not store array elements." );

    queue_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( queue_t );
    ~queue_t() noexcept;

    type_t *pData{ nullptr };                 // Circular raw storage for nCapacity objects.
    usize nCapacity{ 0u };                    // Total physical slots in pData.
    usize nCount{ 0u };                       // Number of constructed queue elements.
    usize iHead{ 0u };                        // Physical slot of the logical front.
    const allocator_t *pAllocator{ nullptr }; // Allocator fixed at Queue_Init.
};

template <typename type_t>
CYPHER_NODISCARD bool_t Queue_Init(
    queue_t<type_t> *pQueue,
    const allocator_t *pAllocator,
    usize nInitialCapacity = 0u ) noexcept;

template <typename type_t>
void Queue_Shutdown( queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
void Queue_Clear( queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Queue_IsValid(
    const queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Queue_Reserve(
    queue_t<type_t> *pQueue,
    usize nCapacity ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Queue_ShrinkToFit(
    queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Queue_Push(
    queue_t<type_t> *pQueue,
    const type_t &value ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Queue_PushMove(
    queue_t<type_t> *pQueue,
    type_t &&value ) noexcept;

template <typename type_t, typename... args_t>
CYPHER_NODISCARD type_t *Queue_Emplace(
    queue_t<type_t> *pQueue,
    args_t &&... args ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Queue_Pop(
    queue_t<type_t> *pQueue,
    type_t *pValueOut = nullptr ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Queue_Front( queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Queue_Front(
    const queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Queue_Back( queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Queue_Back(
    const queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Queue_At(
    queue_t<type_t> *pQueue,
    usize iLogicalIndex ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Queue_At(
    const queue_t<type_t> *pQueue,
    usize iLogicalIndex ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize Queue_Count( const queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize Queue_Capacity( const queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Queue_IsEmpty( const queue_t<type_t> *pQueue ) noexcept;

template <typename type_t>
void Queue_Move(
    queue_t<type_t> *pDest,
    queue_t<type_t> *pSource ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_QUEUE_INL
    #include "CypherCommon_Queue.inl"
#endif

#endif // CYPHER_COMMON_TIER1_QUEUE_H
