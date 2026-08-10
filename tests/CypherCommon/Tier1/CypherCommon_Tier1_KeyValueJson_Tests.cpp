//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_KeyValueJson_Tests.cpp
//  Purpose: Tests strict JSON interchange for CYKV documents.
//  Details: Covers strict grammar rejection, typed values, Unicode escaping,
//           deterministic output, root values, and unsupported binary nodes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueJson.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "KeyValue JSON reads strict typed values and Unicode escapes",
           "[CypherCommon][Tier1][KeyValueJson]" )
{
    constexpr const char *pJson =
        "{\"name\":\"Cypher \\u03a9\",\"values\":[1,-2,1.5,true,null]}";
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    const key_value_parse_result_t parsed = KeyValueJson_Parse(
        StringView_FromCString( pJson ),
        {},
        pDocument );
    REQUIRE( parsed.status == key_value_parse_status_t::OK );

    const key_value_t *pRoot = KeyValue_Root( pDocument );
    const key_value_t *pName = KeyValue_Find(
        pRoot,
        StringView_FromCString( "name" ) );
    string_view_t name{};
    REQUIRE( KeyValue_GetString( pName, &name ) );
    REQUIRE( StringView_Equals(
        name,
        StringView_FromCString( "Cypher \xCE\xA9" ) ) );

    const key_value_t *pValues = KeyValue_Find(
        pRoot,
        StringView_FromCString( "values" ) );
    REQUIRE( KeyValue_ChildCount( pValues ) == 5u );
    REQUIRE( KeyValue_Type( KeyValue_ChildAt( pValues, 0u ) ) ==
             key_value_type_t::I64 );
    REQUIRE( KeyValue_Type( KeyValue_ChildAt( pValues, 1u ) ) ==
             key_value_type_t::I64 );
    REQUIRE( KeyValue_Type( KeyValue_ChildAt( pValues, 2u ) ) ==
             key_value_type_t::F64 );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "KeyValue JSON rejects CYKV extensions",
           "[CypherCommon][Tier1][KeyValueJson]" )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    constexpr const char *invalidJson[] = {
        "{ // comment\n \"a\": 1 }",
        "{ \"a\": 1, }",
        "{ a: 1 }",
        "{ \"a\": +1 }",
        "{ \"a\": 0x10 }",
        "{ \"a\": hex\"00\" }",
        "{ \"a\": 01 }"
    };
    for ( const char *pJson : invalidJson ) {
        const key_value_parse_result_t result = KeyValueJson_Parse(
            StringView_FromCString( pJson ),
            {},
            pDocument );
        REQUIRE( result.status != key_value_parse_status_t::OK );
    }

    const key_value_parse_result_t duplicate = KeyValueJson_Parse(
        StringView_FromCString( "{\"a\":1,\"a\":2}" ),
        {},
        pDocument );
    REQUIRE( duplicate.status == key_value_parse_status_t::DUPLICATE_KEY );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "KeyValue JSON writes valid escaped text and root values",
           "[CypherCommon][Tier1][KeyValueJson]" )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    REQUIRE( KeyValue_SetString(
        pDocument,
        KeyValue_Root( pDocument ),
        StringView_FromCString( "Omega \xCE\xA9" ) ) );

    key_value_json_options_t options{};
    options.bPretty = CY_FALSE;
    options.bEscapeNonAscii = CY_TRUE;
    char output[64]{};
    const key_value_write_result_t written = KeyValueJson_Write(
        KeyValue_Root( pDocument ),
        options,
        output,
        sizeof( output ) );
    REQUIRE( written.status == key_value_write_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "\"Omega \\u03a9\"" ) ) );

    key_value_document_t *pRoundTrip = KeyValue_CreateDocument( {} );
    REQUIRE( pRoundTrip != nullptr );
    REQUIRE( KeyValueJson_Parse(
        StringView_FromCString( output ),
        options,
        pRoundTrip ).status == key_value_parse_status_t::OK );

    KeyValue_DestroyDocument( pRoundTrip );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "KeyValue JSON refuses native binary values",
           "[CypherCommon][Tier1][KeyValueJson]" )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    const byte value = 0x42u;
    REQUIRE( KeyValue_SetBinary(
        pDocument,
        KeyValue_Root( pDocument ),
        { &value, 1u } ) );

    char output[32]{};
    const key_value_write_result_t written = KeyValueJson_Write(
        KeyValue_Root( pDocument ),
        {},
        output,
        sizeof( output ) );
    REQUIRE( written.status == key_value_write_status_t::INVALID_DOCUMENT );
    REQUIRE( output[0] == '\0' );

    KeyValue_DestroyDocument( pDocument );
}
