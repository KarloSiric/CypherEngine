//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_KeyValue_Bench.cpp
//  Purpose: Benchmarks the native CYKV text and binary data paths.
//  Details: Measures complete transactional parsing, deterministic text writing,
//           binary packing, and validated transactional unpacking.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValuePack.h"
#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_KeyValueWriter.h"

#include <benchmark/benchmark.h>

#include <vector>

using namespace cypher::common;

namespace
{

constexpr char CY_KEY_VALUE_BENCH_SOURCE[] = R"cykv(@cykv 1
@schema "cypher.benchmark" 1

{
    metadata = {
        name = "arena_training"
        author = "Cypher tools"
        version = 12
        enabled = true
    }
    renderer = {
        backend = "opengl"
        exposure = 1.25
        shadows = true
        probes = ["hall", "arena", "spawn"]
    }
    player = {
        health = 100
        speed = 320.0
        origin = [128.0, -64.0, 32.0]
        inventory = ["shotgun", "launcher", "medkit"]
    }
    entities = [
        { classname = "light" intensity = 850.0 color = "#ffd8a0" },
        { classname = "spawn" team = 1 active = true },
        { classname = "trigger" target = "arena_gate" once = false }
    ]
    payload = hex"00112233445566778899aabbccddeeff"
})cykv";

key_value_document_t *ParseBenchDocument()
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    if ( pDocument == nullptr ) return nullptr;
    const key_value_parse_result_t parsed = KeyValue_ParseText(
        StringView_FromRange(
            CY_KEY_VALUE_BENCH_SOURCE,
            sizeof( CY_KEY_VALUE_BENCH_SOURCE ) - 1u ),
        {},
        pDocument );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        KeyValue_DestroyDocument( pDocument );
        return nullptr;
    }
    return pDocument;
}

void SetBytes( benchmark::State &state, usize cbPerIteration )
{
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbPerIteration ) );
}

} // namespace

static void BM_KeyValue_ParseText( benchmark::State &state )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    if ( pDocument == nullptr ) {
        state.SkipWithError( "Unable to create CYKV document" );
        return;
    }
    const string_view_t source = StringView_FromRange(
        CY_KEY_VALUE_BENCH_SOURCE,
        sizeof( CY_KEY_VALUE_BENCH_SOURCE ) - 1u );
    for ( auto _ : state ) {
        key_value_parse_result_t parsed = KeyValue_ParseText(
            source,
            {},
            pDocument );
        benchmark::DoNotOptimize( parsed.status );
        benchmark::ClobberMemory();
    }
    SetBytes( state, source.cchLength );
    KeyValue_DestroyDocument( pDocument );
}

static void BM_KeyValue_WriteText( benchmark::State &state )
{
    key_value_document_t *pDocument = ParseBenchDocument();
    if ( pDocument == nullptr ) {
        state.SkipWithError( "Unable to parse CYKV benchmark document" );
        return;
    }
    const key_value_write_result_t measured = KeyValue_WriteText(
        KeyValue_Root( pDocument ),
        {},
        nullptr,
        0u );
    std::vector<char> output( measured.cchRequired + 1u );
    for ( auto _ : state ) {
        key_value_write_result_t written = KeyValue_WriteText(
            KeyValue_Root( pDocument ),
            {},
            output.data(),
            output.size() );
        benchmark::DoNotOptimize( written.cchWritten );
        benchmark::ClobberMemory();
    }
    SetBytes( state, measured.cchRequired );
    KeyValue_DestroyDocument( pDocument );
}

static void BM_KeyValue_PackWrite( benchmark::State &state )
{
    key_value_document_t *pDocument = ParseBenchDocument();
    if ( pDocument == nullptr ) {
        state.SkipWithError( "Unable to parse CYKV benchmark document" );
        return;
    }
    const usize cbRequired = KeyValuePack_RequiredSize(
        KeyValue_Root( pDocument ) );
    std::vector<byte> output( cbRequired );
    for ( auto _ : state ) {
        key_value_pack_result_t written = KeyValuePack_Write(
            KeyValue_Root( pDocument ),
            { output.data(), output.size() } );
        benchmark::DoNotOptimize( written.cbWritten );
        benchmark::ClobberMemory();
    }
    SetBytes( state, cbRequired );
    KeyValue_DestroyDocument( pDocument );
}

static void BM_KeyValue_PackRead( benchmark::State &state )
{
    key_value_document_t *pSource = ParseBenchDocument();
    key_value_document_t *pDest = KeyValue_CreateDocument( {} );
    if ( pSource == nullptr || pDest == nullptr ) {
        if ( pSource != nullptr ) KeyValue_DestroyDocument( pSource );
        if ( pDest != nullptr ) KeyValue_DestroyDocument( pDest );
        state.SkipWithError( "Unable to create CYKV benchmark documents" );
        return;
    }
    const usize cbRequired = KeyValuePack_RequiredSize(
        KeyValue_Root( pSource ) );
    std::vector<byte> packed( cbRequired );
    const key_value_pack_result_t written = KeyValuePack_Write(
        KeyValue_Root( pSource ),
        { packed.data(), packed.size() } );
    if ( written.status != key_value_pack_status_t::OK ) {
        KeyValue_DestroyDocument( pDest );
        KeyValue_DestroyDocument( pSource );
        state.SkipWithError( "Unable to pack CYKV benchmark document" );
        return;
    }
    for ( auto _ : state ) {
        key_value_pack_result_t read = KeyValuePack_Read(
            { packed.data(), packed.size() },
            {},
            pDest );
        benchmark::DoNotOptimize( read.cbRead );
        benchmark::ClobberMemory();
    }
    SetBytes( state, packed.size() );
    KeyValue_DestroyDocument( pDest );
    KeyValue_DestroyDocument( pSource );
}

BENCHMARK( BM_KeyValue_ParseText );
BENCHMARK( BM_KeyValue_WriteText );
BENCHMARK( BM_KeyValue_PackWrite );
BENCHMARK( BM_KeyValue_PackRead );
