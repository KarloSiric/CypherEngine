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

// A slot packs an array index with a generation. Reusing an index increments
// the generation so stale slot values cannot address newly registered TLS data.
namespace
{

constexpr u32 CY_TLS_INDEX_BITS = 8u; // Low handle bits select one of 256 slots.
constexpr u32 CY_TLS_INDEX_MASK = ( 1u << CY_TLS_INDEX_BITS ) - 1u;
static_assert( CY_TLS_MAX_SLOT_COUNT == ( 1u << CY_TLS_INDEX_BITS ) );

struct tls_slot_record_t {
    std::atomic<u32> nGeneration{ 1u };              // Changes on every reuse.
    std::atomic_bool isActive{ false };              // Slot accepts Get/Set calls.
    std::atomic<tls_destructor_t> pDestructor{ nullptr }; // Runs at thread exit.
};

std::mutex g_tlsMutex;
std::array<tls_slot_record_t, CY_TLS_MAX_SLOT_COUNT> g_tlsSlots;
std::atomic<u32> g_tlsAllocatedSlotCount{ 0u };

struct tls_thread_state_t {
    std::array<void *, CY_TLS_MAX_SLOT_COUNT> values{}; // Values owned by this thread.
    std::array<u32, CY_TLS_MAX_SLOT_COUNT> generations{}; // Handle revision per value.

    ~tls_thread_state_t() noexcept
    {
        // Destruct only values whose slot is still active and has the generation
        // recorded by this thread. A destroyed and reused slot is unrelated data.
        for ( u32 nIndex = 0u; nIndex < CY_TLS_MAX_SLOT_COUNT; ++nIndex ) {
            void *pValue = values[nIndex];
            if ( pValue == nullptr ) {
                continue;
            }

            const tls_slot_record_t &record = g_tlsSlots[nIndex];
            if ( record.isActive.load( std::memory_order_acquire ) &&
                 record.nGeneration.load( std::memory_order_acquire ) ==
                     generations[nIndex] ) {
                tls_destructor_t pDestructor =
                    record.pDestructor.load( std::memory_order_acquire );
                if ( pDestructor != nullptr ) {
                    pDestructor( pValue );
                }
            }
        }
    }
};

thread_local tls_thread_state_t g_tlsThreadState;

u32 TLS_IndexFromSlot( tls_slot_t slot ) noexcept
{
    return slot & CY_TLS_INDEX_MASK;
}

u32 TLS_GenerationFromSlot( tls_slot_t slot ) noexcept
{
    return slot >> CY_TLS_INDEX_BITS;
}

tls_slot_t TLS_MakeSlot( u32 nIndex, u32 nGeneration ) noexcept
{
    return ( static_cast<tls_slot_t>( nGeneration ) << CY_TLS_INDEX_BITS ) | nIndex;
}

bool_t TLS_IsValidSlot( tls_slot_t slot ) noexcept
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
    return record.isActive.load( std::memory_order_acquire ) &&
           record.nGeneration.load( std::memory_order_acquire ) == nGeneration;
}

} // namespace

tls_slot_t Cy_TLSCreateSlot( tls_destructor_t pDestructor ) noexcept
{
    std::lock_guard<std::mutex> lock( g_tlsMutex );

    for ( u32 nIndex = 0u; nIndex < CY_TLS_MAX_SLOT_COUNT; ++nIndex ) {
        tls_slot_record_t &record = g_tlsSlots[nIndex];
        if ( !record.isActive.load( std::memory_order_relaxed ) ) {
            // Store destructor first, then release-publish active. Lock-free readers
            // that acquire isActive cannot observe a half-initialized record.
            record.pDestructor.store( pDestructor, std::memory_order_relaxed );
            record.isActive.store( true, std::memory_order_release );
            g_tlsAllocatedSlotCount.fetch_add( 1u, std::memory_order_relaxed );
            return TLS_MakeSlot( nIndex, record.nGeneration.load( std::memory_order_relaxed ) );
        }
    }

    return CY_TLS_INVALID_SLOT;
}

bool_t Cy_TLSDestroySlot( tls_slot_t slot ) noexcept
{
    tls_destructor_t pDestructor = nullptr;
    void *pCurrentThreadValue = nullptr;
    {
        std::lock_guard<std::mutex> lock( g_tlsMutex );

        if ( !TLS_IsValidSlot( slot ) ) {
            return CY_FALSE;
        }

        const u32 nIndex = TLS_IndexFromSlot( slot );
        const u32 nGeneration = TLS_GenerationFromSlot( slot );
        tls_slot_record_t &record = g_tlsSlots[nIndex];

        // Slot destruction can directly clean only the calling thread's value.
        // Other threads reject the stale generation and clean nothing at exit.
        if ( g_tlsThreadState.generations[nIndex] == nGeneration ) {
            pCurrentThreadValue = g_tlsThreadState.values[nIndex];
            g_tlsThreadState.values[nIndex] = nullptr;
            g_tlsThreadState.generations[nIndex] = 0u;
        }
        pDestructor = record.pDestructor.load( std::memory_order_relaxed );

        record.isActive.store( false, std::memory_order_release );
        record.pDestructor.store( nullptr, std::memory_order_release );

        // Generation zero is reserved by the invalid handle. Wrap to one before
        // the generation would overlap the packed index bits.
        u32 nNextGeneration =
            record.nGeneration.load( std::memory_order_relaxed ) + 1u;
        if ( nNextGeneration == 0u ||
             nNextGeneration >= ( CY_U32_MAX >> CY_TLS_INDEX_BITS ) ) {
            nNextGeneration = 1u;
        }
        record.nGeneration.store( nNextGeneration, std::memory_order_release );
        g_tlsAllocatedSlotCount.fetch_sub( 1u, std::memory_order_relaxed );
    }

    // User code runs outside g_tlsMutex; destructors may use other TLS APIs.
    if ( pCurrentThreadValue != nullptr && pDestructor != nullptr ) {
        pDestructor( pCurrentThreadValue );
    }
    return CY_TRUE;
}

bool_t Cy_TLSIsValidSlot( tls_slot_t slot ) noexcept
{
    return TLS_IsValidSlot( slot );
}

bool_t Cy_TLSSetValue( tls_slot_t slot, void *pValue ) noexcept
{
    if ( !TLS_IsValidSlot( slot ) ) {
        return CY_FALSE;
    }

    const u32 nIndex = TLS_IndexFromSlot( slot );
    // Replacing a value does not invoke the destructor. Ownership transfer on Set
    // is explicit; destruction occurs only on slot destruction or thread exit.
    g_tlsThreadState.values[nIndex] = pValue;
    g_tlsThreadState.generations[nIndex] = TLS_GenerationFromSlot( slot );

    return CY_TRUE;
}

void *Cy_TLSGetValue( tls_slot_t slot ) noexcept
{
    if ( !TLS_IsValidSlot( slot ) ) {
        return nullptr;
    }

    const u32 nIndex = TLS_IndexFromSlot( slot );
    if ( g_tlsThreadState.generations[nIndex] !=
         TLS_GenerationFromSlot( slot ) ) {
        return nullptr;
    }

    return g_tlsThreadState.values[nIndex];
}

bool_t Cy_TLSClearValue( tls_slot_t slot ) noexcept
{
    if ( !TLS_IsValidSlot( slot ) ) {
        return CY_FALSE;
    }

    const u32 nIndex = TLS_IndexFromSlot( slot );
    if ( g_tlsThreadState.generations[nIndex] ==
         TLS_GenerationFromSlot( slot ) ) {
        g_tlsThreadState.values[nIndex] = nullptr;
        g_tlsThreadState.generations[nIndex] = 0u;
    }
    return CY_TRUE;
}

u32 Cy_TLSGetAllocatedSlotCount() noexcept
{
    return g_tlsAllocatedSlotCount.load( std::memory_order_acquire );
}

} // namespace cypher::common
