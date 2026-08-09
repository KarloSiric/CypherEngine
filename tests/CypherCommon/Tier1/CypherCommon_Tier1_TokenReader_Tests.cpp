//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_TokenReader_Tests.cpp
//  Purpose: Tests Tier1 parser-facing token reader behavior.
//  Details: These tests protect fixed lookahead, non-consuming expectations,
//           typed conversion, precise error retention, lexer propagation, and reset.
//
//  History:
//  - Created by Karlo Siric on 2026-08-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TokenReader.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

bool_t ViewEquals( string_view_t view, const char *pExpected ) noexcept
{
    return StringView_Equals( view, StringView_FromCString( pExpected ) );
}

token_reader_t MakeReader(
    const char *pSource,
    const lexer_rules_t &rules = Lexer_DefaultRules() )
{
    token_reader_t reader{};
    REQUIRE( TokenReader_Init( &reader, StringView_FromCString( pSource ), rules ) );
    return reader;
}

} // namespace

TEST_CASE( "TokenReader status names remain stable",
           "[CypherCommon][Tier1][TokenReader]" )
{
    REQUIRE( ViewEquals( StringView_FromCString( TokenReader_StatusName( token_reader_status_t::OK ) ), "OK" ) );
    REQUIRE( ViewEquals( StringView_FromCString( TokenReader_StatusName( token_reader_status_t::END_OF_INPUT ) ), "END_OF_INPUT" ) );
    REQUIRE( ViewEquals( StringView_FromCString( TokenReader_StatusName( token_reader_status_t::INVALID_ARGUMENT ) ), "INVALID_ARGUMENT" ) );
    REQUIRE( ViewEquals( StringView_FromCString( TokenReader_StatusName( token_reader_status_t::LEXER_ERROR ) ), "LEXER_ERROR" ) );
    REQUIRE( ViewEquals( StringView_FromCString( TokenReader_StatusName( token_reader_status_t::UNEXPECTED_KIND ) ), "UNEXPECTED_KIND" ) );
    REQUIRE( ViewEquals( StringView_FromCString( TokenReader_StatusName( token_reader_status_t::UNEXPECTED_TEXT ) ), "UNEXPECTED_TEXT" ) );
    REQUIRE( ViewEquals( StringView_FromCString( TokenReader_StatusName( token_reader_status_t::VALUE_PARSE_FAILED ) ), "VALUE_PARSE_FAILED" ) );
    REQUIRE( ViewEquals( StringView_FromCString( TokenReader_StatusName( token_reader_status_t::LOOKAHEAD_EXCEEDED ) ), "LOOKAHEAD_EXCEEDED" ) );
    REQUIRE( ViewEquals( StringView_FromCString( TokenReader_StatusName( static_cast<token_reader_status_t>( 0xffu ) ) ), "UNKNOWN" ) );
}

TEST_CASE( "TokenReader lookahead does not consume buffered tokens",
           "[CypherCommon][Tier1][TokenReader]" )
{
    token_reader_t reader = MakeReader( "alpha beta gamma" );

    token_t token{};
    REQUIRE( TokenReader_Peek( &reader, 0u, &token ) == token_reader_status_t::OK );
    REQUIRE( ViewEquals( token.lexeme, "alpha" ) );
    REQUIRE( TokenReader_Peek( &reader, 2u, &token ) == token_reader_status_t::OK );
    REQUIRE( ViewEquals( token.lexeme, "gamma" ) );
    REQUIRE( reader.nLookahead == 3u );

    REQUIRE( TokenReader_Read( &reader, &token ) == token_reader_status_t::OK );
    REQUIRE( ViewEquals( token.lexeme, "alpha" ) );
    REQUIRE( reader.nLookahead == 2u );

    REQUIRE( TokenReader_Peek(
        &reader,
        CY_TOKEN_READER_LOOKAHEAD_CAPACITY,
        &token ) == token_reader_status_t::LOOKAHEAD_EXCEEDED );
    REQUIRE( TokenReader_LastError( &reader )->status ==
             token_reader_status_t::LOOKAHEAD_EXCEEDED );
}

TEST_CASE( "TokenReader consume operations leave mismatches untouched",
           "[CypherCommon][Tier1][TokenReader]" )
{
    token_reader_t reader = MakeReader( "Alpha = value" );
    token_t token{};

    REQUIRE_FALSE( TokenReader_ConsumeKind( &reader, token_kind_t::INTEGER ) );
    REQUIRE_FALSE( TokenReader_ConsumeText(
        &reader,
        StringView_FromCString( "alpha" ),
        CY_FALSE ) );
    REQUIRE( TokenReader_ConsumeText(
        &reader,
        StringView_FromCString( "alpha" ),
        CY_TRUE,
        &token ) );
    REQUIRE( ViewEquals( token.lexeme, "Alpha" ) );

    REQUIRE( TokenReader_ConsumeKind( &reader, token_kind_t::PUNCTUATION, &token ) );
    REQUIRE( ViewEquals( token.lexeme, "=" ) );
    REQUIRE( TokenReader_ConsumeKind( &reader, token_kind_t::IDENTIFIER, &token ) );
    REQUIRE( ViewEquals( token.lexeme, "value" ) );
}

TEST_CASE( "TokenReader expectation failures retain diagnostic context and input",
           "[CypherCommon][Tier1][TokenReader]" )
{
    token_reader_t reader = MakeReader( "name = 7" );
    token_t token{};

    REQUIRE( TokenReader_ExpectKind(
        &reader,
        token_kind_t::INTEGER,
        &token ) == token_reader_status_t::UNEXPECTED_KIND );

    const token_reader_error_t *pError = TokenReader_LastError( &reader );
    REQUIRE( pError->status == token_reader_status_t::UNEXPECTED_KIND );
    REQUIRE( pError->expectedKind == token_kind_t::INTEGER );
    REQUIRE( ViewEquals( pError->actual.lexeme, "name" ) );

    REQUIRE( TokenReader_ExpectText(
        &reader,
        StringView_FromCString( "other" ),
        CY_FALSE ) == token_reader_status_t::UNEXPECTED_TEXT );
    pError = TokenReader_LastError( &reader );
    REQUIRE( ViewEquals( pError->expectedText, "other" ) );
    REQUIRE_FALSE( pError->bCaseInsensitiveAscii );
    REQUIRE( ViewEquals( pError->actual.lexeme, "name" ) );

    REQUIRE( TokenReader_ExpectText(
        &reader,
        StringView_FromCString( "NAME" ),
        CY_TRUE,
        &token ) == token_reader_status_t::OK );
    REQUIRE( ViewEquals( token.lexeme, "name" ) );

    TokenReader_ClearError( &reader );
    REQUIRE( TokenReader_LastError( &reader )->status == token_reader_status_t::OK );
}

TEST_CASE( "TokenReader converts integer floating and Boolean tokens",
           "[CypherCommon][Tier1][TokenReader]" )
{
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_SIGN_IS_NUMBER_PART;
    token_reader_t reader = MakeReader( "42 -7 0x2A 3.5 TRUE 1", rules );

    string_parse_options_t decimal{};
    u64 nUnsigned = 0u;
    REQUIRE( TokenReader_ReadU64( &reader, decimal, &nUnsigned ) == token_reader_status_t::OK );
    REQUIRE( nUnsigned == 42u );

    i64 nSigned = 0;
    REQUIRE( TokenReader_ReadI64( &reader, decimal, &nSigned ) == token_reader_status_t::OK );
    REQUIRE( nSigned == -7 );

    string_parse_options_t prefixed{};
    prefixed.nBase = 0u;
    prefixed.flags = STRING_PARSE_FLAG_ALLOW_BASE_PREFIX;
    REQUIRE( TokenReader_ReadU64( &reader, prefixed, &nUnsigned ) == token_reader_status_t::OK );
    REQUIRE( nUnsigned == 42u );

    f64 nFloat = 0.0;
    REQUIRE( TokenReader_ReadF64(
        &reader,
        STRING_PARSE_FLAG_NONE,
        &nFloat ) == token_reader_status_t::OK );
    REQUIRE( nFloat == 3.5 );

    bool_t bValue = CY_FALSE;
    REQUIRE( TokenReader_ReadBool(
        &reader,
        STRING_PARSE_FLAG_CASE_INSENSITIVE_BOOL,
        &bValue ) == token_reader_status_t::OK );
    REQUIRE( bValue );

    bValue = CY_FALSE;
    REQUIRE( TokenReader_ReadBool(
        &reader,
        STRING_PARSE_FLAG_ALLOW_NUMERIC_BOOL,
        &bValue ) == token_reader_status_t::OK );
    REQUIRE( bValue );
}

TEST_CASE( "TokenReader parse failures preserve output and offending token",
           "[CypherCommon][Tier1][TokenReader]" )
{
    token_reader_t reader = MakeReader( "18446744073709551616 next" );
    string_parse_options_t options{};
    u64 nValue = 77u;

    REQUIRE( TokenReader_ReadU64(
        &reader,
        options,
        &nValue ) == token_reader_status_t::VALUE_PARSE_FAILED );
    REQUIRE( nValue == 77u );

    const token_reader_error_t *pError = TokenReader_LastError( &reader );
    REQUIRE( pError->status == token_reader_status_t::VALUE_PARSE_FAILED );
    REQUIRE( pError->parseResult.status == string_parse_status_t::NUMERIC_OVERFLOW );
    REQUIRE( ViewEquals( pError->actual.lexeme, "18446744073709551616" ) );

    token_t token{};
    REQUIRE( TokenReader_Read( &reader, &token ) == token_reader_status_t::OK );
    REQUIRE( ViewEquals( token.lexeme, "18446744073709551616" ) );
}

TEST_CASE( "TokenReader propagates lexer failures with their source token",
           "[CypherCommon][Tier1][TokenReader]" )
{
    const char source[] = { static_cast<char>( 0xC3u ), 'x' };
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_ALLOW_UTF8_IDENTIFIERS;

    token_reader_t reader{};
    REQUIRE( TokenReader_Init(
        &reader,
        StringView_FromRange( source, sizeof( source ) ),
        rules ) );

    token_t token{};
    REQUIRE( TokenReader_Read( &reader, &token ) == token_reader_status_t::LEXER_ERROR );
    REQUIRE( token.kind == token_kind_t::ERROR );
    REQUIRE( TokenReader_LastError( &reader )->lexerStatus == lexer_status_t::INVALID_BYTE );
}

TEST_CASE( "TokenReader reports expected end and missing required tokens distinctly",
           "[CypherCommon][Tier1][TokenReader]" )
{
    token_reader_t reader = MakeReader( "value" );
    token_t token{};

    REQUIRE( TokenReader_Read( &reader, &token ) == token_reader_status_t::OK );
    REQUIRE( TokenReader_ExpectKind(
        &reader,
        token_kind_t::END_OF_INPUT,
        &token ) == token_reader_status_t::OK );
    REQUIRE( token.kind == token_kind_t::END_OF_INPUT );
    REQUIRE( TokenReader_LastError( &reader )->status == token_reader_status_t::OK );

    TokenReader_Reset( &reader );
    REQUIRE( TokenReader_Read( &reader, &token ) == token_reader_status_t::OK );
    REQUIRE( TokenReader_ExpectKind(
        &reader,
        token_kind_t::INTEGER,
        &token ) == token_reader_status_t::END_OF_INPUT );
    REQUIRE( TokenReader_LastError( &reader )->status == token_reader_status_t::END_OF_INPUT );
    REQUIRE( TokenReader_LastError( &reader )->expectedKind == token_kind_t::INTEGER );
}

TEST_CASE( "TokenReader reset clears lookahead errors and lexer position",
           "[CypherCommon][Tier1][TokenReader]" )
{
    token_reader_t reader = MakeReader( "one two" );
    token_t token{};

    REQUIRE( TokenReader_Peek( &reader, 1u, &token ) == token_reader_status_t::OK );
    REQUIRE( reader.nLookahead == 2u );
    REQUIRE( TokenReader_Peek(
        &reader,
        CY_TOKEN_READER_LOOKAHEAD_CAPACITY,
        &token ) == token_reader_status_t::LOOKAHEAD_EXCEEDED );

    TokenReader_Reset( &reader );
    REQUIRE( reader.nLookahead == 0u );
    REQUIRE( TokenReader_LastError( &reader )->status == token_reader_status_t::OK );
    REQUIRE( Lexer_Location( &reader.lexer ).iByte == 0u );

    REQUIRE( TokenReader_Read( &reader, &token ) == token_reader_status_t::OK );
    REQUIRE( ViewEquals( token.lexeme, "one" ) );
}
