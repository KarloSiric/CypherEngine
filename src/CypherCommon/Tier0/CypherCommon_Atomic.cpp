//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Atomic.cpp
//  Purpose: Implements CypherCommon Tier0 Atomic fences.
//  Details: This file keeps explicit memory fence entry points out-of-line so
//           low-level systems can share one named synchronization surface.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Atomic.h"

namespace cypher::common
{

// These are process-wide ordering fences, not atomic storage operations. Prefer
// the weakest fence that establishes the documented producer/consumer relation.

void Cy_AtomicFenceAcquire() noexcept
{
    // Prevent later loads/stores from moving before this fence.
    std::atomic_thread_fence( CY_MEMORY_ORDER_ACQUIRE );
}

void Cy_AtomicFenceRelease() noexcept
{
    // Prevent earlier loads/stores from moving after this fence.
    std::atomic_thread_fence( CY_MEMORY_ORDER_RELEASE );
}

void Cy_AtomicFenceAcqRel() noexcept
{
    // Combines acquire and release ordering without a global total order.
    std::atomic_thread_fence( CY_MEMORY_ORDER_ACQ_REL );
}

void Cy_AtomicFenceSeqCst() noexcept
{
    // Participates in the single sequentially consistent order across threads.
    std::atomic_thread_fence( CY_MEMORY_ORDER_SEQ_CST );
}

void Cy_AtomicSignalFence() noexcept
{
    // Compiler ordering only between normal execution and an asynchronous signal;
    // this does not emit a hardware inter-thread fence.
    std::atomic_signal_fence( CY_MEMORY_ORDER_SEQ_CST );
}

} // namespace cypher::common
