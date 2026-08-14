//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory_Scratch.h
//  Purpose: Declares the CypherMemory Memory Scratch module.
//  Details: This file participates in the engine allocation layer for arenas, pools,
//           buckets, scratch memory, and diagnostics. Keep ownership and lifetime
//           rules explicit because allocator bugs corrupt everything above them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_MEMORY_SCRATCH_H
#define CYPHER_ENGINE_MEMORY_SCRATCH_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"
#include "CypherMemory_Arena.h"

namespace cypher::engine::memory
{

/*
================
Scratch Scope

Temporary allocation scope backed by an arena marker. Ending the scope rewinds
the backing arena to the saved marker and invalidates every scratch allocation.
================
*/
struct scratch_scope_t {
    const char *name{ nullptr };
    arena_t *arena{ nullptr };
    arena_marker_t marker{};

    common::usize nUsedAtBegin{ 0u };
    common::u64 nAllocationCountAtBegin{ 0u };
    common::u64 nFailedAllocationCountAtBegin{ 0u };

    mem_error_t lastError{ mem_error_t::OK };
    bool active{ false };
};

struct scratch_stats_t {
    const char *name{ nullptr };

    common::usize nUsedAtBegin{ 0u };
    common::usize nUsedCurrent{ 0u };
    common::usize nUsedSinceBegin{ 0u };
    common::usize capacity{ 0u };

    common::u64 nAllocationCountSinceBegin{ 0u };
    common::u64 nFailedAllocationCountSinceBegin{ 0u };

    bool active{ false };
};

mem_error_t CypherMemory_ScratchBegin( scratch_scope_t &scope, arena_t &arena, const char *name = nullptr );

mem_error_t CypherMemory_ScratchEnd( scratch_scope_t &scope );

scratch_stats_t CypherMemory_ScratchStats( const scratch_scope_t &scope );

void *CypherMemory_ScratchAlloc( scratch_scope_t &scope, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *CypherMemory_ScratchAllocDebug( scratch_scope_t &scope,
                                      common::usize size,
                                      common::usize alignment,
                                      const char *file,
                                      const char *function,
                                      common::i32 line );

void *CypherMemory_ScratchAllocZero( scratch_scope_t &scope, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *CypherMemory_ScratchAllocZeroDebug( scratch_scope_t &scope,
                                          common::usize size,
                                          common::usize alignment,
                                          const char *file,
                                          const char *function,
                                          common::i32 line );

bool CypherMemory_ScratchIsActive( const scratch_scope_t &scope );

mem_error_t CypherMemory_ScratchLastError( const scratch_scope_t &scope );

template <typename T>
T *CypherMemory_ScratchAllocType( scratch_scope_t &scope )
{
    return static_cast<T *>( CypherMemory_ScratchAlloc( scope, sizeof( T ), alignof( T ) ) );
}

template <typename T>
T *CypherMemory_ScratchAllocTypeDebug( scratch_scope_t &scope, const char *file, const char *function, common::i32 line )
{
    return static_cast<T *>( CypherMemory_ScratchAllocDebug( scope, sizeof( T ), alignof( T ), file, function, line ) );
}

template <typename T>
T *CypherMemory_ScratchAllocTypeZero( scratch_scope_t &scope )
{
    return static_cast<T *>( CypherMemory_ScratchAllocZero( scope, sizeof( T ), alignof( T ) ) );
}

template <typename T>
T *CypherMemory_ScratchAllocTypeZeroDebug( scratch_scope_t &scope, const char *file, const char *function, common::i32 line )
{
    return static_cast<T *>( CypherMemory_ScratchAllocZeroDebug( scope, sizeof( T ), alignof( T ), file, function, line ) );
}

template <typename T>
T *CypherMemory_ScratchAllocArray( scratch_scope_t &scope, const common::usize count )
{
    common::usize size = 0u;
    if ( !CypherMemory_MulSizeChecked( sizeof( T ), count, size ) ) {
        scope.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        return nullptr;
    }

    return static_cast<T *>( CypherMemory_ScratchAlloc( scope, size, alignof( T ) ) );
}

template <typename T>
T *CypherMemory_ScratchAllocArrayDebug( scratch_scope_t &scope,
                                        const common::usize count,
                                        const char *file,
                                        const char *function,
                                        common::i32 line )
{
    common::usize size = 0u;
    if ( !CypherMemory_MulSizeChecked( sizeof( T ), count, size ) ) {
        scope.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        return nullptr;
    }

    return static_cast<T *>( CypherMemory_ScratchAllocDebug( scope, size, alignof( T ), file, function, line ) );
}

template <typename T>
T *CypherMemory_ScratchAllocArrayZero( scratch_scope_t &scope, const common::usize count )
{
    common::usize size = 0u;
    if ( !CypherMemory_MulSizeChecked( sizeof( T ), count, size ) ) {
        scope.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        return nullptr;
    }

    return static_cast<T *>( CypherMemory_ScratchAllocZero( scope, size, alignof( T ) ) );
}

template <typename T>
T *CypherMemory_ScratchAllocArrayZeroDebug( scratch_scope_t &scope,
                                            const common::usize count,
                                            const char *file,
                                            const char *function,
                                            common::i32 line )
{
    common::usize size = 0u;
    if ( !CypherMemory_MulSizeChecked( sizeof( T ), count, size ) ) {
        scope.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        return nullptr;
    }

    return static_cast<T *>( CypherMemory_ScratchAllocZeroDebug( scope, size, alignof( T ), file, function, line ) );
}

}       // namespace cypher::engine::memory

#define CYPHER_MEMORY_SCRATCH_ALLOC( SCOPE, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::CypherMemory_ScratchAllocDebug( ( SCOPE ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_SCRATCH_ALLOC_ZERO( SCOPE, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::CypherMemory_ScratchAllocZeroDebug( ( SCOPE ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_SCRATCH_ALLOC_TYPE( SCOPE, TYPE ) \
    ::cypher::engine::memory::CypherMemory_ScratchAllocTypeDebug<TYPE>( ( SCOPE ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_SCRATCH_ALLOC_TYPE_ZERO( SCOPE, TYPE ) \
    ::cypher::engine::memory::CypherMemory_ScratchAllocTypeZeroDebug<TYPE>( ( SCOPE ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_SCRATCH_ALLOC_ARRAY( SCOPE, TYPE, COUNT ) \
    ::cypher::engine::memory::CypherMemory_ScratchAllocArrayDebug<TYPE>( ( SCOPE ), ( COUNT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_SCRATCH_ALLOC_ARRAY_ZERO( SCOPE, TYPE, COUNT ) \
    ::cypher::engine::memory::CypherMemory_ScratchAllocArrayZeroDebug<TYPE>( ( SCOPE ), ( COUNT ), __FILE__, __func__, __LINE__ )

#endif // CYPHER_ENGINE_MEMORY_SCRATCH_H
