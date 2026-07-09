//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Atomic.h
//  Purpose: Declares CypherCommon Tier0 Atomic support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_ATOMIC_H
#define CYPHER_COMMON_TIER0_ATOMIC_H
#pragma once

/*
================
CypherCommon Atomic

Thread-safe scalar primitives used for flags, counters, indices, and state
publication. Higher-level synchronization belongs in Thread, Mutex, TLS,
queues, and the future job system.
================
*/

#include "CypherCommon_BaseTypes.h"

#include <atomic>

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

constexpr memory_order_t CY_MEMORY_ORDER_RELAXED = std::memory_order_relaxed;
constexpr memory_order_t CY_MEMORY_ORDER_ACQUIRE = std::memory_order_acquire;
constexpr memory_order_t CY_MEMORY_ORDER_RELEASE = std::memory_order_release;
constexpr memory_order_t CY_MEMORY_ORDER_ACQ_REL = std::memory_order_acq_rel;
constexpr memory_order_t CY_MEMORY_ORDER_SEQ_CST = std::memory_order_seq_cst;

// Loads a single atomic value with explicit memory-order policy.
template <typename type_t>
type_t Cy_AtomicLoad( const atomic_t<type_t> *pAtomic, memory_order_t order = CY_MEMORY_ORDER_SEQ_CST )
{
    return pAtomic->load( order );
}

// Stores a single atomic value with explicit memory-order policy.
template <typename type_t>
void Cy_AtomicStore( atomic_t<type_t> *pAtomic, type_t value, memory_order_t order = CY_MEMORY_ORDER_SEQ_CST )
{
    pAtomic->store( value, order );
}

// Replaces an atomic value and returns the old value.
template <typename type_t>
type_t Cy_AtomicExchange( atomic_t<type_t> *pAtomic, type_t value, memory_order_t order = CY_MEMORY_ORDER_SEQ_CST )
{
    return pAtomic->exchange( value, order );
}

// Attempts to replace an atomic value only when it still matches pExpected.
template <typename type_t>
bool_t Cy_AtomicCompareExchange( atomic_t<type_t> *pAtomic,
                                 type_t *pExpected,
                                 type_t desired,
                                 memory_order_t successOrder = CY_MEMORY_ORDER_SEQ_CST,
                                 memory_order_t failureOrder = CY_MEMORY_ORDER_SEQ_CST )
{
    return pAtomic->compare_exchange_strong( *pExpected, desired, successOrder, failureOrder );
}

// Adds value to an atomic integer and returns the previous value.
template <typename type_t>
type_t Cy_AtomicFetchAdd( atomic_t<type_t> *pAtomic, type_t value, memory_order_t order = CY_MEMORY_ORDER_SEQ_CST )
{
    return pAtomic->fetch_add( value, order );
}

// Subtracts value from an atomic integer and returns the previous value.
template <typename type_t>
type_t Cy_AtomicFetchSub( atomic_t<type_t> *pAtomic, type_t value, memory_order_t order = CY_MEMORY_ORDER_SEQ_CST )
{
    return pAtomic->fetch_sub( value, order );
}

// Applies a bitwise AND to an atomic integer and returns the previous value.
template <typename type_t>
type_t Cy_AtomicFetchAnd( atomic_t<type_t> *pAtomic, type_t value, memory_order_t order = CY_MEMORY_ORDER_SEQ_CST )
{
    return pAtomic->fetch_and( value, order );
}

// Applies a bitwise OR to an atomic integer and returns the previous value.
template <typename type_t>
type_t Cy_AtomicFetchOr( atomic_t<type_t> *pAtomic, type_t value, memory_order_t order = CY_MEMORY_ORDER_SEQ_CST )
{
    return pAtomic->fetch_or( value, order );
}

// Applies a bitwise XOR to an atomic integer and returns the previous value.
template <typename type_t>
type_t Cy_AtomicFetchXor( atomic_t<type_t> *pAtomic, type_t value, memory_order_t order = CY_MEMORY_ORDER_SEQ_CST )
{
    return pAtomic->fetch_xor( value, order );
}

// Reports whether this atomic instance is lock-free on the active platform.
template <typename type_t>
bool_t Cy_AtomicIsLockFree( const atomic_t<type_t> *pAtomic )
{
    return pAtomic->is_lock_free();
}

// Prevents later memory operations from moving before this point.
void Cy_AtomicFenceAcquire();

// Prevents earlier memory operations from moving after this point.
void Cy_AtomicFenceRelease();

// Combines acquire and release ordering in one fence.
void Cy_AtomicFenceAcqRel();

// Emits the strongest standard atomic fence.
void Cy_AtomicFenceSeqCst();

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_ATOMIC_H
