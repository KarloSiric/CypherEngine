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

#include <string>

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

TEST_CASE( "Tier2 decodes V2 projects with a game profile and settings blocks",
           "[CypherCommon][Tier2][ProjectManifest][V2]" )
{
    key_value_document_t *pDocument = ParseProject( R"cykv(@cykv 1
@schema "cypher.project" 2
{
    id = "reap"
    name = "REAP"
    game = "reap"
    maps_path = "content/maps"
    search_paths = [ "game", "engine" ]
    settings = { editor = { grid = 8 } }
    map_defaults = { gravity = 800.0 }
    written_by_a_newer_tool = { anything = true }
}
)cykv" );
    project_manifest_view_t manifest{};
    const project_manifest_decode_result_t result =
        ProjectManifest_Decode( pDocument, {}, nullptr, 0u, &manifest );
    INFO( ProjectManifest_StatusName( result.status ) );
    REQUIRE( ProjectManifest_DecodeSucceeded( result ) );
    REQUIRE( manifest.nVersion == 2u );
    REQUIRE( ViewEquals( manifest.game, "reap" ) );
    REQUIRE( ViewEquals( manifest.mapsPath, "content/maps" ) );
    REQUIRE( manifest.startMap.cchLength == 0u ); // Optional in V2.
    REQUIRE( manifest.nSearchPaths == 2u );
    REQUIRE( KeyValue_Find( manifest.pSettings, StringView_FromCString( "editor" ) ) != nullptr );
    REQUIRE( KeyValue_Find( manifest.pMapDefaults, StringView_FromCString( "gravity" ) ) != nullptr );
    KeyValue_DestroyDocument( pDocument );

    // Defaults: no game, maps under "maps", no settings blocks.
    pDocument = ParseProject( "@cykv 1\n@schema \"cypher.project\" 2\n{ id = \"reap\" name = \"REAP\" }" );
    manifest = {};
    REQUIRE( ProjectManifest_DecodeSucceeded( ProjectManifest_Decode( pDocument, {}, nullptr, 0u, &manifest ) ) );
    REQUIRE( manifest.game.cchLength == 0u );
    REQUIRE( ViewEquals( manifest.mapsPath, "maps" ) );
    REQUIRE( manifest.pSettings == nullptr );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 V2 projects validate identity and content roots strictly",
           "[CypherCommon][Tier2][ProjectManifest][V2]" )
{
    struct case_t {
        const char *pBody;
        project_manifest_status_t expected;
    };
    const case_t cases[]{
        { "{ id = \"reap\" name = \"REAP\" game = \"Bad Game\" }", project_manifest_status_t::INVALID_GAME },
        { "{ id = \"reap\" name = \"REAP\" maps_path = \"../maps\" }", project_manifest_status_t::INVALID_MAPS_PATH },
        { "{ id = \"reap\" name = \"REAP\" start_map = \"maps/a.cytilemap\" }", project_manifest_status_t::INVALID_START_MAP },
        { "{ name = \"REAP\" }", project_manifest_status_t::INVALID_DOCUMENT },
    };
    for ( const case_t &c : cases ) {
        const std::string source = std::string( "@cykv 1\n@schema \"cypher.project\" 2\n" ) + c.pBody;
        key_value_document_t *pDocument = ParseProject( source.c_str() );
        project_manifest_view_t manifest{};
        CAPTURE( c.pBody );
        REQUIRE( ProjectManifest_Decode( pDocument, {}, nullptr, 0u, &manifest ).status == c.expected );
        KeyValue_DestroyDocument( pDocument );
    }

    key_value_document_t *pDocument = ParseProject( "@cykv 1\n@schema \"cypher.project\" 3\n{ id = \"reap\" name = \"REAP\" }" );
    project_manifest_view_t manifest{};
    REQUIRE( ProjectManifest_Decode( pDocument, {}, nullptr, 0u, &manifest ).status ==
             project_manifest_status_t::UNSUPPORTED_VERSION );
    KeyValue_DestroyDocument( pDocument );

    REQUIRE( Schema_CheckDescriptor( ProjectSchema_V2() ) == schema_descriptor_status_t::OK );
    REQUIRE( ProjectManifest_Identity().nCurrentVersion == CY_PROJECT_SCHEMA_CURRENT_VERSION );
}
