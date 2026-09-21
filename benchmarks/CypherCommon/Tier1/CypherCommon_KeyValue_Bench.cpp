//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_KeyValue_Bench.cpp
//  Purpose: Benchmarks the native CYKV text and binary data paths.
//  Details: Measures complete transactional parsing, CYKV 2 typed-definition
//           expansion and source resolution, deterministic text writing,
//           canonical semantic hashing, binary packing, and validated unpacking.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//  - Expanded CYKV 2 definition and source benchmarks on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValuePack.h"
#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_KeyValueSource.h"
#include "CypherCommon_KeyValueWriter.h"

#include <benchmark/benchmark.h>

#include <string>
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

constexpr char CY_KEY_VALUE_BENCH_TYPED_DEFINITION_PREFIX[] = R"cykv(@cykv 2
@schema "cypher.benchmark" 1

#define ENABLED true
#define COUNT 42u
#define OFFSET -7
#define RATIO 1.5
#define TITLE "typed development record"
#define PAYLOAD hex"00112233445566778899aabbccddeeff"
#define VECTOR [1.0, -2.0, 3.0]
#define RECORD {
    enabled = $ENABLED
    count = $COUNT
    offset = $OFFSET
    ratio = $RATIO
    title = $TITLE
    payload = $PAYLOAD
    vector = $VECTOR
}

{
    records = [
)cykv";

constexpr char CY_KEY_VALUE_BENCH_TYPED_DEFINITION_SUFFIX[] = R"cykv(    ]
}
)cykv";

constexpr char CY_KEY_VALUE_BENCH_INCLUDED_SOURCE[] = R"cykv(@cykv 2
@schema "cypher.shared" 1
{
    palette = {
        accent = [0.25, 0.5, 1.0]
        warning = [1.0, 0.25, 0.0625]
    }
    threshold = 7u
}
)cykv";

constexpr char CY_KEY_VALUE_BENCH_BASE_SOURCE[] = R"cykv(@cykv 2
@schema "cypher.benchmark" 1
{
    name = "inherited"
    health = 100u
    movement = {
        speed = 320.0
        acceleration = 10.0
        enabled = false
    }
    tags = ["benchmark", "base"]
}
)cykv";

constexpr char CY_KEY_VALUE_BENCH_ROOT_SOURCE[] = R"cykv(@cykv 2
@schema "cypher.benchmark" 1

#include "shared.cydf" as shared
#base "base.cydf"
#define LOCAL_SCALE 2.0

{
    name = "resolved"
    scale = $LOCAL_SCALE
    accent = $shared.palette.accent
    movement = {
        enabled = true
    }
}
)cykv";

struct key_value_bench_source_entry_t {
    const char *pRequest{ nullptr };
    const char *pCanonical{ nullptr };
    const char *pText{ nullptr };
};

struct key_value_bench_source_fixture_t {
    const key_value_bench_source_entry_t *pEntries{ nullptr };
    usize nEntries{ 0u };
};

key_value_source_open_status_t OpenBenchSource(
    string_view_t,
    string_view_t referencedPath,
    key_value_source_view_t *pOutSource,
    void *pUserData ) noexcept
{
    if ( pOutSource == nullptr || pUserData == nullptr ) {
        return key_value_source_open_status_t::IO_ERROR;
    }

    const auto &fixture =
        *static_cast<const key_value_bench_source_fixture_t *>( pUserData );
    for ( usize iEntry = 0u; iEntry < fixture.nEntries; ++iEntry ) {
        const key_value_bench_source_entry_t &entry = fixture.pEntries[iEntry];
        if ( StringView_Equals(
                 referencedPath,
                 StringView_FromCString( entry.pRequest ) ) ) {
            *pOutSource = {
                StringView_FromCString( entry.pCanonical ),
                StringView_FromCString( entry.pText ),
                const_cast<key_value_bench_source_entry_t *>( &entry )
            };
            return key_value_source_open_status_t::OK;
        }
    }
    return key_value_source_open_status_t::NOT_FOUND;
}

void ReleaseBenchSource(
    const key_value_source_view_t *,
    void * ) noexcept
{
}

std::string BuildTypedDefinitionSource( usize nExpansions )
{
    constexpr char expansion[] = "        $RECORD,\n";
    std::string source{ CY_KEY_VALUE_BENCH_TYPED_DEFINITION_PREFIX };
    source.reserve(
        source.size() + nExpansions * ( sizeof( expansion ) - 1u ) +
        sizeof( CY_KEY_VALUE_BENCH_TYPED_DEFINITION_SUFFIX ) );
    for ( usize iExpansion = 0u;
          iExpansion < nExpansions;
          ++iExpansion ) {
        source.append( expansion, sizeof( expansion ) - 1u );
    }
    source += CY_KEY_VALUE_BENCH_TYPED_DEFINITION_SUFFIX;
    return source;
}

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

static void BM_KeyValue_ParseTypedDefinitions( benchmark::State &state )
{
    const usize nExpansions = static_cast<usize>( state.range( 0 ) );
    const std::string sourceStorage =
        BuildTypedDefinitionSource( nExpansions );
    const string_view_t source = StringView_FromRange(
        sourceStorage.data(),
        sourceStorage.size() );
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    if ( pDocument == nullptr ) {
        state.SkipWithError( "Unable to create CYKV document" );
        return;
    }

    const key_value_parse_result_t preflight = KeyValue_ParseText(
        source,
        {},
        pDocument );
    if ( preflight.status != key_value_parse_status_t::OK ) {
        KeyValue_DestroyDocument( pDocument );
        state.SkipWithError( "Unable to parse CYKV 2 typed definitions" );
        return;
    }

    for ( auto _ : state ) {
        key_value_parse_result_t parsed = KeyValue_ParseText(
            source,
            {},
            pDocument );
        benchmark::DoNotOptimize( parsed.status );
        benchmark::DoNotOptimize( KeyValue_Root( pDocument ) );
        benchmark::ClobberMemory();
    }
    SetBytes( state, source.cchLength );
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nExpansions ) );
    KeyValue_DestroyDocument( pDocument );
}

static void BM_KeyValue_ParseSourceInMemory( benchmark::State &state )
{
    const key_value_bench_source_entry_t entries[]{
        {
            "shared.cydf",
            "bench/shared.cydf",
            CY_KEY_VALUE_BENCH_INCLUDED_SOURCE
        },
        {
            "base.cydf",
            "bench/base.cydf",
            CY_KEY_VALUE_BENCH_BASE_SOURCE
        }
    };
    key_value_bench_source_fixture_t fixture{
        entries,
        sizeof( entries ) / sizeof( entries[0] )
    };
    key_value_source_options_t options{};
    options.pfnOpen = OpenBenchSource;
    options.pfnRelease = ReleaseBenchSource;
    options.pUserData = &fixture;

    const string_view_t rootPath =
        StringView_FromCString( "bench/root.cydf" );
    const string_view_t rootSource = StringView_FromRange(
        CY_KEY_VALUE_BENCH_ROOT_SOURCE,
        sizeof( CY_KEY_VALUE_BENCH_ROOT_SOURCE ) - 1u );
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    if ( pDocument == nullptr ) {
        state.SkipWithError( "Unable to create CYKV document" );
        return;
    }

    const key_value_source_result_t preflight = KeyValue_ParseSource(
        rootPath,
        rootSource,
        options,
        pDocument );
    if ( preflight.status != key_value_source_status_t::OK ) {
        KeyValue_DestroyDocument( pDocument );
        state.SkipWithError( "Unable to resolve in-memory CYKV 2 sources" );
        return;
    }

    usize cbResolved = preflight.cbAggregateSources;
    for ( auto _ : state ) {
        key_value_source_result_t resolved = KeyValue_ParseSource(
            rootPath,
            rootSource,
            options,
            pDocument );
        cbResolved = resolved.cbAggregateSources;
        benchmark::DoNotOptimize( resolved.status );
        benchmark::DoNotOptimize( resolved.nUniqueSources );
        benchmark::DoNotOptimize( resolved.nDependencyEdges );
        benchmark::DoNotOptimize( KeyValue_Root( pDocument ) );
        benchmark::ClobberMemory();
    }
    SetBytes( state, cbResolved );
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( preflight.nUniqueSources ) );
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

static void BM_KeyValue_HashCanonicalDocument( benchmark::State &state )
{
    key_value_document_t *pDocument = ParseBenchDocument();
    if ( pDocument == nullptr ) {
        state.SkipWithError( "Unable to parse CYKV benchmark document" );
        return;
    }
    usize cbHashed = 0u;
    for ( auto _ : state ) {
        key_value_canonical_hash_result_t hashed =
            KeyValue_HashCanonicalDocument( pDocument );
        if ( hashed.status != key_value_write_status_t::OK ) {
            state.SkipWithError( "Unable to hash canonical CYKV document" );
            break;
        }
        cbHashed = hashed.cbHashed;
        benchmark::DoNotOptimize( hashed.hash.low );
        benchmark::DoNotOptimize( hashed.hash.high );
        benchmark::ClobberMemory();
    }
    SetBytes( state, cbHashed );
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
BENCHMARK( BM_KeyValue_ParseTypedDefinitions )
    ->Arg( 1 )
    ->Arg( 8 )
    ->Arg( 32 );
BENCHMARK( BM_KeyValue_ParseSourceInMemory );
BENCHMARK( BM_KeyValue_WriteText );
BENCHMARK( BM_KeyValue_HashCanonicalDocument );
BENCHMARK( BM_KeyValue_PackWrite );
BENCHMARK( BM_KeyValue_PackRead );
