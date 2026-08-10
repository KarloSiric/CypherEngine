//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_KeyValueParser_Tests.cpp
//  Purpose: Tests CYKV 1 document parsing and conformance rules.
//  Details: Covers headers, exact scalar types, comments, multiline strings,
//           malformed source, bounded input, and transactional failure behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueParser.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

key_value_document_t *CreateDocument()
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    return pDocument;
}

key_value_t *FindRequired( key_value_t *pObject, const char *pName )
{
    key_value_t *pValue = KeyValue_Find(
        pObject,
        StringView_FromCString( pName ) );
    REQUIRE( pValue != nullptr );
    return pValue;
}

key_value_parse_status_t ParseStatus(
    const char *pSource,
    const key_value_parse_options_t &options = {} )
{
    key_value_document_t *pDocument = CreateDocument();
    const key_value_parse_status_t status = KeyValue_ParseText(
        StringView_FromCString( pSource ),
        options,
        pDocument ).status;
    KeyValue_DestroyDocument( pDocument );
    return status;
}

} // namespace

TEST_CASE( "CYKV parser reads a complete version 1 document",
           "[CypherCommon][Tier1][CYKV][Conformance]" )
{
    constexpr const char *pSource = R"cykv(@cykv 1
@schema "cypher.test" 3

{
    // Native CYKV permits comments and readable unquoted keys.
    title = "Cypher\nEngine"
    enabled = true
    unsigned_value = 0x2au
    signed_value = -7
    signed_positive = +9
    ratio = 1.25
    payload = hex"00ff80"
    values = [null, false, "last",]
    nested = { path = "data/shaders" }
    message = """
        first line
          indented line
        """
}
)cykv";

    key_value_document_t *pDocument = CreateDocument();
    const key_value_parse_result_t result = KeyValue_ParseText(
        StringView_FromCString( pSource ),
        {},
        pDocument );
    REQUIRE( result.status == key_value_parse_status_t::OK );

    const key_value_document_header_t header =
        KeyValue_DocumentHeader( pDocument );
    REQUIRE( header.nLanguageVersion == CYKV_LANGUAGE_VERSION );
    REQUIRE( header.nSchemaVersion == 3u );
    REQUIRE( StringView_Equals(
        header.schemaId,
        StringView_FromCString( "cypher.test" ) ) );

    key_value_t *pRoot = KeyValue_Root( pDocument );
    REQUIRE( KeyValue_Type( pRoot ) == key_value_type_t::OBJECT );
    REQUIRE( KeyValue_ChildCount( pRoot ) == 10u );

    string_view_t title{};
    REQUIRE( KeyValue_GetString( FindRequired( pRoot, "title" ), &title ) );
    REQUIRE( StringView_Equals(
        title,
        StringView_FromCString( "Cypher\nEngine" ) ) );

    bool_t enabled = CY_FALSE;
    REQUIRE( KeyValue_GetBool(
        FindRequired( pRoot, "enabled" ),
        &enabled ) );
    REQUIRE( enabled );

    u64 unsignedValue = 0u;
    REQUIRE( KeyValue_GetU64(
        FindRequired( pRoot, "unsigned_value" ),
        &unsignedValue ) );
    REQUIRE( unsignedValue == 42u );

    i64 signedValue = 0;
    REQUIRE( KeyValue_GetI64(
        FindRequired( pRoot, "signed_value" ),
        &signedValue ) );
    REQUIRE( signedValue == -7 );
    REQUIRE( KeyValue_GetI64(
        FindRequired( pRoot, "signed_positive" ),
        &signedValue ) );
    REQUIRE( signedValue == 9 );

    f64 ratio = 0.0;
    REQUIRE( KeyValue_GetF64( FindRequired( pRoot, "ratio" ), &ratio ) );
    REQUIRE( ratio == 1.25 );

    binary_block_t payload{};
    REQUIRE( KeyValue_GetBinary(
        FindRequired( pRoot, "payload" ),
        &payload ) );
    REQUIRE( payload.cbSize == 3u );
    REQUIRE( payload.pData[0] == 0x00u );
    REQUIRE( payload.pData[1] == 0xFFu );
    REQUIRE( payload.pData[2] == 0x80u );

    key_value_t *pValues = FindRequired( pRoot, "values" );
    REQUIRE( KeyValue_Type( pValues ) == key_value_type_t::ARRAY );
    REQUIRE( KeyValue_ChildCount( pValues ) == 3u );

    string_view_t message{};
    REQUIRE( KeyValue_GetString(
        FindRequired( pRoot, "message" ),
        &message ) );
    REQUIRE( StringView_Equals(
        message,
        StringView_FromCString( "first line\n  indented line" ) ) );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV parser enforces headers and structural separators",
           "[CypherCommon][Tier1][CYKV][Conformance]" )
{
    REQUIRE( ParseStatus( "{}" ) ==
             key_value_parse_status_t::INVALID_HEADER );
    REQUIRE( ParseStatus( "\n@cykv 1\n@schema \"cypher.test\" 1\n{}" ) ==
             key_value_parse_status_t::INVALID_HEADER );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n{}" ) ==
        key_value_parse_status_t::UNSUPPORTED_VERSION );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"invalid\" 1\n{}" ) ==
        key_value_parse_status_t::INVALID_SCHEMA );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ a = 1, b = 2 }" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ a = 1 a = 2 }" ) ==
        key_value_parse_status_t::DUPLICATE_KEY );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n[1, 2]" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ a = [1, 2,] }" ) ==
        key_value_parse_status_t::OK );
}

TEST_CASE( "CYKV parser preserves exact numeric types",
           "[CypherCommon][Tier1][CYKV][Conformance]" )
{
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ value = 42 }" ) ==
        key_value_parse_status_t::OK );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ value = 42u }" ) ==
        key_value_parse_status_t::OK );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ value = 01 }" ) !=
        key_value_parse_status_t::OK );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ value = 1. }" ) !=
        key_value_parse_status_t::OK );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ value = .5 }" ) !=
        key_value_parse_status_t::OK );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ value = 1_000.25 }" ) ==
        key_value_parse_status_t::OK );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ value = 18446744073709551615u }" ) ==
        key_value_parse_status_t::OK );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ value = 18446744073709551615 }" ) !=
        key_value_parse_status_t::OK );
}

TEST_CASE( "CYKV parser enforces bounded input and containers",
           "[CypherCommon][Tier1][CYKV][Limits]" )
{
    constexpr const char *pSource =
        "@cykv 1\n@schema \"cypher.test\" 1\n{ a = 1 b = 2 }";
    key_value_parse_options_t options{};

    options.cbMaxInput = 8u;
    REQUIRE( ParseStatus( pSource, options ) ==
             key_value_parse_status_t::INPUT_LIMIT );

    options = {};
    options.nMaxNodes = 2u;
    REQUIRE( ParseStatus( pSource, options ) ==
             key_value_parse_status_t::NODE_LIMIT );

    options = {};
    options.cbMaxStringData = 5u;
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{}",
        options ) == key_value_parse_status_t::STRING_LIMIT );

    options = {};
    options.nMaxContainerValues = 1u;
    REQUIRE( ParseStatus( pSource, options ) ==
             key_value_parse_status_t::CONTAINER_LIMIT );

    options = {};
    options.nMaxDepth = 1u;
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ a = { b = { c = 1 } } }",
        options ) == key_value_parse_status_t::DEPTH_LIMIT );

    options = {};
    options.nMaxCommentDepth = 1u;
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{/* outer /* inner */ */}",
        options ) == key_value_parse_status_t::COMMENT_DEPTH_LIMIT );
}

TEST_CASE( "CYKV parser failure preserves the destination document",
           "[CypherCommon][Tier1][CYKV][Transaction]" )
{
    key_value_document_t *pDocument = CreateDocument();
    REQUIRE( KeyValue_SetDocumentHeader(
        pDocument,
        {
            CYKV_LANGUAGE_VERSION,
            StringView_FromCString( "cypher.stable" ),
            7u
        } ) );
    REQUIRE( KeyValue_SetRootType(
        pDocument,
        key_value_type_t::OBJECT ) );
    key_value_t *pStable = KeyValue_ObjectInsert(
        pDocument,
        KeyValue_Root( pDocument ),
        StringView_FromCString( "stable" ),
        key_value_type_t::STRING );
    REQUIRE( pStable != nullptr );
    REQUIRE( KeyValue_SetString(
        pDocument,
        pStable,
        StringView_FromCString( "unchanged" ) ) );

    const key_value_parse_result_t result = KeyValue_ParseText(
        StringView_FromCString(
            "@cykv 1\n@schema \"cypher.test\" 1\n{ broken = [1, 2, }" ),
        {},
        pDocument );
    REQUIRE( result.status == key_value_parse_status_t::SYNTAX_ERROR );

    const key_value_document_header_t header =
        KeyValue_DocumentHeader( pDocument );
    REQUIRE( header.nSchemaVersion == 7u );
    REQUIRE( StringView_Equals(
        header.schemaId,
        StringView_FromCString( "cypher.stable" ) ) );

    string_view_t stable{};
    REQUIRE( KeyValue_GetString(
        KeyValue_Find(
            KeyValue_Root( pDocument ),
            StringView_FromCString( "stable" ) ),
        &stable ) );
    REQUIRE( StringView_Equals(
        stable,
        StringView_FromCString( "unchanged" ) ) );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV parser reports encoding lexical and trailing failures",
           "[CypherCommon][Tier1][CYKV][Diagnostics]" )
{
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ text = \"unterminated }" ) ==
        key_value_parse_status_t::LEXER_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{} {}" ) ==
        key_value_parse_status_t::TRAILING_INPUT );
    REQUIRE( ParseStatus(
        "@cykv 1\r@schema \"cypher.test\" 1\n{}" ) ==
        key_value_parse_status_t::INVALID_ENCODING );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ text = \"\\x41\" }" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );

    constexpr char invalidUtf8[]{
        '@', 'c', 'y', 'k', 'v', ' ', '1', '\n',
        '@', 's', 'c', 'h', 'e', 'm', 'a', ' ', '"',
        'c', 'y', 'p', 'h', 'e', 'r', '.', 't', 'e', 's', 't', '"', ' ', '1', '\n',
        '{', ' ', 'x', ' ', '=', ' ', '"', static_cast<char>( 0xC0u ), '"', ' ', '}'
    };
    key_value_document_t *pDocument = CreateDocument();
    const key_value_parse_result_t invalid = KeyValue_ParseText(
        { invalidUtf8, sizeof( invalidUtf8 ) },
        {},
        pDocument );
    REQUIRE( invalid.status == key_value_parse_status_t::INVALID_ENCODING );
    KeyValue_DestroyDocument( pDocument );

    REQUIRE( StringView_Equals(
        StringView_FromCString( KeyValue_ParseStatusName(
            key_value_parse_status_t::INVALID_HEADER ) ),
        StringView_FromCString( "INVALID_HEADER" ) ) );
}
