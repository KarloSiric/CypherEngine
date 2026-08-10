//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier2/CypherCommon_Tier2_ProjectManifest_Tests.cpp
//  Purpose: Tests typed decoding and semantic policy for project manifests.
//  Details: Covers successful borrowed views, stable identifiers, canonical virtual
//           paths, map extensions, duplicate mounts, and transactional failure.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_ProjectManifest.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

key_value_document_t *ParseProject( const char *pBody )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    const key_value_parse_result_t result = KeyValue_ParseText(
        StringView_FromCString( pBody ),
        {},
        pDocument );
    REQUIRE( result.status == key_value_parse_status_t::OK );
    return pDocument;
}

bool_t ViewEquals( string_view_t view, const char *pText )
{
    return StringView_Equals( view, StringView_FromCString( pText ) );
}

} // namespace

TEST_CASE( "Tier2 decodes a canonical project manifest",
           "[CypherCommon][Tier2][ProjectManifest]" )
{
    key_value_document_t *pDocument = ParseProject( R"cykv(@cykv 1
@schema "cypher.project" 1
{
    id = "reap"
    name = "REAP"
    start_map = "maps/facility.cymap"
    search_paths = ["game", "engine", "mods/base"]
}
)cykv" );

    project_manifest_view_t manifest{};
    schema_diagnostic_t diagnostics[8]{};
    const project_manifest_decode_result_t result = ProjectManifest_Decode(
        pDocument,
        {},
        diagnostics,
        sizeof( diagnostics ) / sizeof( diagnostics[0] ),
        &manifest );

    REQUIRE( ProjectManifest_DecodeSucceeded( result ) );
    REQUIRE( result.validation.nErrors == 0u );
    REQUIRE( ViewEquals( manifest.id, "reap" ) );
    REQUIRE( ViewEquals( manifest.name, "REAP" ) );
    REQUIRE( ViewEquals( manifest.startMap, "maps/facility.cymap" ) );
    REQUIRE( manifest.nSearchPaths == 3u );
    REQUIRE( ViewEquals( manifest.searchPaths[0], "game" ) );
    REQUIRE( ViewEquals( manifest.searchPaths[1], "engine" ) );
    REQUIRE( ViewEquals( manifest.searchPaths[2], "mods/base" ) );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 project manifests enforce semantic identifiers and paths",
           "[CypherCommon][Tier2][ProjectManifest]" )
{
    SECTION( "project identifiers are stable lowercase ASCII identifiers" )
    {
        key_value_document_t *pDocument = ParseProject(
            "@cykv 1\n@schema \"cypher.project\" 1\n"
            "{ id = \"REAP\" name = \"REAP\" "
            "start_map = \"maps/a.cymap\" }" );
        project_manifest_view_t manifest{};
        const project_manifest_decode_result_t result = ProjectManifest_Decode(
            pDocument,
            {},
            nullptr,
            0u,
            &manifest );
        REQUIRE( result.status ==
                 project_manifest_status_t::INVALID_PROJECT_ID );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "startup maps use canonical virtual paths" )
    {
        key_value_document_t *pDocument = ParseProject(
            "@cykv 1\n@schema \"cypher.project\" 1\n"
            "{ id = \"reap\" name = \"REAP\" "
            "start_map = \"Maps/a.cymap\" }" );
        project_manifest_view_t manifest{};
        const project_manifest_decode_result_t result = ProjectManifest_Decode(
            pDocument,
            {},
            nullptr,
            0u,
            &manifest );
        REQUIRE( result.status ==
                 project_manifest_status_t::INVALID_START_MAP );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "startup maps use cymap resources" )
    {
        key_value_document_t *pDocument = ParseProject(
            "@cykv 1\n@schema \"cypher.project\" 1\n"
            "{ id = \"reap\" name = \"REAP\" "
            "start_map = \"maps/a.txt\" }" );
        project_manifest_view_t manifest{};
        const project_manifest_decode_result_t result = ProjectManifest_Decode(
            pDocument,
            {},
            nullptr,
            0u,
            &manifest );
        REQUIRE( result.status ==
                 project_manifest_status_t::INVALID_START_MAP );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "search paths are canonical" )
    {
        key_value_document_t *pDocument = ParseProject(
            "@cykv 1\n@schema \"cypher.project\" 1\n"
            "{ id = \"reap\" name = \"REAP\" "
            "start_map = \"maps/a.cymap\" "
            "search_paths = [\"game\", \"mods/../base\"] }" );
        project_manifest_view_t manifest{};
        const project_manifest_decode_result_t result = ProjectManifest_Decode(
            pDocument,
            {},
            nullptr,
            0u,
            &manifest );
        REQUIRE( result.status ==
                 project_manifest_status_t::INVALID_SEARCH_PATH );
        REQUIRE( result.iSearchPath == 1u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "search paths are unique" )
    {
        key_value_document_t *pDocument = ParseProject(
            "@cykv 1\n@schema \"cypher.project\" 1\n"
            "{ id = \"reap\" name = \"REAP\" "
            "start_map = \"maps/a.cymap\" "
            "search_paths = [\"game\", \"game\"] }" );
        project_manifest_view_t manifest{};
        const project_manifest_decode_result_t result = ProjectManifest_Decode(
            pDocument,
            {},
            nullptr,
            0u,
            &manifest );
        REQUIRE( result.status ==
                 project_manifest_status_t::DUPLICATE_SEARCH_PATH );
        REQUIRE( result.iSearchPath == 1u );
        KeyValue_DestroyDocument( pDocument );
    }
}

TEST_CASE( "Tier2 project decoding commits output only on success",
           "[CypherCommon][Tier2][ProjectManifest][Transaction]" )
{
    key_value_document_t *pDocument = ParseProject(
        "@cykv 1\n@schema \"cypher.project\" 1\n"
        "{ id = \"reap\" name = \"REAP\" start_map = 7 }" );
    project_manifest_view_t manifest{};
    manifest.id = StringView_FromCString( "unchanged" );
    manifest.nSearchPaths = 7u;

    schema_diagnostic_t diagnostic{};
    const project_manifest_decode_result_t result = ProjectManifest_Decode(
        pDocument,
        {},
        &diagnostic,
        1u,
        &manifest );

    REQUIRE( result.status == project_manifest_status_t::INVALID_DOCUMENT );
    REQUIRE( diagnostic.code == schema_diagnostic_code_t::TYPE_MISMATCH );
    REQUIRE( ViewEquals( manifest.id, "unchanged" ) );
    REQUIRE( manifest.nSearchPaths == 7u );
    REQUIRE( StringView_Equals(
        StringView_FromCString( ProjectManifest_StatusName( result.status ) ),
        StringView_FromCString( "INVALID_DOCUMENT" ) ) );
    KeyValue_DestroyDocument( pDocument );
}
