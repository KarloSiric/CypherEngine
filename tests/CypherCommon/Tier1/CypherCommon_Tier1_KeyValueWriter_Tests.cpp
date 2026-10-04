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

#include <cstring>
#include <string>

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

TEST_CASE( "KeyValue canonical hashes identify semantic documents",
           "[CypherCommon][Tier1][KeyValueWriter][Hash]" )
{
    constexpr string_view_t firstText{
        "@cykv 1\n@schema \"cypher.test\" 1\n"
        "{ zulu = 7 alpha = 7u text = \"same\" }",
        sizeof( "@cykv 1\n@schema \"cypher.test\" 1\n"
                "{ zulu = 7 alpha = 7u text = \"same\" }" ) - 1u
    };
    constexpr string_view_t reorderedText{
        "@cykv 1\n@schema \"cypher.test\" 1\n"
        "{ // formatting and order are authoring-only\n"
        "  text = \"same\"\n  alpha = 7u\n  zulu = 7\n}",
        sizeof( "@cykv 1\n@schema \"cypher.test\" 1\n"
                "{ // formatting and order are authoring-only\n"
                "  text = \"same\"\n  alpha = 7u\n  zulu = 7\n}" ) - 1u
    };
    constexpr string_view_t changedText{
        "@cykv 1\n@schema \"cypher.test\" 1\n"
        "{ zulu = 8 alpha = 7u text = \"same\" }",
        sizeof( "@cykv 1\n@schema \"cypher.test\" 1\n"
                "{ zulu = 8 alpha = 7u text = \"same\" }" ) - 1u
    };

    key_value_document_t *pFirst = KeyValue_CreateDocument( {} );
    key_value_document_t *pReordered = KeyValue_CreateDocument( {} );
    key_value_document_t *pChanged = KeyValue_CreateDocument( {} );
    REQUIRE( pFirst != nullptr );
    REQUIRE( pReordered != nullptr );
    REQUIRE( pChanged != nullptr );
    REQUIRE( KeyValue_ParseText( firstText, {}, pFirst ).status ==
             key_value_parse_status_t::OK );
    REQUIRE( KeyValue_ParseText( reorderedText, {}, pReordered ).status ==
             key_value_parse_status_t::OK );
    REQUIRE( KeyValue_ParseText( changedText, {}, pChanged ).status ==
             key_value_parse_status_t::OK );

    const key_value_canonical_hash_result_t first =
        KeyValue_HashCanonicalDocument( pFirst );
    const key_value_canonical_hash_result_t reordered =
        KeyValue_HashCanonicalDocument( pReordered );
    const key_value_canonical_hash_result_t changed =
        KeyValue_HashCanonicalDocument( pChanged );
    REQUIRE( first.status == key_value_write_status_t::OK );
    REQUIRE( reordered.status == key_value_write_status_t::OK );
    REQUIRE( changed.status == key_value_write_status_t::OK );
    REQUIRE( first.cbHashed == reordered.cbHashed );
    REQUIRE( ContentHash_IsValid( first.hash ) );
    REQUIRE( ContentHash_Equals( first.hash, reordered.hash ) );
    REQUIRE_FALSE( ContentHash_Equals( first.hash, changed.hash ) );

    const key_value_canonical_hash_result_t invalid =
        KeyValue_HashCanonicalDocument( nullptr );
    REQUIRE( invalid.status == key_value_write_status_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( ContentHash_IsValid( invalid.hash ) );

    KeyValue_DestroyDocument( pChanged );
    KeyValue_DestroyDocument( pReordered );
    KeyValue_DestroyDocument( pFirst );
}

namespace
{

// Compact layout: the options map chunks and other read-and-diffed
// documents use (bare keys, shortest reals, one-line and packed containers).
key_value_document_t *ParseDocument( const char *pText )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    const key_value_parse_result_t parsed = KeyValue_ParseText( StringView_FromCString( pText ), {}, pDocument );
    REQUIRE( parsed.status == key_value_parse_status_t::OK );
    return pDocument;
}

std::string WriteCompact( const key_value_document_t *pDocument, u16 nLineWidth )
{
    key_value_write_options_t options{};
    options.flags |= KEY_VALUE_WRITE_FLAG_BARE_KEYS | KEY_VALUE_WRITE_FLAG_SHORTEST_REALS;
    options.nLineWidth = nLineWidth;
    const key_value_write_result_t measured = KeyValue_WriteText( KeyValue_Root( pDocument ), options, nullptr, 0u );
    REQUIRE( ( measured.status == key_value_write_status_t::OK || measured.status == key_value_write_status_t::OUTPUT_TRUNCATED ) );
    std::string text( measured.cchRequired + 1u, '\0' );
    const key_value_write_result_t written = KeyValue_WriteText( KeyValue_Root( pDocument ), options, text.data(), text.size() );
    REQUIRE( written.status == key_value_write_status_t::OK );
    text.resize( written.cchWritten );
    return text;
}

bool ValuesEqual( const key_value_t *pA, const key_value_t *pB )
{
    if ( KeyValue_Type( pA ) != KeyValue_Type( pB ) || !StringView_Equals( KeyValue_Name( pA ), KeyValue_Name( pB ) ) ) { return false; }
    if ( KeyValue_ChildCount( pA ) != KeyValue_ChildCount( pB ) ) { return false; }
    f64 flA = 0.0, flB = 0.0;
    if ( KeyValue_GetF64( pA, &flA ) && KeyValue_GetF64( pB, &flB ) && std::memcmp( &flA, &flB, sizeof( f64 ) ) != 0 ) { return false; }
    i64 iA = 0, iB = 0;
    if ( KeyValue_GetI64( pA, &iA ) && KeyValue_GetI64( pB, &iB ) && iA != iB ) { return false; }
    u64 uA = 0u, uB = 0u;
    if ( KeyValue_GetU64( pA, &uA ) && KeyValue_GetU64( pB, &uB ) && uA != uB ) { return false; }
    string_view_t sA{}, sB{};
    if ( KeyValue_GetString( pA, &sA ) && KeyValue_GetString( pB, &sB ) && !StringView_Equals( sA, sB ) ) { return false; }
    for ( usize i = 0u; i < KeyValue_ChildCount( pA ); ++i ) {
        if ( !ValuesEqual( KeyValue_ChildAt( pA, i ), KeyValue_ChildAt( pB, i ) ) ) { return false; }
    }
    return true;
}

} // namespace

TEST_CASE( "Shortest reals round-trip with the fewest digits", "[keyvalue][writer][compact]" )
{
    key_value_document_t *pDocument = ParseDocument(
        "@cykv 1\n@schema \"cypher.test\" 1\n"
        "{ a = 0.1 b = 39.37 c = 8192.0 d = -0.0 e = 1.0e-7 f = 15.000000000000002 g = 0.3333333333333333 h = 1.0e300 }" );
    const std::string text = WriteCompact( pDocument, 0u );
    CHECK( text.find( "a = 0.1\n" ) != std::string::npos );
    CHECK( text.find( "b = 39.37\n" ) != std::string::npos );
    CHECK( text.find( "c = 8192.0\n" ) != std::string::npos );
    CHECK( text.find( "d = -0.0\n" ) != std::string::npos );
    CHECK( text.find( "f = 15.000000000000002\n" ) != std::string::npos );
    key_value_document_t *pBack = ParseDocument( text.c_str() );
    CHECK( ValuesEqual( KeyValue_Root( pDocument ), KeyValue_Root( pBack ) ) );
    KeyValue_DestroyDocument( pBack );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Bare keys are written only where the grammar allows them", "[keyvalue][writer][compact]" )
{
    key_value_document_t *pDocument = ParseDocument(
        "@cykv 1\n@schema \"cypher.test\" 1\n"
        "{ plain = 1 \"ui.background\" = 2 \"with space\" = 3 \"true\" = 4 \"9lives\" = 5 \"a-b_c\" = 6 \"null\" = 7 }" );
    const std::string text = WriteCompact( pDocument, 0u );
    CHECK( text.find( "\n    plain = 1" ) != std::string::npos );
    CHECK( text.find( "\n    ui.background = 2" ) != std::string::npos );
    CHECK( text.find( "\"with space\" = 3" ) != std::string::npos );
    CHECK( text.find( "\"true\" = 4" ) != std::string::npos );
    CHECK( text.find( "\"9lives\" = 5" ) != std::string::npos );
    CHECK( text.find( "\n    a-b_c = 6" ) != std::string::npos );
    CHECK( text.find( "\"null\" = 7" ) != std::string::npos );
    key_value_document_t *pBack = ParseDocument( text.c_str() );
    CHECK( ValuesEqual( KeyValue_Root( pDocument ), KeyValue_Root( pBack ) ) );
    KeyValue_DestroyDocument( pBack );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Dotted keys can stay quoted while other keys go bare", "[keyvalue][writer][compact]" )
{
    key_value_document_t *pDocument = ParseDocument(
        "@cykv 1\n@schema \"cypher.test\" 1\n"
        "{ colors = { \"ui.border\" = \"auto\" } plain_key = 1 \"a-b\" = 2 }" );
    key_value_write_options_t options{};
    options.flags |= KEY_VALUE_WRITE_FLAG_BARE_KEYS | KEY_VALUE_WRITE_FLAG_QUOTE_DOTTED_KEYS;
    options.nLineWidth = 80u;
    char buffer[256]{};
    const key_value_write_result_t written = KeyValue_WriteText( KeyValue_Root( pDocument ), options, buffer, sizeof( buffer ) );
    REQUIRE( written.status == key_value_write_status_t::OK );
    const std::string text( buffer, written.cchWritten );
    CHECK( text.find( "colors = { \"ui.border\" = \"auto\" }" ) != std::string::npos ); // One-line form measures the quotes too.
    CHECK( text.find( "\n    plain_key = 1" ) != std::string::npos );
    CHECK( text.find( "\n    a-b = 2" ) != std::string::npos );
    // The flag alone changes nothing: quoted is already the default.
    options.flags = KEY_VALUE_WRITE_FLAG_PRETTY | KEY_VALUE_WRITE_FLAG_QUOTE_DOTTED_KEYS;
    const key_value_write_result_t quoted = KeyValue_WriteText( KeyValue_Root( pDocument ), options, buffer, sizeof( buffer ) );
    REQUIRE( quoted.status == key_value_write_status_t::OK );
    CHECK( std::string( buffer, quoted.cchWritten ).find( "\"plain_key\" = 1" ) != std::string::npos );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Short containers go on one line and long ones spread", "[keyvalue][writer][compact]" )
{
    key_value_document_t *pDocument = ParseDocument(
        "@cykv 1\n@schema \"cypher.test\" 1\n"
        "{ origin = [ 128.0, -64.0, 16.0 ] empty = [] none = {} "
        "out = { output = \"on_trigger\" target = \"wave1\" delay = 0.5 } "
        "long = { first = \"aaaaaaaaaaaaaaaaaaaa\" second = \"bbbbbbbbbbbbbbbbbbbb\" third = [ 1, 2, 3 ] } }" );
    const std::string text = WriteCompact( pDocument, 80u );
    CHECK( text.find( "    origin = [ 128.0, -64.0, 16.0 ]\n" ) != std::string::npos );
    CHECK( text.find( "    empty = []\n" ) != std::string::npos );
    CHECK( text.find( "    none = {}\n" ) != std::string::npos );
    CHECK( text.find( "    out = { output = \"on_trigger\" target = \"wave1\" delay = 0.5 }\n" ) != std::string::npos );
    CHECK( text.find( "    long = {\n        first = \"aaaaaaaaaaaaaaaaaaaa\"\n" ) != std::string::npos );
    CHECK( text.find( "        third = [ 1, 2, 3 ]\n" ) != std::string::npos );
    // No line is longer than the width unless a single value is.
    usize iStart = 0u;
    while ( iStart < text.size() ) {
        const usize iEnd = text.find( '\n', iStart );
        const usize cch = ( iEnd == std::string::npos ? text.size() : iEnd ) - iStart;
        CHECK( cch <= 80u );
        iStart = iEnd == std::string::npos ? text.size() : iEnd + 1u;
    }
    key_value_document_t *pBack = ParseDocument( text.c_str() );
    CHECK( ValuesEqual( KeyValue_Root( pDocument ), KeyValue_Root( pBack ) ) );
    KeyValue_DestroyDocument( pBack );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Long scalar lists are packed and the root always spreads", "[keyvalue][writer][compact]" )
{
    std::string source = "@cykv 1\n@schema \"cypher.test\" 1\n{ ids = [ ";
    for ( int i = 0; i < 40; ++i ) { source += ( i == 0 ? "" : ", " ) + std::to_string( 1000 + i ) + "u"; }
    source += " ] rows = [ [ 1.0, 2.0 ], [ 3.0, 4.0 ] ] }";
    key_value_document_t *pDocument = ParseDocument( source.c_str() );
    const std::string text = WriteCompact( pDocument, 50u );
    // 8 columns of indent + six "1000u, " entries fill the 50 columns.
    CHECK( text.find( "{\n    ids = [\n        1000u, 1001u, 1002u, 1003u, 1004u, 1005u,\n        1006u," ) != std::string::npos );
    CHECK( text.find( "1039u\n    ]\n" ) != std::string::npos );
    CHECK( text.find( "rows = [ [ 1.0, 2.0 ], [ 3.0, 4.0 ] ]" ) != std::string::npos );
    key_value_document_t *pBack = ParseDocument( text.c_str() );
    CHECK( ValuesEqual( KeyValue_Root( pDocument ), KeyValue_Root( pBack ) ) );
    KeyValue_DestroyDocument( pBack );

    key_value_document_t *pTiny = ParseDocument( "@cykv 1\n@schema \"cypher.test\" 1\n{ a = 1 }" );
    CHECK( WriteCompact( pTiny, 120u ).find( "{\n    a = 1\n}" ) != std::string::npos );
    KeyValue_DestroyDocument( pTiny );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Canonical output ignores the compact layout", "[keyvalue][writer][compact]" )
{
    key_value_document_t *pDocument = ParseDocument( "@cykv 1\n@schema \"cypher.test\" 1\n{ b = [ 1, 2 ] a = 0.1 }" );
    key_value_write_options_t canonical{};
    canonical.flags = KEY_VALUE_WRITE_FLAG_CANONICAL;
    key_value_write_options_t both = canonical;
    both.flags |= KEY_VALUE_WRITE_FLAG_BARE_KEYS | KEY_VALUE_WRITE_FLAG_SHORTEST_REALS;
    both.nLineWidth = 80u;
    char a[256]{};
    char b[256]{};
    const key_value_write_result_t ra = KeyValue_WriteText( KeyValue_Root( pDocument ), canonical, a, sizeof( a ) );
    const key_value_write_result_t rb = KeyValue_WriteText( KeyValue_Root( pDocument ), both, b, sizeof( b ) );
    REQUIRE( ra.status == key_value_write_status_t::OK );
    REQUIRE( rb.status == key_value_write_status_t::OK );
    CHECK( std::string( a, ra.cchWritten ) == std::string( b, rb.cchWritten ) );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Sibling records share one layout", "[keyvalue][writer][compact]" )
{
    // Two faces fit on a line and one does not: all three spread, rather
    // than alternating by a character of difference.
    key_value_document_t *pDocument = ParseDocument(
        "@cykv 1\n@schema \"cypher.test\" 1\n"
        "{ faces = [ { id = 1u plane = [ 1.0, 0.0, 0.0, 32.0 ] }, { id = 2u plane = [ -1.0, 0.0, 0.0, 32.0 ] },"
        " { id = 3u plane = [ 0.70710678118654757, 0.70710678118654757, 0.0, 181.01933598375618 ] } ]"
        " short = [ { a = 1 }, { b = 2 } ] }" );
    const std::string text = WriteCompact( pDocument, 60u );
    INFO( text );
    CHECK( text.find( "faces = [\n        {\n            id = 1u\n" ) != std::string::npos );
    CHECK( text.find( "            id = 2u\n" ) != std::string::npos );
    CHECK( text.find( "short = [ { a = 1 }, { b = 2 } ]" ) != std::string::npos );
    key_value_document_t *pBack = ParseDocument( text.c_str() );
    CHECK( ValuesEqual( KeyValue_Root( pDocument ), KeyValue_Root( pBack ) ) );
    KeyValue_DestroyDocument( pBack );

    const std::string wide = WriteCompact( pDocument, 120u );
    INFO( wide );
    CHECK( wide.find( "        { id = 1u plane = [ 1.0, 0.0, 0.0, 32.0 ] },\n        { id = 2u plane = [ -1.0, 0.0, 0.0, 32.0 ] },\n" ) != std::string::npos );
    KeyValue_DestroyDocument( pDocument );
}

namespace
{

std::string WriteValueText( const key_value_t *pValue, const key_value_write_options_t &options )
{
    const auto measured = KeyValue_WriteValueText( pValue, options, nullptr, 0u );
    REQUIRE( measured.status == key_value_write_status_t::OUTPUT_TRUNCATED );
    REQUIRE( measured.cchWritten == 0u );
    REQUIRE( measured.cchRequired != 0u );
    std::string text( measured.cchRequired + 1u, '\0' );
    const auto written = KeyValue_WriteValueText( pValue, options, text.data(), text.size() );
    REQUIRE( written.status == key_value_write_status_t::OK );
    REQUIRE( written.cchWritten == measured.cchRequired );
    REQUIRE( text[written.cchWritten] == '\0' );
    text.resize( written.cchWritten );
    return text;
}

// Native documents retain mandatory headers and an object root. A fragment
// consumer puts the value in one member rather than changing parser policy.
key_value_document_t *ParseValueText( const std::string &text )
{
    const std::string source = "@cykv 1\n@schema \"cypher.fragment_test\" 1\n{ fragment = " + text + " }";
    return ParseDocument( source.c_str() );
}

} // namespace

TEST_CASE( "Native value text writes owned array and object children without their document header or member name", "[CypherCommon][Tier1][KeyValueWriter][value-text]" )
{
    auto *document = ParseDocument(
        "@cykv 1\n@schema \"cypher.test\" 1\n"
        "{ object = { signed = -3 unsigned = 18446744073709551615u real = 1.0 text = \"Ž \\\"line\\\"\\n\\\\end\" }"
        " array = [ -3, 7u, 1.0, true, null ] }" );
    key_value_write_options_t options{};
    options.flags = KEY_VALUE_WRITE_FLAG_PRETTY | KEY_VALUE_WRITE_FLAG_BARE_KEYS | KEY_VALUE_WRITE_FLAG_SHORTEST_REALS;
    for ( const char *name : { "object", "array" } ) {
        const auto *value = KeyValue_Find( KeyValue_Root( document ), StringView_FromCString( name ) );
        REQUIRE( value != nullptr );
        const std::string text = WriteValueText( value, options );
        CHECK( text.find( "@cykv" ) == std::string::npos );
        CHECK( text.find( "@schema" ) == std::string::npos );
        CHECK( text.front() == ( KeyValue_Type( value ) == key_value_type_t::OBJECT ? '{' : '[' ) );
        CHECK( text.find( "1.0" ) != std::string::npos );
        CHECK( text.find( 'u' ) != std::string::npos );
        auto *roundTrip = ParseValueText( text );
        const auto *fragment = KeyValue_Find( KeyValue_Root( roundTrip ), StringView_FromCString( "fragment" ) );
        REQUIRE( KeyValue_Type( fragment ) == KeyValue_Type( value ) );
        REQUIRE( KeyValue_ChildCount( fragment ) == KeyValue_ChildCount( value ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( value ); ++i ) {
            CHECK( ValuesEqual( KeyValue_ChildAt( value, i ), KeyValue_ChildAt( fragment, i ) ) );
        }
        if ( KeyValue_Type( fragment ) == key_value_type_t::OBJECT ) {
            u64 unsignedValue = 0u;
            REQUIRE( KeyValue_GetU64( KeyValue_Find( fragment, StringView_FromCString( "unsigned" ) ), &unsignedValue ) );
            CHECK( unsignedValue == 18446744073709551615ull );
            CHECK( KeyValue_Type( KeyValue_Find( fragment, StringView_FromCString( "real" ) ) ) == key_value_type_t::F64 );
            string_view_t escaped{};
            REQUIRE( KeyValue_GetString( KeyValue_Find( fragment, StringView_FromCString( "text" ) ), &escaped ) );
            CHECK( StringView_Equals( escaped, StringView_FromCString( "Ž \"line\"\n\\end" ) ) );
        } else {
            CHECK( KeyValue_Type( KeyValue_ChildAt( fragment, 0u ) ) == key_value_type_t::I64 );
            CHECK( KeyValue_Type( KeyValue_ChildAt( fragment, 1u ) ) == key_value_type_t::U64 );
            CHECK( KeyValue_Type( KeyValue_ChildAt( fragment, 2u ) ) == key_value_type_t::F64 );
            CHECK( KeyValue_Type( KeyValue_ChildAt( fragment, 3u ) ) == key_value_type_t::BOOL );
            CHECK( KeyValue_Type( KeyValue_ChildAt( fragment, 4u ) ) == key_value_type_t::NULL_VALUE );
        }
        KeyValue_DestroyDocument( roundTrip );
    }
    KeyValue_DestroyDocument( document );
}

TEST_CASE( "Native value text supports scalar roots and empty values without requiring document identity", "[CypherCommon][Tier1][KeyValueWriter][value-text]" )
{
    auto *document = KeyValue_CreateDocument( {} );
    REQUIRE( document != nullptr );
    auto *root = KeyValue_Root( document );
    key_value_write_options_t options{};
    options.flags = KEY_VALUE_WRITE_FLAG_NONE;
    REQUIRE( KeyValue_SetU64( document, root, 7u ) );
    CHECK( WriteValueText( root, options ) == "7u" );
    REQUIRE( KeyValue_SetF64( document, root, 1.0 ) );
    CHECK( WriteValueText( root, options ) == "1.0" );
    REQUIRE( KeyValue_SetI64( document, root, -3 ) );
    CHECK( WriteValueText( root, options ) == "-3" );
    REQUIRE( KeyValue_SetBool( document, root, CY_FALSE ) );
    CHECK( WriteValueText( root, options ) == "false" );
    REQUIRE( KeyValue_SetString( document, root, {} ) );
    CHECK( WriteValueText( root, options ) == "\"\"" );
    REQUIRE( KeyValue_SetNull( document, root ) );
    CHECK( WriteValueText( root, options ) == "null" );
    REQUIRE( KeyValue_SetContainerType( document, root, key_value_type_t::ARRAY ) );
    CHECK( WriteValueText( root, options ) == "[]" );
    CHECK( KeyValue_WriteText( root, options, nullptr, 0u ).status == key_value_write_status_t::INVALID_DOCUMENT );
    REQUIRE( KeyValue_SetContainerType( document, root, key_value_type_t::OBJECT ) );
    CHECK( WriteValueText( root, options ) == "{}" );
    CHECK( KeyValue_WriteText( root, options, nullptr, 0u ).status == key_value_write_status_t::INVALID_DOCUMENT );
    KeyValue_DestroyDocument( document );
}

TEST_CASE( "Native value text shares bounded buffer accounting and depth validation", "[CypherCommon][Tier1][KeyValueWriter][value-text]" )
{
    auto *document = ParseDocument( "@cykv 1\n@schema \"cypher.test\" 1\n{ value = [ 1u, 2.0, 3 ] deep = [ [ [ 4u ] ] ] }" );
    const auto *value = KeyValue_Find( KeyValue_Root( document ), StringView_FromCString( "value" ) );
    key_value_write_options_t options{};
    options.flags = KEY_VALUE_WRITE_FLAG_NONE;
    const std::string full = WriteValueText( value, options );
    char buffer[]{ '!', '!', '!', '!', '!', '!' };
    const auto truncated = KeyValue_WriteValueText( value, options, buffer, 5u );
    CHECK( truncated.status == key_value_write_status_t::OUTPUT_TRUNCATED );
    CHECK( truncated.cchWritten == 4u );
    CHECK( truncated.cchRequired == full.size() );
    CHECK( std::string( buffer, 4u ) == full.substr( 0u, 4u ) );
    CHECK( buffer[4] == '\0' );
    CHECK( buffer[5] == '!' );
    char terminator = '!';
    const auto oneByte = KeyValue_WriteValueText( value, options, &terminator, 1u );
    CHECK( oneByte.status == key_value_write_status_t::OUTPUT_TRUNCATED );
    CHECK( oneByte.cchWritten == 0u );
    CHECK( oneByte.cchRequired == full.size() );
    CHECK( terminator == '\0' );
    terminator = '!';
    const auto zeroBytes = KeyValue_WriteValueText( value, options, &terminator, 0u );
    CHECK( zeroBytes.status == key_value_write_status_t::OUTPUT_TRUNCATED );
    CHECK( zeroBytes.cchRequired == full.size() );
    CHECK( terminator == '!' );
    options.nMaxDepth = 1u;
    const auto depth = KeyValue_WriteValueText( KeyValue_Find( KeyValue_Root( document ), StringView_FromCString( "deep" ) ), options, nullptr, 0u );
    CHECK( depth.status == key_value_write_status_t::DEPTH_LIMIT );
    KeyValue_DestroyDocument( document );
}

TEST_CASE( "Native value text keeps document headers and canonical serialization independent", "[CypherCommon][Tier1][KeyValueWriter][value-text]" )
{
    auto *document = ParseDocument( "@cykv 1\n@schema \"cypher.test\" 1\n{ b = 7u a = 1.0 }" );
    const auto before = KeyValue_HashCanonicalDocument( document );
    REQUIRE( before.status == key_value_write_status_t::OK );
    key_value_write_options_t options{};
    options.flags = KEY_VALUE_WRITE_FLAG_CANONICAL | KEY_VALUE_WRITE_FLAG_PRETTY | KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE;
    const std::string fragment = WriteValueText( KeyValue_Root( document ), options );
    CHECK( fragment == "{\"a\"=1.0 \"b\"=7u}" );
    char text[256]{};
    const auto written = KeyValue_WriteText( KeyValue_Root( document ), options, text, sizeof( text ) );
    REQUIRE( written.status == key_value_write_status_t::OK );
    CHECK( std::string( text, written.cchWritten ) == "@cykv 1\n@schema \"cypher.test\" 1\n" + fragment );
    const auto after = KeyValue_HashCanonicalDocument( document );
    REQUIRE( after.status == key_value_write_status_t::OK );
    CHECK( ContentHash_Equals( before.hash, after.hash ) );
    CHECK( before.cbHashed == after.cbHashed );
    options.flags = KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE;
    CHECK( WriteValueText( KeyValue_Find( KeyValue_Root( document ), StringView_FromCString( "b" ) ), options ) == "7u\n" );
    KeyValue_DestroyDocument( document );
}

TEST_CASE( "Native value text escapes UTF-8 scalars and rejects invalid arguments", "[CypherCommon][Tier1][KeyValueWriter][value-text]" )
{
    auto *document = KeyValue_CreateDocument( {} );
    REQUIRE( document != nullptr );
    auto *value = KeyValue_Root( document );
    REQUIRE( KeyValue_SetString( document, value, StringView_FromCString( "Ž \"line\"\n\\end" ) ) );
    key_value_write_options_t options{};
    options.flags = KEY_VALUE_WRITE_FLAG_ASCII_ONLY;
    const std::string text = WriteValueText( value, options );
    CHECK( text.find( "\\u" ) != std::string::npos );
    CHECK( text.find( "\\\"line\\\"" ) != std::string::npos );
    CHECK( text.find( "\\n" ) != std::string::npos );
    CHECK( text.find( "\\\\end" ) != std::string::npos );
    auto *roundTrip = ParseValueText( text );
    string_view_t parsed{};
    REQUIRE( KeyValue_GetString( KeyValue_Find( KeyValue_Root( roundTrip ), StringView_FromCString( "fragment" ) ), &parsed ) );
    CHECK( StringView_Equals( parsed, StringView_FromCString( "Ž \"line\"\n\\end" ) ) );
    char buffer[8]{};
    const auto nullValue = KeyValue_WriteValueText( nullptr, options, buffer, sizeof( buffer ) );
    CHECK( nullValue.status == key_value_write_status_t::INVALID_ARGUMENT );
    CHECK( nullValue.cchWritten == 0u );
    CHECK( nullValue.cchRequired == 0u );
    CHECK( KeyValue_WriteValueText( value, options, nullptr, 1u ).status == key_value_write_status_t::INVALID_ARGUMENT );
    options.flags = static_cast<flags32_t>( ~0u );
    CHECK( KeyValue_WriteValueText( value, options, buffer, sizeof( buffer ) ).status == key_value_write_status_t::INVALID_ARGUMENT );
    options.flags = KEY_VALUE_WRITE_FLAG_NONE;
    options.nMaxDepth = 0u;
    CHECK( KeyValue_WriteValueText( value, options, buffer, sizeof( buffer ) ).status == key_value_write_status_t::INVALID_ARGUMENT );
    options.nMaxDepth = CY_USIZE_MAX;
    CHECK( KeyValue_WriteValueText( value, options, buffer, sizeof( buffer ) ).status == key_value_write_status_t::INVALID_ARGUMENT );
    KeyValue_DestroyDocument( roundTrip );
    KeyValue_DestroyDocument( document );
}
