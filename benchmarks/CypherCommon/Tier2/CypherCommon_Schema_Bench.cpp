//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier2/CypherCommon_Schema_Bench.cpp
//  Purpose: Benchmarks allocation-free Tier2 CYKV schema validation.
//  Details: Parsing occurs during fixture setup so measured iterations cover only
//           descriptor traversal, constraints, paths, and diagnostic collection.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_ProjectSchema.h"
#include "CypherCommon_Schema.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

class schema_fixture_t : public benchmark::Fixture {
public:
    void TearDown( const benchmark::State & ) override
    {
        KeyValue_DestroyDocument( m_pDocument );
        m_pDocument = nullptr;
    }

protected:
    void Parse( const char *pSource )
    {
        m_pDocument = KeyValue_CreateDocument( {} );
        if ( m_pDocument == nullptr ) {
            m_status = key_value_parse_status_t::OUT_OF_MEMORY;
            return;
        }
        m_status = KeyValue_ParseText(
            StringView_FromCString( pSource ),
            {},
            m_pDocument ).status;
    }

    key_value_document_t *m_pDocument{ nullptr };
    key_value_parse_status_t m_status{
        key_value_parse_status_t::INVALID_ARGUMENT
    };
};

class valid_project_fixture_t : public schema_fixture_t {
public:
    void SetUp( const benchmark::State & ) override
    {
        Parse(
            "@cykv 1\n@schema \"cypher.project\" 1\n"
            "{ id = \"reap\" name = \"REAP\" "
            "start_map = \"maps/facility.cymap\" "
            "search_paths = [\"game\", \"engine\", \"mods/base\",] }" );
    }
};

class invalid_project_fixture_t : public schema_fixture_t {
public:
    void SetUp( const benchmark::State & ) override
    {
        Parse(
            "@cykv 1\n@schema \"cypher.project\" 1\n"
            "{ name = \"\" start_map = 7 "
            "search_paths = [\"game\", 9] extra = true }" );
    }
};

BENCHMARK_F( valid_project_fixture_t, ValidateValidProject )(
    benchmark::State &state )
{
    if ( m_status != key_value_parse_status_t::OK ) {
        state.SkipWithError( "CYKV benchmark fixture failed to parse" );
        return;
    }

    schema_diagnostic_t diagnostics[8]{};
    for ( auto _ : state ) {
        schema_validation_result_t result = Schema_ValidateDocument(
            ProjectSchema_V1(),
            m_pDocument,
            {},
            diagnostics,
            sizeof( diagnostics ) / sizeof( diagnostics[0] ) );
        benchmark::DoNotOptimize( result );
        benchmark::ClobberMemory();
    }
}

BENCHMARK_F( invalid_project_fixture_t, ValidateInvalidProject )(
    benchmark::State &state )
{
    if ( m_status != key_value_parse_status_t::OK ) {
        state.SkipWithError( "CYKV benchmark fixture failed to parse" );
        return;
    }

    schema_diagnostic_t diagnostics[16]{};
    for ( auto _ : state ) {
        schema_validation_result_t result = Schema_ValidateDocument(
            ProjectSchema_V1(),
            m_pDocument,
            {},
            diagnostics,
            sizeof( diagnostics ) / sizeof( diagnostics[0] ) );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( diagnostics );
        benchmark::ClobberMemory();
    }
}

} // namespace
