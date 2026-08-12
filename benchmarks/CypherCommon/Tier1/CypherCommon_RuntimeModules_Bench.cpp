//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_RuntimeModules_Bench.cpp
//  Purpose: Benchmarks Tier1 runtime coordination primitives.
//  Details: Measures interface lookup, intrusive lists, reference counts, reliable
//           timing, sequence tracking, instance logs, undo history, and UUID text.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_InstanceLog.h"
#include "CypherCommon_Interface.h"
#include "CypherCommon_IntrusiveList.h"
#include "CypherCommon_RefCount.h"
#include "CypherCommon_ReliableTimer.h"
#include "CypherCommon_SequenceNumber.h"
#include "CypherCommon_UndoRedo.h"
#include "CypherCommon_UniqueId.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

struct interface_bench_state_t {
    u32 nValue{ 42u };
};

void *CreateBenchInterface(
    const interface_id_t &,
    void *pUserData ) noexcept
{
    return pUserData;
}

void ReleaseBenchInterface( void *, void * ) noexcept
{
}

void BM_InterfaceRegistry_Lookup( benchmark::State &state )
{
    interface_registry_t *pRegistry = InterfaceRegistry_Create(
        Allocator_GetSystem(),
        8u );
    interface_bench_state_t interfaceState{};
    const interface_factory_desc_t factory{
        { StringView_FromCString( "CypherRenderer" ), 3u, 7u },
        CreateBenchInterface,
        ReleaseBenchInterface,
        &interfaceState
    };
    if ( pRegistry == nullptr ||
         !InterfaceRegistry_Register( pRegistry, factory ) ) {
        InterfaceRegistry_Destroy( pRegistry );
        state.SkipWithError( "Interface registry setup failed." );
        return;
    }

    const interface_id_t requested{
        StringView_FromCString( "CypherRenderer" ),
        3u,
        5u
    };
    for ( auto _ : state ) {
        void *pInterface = InterfaceRegistry_CreateInterface(
            pRegistry,
            requested );
        benchmark::DoNotOptimize( pInterface );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    InterfaceRegistry_Destroy( pRegistry );
}

void BM_InterfaceRegistry_RegisterUnregister( benchmark::State &state )
{
    interface_registry_t *pRegistry = InterfaceRegistry_Create(
        Allocator_GetSystem(),
        8u );
    interface_bench_state_t interfaceState{};
    const interface_factory_desc_t factory{
        { StringView_FromCString( "CypherPhysics" ), 1u, 0u },
        CreateBenchInterface,
        ReleaseBenchInterface,
        &interfaceState
    };
    if ( pRegistry == nullptr ) {
        state.SkipWithError( "Interface registry creation failed." );
        return;
    }

    for ( auto _ : state ) {
        bool_t registered = InterfaceRegistry_Register( pRegistry, factory );
        bool_t unregistered = InterfaceRegistry_Unregister(
            pRegistry,
            factory.provided.name,
            factory.provided.nMajorVersion );
        benchmark::DoNotOptimize( registered );
        benchmark::DoNotOptimize( unregistered );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    InterfaceRegistry_Destroy( pRegistry );
}

void BM_IntrusiveList_LinkUnlink( benchmark::State &state )
{
    intrusive_list_t list{};
    intrusive_list_node_t node{};
    IntrusiveList_Init( &list );

    for ( auto _ : state ) {
        IntrusiveList_PushBack( &list, &node );
        benchmark::DoNotOptimize( IntrusiveList_Front( &list ) );
        IntrusiveList_Remove( &list, &node );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_IntrusiveList_Traverse256( benchmark::State &state )
{
    constexpr usize nNodeCount = 256u;
    intrusive_list_t list{};
    std::array<intrusive_list_node_t, nNodeCount> nodes{};
    IntrusiveList_Init( &list );
    for ( auto &node : nodes ) {
        IntrusiveList_PushBack( &list, &node );
    }

    for ( auto _ : state ) {
        usize nVisited = 0u;
        for ( const intrusive_list_node_t *pNode = IntrusiveList_Front( &list );
              pNode != nullptr;
              pNode = IntrusiveList_Next( &list, pNode ) ) {
            benchmark::DoNotOptimize( pNode );
            ++nVisited;
        }
        benchmark::DoNotOptimize( nVisited );
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nNodeCount ) );
    IntrusiveList_Clear( &list );
}

void BM_RefCount_AddRelease( benchmark::State &state )
{
    ref_count_t count{};
    RefCount_Init( &count );

    for ( auto _ : state ) {
        u32 nAdded = RefCount_AddRef( &count );
        u32 nRemaining = RefCount_Release( &count );
        benchmark::DoNotOptimize( nAdded );
        benchmark::DoNotOptimize( nRemaining );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_ReliableTimer_RttUpdate( benchmark::State &state )
{
    reliable_timer_t timer{};
    if ( !ReliableTimer_Init( &timer, {} ) ) {
        state.SkipWithError( "Reliable timer initialization failed." );
        return;
    }

    u64 nSample = 0u;
    for ( auto _ : state ) {
        const f64 flRtt = 0.020 +
            static_cast<f64>( nSample & 15u ) * 0.0005;
        bool_t accepted = ReliableTimer_AddRttSample( &timer, flRtt );
        benchmark::DoNotOptimize( accepted );
        ++nSample;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_ReliableTimer_DeadlineCycle( benchmark::State &state )
{
    reliable_timer_t timer{};
    if ( !ReliableTimer_Init( &timer, {} ) ||
         !ReliableTimer_AddRttSample( &timer, 0.025 ) ) {
        state.SkipWithError( "Reliable timer setup failed." );
        return;
    }

    f64 flNow = 100.0;
    for ( auto _ : state ) {
        ReliableTimer_Arm( &timer, flNow );
        bool_t bExpired = ReliableTimer_HasExpired(
            &timer,
            timer.flDeadlineSeconds );
        ReliableTimer_Disarm( &timer );
        benchmark::DoNotOptimize( bExpired );
        flNow += 0.001;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_SequenceAck32_RecordContains( benchmark::State &state )
{
    sequence_ack32_t acknowledgements{};
    u32 nSequence = 1u;

    for ( auto _ : state ) {
        bool_t bRecorded = SequenceAck32_Record(
            &acknowledgements,
            nSequence );
        bool_t bContained = SequenceAck32_Contains(
            &acknowledgements,
            nSequence - 1u );
        benchmark::DoNotOptimize( bRecorded );
        benchmark::DoNotOptimize( bContained );
        ++nSequence;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

error_code_t ApplyUndoDelta(
    binary_block_t payload,
    void *pUserData ) noexcept
{
    auto *pValue = static_cast<i32 *>( pUserData );
    *pValue -= static_cast<i32>( payload.pData[0] );
    return CY_ERROR_OK;
}

error_code_t ApplyRedoDelta(
    binary_block_t payload,
    void *pUserData ) noexcept
{
    auto *pValue = static_cast<i32 *>( pUserData );
    *pValue += static_cast<i32>( payload.pData[0] );
    return CY_ERROR_OK;
}

void BM_UndoRedo_Cycle( benchmark::State &state )
{
    undo_history_t *pHistory = UndoRedo_Create(
        { Allocator_GetSystem(), 8u, 64u } );
    i32 nValue = 1;
    byte delta = 1u;
    const undo_operation_desc_t operation{
        1u,
        0u,
        StringView_FromCString( "Move" ),
        BinaryBlock_FromData( &delta, sizeof( delta ) ),
        ApplyUndoDelta,
        ApplyRedoDelta,
        &nValue
    };
    if ( pHistory == nullptr || !UndoRedo_Push( pHistory, operation ) ) {
        UndoRedo_Destroy( pHistory );
        state.SkipWithError( "Undo history setup failed." );
        return;
    }

    for ( auto _ : state ) {
        error_code_t undoResult = UndoRedo_Undo( pHistory );
        error_code_t redoResult = UndoRedo_Redo( pHistory );
        benchmark::DoNotOptimize( undoResult );
        benchmark::DoNotOptimize( redoResult );
        benchmark::DoNotOptimize( nValue );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    UndoRedo_Destroy( pHistory );
}

void BM_InstanceLog_AddBounded( benchmark::State &state )
{
    instance_log_t *pLog = InstanceLog_Create(
        { Allocator_GetSystem(), 256u, 64u * CY_KIB } );
    if ( pLog == nullptr ) {
        state.SkipWithError( "Instance log creation failed." );
        return;
    }

    const string_view_t category = StringView_FromCString( "resource" );
    const string_view_t message = StringView_FromCString(
        "Compiled resource dependency changed." );
    for ( auto _ : state ) {
        bool_t added = InstanceLog_Add(
            pLog,
            log_level_t::Info,
            category,
            message );
        benchmark::DoNotOptimize( added );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    InstanceLog_Destroy( pLog );
}

void BM_UniqueId_ParseFormat( benchmark::State &state )
{
    const string_view_t text = StringView_FromCString(
        "00112233-4455-4677-8899-aabbccddeeff" );
    char output[CY_UNIQUE_ID_STRING_CAPACITY]{};

    for ( auto _ : state ) {
        unique_id_t id{};
        bool_t parsed = UniqueId_FromString( text, &id );
        usize cchWritten = UniqueId_ToString( id, output, sizeof( output ) );
        benchmark::DoNotOptimize( parsed );
        benchmark::DoNotOptimize( cchWritten );
        benchmark::DoNotOptimize( output );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

} // namespace

BENCHMARK( BM_InterfaceRegistry_Lookup );
BENCHMARK( BM_InterfaceRegistry_RegisterUnregister );
BENCHMARK( BM_IntrusiveList_LinkUnlink );
BENCHMARK( BM_IntrusiveList_Traverse256 );
BENCHMARK( BM_RefCount_AddRelease );
BENCHMARK( BM_ReliableTimer_RttUpdate );
BENCHMARK( BM_ReliableTimer_DeadlineCycle );
BENCHMARK( BM_SequenceAck32_RecordContains );
BENCHMARK( BM_UndoRedo_Cycle );
BENCHMARK( BM_InstanceLog_AddBounded );
BENCHMARK( BM_UniqueId_ParseFormat );
