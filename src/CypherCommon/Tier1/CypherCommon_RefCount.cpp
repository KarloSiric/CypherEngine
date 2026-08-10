//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_RefCount.cpp
//  Purpose: Implements intrusive atomic reference counters.
//  Details: Reference acquisition prevents overflow and resurrection. The final
//           release uses acquire-release ordering before ownership returns to caller.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_RefCount.h"

namespace cypher::common
{

void RefCount_Init( ref_count_t *pRefCount, u32 nInitialReferences ) noexcept
{
    CY_ASSERT_MSG( pRefCount != nullptr, "RefCount_Init requires storage." );
    CY_ASSERT_MSG(
        nInitialReferences > 0u,
        "RefCount_Init requires at least one owning reference." );
    if ( pRefCount == nullptr ) {
        return;
    }
    Cy_AtomicStore(
        &pRefCount->nReferences,
        nInitialReferences,
        CY_MEMORY_ORDER_RELAXED );
}

u32 RefCount_AddRef( ref_count_t *pRefCount ) noexcept
{
    CY_ASSERT_MSG( pRefCount != nullptr, "RefCount_AddRef requires a counter." );
    if ( pRefCount == nullptr ) {
        return 0u;
    }

    u32 nCurrent = Cy_AtomicLoad(
        &pRefCount->nReferences,
        CY_MEMORY_ORDER_RELAXED );
    for ( ;; ) {
        const bool_t bCanAcquire = nCurrent > 0u && nCurrent < CY_U32_MAX;
        CY_ASSERT_MSG(
            bCanAcquire,
            "RefCount_AddRef cannot resurrect or overflow a counter." );
        if ( !bCanAcquire ) {
            return nCurrent;
        }
        if ( Cy_AtomicCompareExchangeWeak(
                 &pRefCount->nReferences,
                 &nCurrent,
                 nCurrent + 1u,
                 CY_MEMORY_ORDER_RELAXED,
                 CY_MEMORY_ORDER_RELAXED ) ) {
            return nCurrent + 1u;
        }
    }
}

u32 RefCount_Release( ref_count_t *pRefCount ) noexcept
{
    CY_ASSERT_MSG( pRefCount != nullptr, "RefCount_Release requires a counter." );
    if ( pRefCount == nullptr ) {
        return 0u;
    }

    u32 nCurrent = Cy_AtomicLoad(
        &pRefCount->nReferences,
        CY_MEMORY_ORDER_RELAXED );
    for ( ;; ) {
        CY_ASSERT_MSG( nCurrent > 0u, "RefCount_Release cannot underflow a counter." );
        if ( nCurrent == 0u ) {
            return 0u;
        }
        if ( Cy_AtomicCompareExchangeWeak(
                 &pRefCount->nReferences,
                 &nCurrent,
                 nCurrent - 1u,
                 CY_MEMORY_ORDER_ACQ_REL,
                 CY_MEMORY_ORDER_RELAXED ) ) {
            return nCurrent - 1u;
        }
    }
}

u32 RefCount_Load( const ref_count_t *pRefCount ) noexcept
{
    CY_ASSERT_MSG( pRefCount != nullptr, "RefCount_Load requires a counter." );
    return pRefCount != nullptr
        ? Cy_AtomicLoad( &pRefCount->nReferences, CY_MEMORY_ORDER_ACQUIRE )
        : 0u;
}

} // namespace cypher::common
