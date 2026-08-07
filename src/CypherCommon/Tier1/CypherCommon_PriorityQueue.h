//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_PriorityQueue.h
//  Purpose: Declares binary-heap priority queues.
//  Details: PriorityQueue owns contiguous vector storage. The comparison policy
//           determines which value appears at the top.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_PRIORITYQUEUE_H
#define CYPHER_COMMON_TIER1_PRIORITYQUEUE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Functor.h"
#include "CypherCommon_Vector.h"

namespace cypher::common
{

template <typename type_t, typename compare_t = less_t<type_t>>
struct priority_queue_t {
    vector_t<type_t> storage{};
    compare_t compare{};
};

template <typename type_t, typename compare_t>
CYPHER_NODISCARD bool_t PriorityQueue_Init(
    priority_queue_t<type_t, compare_t> *pQueue,
    const allocator_t *pAllocator,
    usize nInitialCapacity = 0u,
    compare_t compare = {} ) noexcept;

template <typename type_t, typename compare_t>
void PriorityQueue_Shutdown(
    priority_queue_t<type_t, compare_t> *pQueue ) noexcept;

template <typename type_t, typename compare_t>
CYPHER_NODISCARD bool_t PriorityQueue_Push(
    priority_queue_t<type_t, compare_t> *pQueue,
    const type_t &value ) noexcept;

template <typename type_t, typename compare_t>
CYPHER_NODISCARD bool_t PriorityQueue_Pop(
    priority_queue_t<type_t, compare_t> *pQueue,
    type_t *pValueOut = nullptr ) noexcept;

template <typename type_t, typename compare_t>
CYPHER_NODISCARD type_t *PriorityQueue_Top(
    priority_queue_t<type_t, compare_t> *pQueue ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_PRIORITYQUEUE_H
