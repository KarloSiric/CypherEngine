//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier2/CypherCommon_ProjectData_Bench.cpp
//  Purpose: Benchmarks Tier2 project and settings data contracts.
//  Details: Measures schema-registry lookup and validation plus typed project and
//           settings decoding from already parsed CYKV documents.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_ProjectManifest.h"
#include "CypherCommon_SchemaRegistry.h"
#include "CypherCommon_Settings.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

key_value_document_t *ParseDocument( string_view_t source ) noexcept
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    if ( pDocument == nullptr ) {
        return nullptr;
    }
    const key_value_parse_result_t result = KeyValue_ParseText(
        source,
        {},
        pDocument );
    if ( result.status != key_value_parse_status_t::OK ) {
        KeyValue_DestroyDocument( pDocument );
        return nullptr;
    }
    return pDocument;
}

key_value_document_t *MakeProjectDocument() noexcept
{
    return ParseDocument( StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.project" 1
{
    id = "reap"
    name = "REAP"
    start_map = "maps/facility.cymap"
    search_paths = ["game", "engine", "mods/base", "workshop"]
}
)cykv" ) );
}

key_value_document_t *MakeSettingsDocument() noexcept
{
    return ParseDocument( StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.settings" 1
{
    display = {
        width = 2560
        height = 1440
        mode = "borderless"
        vsync = false
    }
}
)cykv" ) );
}

void BM_SchemaRegistry_Find( benchmark::State &state )
{
    const schema_descriptor_t *storage[8]{};
    schema_registry_t registry{};
    if ( !SchemaRegistry_Init( &registry, storage, 8u ) ||
         SchemaRegistry_Register( &registry, ProjectSchema_V1() ) !=
             schema_registry_status_t::OK ||
         SchemaRegistry_Register( &registry, SettingsSchema_V1() ) !=
             schema_registry_status_t::OK ) {
        state.SkipWithError( "Schema registry setup failed." );
        return;
    }

    const string_view_t schemaId = StringView_FromCString( "cypher.project" );
    for ( auto _ : state ) {
        const schema_descriptor_t *pSchema = SchemaRegistry_Find(
            &registry,
            schemaId,
            CY_PROJECT_SCHEMA_VERSION );
        benchmark::DoNotOptimize( pSchema );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_SchemaRegistry_ValidateProject( benchmark::State &state )
{
    key_value_document_t *pDocument = MakeProjectDocument();
    const schema_descriptor_t *storage[8]{};
    schema_registry_t registry{};
    if ( pDocument == nullptr ||
         !SchemaRegistry_Init( &registry, storage, 8u ) ||
         SchemaRegistry_Register( &registry, ProjectSchema_V1() ) !=
             schema_registry_status_t::OK ) {
        KeyValue_DestroyDocument( pDocument );
        state.SkipWithError( "Schema validation benchmark setup failed." );
        return;
    }

    schema_diagnostic_t diagnostics[8]{};
    for ( auto _ : state ) {
        schema_validation_result_t result = SchemaRegistry_ValidateDocument(
            &registry,
            pDocument,
            {},
            diagnostics,
            8u );
        benchmark::DoNotOptimize( result );
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    KeyValue_DestroyDocument( pDocument );
}

void BM_ProjectManifest_Decode( benchmark::State &state )
{
    key_value_document_t *pDocument = MakeProjectDocument();
    if ( pDocument == nullptr ) {
        state.SkipWithError( "Project document parsing failed." );
        return;
    }

    schema_diagnostic_t diagnostics[8]{};
    for ( auto _ : state ) {
        project_manifest_view_t manifest{};
        project_manifest_decode_result_t result = ProjectManifest_Decode(
            pDocument,
            {},
            diagnostics,
            8u,
            &manifest );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( manifest );
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    KeyValue_DestroyDocument( pDocument );
}

void BM_CypherSettings_Decode( benchmark::State &state )
{
    key_value_document_t *pDocument = MakeSettingsDocument();
    if ( pDocument == nullptr ) {
        state.SkipWithError( "Settings document parsing failed." );
        return;
    }

    schema_diagnostic_t diagnostics[8]{};
    for ( auto _ : state ) {
        cypher_settings_t settings{};
        cypher_settings_decode_result_t result = CypherSettings_Decode(
            pDocument,
            {},
            diagnostics,
            8u,
            &settings );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( settings );
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    KeyValue_DestroyDocument( pDocument );
}

} // namespace

BENCHMARK( BM_SchemaRegistry_Find );
BENCHMARK( BM_SchemaRegistry_ValidateProject );
BENCHMARK( BM_ProjectManifest_Decode );
BENCHMARK( BM_CypherSettings_Decode );
