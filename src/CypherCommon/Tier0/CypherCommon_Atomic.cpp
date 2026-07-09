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

void Cy_AtomicFenceAcquire()
{
    std::atomic_thread_fence( CY_MEMORY_ORDER_ACQUIRE );
}

void Cy_AtomicFenceRelease()
{
    std::atomic_thread_fence( CY_MEMORY_ORDER_RELEASE );
}

void Cy_AtomicFenceAcqRel()
{
    std::atomic_thread_fence( CY_MEMORY_ORDER_ACQ_REL );
}

void Cy_AtomicFenceSeqCst()
{
    std::atomic_thread_fence( CY_MEMORY_ORDER_SEQ_CST );
}

} // namespace cypher::common
