//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/ResourceSystem/CypherCommon_ResourceSystem_Bench.cpp
//  Purpose: Benchmarks stable resource identifiers and packed handles.
//  Details: Measurements cover type hashing, path hashing, text conversion,
//           and the runtime handle operations expected on resource hot paths.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ResourceHandle.h"
#include "CypherCommon_ResourceId.h"

#include <benchmark/benchmark.h>

#include <string>

using namespace cypher::common;

static void BM_ResourceTypeId( benchmark::State &state )
{
    string_view_t typeName = StringView_FromCString( "animated_material" );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( typeName );
        resource_type_id_t id = ResourceTypeId_FromName( typeName );
        benchmark::DoNotOptimize( id );
    }
}

BENCHMARK( BM_ResourceTypeId );

static void BM_ResourceIdFromPath( benchmark::State &state )
{
    const usize cchPath = static_cast<usize>( state.range( 0 ) );
    std::string path( cchPath, 'a' );
    path.replace( 0u, 7u, "assets/" );
    string_view_t pathView{ path.data(), path.size() };
    const resource_type_id_t type = ResourceTypeId_FromName(
        StringView_FromCString( "material" ) );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pathView );
        resource_id_t id = ResourceId_FromPath( pathView, type );
        benchmark::DoNotOptimize( id );
    }
    state.SetBytesProcessed(
        state.iterations() * static_cast<benchmark::IterationCount>( cchPath ) );
}

BENCHMARK( BM_ResourceIdFromPath )->Arg( 16 )->Arg( 64 )->Arg( 256 )->Arg( 1024 );

static void BM_ResourceIdTextRoundTrip( benchmark::State &state )
{
    const resource_id_t source{ 0x0123456789ABCDEFull };
    for ( auto _ : state ) {
        char text[CY_RESOURCE_ID_STRING_CAPACITY]{};
        resource_id_t parsed{};
        usize cchWritten = ResourceId_ToString(
            source, text, sizeof( text ) );
        bool_t bParsed = ResourceId_FromString(
            { text, cchWritten }, &parsed );
        benchmark::DoNotOptimize( bParsed );
        benchmark::DoNotOptimize( parsed );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_ResourceIdTextRoundTrip );

static void BM_ResourceHandleMakeAndUnpack( benchmark::State &state )
{
    resource_slot_t iSlot = 1u;
    for ( auto _ : state ) {
        resource_handle_t handle{};
        bool_t bMade = ResourceHandle_TryMake(
            iSlot, 17u, 9u, &handle );
        resource_handle_parts_t parts = ResourceHandle_Unpack( handle );
        benchmark::DoNotOptimize( bMade );
        benchmark::DoNotOptimize( parts );
        ++iSlot;
    }
}

BENCHMARK( BM_ResourceHandleMakeAndUnpack );
