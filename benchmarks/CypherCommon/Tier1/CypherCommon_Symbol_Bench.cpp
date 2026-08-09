//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Symbol_Bench.cpp
//  Purpose: Benchmarks symbol lookup, duplicate interning, and resolution.
//  Details: A populated table measures the common name-to-symbol and symbol-to-name
//           paths independently of fixture construction and allocator growth.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Symbol.h"

#include <benchmark/benchmark.h>

#include <array>
#include <cstdio>

using namespace cypher::common;

namespace
{

constexpr usize CY_BENCH_SYMBOL_COUNT = 1024u;

struct symbol_fixture_t {
    std::array<std::array<char, 40u>, CY_BENCH_SYMBOL_COUNT> names{};
    symbol_table_t *pTable{ nullptr };
    symbol_t finalSymbol{};

    symbol_fixture_t()
    {
        symbol_table_desc_t desc{};
        desc.pAllocator = Allocator_GetSystem();
        desc.nInitialCapacity = CY_BENCH_SYMBOL_COUNT;
        pTable = SymbolTable_Create( desc );
        if ( pTable == nullptr ) {
            return;
        }
        for ( usize iName = 0u; iName < names.size(); ++iName ) {
            std::snprintf(
                names[iName].data(),
                names[iName].size(),
                "entity.component.type_%04zu",
                iName );
            finalSymbol = SymbolTable_Intern(
                pTable,
                StringView_FromCString( names[iName].data() ) );
            if ( !Symbol_IsValid( finalSymbol ) ) {
                SymbolTable_Destroy( pTable );
                pTable = nullptr;
                return;
            }
        }
    }

    ~symbol_fixture_t()
    {
        SymbolTable_Destroy( pTable );
    }
};

} // namespace

static void BM_SymbolFind( benchmark::State &state )
{
    symbol_fixture_t fixture{};
    if ( fixture.pTable == nullptr ) {
        state.SkipWithError( "Symbol fixture creation failed." );
        return;
    }
    const string_view_t name = StringView_FromCString(
        fixture.names.back().data() );
    for ( auto _ : state ) {
        symbol_t symbol = SymbolTable_Find( fixture.pTable, name );
        benchmark::DoNotOptimize( symbol );
    }
}

BENCHMARK( BM_SymbolFind );

static void BM_SymbolInternDuplicate( benchmark::State &state )
{
    symbol_fixture_t fixture{};
    if ( fixture.pTable == nullptr ) {
        state.SkipWithError( "Symbol fixture creation failed." );
        return;
    }
    const string_view_t name = StringView_FromCString(
        fixture.names[CY_BENCH_SYMBOL_COUNT / 2u].data() );
    for ( auto _ : state ) {
        symbol_t symbol = SymbolTable_Intern( fixture.pTable, name );
        benchmark::DoNotOptimize( symbol );
    }
}

BENCHMARK( BM_SymbolInternDuplicate );

static void BM_SymbolResolve( benchmark::State &state )
{
    symbol_fixture_t fixture{};
    if ( fixture.pTable == nullptr ) {
        state.SkipWithError( "Symbol fixture creation failed." );
        return;
    }
    for ( auto _ : state ) {
        string_view_t text = SymbolTable_Resolve(
            fixture.pTable,
            fixture.finalSymbol );
        benchmark::DoNotOptimize( text );
    }
}

BENCHMARK( BM_SymbolResolve );
