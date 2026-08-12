//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_DataModules_Bench.cpp
//  Purpose: Benchmarks Tier1 data processing and structured-data modules.
//  Details: Measures representative parsing, lookup, diff, expression, SoA, and
//           sorting workloads with allocation and setup policy made explicit.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Config.h"
#include "CypherCommon_DataManager.h"
#include "CypherCommon_Diff.h"
#include "CypherCommon_ExpressionEvaluator.h"
#include "CypherCommon_HeapSort.h"
#include "CypherCommon_KeyValueJson.h"
#include "CypherCommon_SoaContainer.h"
#include "CypherCommon_Sort.h"
#include "CypherCommon_StringFormat.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <vector>

using namespace cypher::common;

namespace
{

convar_desc_t MakeBenchConVar( const char *pName, const char *pDefault ) noexcept
{
    return {
        StringView_FromCString( pName ),
        {},
        convar_type_t::I64,
        StringView_FromCString( pDefault ),
        StringView_FromCString( "0" ),
        StringView_FromCString( "100000" ),
        CONVAR_FLAG_NONE,
        nullptr,
        nullptr
    };
}

void BM_Config_Load( benchmark::State &state )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    if ( pSystem == nullptr ) {
        state.SkipWithError( "Command system creation failed." );
        return;
    }
    constexpr const char *names[]{
        "r_width", "r_height", "r_vsync", "s_volume"
    };
    constexpr const char *defaults[]{ "1920", "1080", "1", "80" };
    for ( usize iValue = 0u; iValue < 4u; ++iValue ) {
        const convar_register_result_t registered = CommandSystem_RegisterConVar(
            pSystem,
            MakeBenchConVar( names[iValue], defaults[iValue] ) );
        if ( !Cy_ErrorSucceeded( registered.error ) ) {
            CommandSystem_Destroy( pSystem );
            state.SkipWithError( "ConVar registration failed." );
            return;
        }
    }

    const config_source_t source{
        StringView_FromCString( "benchmark.cfg" ),
        StringView_FromCString(
            "# renderer settings\n"
            "r_width 2560\n"
            "r_height 1440\n"
            "r_vsync 0\n"
            "s_volume 75\n" ),
        CONFIG_FLAG_STOP_ON_ERROR
    };
    for ( auto _ : state ) {
        config_load_result_t result = Config_Load( source, pSystem, {} );
        benchmark::DoNotOptimize( result );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( source.text.cchLength ) );
    CommandSystem_Destroy( pSystem );
}

void BM_DataManager_Find( benchmark::State &state )
{
    constexpr usize nEntryCount = 256u;
    data_manager_t *pManager = DataManager_Create(
        Allocator_GetSystem(),
        nEntryCount );
    if ( pManager == nullptr ) {
        state.SkipWithError( "Data manager creation failed." );
        return;
    }

    std::array<std::array<char, 32>, nEntryCount> names{};
    std::array<u32, nEntryCount> values{};
    for ( usize iEntry = 0u; iEntry < nEntryCount; ++iEntry ) {
        const string_format_result_t formatted = StringFormat_Printf(
            names[iEntry].data(),
            names[iEntry].size(),
            "resource.%03zu",
            iEntry );
        values[iEntry] = static_cast<u32>( iEntry );
        if ( formatted.status != string_format_status_t::OK ||
             !DataManager_Register(
                 pManager,
                 {
                     StringView_FromCString( names[iEntry].data() ),
                     &values[iEntry],
                     nullptr,
                     nullptr
                 } ) ) {
            DataManager_Destroy( pManager );
            state.SkipWithError( "Data manager population failed." );
            return;
        }
    }

    usize iEntry = 0u;
    for ( auto _ : state ) {
        void *pValue = DataManager_Find(
            pManager,
            StringView_FromCString(
                names[iEntry & ( nEntryCount - 1u )].data() ) );
        benchmark::DoNotOptimize( pValue );
        iEntry += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    DataManager_Destroy( pManager );
}

void BM_Diff_Generate64KiB( benchmark::State &state )
{
    constexpr usize cbData = 64u * CY_KIB;
    std::vector<byte> source( cbData );
    std::vector<byte> target( cbData );
    for ( usize iByte = 0u; iByte < cbData; ++iByte ) {
        source[iByte] = static_cast<byte>( ( iByte * 31u ) & 0xFFu );
        target[iByte] = source[iByte];
    }
    for ( usize iByte = 8192u; iByte < 12288u; ++iByte ) {
        target[iByte] ^= static_cast<byte>( 0x5Au );
    }

    binary_diff_t diff{};
    if ( !Diff_Init( &diff, Allocator_GetSystem() ) ) {
        state.SkipWithError( "Diff initialization failed." );
        return;
    }
    for ( auto _ : state ) {
        diff_status_t status = Diff_Generate(
            { source.data(), source.size() },
            { target.data(), target.size() },
            &diff );
        benchmark::DoNotOptimize( status );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbData ) );
    Diff_Shutdown( &diff );
}

void BM_ExpressionEvaluator_Evaluate( benchmark::State &state )
{
    const string_view_t expression = StringView_FromCString(
        "((17 + 5) * 3 >= 60) && (128 / 4 == 32) || false" );
    for ( auto _ : state ) {
        expression_result_t result = ExpressionEvaluator_Evaluate( expression );
        benchmark::DoNotOptimize( result );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( expression.cchLength ) );
}

void BM_KeyValueJson_Parse( benchmark::State &state )
{
    const string_view_t json = StringView_FromCString(
        "{\"name\":\"facility\",\"revision\":42,\"enabled\":true,"
        "\"transform\":[1.0,2.0,3.0],\"tags\":[\"arena\",\"retro\","
        "\"networked\"],\"settings\":{\"quality\":3,\"seed\":9127}}" );
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    if ( pDocument == nullptr ) {
        state.SkipWithError( "KeyValue document creation failed." );
        return;
    }
    for ( auto _ : state ) {
        key_value_parse_result_t result = KeyValueJson_Parse(
            json,
            {},
            pDocument );
        benchmark::DoNotOptimize( result );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( json.cchLength ) );
    KeyValue_DestroyDocument( pDocument );
}

void BM_SoaContainer_ColumnTraversal( benchmark::State &state )
{
    constexpr usize nRows = 4096u;
    const soa_column_desc_t columns[]{
        { sizeof( u32 ), alignof( u32 ) },
        { sizeof( f32 ), alignof( f32 ) },
        { sizeof( f32 ), alignof( f32 ) }
    };
    soa_container_t container{};
    if ( !SoaContainer_Init(
             &container,
             { columns, 3u, Allocator_GetSystem(), nRows } ) ||
         !SoaContainer_Resize( &container, nRows ) ) {
        state.SkipWithError( "SoA container setup failed." );
        return;
    }
    auto *pIds = static_cast<u32 *>( SoaContainer_Column( &container, 0u ) );
    auto *pX = static_cast<f32 *>( SoaContainer_Column( &container, 1u ) );
    auto *pY = static_cast<f32 *>( SoaContainer_Column( &container, 2u ) );
    for ( usize iRow = 0u; iRow < nRows; ++iRow ) {
        pIds[iRow] = static_cast<u32>( iRow );
        pX[iRow] = static_cast<f32>( iRow ) * 0.25f;
        pY[iRow] = static_cast<f32>( iRow ) * -0.125f;
    }

    for ( auto _ : state ) {
        f64 sum = 0.0;
        for ( usize iRow = 0u; iRow < nRows; ++iRow ) {
            sum += static_cast<f64>( pIds[iRow] ) + pX[iRow] + pY[iRow];
        }
        benchmark::DoNotOptimize( sum );
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nRows ) );
    SoaContainer_Shutdown( &container );
}

template <bool_t bHeapSort>
void BM_Sort4096( benchmark::State &state )
{
    constexpr usize nCount = 4096u;
    std::array<u32, nCount> baseline{};
    std::array<u32, nCount> working{};
    for ( usize iValue = 0u; iValue < nCount; ++iValue ) {
        baseline[iValue] = static_cast<u32>(
            ( iValue * 2654435761u ) ^ ( iValue >> 3u ) );
    }

    for ( auto _ : state ) {
        state.PauseTiming();
        working = baseline;
        state.ResumeTiming();
        if constexpr ( bHeapSort ) {
            HeapSort_Sort( Span_Make( working.data(), working.size() ) );
        } else {
            Sort_Unstable( Span_Make( working.data(), working.size() ) );
        }
        benchmark::DoNotOptimize( working );
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount ) );
}

void BM_SortStable4096( benchmark::State &state )
{
    constexpr usize nCount = 4096u;
    std::array<u32, nCount> baseline{};
    std::array<u32, nCount> working{};
    std::array<byte, sizeof( u32 ) * nCount> scratch{};
    for ( usize iValue = 0u; iValue < nCount; ++iValue ) {
        baseline[iValue] = static_cast<u32>(
            ( iValue * 2246822519u ) ^ ( iValue >> 5u ) );
    }

    for ( auto _ : state ) {
        state.PauseTiming();
        working = baseline;
        state.ResumeTiming();
        bool_t sorted = Sort_Stable(
            Span_Make( working.data(), working.size() ),
            less_t<u32>{},
            Span_Make( scratch.data(), scratch.size() ) );
        benchmark::DoNotOptimize( sorted );
        benchmark::DoNotOptimize( working );
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount ) );
}

} // namespace

BENCHMARK( BM_Config_Load );
BENCHMARK( BM_DataManager_Find );
BENCHMARK( BM_Diff_Generate64KiB );
BENCHMARK( BM_ExpressionEvaluator_Evaluate );
BENCHMARK( BM_KeyValueJson_Parse );
BENCHMARK( BM_SoaContainer_ColumnTraversal );
BENCHMARK_TEMPLATE( BM_Sort4096, CY_FALSE );
BENCHMARK_TEMPLATE( BM_Sort4096, CY_TRUE );
BENCHMARK( BM_SortStable4096 );
