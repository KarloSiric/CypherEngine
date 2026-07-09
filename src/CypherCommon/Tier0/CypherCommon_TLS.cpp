//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_TLS.cpp
//  Purpose: Implements CypherCommon Tier0 thread-local storage slots.
//  Details: TLS is used for per-thread engine context such as scratch memory,
//           profiler state, logging context, and future worker-local data.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TLS.h"

#include <array>
#include <atomic>
#include <mutex>

namespace cypher::common
{
namespace
{

constexpr u32 CY_TLS_INDEX_BITS = 10u;
constexpr u32 CY_TLS_INDEX_MASK = ( 1u << CY_TLS_INDEX_BITS ) - 1u;
constexpr u32 CY_TLS_MAX_SLOT_COUNT = 1u << CY_TLS_INDEX_BITS;

struct tls_slot_record_t {
    std::atomic<u32> nGeneration{ 1u };
    std::atomic_bool bActive{ false };
};

std::mutex g_tlsMutex;
std::array<tls_slot_record_t, CY_TLS_MAX_SLOT_COUNT> g_tlsSlots;

thread_local std::array<void *, CY_TLS_MAX_SLOT_COUNT> g_tlsThreadValues = {};
thread_local std::array<u32, CY_TLS_MAX_SLOT_COUNT> g_tlsThreadGenerations = {};

u32 TLS_IndexFromSlot( tls_slot_t slot )
{
    return slot & CY_TLS_INDEX_MASK;
}

u32 TLS_GenerationFromSlot( tls_slot_t slot )
{
    return slot >> CY_TLS_INDEX_BITS;
}

tls_slot_t TLS_MakeSlot( u32 nIndex, u32 nGeneration )
{
    return ( static_cast<tls_slot_t>( nGeneration ) << CY_TLS_INDEX_BITS ) | nIndex;
}

bool_t TLS_IsValidSlot( tls_slot_t slot )
{
    if ( slot == CY_TLS_INVALID_SLOT ) {
        return CY_FALSE;
    }

    const u32 nIndex = TLS_IndexFromSlot( slot );
    const u32 nGeneration = TLS_GenerationFromSlot( slot );

    if ( nIndex >= CY_TLS_MAX_SLOT_COUNT || nGeneration == 0u ) {
        return CY_FALSE;
    }

    const tls_slot_record_t &record = g_tlsSlots[nIndex];
    return record.bActive.load( std::memory_order_acquire ) &&
           record.nGeneration.load( std::memory_order_acquire ) == nGeneration;
}

} // namespace

tls_slot_t Cy_TLSCreateSlot()
{
    std::lock_guard<std::mutex> lock( g_tlsMutex );

    for ( u32 nIndex = 0u; nIndex < CY_TLS_MAX_SLOT_COUNT; ++nIndex ) {
        tls_slot_record_t &record = g_tlsSlots[nIndex];
        if ( !record.bActive.load( std::memory_order_relaxed ) ) {
            record.bActive.store( true, std::memory_order_release );
            return TLS_MakeSlot( nIndex, record.nGeneration.load( std::memory_order_relaxed ) );
        }
    }

    return CY_TLS_INVALID_SLOT;
}

void Cy_TLSDestroySlot( tls_slot_t slot )
{
    std::lock_guard<std::mutex> lock( g_tlsMutex );

    if ( !TLS_IsValidSlot( slot ) ) {
        return;
    }

    tls_slot_record_t &record = g_tlsSlots[TLS_IndexFromSlot( slot )];
    record.bActive.store( false, std::memory_order_release );

    u32 nNextGeneration = record.nGeneration.load( std::memory_order_relaxed ) + 1u;
    if ( nNextGeneration == 0u || nNextGeneration > ( CY_U32_MAX >> CY_TLS_INDEX_BITS ) ) {
        nNextGeneration = 1u;
    }

    record.nGeneration.store( nNextGeneration, std::memory_order_release );

    const u32 nIndex = TLS_IndexFromSlot( slot );
    g_tlsThreadValues[nIndex] = nullptr;
    g_tlsThreadGenerations[nIndex] = 0u;
}

bool_t Cy_TLSIsValidSlot( tls_slot_t slot )
{
    return TLS_IsValidSlot( slot );
}

bool_t Cy_TLSSetValue( tls_slot_t slot, void *pValue )
{
    if ( !TLS_IsValidSlot( slot ) ) {
        return CY_FALSE;
    }

    const u32 nIndex = TLS_IndexFromSlot( slot );
    g_tlsThreadValues[nIndex] = pValue;
    g_tlsThreadGenerations[nIndex] = TLS_GenerationFromSlot( slot );

    return CY_TRUE;
}

void *Cy_TLSGetValue( tls_slot_t slot )
{
    if ( !TLS_IsValidSlot( slot ) ) {
        return nullptr;
    }

    const u32 nIndex = TLS_IndexFromSlot( slot );
    if ( g_tlsThreadGenerations[nIndex] != TLS_GenerationFromSlot( slot ) ) {
        return nullptr;
    }

    return g_tlsThreadValues[nIndex];
}

void Cy_TLSClearValue( tls_slot_t slot )
{
    if ( !TLS_IsValidSlot( slot ) ) {
        return;
    }

    const u32 nIndex = TLS_IndexFromSlot( slot );
    if ( g_tlsThreadGenerations[nIndex] == TLS_GenerationFromSlot( slot ) ) {
        g_tlsThreadValues[nIndex] = nullptr;
        g_tlsThreadGenerations[nIndex] = 0u;
    }
}

} // namespace cypher::common
