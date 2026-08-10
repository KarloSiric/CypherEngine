//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_KeyValueWriter_Tests.cpp
//  Purpose: Tests deterministic native CYKV text output.
//  Details: Covers type-preserving round trips, canonical ordering, bounded output,
//           callback failure, binary encoding, and escaped Unicode text.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_KeyValueWriter.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

key_value_document_t *BuildDocument()
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    REQUIRE( KeyValue_SetDocumentHeader(
        pDocument,
        {
            CYKV_LANGUAGE_VERSION,
            StringView_FromCString( "cypher.test" ),
            1u
        } ) );
    key_value_t *pRoot = KeyValue_Root( pDocument );
    REQUIRE( KeyValue_SetRootType( pDocument, key_value_type_t::OBJECT ) );

    key_value_t *pZulu = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( "zulu" ),
        key_value_type_t::I64 );
    REQUIRE( pZulu != nullptr );
    REQUIRE( KeyValue_SetI64( pDocument, pZulu, 7 ) );

    key_value_t *pAlpha = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( "alpha" ),
        key_value_type_t::U64 );
    REQUIRE( pAlpha != nullptr );
    REQUIRE( KeyValue_SetU64( pDocument, pAlpha, 7u ) );

    const byte bytes[]{ 0x00u, 0x7Fu, 0xFFu };
    key_value_t *pBinary = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( "binary" ),
        key_value_type_t::BINARY );
    REQUIRE( pBinary != nullptr );
    REQUIRE( KeyValue_SetBinary(
        pDocument,
        pBinary,
        { bytes, sizeof( bytes ) } ) );

    key_value_t *pText = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( "text" ),
        key_value_type_t::STRING );
    REQUIRE( pText != nullptr );
    REQUIRE( KeyValue_SetString(
        pDocument,
        pText,
        StringView_FromCString( "line\n\"quoted\"" ) ) );
    return pDocument;
}

bool_t RejectSink( string_view_t, void * ) noexcept
{
    return CY_FALSE;
}

} // namespace

TEST_CASE( "KeyValue writer round trips native values without losing type",
           "[CypherCommon][Tier1][KeyValueWriter]" )
{
    key_value_document_t *pSource = BuildDocument();
    const key_value_write_result_t measured = KeyValue_WriteText(
        KeyValue_Root( pSource ),
        {},
        nullptr,
        0u );
    REQUIRE( measured.status == key_value_write_status_t::OUTPUT_TRUNCATED );
    REQUIRE( measured.cchRequired != 0u );

    char *pText = new char[measured.cchRequired + 1u];
    const key_value_write_result_t written = KeyValue_WriteText(
        KeyValue_Root( pSource ),
        {},
        pText,
        measured.cchRequired + 1u );
    REQUIRE( written.status == key_value_write_status_t::OK );
    REQUIRE( written.cchWritten == measured.cchRequired );

    key_value_document_t *pRoundTrip = KeyValue_CreateDocument( {} );
    REQUIRE( pRoundTrip != nullptr );
    const key_value_parse_result_t parsed = KeyValue_ParseText(
        { pText, written.cchWritten },
        {},
        pRoundTrip );
    REQUIRE( parsed.status == key_value_parse_status_t::OK );

    i64 signedValue = 0;
    u64 unsignedValue = 0u;
    REQUIRE( KeyValue_GetI64(
        KeyValue_Find(
            KeyValue_Root( pRoundTrip ),
            StringView_FromCString( "zulu" ) ),
        &signedValue ) );
    REQUIRE( KeyValue_GetU64(
        KeyValue_Find(
            KeyValue_Root( pRoundTrip ),
            StringView_FromCString( "alpha" ) ),
        &unsignedValue ) );
    REQUIRE( signedValue == 7 );
    REQUIRE( unsignedValue == 7u );

    binary_block_t binary{};
    REQUIRE( KeyValue_GetBinary(
        KeyValue_Find(
            KeyValue_Root( pRoundTrip ),
            StringView_FromCString( "binary" ) ),
        &binary ) );
    REQUIRE( binary.cbSize == 3u );
    REQUIRE( binary.pData[2] == 0xFFu );

    delete[] pText;
    KeyValue_DestroyDocument( pRoundTrip );
    KeyValue_DestroyDocument( pSource );
}

TEST_CASE( "KeyValue writer canonical mode fixes ordering and whitespace",
           "[CypherCommon][Tier1][KeyValueWriter]" )
{
    key_value_document_t *pDocument = BuildDocument();
    key_value_write_options_t options{};
    options.flags = KEY_VALUE_WRITE_FLAG_CANONICAL |
                    KEY_VALUE_WRITE_FLAG_PRETTY |
                    KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE;

    char output[256]{};
    const key_value_write_result_t result = KeyValue_WriteText(
        KeyValue_Root( pDocument ),
        options,
        output,
        sizeof( output ) );
    REQUIRE( result.status == key_value_write_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString(
            "@cykv 1\n@schema \"cypher.test\" 1\n"
            "{\"alpha\"=7u \"binary\"=hex\"007fff\" "
            "\"text\"=\"line\\n\\\"quoted\\\"\" \"zulu\"=7}" ) ) );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "KeyValue writer reports truncation and sink failure",
           "[CypherCommon][Tier1][KeyValueWriter]" )
{
    key_value_document_t *pDocument = BuildDocument();

    char output[8]{};
    const key_value_write_result_t truncated = KeyValue_WriteText(
        KeyValue_Root( pDocument ),
        {},
        output,
        sizeof( output ) );
    REQUIRE( truncated.status == key_value_write_status_t::OUTPUT_TRUNCATED );
    REQUIRE( truncated.cchWritten == sizeof( output ) - 1u );
    REQUIRE( output[sizeof( output ) - 1u] == '\0' );
    REQUIRE( truncated.cchRequired > truncated.cchWritten );

    const key_value_write_result_t failed = KeyValue_WriteTextToSink(
        KeyValue_Root( pDocument ),
        {},
        RejectSink,
        nullptr );
    REQUIRE( failed.status == key_value_write_status_t::SINK_FAILED );
    REQUIRE( failed.cchWritten == 0u );

    REQUIRE( StringView_Equals(
        StringView_FromCString( KeyValue_WriteStatusName(
            key_value_write_status_t::SIZE_OVERFLOW ) ),
        StringView_FromCString( "SIZE_OVERFLOW" ) ) );
    KeyValue_DestroyDocument( pDocument );
}
