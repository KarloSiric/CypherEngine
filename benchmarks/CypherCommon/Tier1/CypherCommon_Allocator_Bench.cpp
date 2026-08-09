//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Allocator_Bench.cpp
//  Purpose: Benchmarks Tier1 allocator dispatch and system-backed allocation.
//  Details: Measures validation, allocation/free lifecycle cost, alignment overhead,
//           owned release, and allocate-copy-free reallocation against direct C++.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Allocator.h"

#include <benchmark/benchmark.h>

#include <new>

using namespace cypher::common;

namespace
{

usize EffectiveAlignment( usize nAlignment ) noexcept
{
    return nAlignment < CY_ALLOCATOR_DEFAULT_ALIGNMENT
        ? CY_ALLOCATOR_DEFAULT_ALIGNMENT
        : nAlignment;
}

void SetAllocationItems( benchmark::State &state )
{
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

} // namespace

static void BM_AllocatorIsValid( benchmark::State &state )
{
    const allocator_t *pAllocator = Allocator_GetSystem();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Allocator_IsValid( pAllocator ) );
    }

    SetAllocationItems( state );
}

static void BM_AllocatorAllocateFree( benchmark::State &state )
{
    const allocator_t *pAllocator = Allocator_GetSystem();
    const usize cbSize = static_cast<usize>( state.range( 0 ) );
    const usize nAlignment = static_cast<usize>( state.range( 1 ) );

    for ( auto _ : state ) {
        void *pMemory = Allocator_Allocate( pAllocator, cbSize, nAlignment );
        if ( pMemory == nullptr ) {
            state.SkipWithError( "System allocator returned nullptr." );
            return;
        }

        benchmark::DoNotOptimize( pMemory );
        benchmark::ClobberMemory();
        Allocator_Free( pAllocator, pMemory, cbSize, nAlignment );
    }

    SetAllocationItems( state );
}

static void BM_DirectAlignedNewDelete( benchmark::State &state )
{
    const usize cbSize = static_cast<usize>( state.range( 0 ) );
    const usize nAlignment = EffectiveAlignment(
        static_cast<usize>( state.range( 1 ) ) );
    const auto alignment = static_cast<std::align_val_t>( nAlignment );

    for ( auto _ : state ) {
        void *pMemory = ::operator new( cbSize, alignment, std::nothrow );
        if ( pMemory == nullptr ) {
            state.SkipWithError( "Aligned operator new returned nullptr." );
            return;
        }

        benchmark::DoNotOptimize( pMemory );
        benchmark::ClobberMemory();
        ::operator delete( pMemory, alignment );
    }

    SetAllocationItems( state );
}

static void BM_AllocatorReallocateLifecycle( benchmark::State &state )
{
    const allocator_t *pAllocator = Allocator_GetSystem();
    const usize cbOldSize = static_cast<usize>( state.range( 0 ) );
    const usize cbNewSize = static_cast<usize>( state.range( 1 ) );

    for ( auto _ : state ) {
        void *pMemory = Allocator_Allocate( pAllocator, cbOldSize );
        if ( pMemory == nullptr ) {
            state.SkipWithError( "Initial allocation returned nullptr." );
            return;
        }

        Cy_MemSet( pMemory, 0xA5u, cbOldSize );
        pMemory = Allocator_Reallocate(
            pAllocator,
            pMemory,
            cbOldSize,
            cbNewSize );
        if ( pMemory == nullptr ) {
            state.SkipWithError( "Fallback reallocation returned nullptr." );
            return;
        }

        benchmark::DoNotOptimize( pMemory );
        benchmark::ClobberMemory();
        Allocator_Free( pAllocator, pMemory, cbNewSize );
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbOldSize ) );
}

static void BM_AllocatorAllocateZeroed( benchmark::State &state )
{
    const allocator_t *pAllocator = Allocator_GetSystem();
    const usize cbSize = static_cast<usize>( state.range( 0 ) );

    for ( auto _ : state ) {
        void *pMemory = Allocator_AllocateZeroed( pAllocator, cbSize );
        if ( pMemory == nullptr ) {
            state.SkipWithError( "Zeroed allocation returned nullptr." );
            return;
        }

        benchmark::DoNotOptimize( pMemory );
        benchmark::ClobberMemory();
        Allocator_Free( pAllocator, pMemory, cbSize );
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbSize ) );
}

static void BM_AllocatorOwnedRelease( benchmark::State &state )
{
    const allocator_t *pAllocator = Allocator_GetSystem();
    constexpr usize cbSize = 256u;

    for ( auto _ : state ) {
        owned_allocation_t allocation{};
        if ( !Allocator_AllocateOwned(
                 &allocation,
                 pAllocator,
                 cbSize ) ) {
            state.SkipWithError( "Owned allocation returned nullptr." );
            return;
        }

        benchmark::DoNotOptimize( allocation.pData );
        benchmark::ClobberMemory();
        Allocator_FreeOwned( &allocation );
        benchmark::DoNotOptimize( allocation.pData );
    }

    SetAllocationItems( state );
}

BENCHMARK( BM_AllocatorIsValid );

BENCHMARK( BM_AllocatorAllocateFree )
    ->Args( { 16, static_cast<i64>( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) } )
    ->Args( { 64, static_cast<i64>( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) } )
    ->Args( { 256, static_cast<i64>( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) } )
    ->Args( { 4096, static_cast<i64>( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) } )
    ->Args( { 65536, static_cast<i64>( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) } )
    ->Args( { 256, 64 } )
    ->Args( { 4096, 256 } );

BENCHMARK( BM_DirectAlignedNewDelete )
    ->Args( { 16, static_cast<i64>( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) } )
    ->Args( { 64, static_cast<i64>( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) } )
    ->Args( { 256, static_cast<i64>( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) } )
    ->Args( { 4096, static_cast<i64>( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) } )
    ->Args( { 65536, static_cast<i64>( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) } )
    ->Args( { 256, 64 } )
    ->Args( { 4096, 256 } );

BENCHMARK( BM_AllocatorReallocateLifecycle )
    ->Args( { 64, 256 } )
    ->Args( { 256, 4096 } )
    ->Args( { 4096, 65536 } );

BENCHMARK( BM_AllocatorAllocateZeroed )
    ->Arg( 64 )
    ->Arg( 256 )
    ->Arg( 4096 )
    ->Arg( 65536 );

BENCHMARK( BM_AllocatorOwnedRelease );
