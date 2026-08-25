//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory_Arena.h
//  Purpose: Declares the CypherMemory Memory Arena module.
//  Details: This file participates in the engine allocation layer for arenas, pools,
//           buckets, scratch memory, and diagnostics. Keep ownership and lifetime
//           rules explicit because allocator bugs corrupt everything above them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_MEMORY_ARENA_H
#define CYPHER_ENGINE_MEMORY_ARENA_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"
#include "CypherMemory_Error.h"
#include <cassert>
#include <limits>

namespace cypher::engine::memory
{

/*
================
Memory Size Helpers
================
*/
constexpr inline common::usize Mem_Kilobytes( const common::usize value )
{
    return value * 1024u;
}

constexpr inline common::usize Mem_Megabytes( const common::usize value )
{
    return Mem_Kilobytes( value ) * 1024u;
}

constexpr inline common::usize Mem_Gigabytes( const common::usize value )
{
    return Mem_Megabytes( value ) * 1024u;
}

/*
================
Arena Constants
================
*/
constexpr common::usize CYPHER_MEMORY_DEFAULT_ALIGNMENT = alignof( common::usize );

constexpr common::usize CYPHER_MEMORY_KIB               = 1024u;
constexpr common::usize CYPHER_MEMORY_MIB               = 1024u * CYPHER_MEMORY_KIB;
constexpr common::usize CYPHER_MEMORY_GIB               = 1024u * CYPHER_MEMORY_MIB;

constexpr common::u32 CYPHER_MEMORY_ARENA_FLAG_NONE                  = 0u;               // 0000
constexpr common::u32 CYPHER_MEMORY_ARENA_FLAG_ZERO_ON_ALLOC         = 1u << 0u;         // 0001
constexpr common::u32 CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET        = 1u << 1u;         // 0010
constexpr common::u32 CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_SHUTDOWN     = 1u << 2u;         // 0100
constexpr common::u32 CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC  = 1u << 3u;         // 1000
constexpr common::u32 CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET     = 1u << 4u;         // 1 0000

constexpr common::usize CYPHER_MEMORY_ARENA_ALLOCATION_TRACE_COUNT = 64u;

/*
================
Arena Helpers
================
*/
constexpr inline bool Mem_IsPowerOfTwo( const common::usize value )
{
    // Quick fast O(1) formula for checking if the binary value is power of two.
    return ( ( value > 0 ) && ( value & ( value - 1 ) ) == 0 );
}

constexpr inline bool Mem_AddSizeChecked( const common::usize a, const common::usize b, common::usize &valueOut )
{
    const common::usize nMaxValue = std::numeric_limits<common::usize>::max();

    if ( a > nMaxValue - b ) {
        return false;
    }

    valueOut = a + b;
    return true;
}

constexpr inline bool Mem_MulSizeChecked( const common::usize a, const common::usize b, common::usize &valueOut )
{
    const common::usize nMaxValue = std::numeric_limits<common::usize>::max();

    if ( a != 0u && b > nMaxValue / a ) {
        return false;
    }

    valueOut = a * b;
    return true;
}

constexpr inline bool Mem_AlignForwardChecked( const common::usize value, const common::usize alignment, common::usize &valueOut )
{
    if ( !Mem_IsPowerOfTwo( alignment ) ) {
        return false;
    }

    const common::usize mask = alignment - 1u;

    if ( value > std::numeric_limits<common::usize>::max() - mask ) {
        return false;
    }

    valueOut = ( value + mask ) & ~mask;
    return true;
}

constexpr inline common::usize Mem_AlignForward( common::usize value, common::usize alignment )
{
    assert( Mem_IsPowerOfTwo( alignment ) );

    common::usize result = value;
    const bool aligned = Mem_AlignForwardChecked( value, alignment, result );
    assert( aligned );

    return result;
}

enum class arena_backing_t : common::u8 {
    ARENA_HEAP = 0,       // Arena owns one heap allocation of its full capacity.
    ARENA_EXTERNAL_BUFFER,// Caller owns the fixed backing span and its lifetime.
    ARENA_VIRTUAL_MEMORY  // Arena reserves address space and commits pages on demand.
};

/*
================
Arena Description

Creation request for an arena that owns its backing memory.
================
*/
struct arena_desc_t {
    const char *name{ nullptr };                            // Borrowed diagnostic name; must outlive the arena.

    common::usize capacity{ 0u };                           // Maximum allocatable backing extent in bytes.
    common::usize initialCommit{ 0u };                      // Bytes committed immediately for virtual backing.
    void *pExternalBuffer{ nullptr };                       // Caller-owned base when backing is EXTERNAL_BUFFER.

    common::u32 flags{ CYPHER_MEMORY_ARENA_FLAG_NONE };     // CYPHER_MEMORY_ARENA_FLAG_* behavior bits.
    arena_backing_t backing{ arena_backing_t::ARENA_HEAP }; // Source and ownership of the backing memory.
};

/*
================
Arena Marker

Saved arena offset. Rewinding to this marker releases everything allocated
after this point.
================
*/
struct arena_marker_t {
    common::usize used{ 0u };                               // Saved linear cursor in bytes from arena base.
};

/*
================
Arena Allocation Trace

Small debug record for recent arena allocation callsites.
================
*/
struct arena_allocation_trace_t {
    const char *file{ nullptr };                            // Borrowed source filename for the operation.
    const char *function{ nullptr };                        // Borrowed source function name.
    common::i32 line{ 0 };                                  // One-based source line, or zero when unavailable.

    void *ptr{ nullptr };                                   // Returned allocation address, null on failure.
    common::usize size{ 0u };                               // Requested payload size in bytes.
    common::usize alignment{ 0u };                          // Requested power-of-two alignment in bytes.
    common::usize nUsedAfter{ 0u };                         // Arena cursor after the attempted allocation.

    common::u64 nAllocationIndex{ 0u };                     // Monotonic operation number for chronological ordering.
    mem_error_t error{ mem_error_t::OK };                   // Result associated with this trace record.
    bool failed{ false };                                   // True when no allocation was returned.
};

/*
================
Arena Stats

Snapshot of arena memory usage.
================
*/
struct arena_stats_t {
    const char *name{ nullptr };                            // Borrowed arena diagnostic name.

    common::usize capacity{ 0u };                           // Reserved or allocated backing extent in bytes.
    common::usize used{ 0u };                               // Current linear cursor in bytes.
    common::usize remaining{ 0u };                          // capacity - used after invariant validation.
    common::usize nPeakUsed{ 0u };                          // Highest observed cursor since initialization/reset.

    common::u64 nAllocationCount{ 0u };                     // Successful allocation operations.
    common::u64 nFailedAllocationCount{ 0u };               // Rejected or exhausted allocation operations.

    common::usize committed{ 0u };                          // Backing bytes currently accessible to the process.
    common::usize initialCommit{ 0u };                      // Minimum commit retained across decommit resets.
};

/*
================
Arena

Linear allocator state. The arena owns or references one contiguous memory
block and serves allocations by moving the used offset forward.
================
*/
struct arena_t {
    const char *name{ nullptr };                            // Borrowed diagnostic name; never freed by the arena.

    common::byte *base{ nullptr };                          // First byte of the contiguous backing region.

    common::usize capacity{ 0u };                           // Maximum cursor position in bytes.
    common::usize used{ 0u };                               // Current allocation cursor in bytes.
    common::usize nPeakUsed{ 0u };                          // Highest observed cursor for diagnostics.

    common::usize committed{ 0u };                          // Accessible prefix of virtual backing in bytes.
    common::usize initialCommit{ 0u };                      // Commit floor restored after decommit-on-reset.
    common::usize nPageSize{ 0u };                          // Platform page granularity used for commit operations.

    common::u64 nAllocationCount{ 0u };                     // Successful allocations since counters were reset.
    common::u64 nFailedAllocationCount{ 0u };               // Failed allocations since counters were reset.

    common::u32 flags{ CYPHER_MEMORY_ARENA_FLAG_NONE };     // Active CYPHER_MEMORY_ARENA_FLAG_* policy bits.

    mem_error_t lastError{ mem_error_t::OK };               // Result of the most recent mutating operation.

    arena_allocation_trace_t pAllocationTraces[CYPHER_MEMORY_ARENA_ALLOCATION_TRACE_COUNT]{}; // Recent-operation ring.
    common::usize nAllocationTraceIndex{ 0u };              // Slot overwritten by the next trace.
    common::usize nAllocationTraceCount{ 0u };              // Valid records, capped at ring capacity.

    arena_backing_t backing{ arena_backing_t::ARENA_HEAP }; // Allocation/release strategy for base.

    bool initialized{ false };                              // Public operations are valid only while true.
    bool pOwnsMemory{ false };                              // Shutdown releases base only when ownership is true.
};

/*
================
Arena Functions

List of functions necessary to use for creating arena memory layouts.
================
*/

mem_error_t Mem_ArenaInit( arena_t &arena, const arena_desc_t &arenaDesc );

void Mem_ArenaShutdown( arena_t &arena );

arena_stats_t Mem_ArenaStats( const arena_t &arena );

void Mem_ArenaResetCounters( arena_t &arena );

void Mem_ArenaReset( arena_t &arena );

void *Mem_ArenaAlloc( arena_t &arena, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *Mem_ArenaAllocDebug( arena_t &arena,
                                    common::usize size,
                                    common::usize alignment,
                                    const char *file,
                                    const char *function,
                                    common::i32 line );

void *Mem_ArenaAllocZero( arena_t &arena, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *Mem_ArenaAllocZeroDebug( arena_t &arena,
                                        common::usize size,
                                        common::usize alignment,
                                        const char *file,
                                        const char *function,
                                        common::i32 line );

arena_marker_t Mem_ArenaGetMarker( const arena_t &arena );

mem_error_t Mem_ArenaRewind( arena_t &arena, arena_marker_t marker );

bool Mem_ArenaContains( const arena_t &arena, const void *ptr );

mem_error_t Mem_ArenaLastError( const arena_t &arena );

bool Mem_ArenaIsInitialized( const arena_t &arena );

common::usize Mem_ArenaUsed( const arena_t &arena );

common::f32 Mem_ArenaUsageRatio( const arena_t &arena );

common::usize Mem_ArenaCapacity( const arena_t &arena );

common::usize Mem_ArenaRemaining( const arena_t &arena );

const arena_allocation_trace_t *Mem_ArenaAllocationTraces( const arena_t &arena, common::usize &nOutCount );

/*
================
Arena Helper Functions

List of necessary and helpful functions
================
*/

template <typename T>
T *Mem_ArenaAllocType( arena_t &arena ) {
    return static_cast<T *>(
            Mem_ArenaAlloc(
            arena,
            sizeof( T ),
            alignof( T ) ) );
}

template <typename T>
T *Mem_ArenaAllocTypeDebug( arena_t &arena, const char *file, const char *function, common::i32 line )
{
    return static_cast<T *>(
            Mem_ArenaAllocDebug(
            arena,
            sizeof( T ),
            alignof( T ),
            file,
            function,
            line ) );
}

template <typename T>
T *Mem_ArenaAllocArray( arena_t &arena, const common::usize count )
{
    common::usize size = 0u;
    if ( !Mem_MulSizeChecked( sizeof( T ), count, size ) ) {
        arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        ++arena.nFailedAllocationCount;
        return nullptr;
    }

    return static_cast<T *>(
            Mem_ArenaAlloc(
            arena,
            size,
            alignof( T ) ) );
}

template <typename T>
T *Mem_ArenaAllocArrayDebug( arena_t &arena, const common::usize count, const char *file, const char *function, common::i32 line )
{
    common::usize size = 0u;
    if ( !Mem_MulSizeChecked( sizeof( T ), count, size ) ) {
        arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        ++arena.nFailedAllocationCount;
        return nullptr;
    }

    return static_cast<T *>(
            Mem_ArenaAllocDebug(
            arena,
            size,
            alignof( T ),
            file,
            function,
            line ) );
}

template <typename T>
T *Mem_ArenaAllocArrayZero( arena_t &arena, const common::usize count )
{
    common::usize size = 0u;
    if ( !Mem_MulSizeChecked( sizeof( T ), count, size ) ) {
        arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        ++arena.nFailedAllocationCount;
        return nullptr;
    }

    return static_cast<T *>(
            Mem_ArenaAllocZero(
            arena,
            size,
            alignof( T ) ) );
}

template <typename T>
T *Mem_ArenaAllocArrayZeroDebug( arena_t &arena, const common::usize count, const char *file, const char *function, common::i32 line )
{
    common::usize size = 0u;
    if ( !Mem_MulSizeChecked( sizeof( T ), count, size ) ) {
        arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        ++arena.nFailedAllocationCount;
        return nullptr;
    }

    return static_cast<T *>(
            Mem_ArenaAllocZeroDebug(
            arena,
            size,
            alignof( T ),
            file,
            function,
            line ) );
}

template <typename T>
T *Mem_ArenaAllocTypeZero( arena_t &arena )
{
        return static_cast<T *>(
            Mem_ArenaAllocZero(
            arena,
            sizeof( T ),
            alignof( T ) ) );
}

template <typename T>
T *Mem_ArenaAllocTypeZeroDebug( arena_t &arena, const char *file, const char *function, common::i32 line )
{
        return static_cast<T *>(
            Mem_ArenaAllocZeroDebug(
            arena,
            sizeof( T ),
            alignof( T ),
            file,
            function,
            line ) );
}

}       // namespace cypher::engine::memory

#define CYPHER_MEMORY_ARENA_ALLOC( ARENA, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::Mem_ArenaAllocDebug( ( ARENA ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_ARENA_ALLOC_ZERO( ARENA, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::Mem_ArenaAllocZeroDebug( ( ARENA ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_ARENA_ALLOC_TYPE( ARENA, TYPE ) \
    ::cypher::engine::memory::Mem_ArenaAllocTypeDebug<TYPE>( ( ARENA ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_ARENA_ALLOC_TYPE_ZERO( ARENA, TYPE ) \
    ::cypher::engine::memory::Mem_ArenaAllocTypeZeroDebug<TYPE>( ( ARENA ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_ARENA_ALLOC_ARRAY( ARENA, TYPE, COUNT ) \
    ::cypher::engine::memory::Mem_ArenaAllocArrayDebug<TYPE>( ( ARENA ), ( COUNT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_ARENA_ALLOC_ARRAY_ZERO( ARENA, TYPE, COUNT ) \
    ::cypher::engine::memory::Mem_ArenaAllocArrayZeroDebug<TYPE>( ( ARENA ), ( COUNT ), __FILE__, __func__, __LINE__ )

#endif // CYPHER_ENGINE_MEMORY_ARENA_H
