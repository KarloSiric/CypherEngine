//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_ConVar_Bench.cpp
//  Purpose: Benchmarks typed console-variable conversion.
//  Details: Measures primitive parsing, descriptor range enforcement, and
//           round-trip floating-point formatting without registry overhead.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ConVar.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_ConVarParseI64( benchmark::State &state )
{
    const string_view_t text = StringView_FromCString( "-2147483648" );
    for ( auto _ : state ) {
        convar_value_t value{};
        benchmark::DoNotOptimize( ConVar_ParseValue(
            convar_type_t::I64,
            text,
            &value ) );
        benchmark::DoNotOptimize( value.value.data.iValue );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( text.cchLength ) );
}

void BM_ConVarParseF64WithBounds( benchmark::State &state )
{
    convar_desc_t desc{};
    desc.name = StringView_FromCString( "player.speed" );
    desc.type = convar_type_t::F64;
    desc.defaultValue = StringView_FromCString( "320" );
    desc.minValue = StringView_FromCString( "1" );
    desc.maxValue = StringView_FromCString( "1000" );
    const string_view_t text = StringView_FromCString( "640.25" );

    for ( auto _ : state ) {
        convar_value_t value{};
        benchmark::DoNotOptimize( ConVar_ParseValueForDesc(
            desc,
            text,
            &value ) );
        benchmark::DoNotOptimize( value.value.data.flValue );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( text.cchLength ) );
}

void BM_ConVarFormatF64( benchmark::State &state )
{
    const convar_value_t value{ Variant_FromF64( 12345.6789012345 ) };
    for ( auto _ : state ) {
        char text[128]{};
        benchmark::DoNotOptimize(
            ConVar_FormatValue( value, text, sizeof( text ) ) );
        benchmark::DoNotOptimize( text );
    }
}

} // namespace

BENCHMARK( BM_ConVarParseI64 );
BENCHMARK( BM_ConVarParseF64WithBounds );
BENCHMARK( BM_ConVarFormatF64 );
