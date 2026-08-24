//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Atomic.h
//  Purpose: Normalizes atomic scalar operations and legal memory-order combinations.
//  Details: The default remains sequential consistency; weaker orders require a
//           documented synchronization argument at the call site.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_ATOMIC_H
#define CYPHER_COMMON_TIER0_ATOMIC_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Atomic

Thread-safe scalar primitives used for flags, counters, indices, and state
publication. Higher-level synchronization belongs in Thread, Mutex, TLS,
queues, and the future job system.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

#include <atomic>
#include <type_traits>

namespace cypher::common
{

template <typename type_t>
using atomic_t = std::atomic<type_t>;

template <typename type_t>
using atomic_ptr_t = std::atomic<type_t *>;

using atomic_bool_t = std::atomic<bool_t>;
using atomic_i32_t = std::atomic<i32>;
using atomic_u32_t = std::atomic<u32>;
using atomic_i64_t = std::atomic<i64>;
using atomic_u64_t = std::atomic<u64>;
using atomic_usize_t = std::atomic<usize>;
using atomic_flag_t = std::atomic_flag;

using memory_order_t = std::memory_order;

constexpr memory_order_t CY_MEMORY_ORDER_RELAXED = std::memory_order_relaxed; // Atomicity only.
constexpr memory_order_t CY_MEMORY_ORDER_ACQUIRE = std::memory_order_acquire; // Orders following reads/writes.
constexpr memory_order_t CY_MEMORY_ORDER_RELEASE = std::memory_order_release; // Publishes preceding reads/writes.
constexpr memory_order_t CY_MEMORY_ORDER_ACQ_REL = std::memory_order_acq_rel;  // Acquire and release on RMW.
constexpr memory_order_t CY_MEMORY_ORDER_SEQ_CST = std::memory_order_seq_cst; // Single global total order.

// Returns a valid load/wait order, falling back to sequential consistency.
CYPHER_NODISCARD constexpr memory_order_t Cy_AtomicNormalizeLoadOrder(
    memory_order_t order ) noexcept
{
    switch ( order ) {
        case CY_MEMORY_ORDER_RELAXED:
        case CY_MEMORY_ORDER_ACQUIRE:
        case CY_MEMORY_ORDER_SEQ_CST:
            return order;
        case CY_MEMORY_ORDER_RELEASE:
        case CY_MEMORY_ORDER_ACQ_REL:
        default:
            return CY_MEMORY_ORDER_SEQ_CST;
    }
}

// Returns a valid store order, falling back to sequential consistency.
CYPHER_NODISCARD constexpr memory_order_t Cy_AtomicNormalizeStoreOrder(
    memory_order_t order ) noexcept
{
    switch ( order ) {
        case CY_MEMORY_ORDER_RELAXED:
        case CY_MEMORY_ORDER_RELEASE:
        case CY_MEMORY_ORDER_SEQ_CST:
            return order;
        case CY_MEMORY_ORDER_ACQUIRE:
        case CY_MEMORY_ORDER_ACQ_REL:
        default:
            return CY_MEMORY_ORDER_SEQ_CST;
    }
}

// Returns a valid read-modify-write order, falling back to sequential consistency.
CYPHER_NODISCARD constexpr memory_order_t Cy_AtomicNormalizeReadModifyWriteOrder(
    memory_order_t order ) noexcept
{
    switch ( order ) {
        case CY_MEMORY_ORDER_RELAXED:
        case CY_MEMORY_ORDER_ACQUIRE:
        case CY_MEMORY_ORDER_RELEASE:
        case CY_MEMORY_ORDER_ACQ_REL:
        case CY_MEMORY_ORDER_SEQ_CST:
            return order;
        default:
            return CY_MEMORY_ORDER_SEQ_CST;
    }
}

// Returns the strongest legal compare-exchange failure order for a success order.
CYPHER_NODISCARD constexpr memory_order_t Cy_AtomicDefaultFailureOrder(
    memory_order_t successOrder ) noexcept
{
    switch ( successOrder ) {
        case CY_MEMORY_ORDER_RELEASE:
            return CY_MEMORY_ORDER_RELAXED;
        case CY_MEMORY_ORDER_ACQ_REL:
            return CY_MEMORY_ORDER_ACQUIRE;
        default:
            return Cy_AtomicNormalizeLoadOrder( successOrder );
    }
}

// Prevents an invalid or stronger-than-success CAS failure order.
CYPHER_NODISCARD constexpr memory_order_t Cy_AtomicNormalizeFailureOrder(
    memory_order_t successOrder,
    memory_order_t failureOrder ) noexcept
{
    const memory_order_t normalizedSuccess =
        Cy_AtomicNormalizeReadModifyWriteOrder( successOrder );

    if ( failureOrder == CY_MEMORY_ORDER_RELEASE ||
         failureOrder == CY_MEMORY_ORDER_ACQ_REL ) {
        return Cy_AtomicDefaultFailureOrder( normalizedSuccess );
    }

    switch ( normalizedSuccess ) {
        case CY_MEMORY_ORDER_RELAXED:
        case CY_MEMORY_ORDER_RELEASE:
            return failureOrder == CY_MEMORY_ORDER_RELAXED
                ? failureOrder
                : Cy_AtomicDefaultFailureOrder( normalizedSuccess );
        case CY_MEMORY_ORDER_ACQUIRE:
        case CY_MEMORY_ORDER_ACQ_REL:
            return failureOrder == CY_MEMORY_ORDER_RELAXED ||
                   failureOrder == CY_MEMORY_ORDER_ACQUIRE
                ? failureOrder
                : Cy_AtomicDefaultFailureOrder( normalizedSuccess );
        case CY_MEMORY_ORDER_SEQ_CST:
        default:
            return Cy_AtomicNormalizeLoadOrder( failureOrder );
    }
}

// Loads a single atomic value with explicit memory-order policy.
template <typename type_t>
CYPHER_NODISCARD type_t Cy_AtomicLoad(
    const atomic_t<type_t> *pAtomic,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pAtomic->load( Cy_AtomicNormalizeLoadOrder( order ) );
}

// Stores a single atomic value with explicit memory-order policy.
template <typename type_t>
void Cy_AtomicStore(
    atomic_t<type_t> *pAtomic,
    type_t value,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    pAtomic->store( value, Cy_AtomicNormalizeStoreOrder( order ) );
}

// Replaces an atomic value and returns the old value.
template <typename type_t>
CYPHER_NODISCARD type_t Cy_AtomicExchange(
    atomic_t<type_t> *pAtomic,
    type_t value,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pAtomic->exchange(
        value,
        Cy_AtomicNormalizeReadModifyWriteOrder( order ) );
}

// Strong CAS: updates expected on failure and does not fail spuriously.
template <typename type_t>
CYPHER_NODISCARD bool_t Cy_AtomicCompareExchange(
    atomic_t<type_t> *pAtomic,
    type_t *pExpected,
    type_t desired,
    memory_order_t successOrder = CY_MEMORY_ORDER_SEQ_CST,
    memory_order_t failureOrder = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    const memory_order_t normalizedSuccess =
        Cy_AtomicNormalizeReadModifyWriteOrder( successOrder );
    return pAtomic->compare_exchange_strong(
        *pExpected,
        desired,
        normalizedSuccess,
        Cy_AtomicNormalizeFailureOrder( normalizedSuccess, failureOrder ) );
}

// Weak CAS: may fail spuriously and is intended for retry loops.
template <typename type_t>
CYPHER_NODISCARD bool_t Cy_AtomicCompareExchangeWeak(
    atomic_t<type_t> *pAtomic,
    type_t *pExpected,
    type_t desired,
    memory_order_t successOrder = CY_MEMORY_ORDER_SEQ_CST,
    memory_order_t failureOrder = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    const memory_order_t normalizedSuccess =
        Cy_AtomicNormalizeReadModifyWriteOrder( successOrder );
    return pAtomic->compare_exchange_weak(
        *pExpected,
        desired,
        normalizedSuccess,
        Cy_AtomicNormalizeFailureOrder( normalizedSuccess, failureOrder ) );
}

// Adds value to an atomic integer and returns the previous value.
template <typename type_t>
    requires( std::is_integral_v<type_t> && !std::is_same_v<type_t, bool_t> )
CYPHER_NODISCARD type_t Cy_AtomicFetchAdd(
    atomic_t<type_t> *pAtomic,
    type_t value,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pAtomic->fetch_add(
        value,
        Cy_AtomicNormalizeReadModifyWriteOrder( order ) );
}

// Subtracts value from an atomic integer and returns the previous value.
template <typename type_t>
    requires( std::is_integral_v<type_t> && !std::is_same_v<type_t, bool_t> )
CYPHER_NODISCARD type_t Cy_AtomicFetchSub(
    atomic_t<type_t> *pAtomic,
    type_t value,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pAtomic->fetch_sub(
        value,
        Cy_AtomicNormalizeReadModifyWriteOrder( order ) );
}

// Applies a bitwise AND to an atomic integer and returns the previous value.
template <typename type_t>
    requires( std::is_integral_v<type_t> && !std::is_same_v<type_t, bool_t> )
CYPHER_NODISCARD type_t Cy_AtomicFetchAnd(
    atomic_t<type_t> *pAtomic,
    type_t value,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pAtomic->fetch_and(
        value,
        Cy_AtomicNormalizeReadModifyWriteOrder( order ) );
}

// Applies a bitwise OR to an atomic integer and returns the previous value.
template <typename type_t>
    requires( std::is_integral_v<type_t> && !std::is_same_v<type_t, bool_t> )
CYPHER_NODISCARD type_t Cy_AtomicFetchOr(
    atomic_t<type_t> *pAtomic,
    type_t value,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pAtomic->fetch_or(
        value,
        Cy_AtomicNormalizeReadModifyWriteOrder( order ) );
}

// Applies a bitwise XOR to an atomic integer and returns the previous value.
template <typename type_t>
    requires( std::is_integral_v<type_t> && !std::is_same_v<type_t, bool_t> )
CYPHER_NODISCARD type_t Cy_AtomicFetchXor(
    atomic_t<type_t> *pAtomic,
    type_t value,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pAtomic->fetch_xor(
        value,
        Cy_AtomicNormalizeReadModifyWriteOrder( order ) );
}

// Advances an atomic pointer by an element count and returns the previous pointer.
template <typename type_t>
CYPHER_NODISCARD type_t *Cy_AtomicFetchAdd(
    atomic_ptr_t<type_t> *pAtomic,
    isize nElementCount,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pAtomic->fetch_add(
        nElementCount,
        Cy_AtomicNormalizeReadModifyWriteOrder( order ) );
}

// Moves an atomic pointer backward by an element count and returns the old pointer.
template <typename type_t>
CYPHER_NODISCARD type_t *Cy_AtomicFetchSub(
    atomic_ptr_t<type_t> *pAtomic,
    isize nElementCount,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pAtomic->fetch_sub(
        nElementCount,
        Cy_AtomicNormalizeReadModifyWriteOrder( order ) );
}

// Reports whether this atomic instance is lock-free on the active platform.
template <typename type_t>
CYPHER_NODISCARD bool_t Cy_AtomicIsLockFree(
    const atomic_t<type_t> *pAtomic ) noexcept
{
    return pAtomic->is_lock_free();
}

// Blocks until the value differs from oldValue; callers must loop for predicates.
template <typename type_t>
void Cy_AtomicWait(
    const atomic_t<type_t> *pAtomic,
    type_t oldValue,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    pAtomic->wait( oldValue, Cy_AtomicNormalizeLoadOrder( order ) );
}

// Wakes at least one waiter on this atomic object.
template <typename type_t>
void Cy_AtomicNotifyOne( atomic_t<type_t> *pAtomic ) noexcept
{
    pAtomic->notify_one();
}

// Wakes every waiter on this atomic object.
template <typename type_t>
void Cy_AtomicNotifyAll( atomic_t<type_t> *pAtomic ) noexcept
{
    pAtomic->notify_all();
}

// Returns the current atomic-flag state.
CYPHER_NODISCARD inline bool_t Cy_AtomicFlagTest(
    const atomic_flag_t *pFlag,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pFlag->test( Cy_AtomicNormalizeLoadOrder( order ) );
}

// Sets an atomic flag and returns its previous state.
CYPHER_NODISCARD inline bool_t Cy_AtomicFlagTestAndSet(
    atomic_flag_t *pFlag,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    return pFlag->test_and_set(
        Cy_AtomicNormalizeReadModifyWriteOrder( order ) );
}

// Clears an atomic flag.
inline void Cy_AtomicFlagClear(
    atomic_flag_t *pFlag,
    memory_order_t order = CY_MEMORY_ORDER_SEQ_CST ) noexcept
{
    pFlag->clear( Cy_AtomicNormalizeStoreOrder( order ) );
}

// Prevents later memory operations from moving before this point.
CYPHER_COMMON_API void Cy_AtomicFenceAcquire() noexcept;

// Prevents earlier memory operations from moving after this point.
CYPHER_COMMON_API void Cy_AtomicFenceRelease() noexcept;

// Combines acquire and release ordering in one fence.
CYPHER_COMMON_API void Cy_AtomicFenceAcqRel() noexcept;

// Emits the strongest standard atomic fence.
CYPHER_COMMON_API void Cy_AtomicFenceSeqCst() noexcept;

// Constrains compiler reordering without creating inter-thread synchronization.
CYPHER_COMMON_API void Cy_AtomicSignalFence() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_ATOMIC_H
