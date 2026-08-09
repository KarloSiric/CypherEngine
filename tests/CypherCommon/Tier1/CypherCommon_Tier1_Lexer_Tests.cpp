//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Lexer_Tests.cpp
//  Purpose: Tests the allocation-free Tier1 lexical scanner.
//  Details: These tests protect bounded tokenization, exact source locations,
//           configurable grammar rules, malformed-input diagnostics, UTF-8 handling,
//           token limits, and checkpoint restoration.
//
//  History:
//  - Created by Karlo Siric on 2026-08-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Lexer.h"
#include "CypherCommon_StringView.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

bool_t ViewEquals( string_view_t view, const char *pExpected ) noexcept
{
    return StringView_Equals( view, StringView_FromCString( pExpected ) );
}

token_t ReadToken(
    lexer_t *pLexer,
    token_kind_t expectedKind,
    const char *pExpectedLexeme )
{
    token_t token{};
    REQUIRE( Lexer_Read( pLexer, &token ) == lexer_status_t::OK );
    REQUIRE( token.kind == expectedKind );
    REQUIRE( ViewEquals( token.lexeme, pExpectedLexeme ) );
    return token;
}

lexer_status_t ReadFirstStatus(
    const char *pSource,
    const lexer_rules_t &rules,
    token_t *pTokenOut )
{
    lexer_t lexer{};
    REQUIRE( Lexer_Init( &lexer, StringView_FromCString( pSource ), rules ) );
    return Lexer_Read( &lexer, pTokenOut );
}

} // namespace

TEST_CASE( "Lexer default rules expose stable policy and status names",
           "[CypherCommon][Tier1][Lexer]" )
{
    const lexer_rules_t rules = Lexer_DefaultRules();
    REQUIRE( ( rules.flags & LEXER_FLAG_ALLOW_LINE_COMMENTS ) != 0u );
    REQUIRE( ( rules.flags & LEXER_FLAG_ALLOW_BLOCK_COMMENTS ) != 0u );
    REQUIRE( ( rules.flags & LEXER_FLAG_ALLOW_ESCAPE_SEQUENCES ) != 0u );
    REQUIRE( rules.cchMaxToken == CY_INVALID_SIZE );

    REQUIRE( ViewEquals( StringView_FromCString( Lexer_StatusName( lexer_status_t::OK ) ), "OK" ) );
    REQUIRE( ViewEquals( StringView_FromCString( Lexer_StatusName( lexer_status_t::END_OF_INPUT ) ), "END_OF_INPUT" ) );
    REQUIRE( ViewEquals( StringView_FromCString( Lexer_StatusName( lexer_status_t::INVALID_ARGUMENT ) ), "INVALID_ARGUMENT" ) );
    REQUIRE( ViewEquals( StringView_FromCString( Lexer_StatusName( lexer_status_t::INVALID_BYTE ) ), "INVALID_BYTE" ) );
    REQUIRE( ViewEquals( StringView_FromCString( Lexer_StatusName( lexer_status_t::INVALID_NUMBER ) ), "INVALID_NUMBER" ) );
    REQUIRE( ViewEquals( StringView_FromCString( Lexer_StatusName( lexer_status_t::INVALID_ESCAPE ) ), "INVALID_ESCAPE" ) );
    REQUIRE( ViewEquals( StringView_FromCString( Lexer_StatusName( lexer_status_t::UNTERMINATED_STRING ) ), "UNTERMINATED_STRING" ) );
    REQUIRE( ViewEquals( StringView_FromCString( Lexer_StatusName( lexer_status_t::UNTERMINATED_COMMENT ) ), "UNTERMINATED_COMMENT" ) );
    REQUIRE( ViewEquals( StringView_FromCString( Lexer_StatusName( lexer_status_t::TOKEN_TOO_LONG ) ), "TOKEN_TOO_LONG" ) );
    REQUIRE( ViewEquals( StringView_FromCString( Lexer_StatusName( static_cast<lexer_status_t>( 0xffu ) ) ), "UNKNOWN" ) );
}

TEST_CASE( "Lexer reads identifiers punctuation and numeric tokens without allocation",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_SIGN_IS_NUMBER_PART;

    lexer_t lexer{};
    REQUIRE( Lexer_Init(
        &lexer,
        StringView_FromCString( "player_1 = -42 3.5 0x2A" ),
        rules ) );

    ReadToken( &lexer, token_kind_t::IDENTIFIER, "player_1" );
    ReadToken( &lexer, token_kind_t::PUNCTUATION, "=" );

    const token_t negative = ReadToken( &lexer, token_kind_t::INTEGER, "-42" );
    REQUIRE( ( negative.flags & TOKEN_FLAG_NEGATIVE ) != 0u );

    ReadToken( &lexer, token_kind_t::FLOAT, "3.5" );
    const token_t hexadecimal = ReadToken( &lexer, token_kind_t::INTEGER, "0x2A" );
    REQUIRE( ( hexadecimal.flags & TOKEN_FLAG_BASE_PREFIX ) != 0u );

    token_t end{};
    REQUIRE( Lexer_Read( &lexer, &end ) == lexer_status_t::END_OF_INPUT );
    REQUIRE( end.kind == token_kind_t::END_OF_INPUT );
    REQUIRE( Lexer_IsAtEnd( &lexer ) );
}

TEST_CASE( "Lexer tracks LF and CRLF as one logical newline",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_EMIT_NEWLINES;

    lexer_t lexer{};
    REQUIRE( Lexer_Init( &lexer, StringView_FromCString( "one\r\ntwo\nthree" ), rules ) );

    const token_t one = ReadToken( &lexer, token_kind_t::IDENTIFIER, "one" );
    REQUIRE( one.range.begin.iByte == 0u );
    REQUIRE( one.range.begin.nLine == 1u );
    REQUIRE( one.range.begin.nColumn == 1u );
    REQUIRE( one.range.end.iByte == 3u );
    REQUIRE( one.range.end.nLine == 1u );
    REQUIRE( one.range.end.nColumn == 4u );

    const token_t crlf = ReadToken( &lexer, token_kind_t::NEWLINE, "\r\n" );
    REQUIRE( crlf.range.begin.iByte == 3u );
    REQUIRE( crlf.range.end.iByte == 5u );
    REQUIRE( crlf.range.end.nLine == 2u );
    REQUIRE( crlf.range.end.nColumn == 1u );

    const token_t two = ReadToken( &lexer, token_kind_t::IDENTIFIER, "two" );
    REQUIRE( two.range.begin.nLine == 2u );
    REQUIRE( two.range.begin.nColumn == 1u );

    const token_t lf = ReadToken( &lexer, token_kind_t::NEWLINE, "\n" );
    REQUIRE( lf.range.end.nLine == 3u );
    REQUIRE( lf.range.end.nColumn == 1u );

    const token_t three = ReadToken( &lexer, token_kind_t::IDENTIFIER, "three" );
    REQUIRE( three.range.begin.nLine == 3u );
    REQUIRE( three.range.begin.nColumn == 1u );
}

TEST_CASE( "Lexer skips comments and supports nested block comments by policy",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_ALLOW_NESTED_BLOCK_COMMENT;

    lexer_t lexer{};
    REQUIRE( Lexer_Init(
        &lexer,
        StringView_FromCString( "first /* outer /* nested */ tail */ // line\n second" ),
        rules ) );

    ReadToken( &lexer, token_kind_t::IDENTIFIER, "first" );
    const token_t second = ReadToken( &lexer, token_kind_t::IDENTIFIER, "second" );
    REQUIRE( second.range.begin.nLine == 2u );
}

TEST_CASE( "Lexer emits comments and newlines when requested",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_EMIT_COMMENTS | LEXER_FLAG_EMIT_NEWLINES;

    lexer_t lexer{};
    REQUIRE( Lexer_Init( &lexer, StringView_FromCString( "// note\r\nname" ), rules ) );

    ReadToken( &lexer, token_kind_t::COMMENT, "// note" );
    ReadToken( &lexer, token_kind_t::NEWLINE, "\r\n" );
    ReadToken( &lexer, token_kind_t::IDENTIFIER, "name" );
}

TEST_CASE( "Lexer uses the longest configured punctuation match",
           "[CypherCommon][Tier1][Lexer]" )
{
    const string_view_t punctuations[] = {
        StringView_FromCString( "=" ),
        StringView_FromCString( "==" ),
        StringView_FromCString( "=>" )
    };

    lexer_rules_t rules = Lexer_DefaultRules();
    rules.pPunctuations = punctuations;
    rules.nPunctuationCount = 3u;

    lexer_t lexer{};
    REQUIRE( Lexer_Init( &lexer, StringView_FromCString( "a==b=>c" ), rules ) );
    ReadToken( &lexer, token_kind_t::IDENTIFIER, "a" );
    ReadToken( &lexer, token_kind_t::PUNCTUATION, "==" );
    ReadToken( &lexer, token_kind_t::IDENTIFIER, "b" );
    ReadToken( &lexer, token_kind_t::PUNCTUATION, "=>" );
    ReadToken( &lexer, token_kind_t::IDENTIFIER, "c" );
}

TEST_CASE( "Lexer accepts configured identifier bytes",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_rules_t rules = Lexer_DefaultRules();
    CharacterSet_Add( &rules.identifierStartExtra, '@' );
    CharacterSet_Add( &rules.identifierBodyExtra, '-' );

    lexer_t lexer{};
    REQUIRE( Lexer_Init( &lexer, StringView_FromCString( "@player-name" ), rules ) );
    ReadToken( &lexer, token_kind_t::IDENTIFIER, "@player-name" );
}

TEST_CASE( "Lexer validates UTF-8 identifiers without decoding their contents",
           "[CypherCommon][Tier1][Lexer]" )
{
    const char validUtf8[] = {
        static_cast<char>( 0xC3u ), static_cast<char>( 0xA9u ), 'l', 'a', 'n'
    };

    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_ALLOW_UTF8_IDENTIFIERS;

    lexer_t lexer{};
    REQUIRE( Lexer_Init( &lexer, StringView_FromRange( validUtf8, sizeof( validUtf8 ) ), rules ) );

    token_t token{};
    REQUIRE( Lexer_Read( &lexer, &token ) == lexer_status_t::OK );
    REQUIRE( token.kind == token_kind_t::IDENTIFIER );
    REQUIRE( token.lexeme.pData == validUtf8 );
    REQUIRE( token.lexeme.cchLength == sizeof( validUtf8 ) );

    const char invalidUtf8[] = { static_cast<char>( 0xC3u ), 'x' };
    REQUIRE( Lexer_Init( &lexer, StringView_FromRange( invalidUtf8, sizeof( invalidUtf8 ) ), rules ) );
    REQUIRE( Lexer_Read( &lexer, &token ) == lexer_status_t::INVALID_BYTE );
    REQUIRE( token.kind == token_kind_t::ERROR );
    REQUIRE( lexer.errorLocation.iByte == 0u );
}

TEST_CASE( "Lexer records quoted and escaped literal policy",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_t lexer{};
    REQUIRE( Lexer_Init(
        &lexer,
        StringView_FromCString( "\"line\\n\" 'x'" ),
        Lexer_DefaultRules() ) );

    const token_t stringToken = ReadToken( &lexer, token_kind_t::STRING, "\"line\\n\"" );
    REQUIRE( ( stringToken.flags & TOKEN_FLAG_QUOTED ) != 0u );
    REQUIRE( ( stringToken.flags & TOKEN_FLAG_HAS_ESCAPES ) != 0u );

    const token_t characterToken = ReadToken( &lexer, token_kind_t::CHARACTER, "'x'" );
    REQUIRE( ( characterToken.flags & TOKEN_FLAG_QUOTED ) != 0u );

    lexer_rules_t singleQuoteRules = Lexer_DefaultRules();
    singleQuoteRules.flags |= LEXER_FLAG_ALLOW_SINGLE_QUOTED_STRING;
    REQUIRE( Lexer_Init( &lexer, StringView_FromCString( "'text'" ), singleQuoteRules ) );
    ReadToken( &lexer, token_kind_t::STRING, "'text'" );
}

TEST_CASE( "Lexer rejects malformed escapes and unterminated quoted input",
           "[CypherCommon][Tier1][Lexer]" )
{
    const lexer_rules_t rules = Lexer_DefaultRules();
    token_t token{};

    REQUIRE( ReadFirstStatus( "\"\\q\"", rules, &token ) == lexer_status_t::INVALID_ESCAPE );
    REQUIRE( token.kind == token_kind_t::ERROR );

    REQUIRE( ReadFirstStatus( "\"\\u12\"", rules, &token ) == lexer_status_t::INVALID_ESCAPE );
    REQUIRE( ReadFirstStatus( "\"missing", rules, &token ) == lexer_status_t::UNTERMINATED_STRING );
    REQUIRE( ReadFirstStatus( "/* missing", rules, &token ) == lexer_status_t::UNTERMINATED_COMMENT );
}

TEST_CASE( "Lexer recognizes supported numeric spellings",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_SIGN_IS_NUMBER_PART;

    lexer_t lexer{};
    REQUIRE( Lexer_Init(
        &lexer,
        StringView_FromCString( "+12 -0x2A .5 1. 6.02e23 1_000 0b1010 0o77" ),
        rules ) );

    ReadToken( &lexer, token_kind_t::INTEGER, "+12" );
    ReadToken( &lexer, token_kind_t::INTEGER, "-0x2A" );
    ReadToken( &lexer, token_kind_t::FLOAT, ".5" );
    ReadToken( &lexer, token_kind_t::FLOAT, "1." );
    ReadToken( &lexer, token_kind_t::FLOAT, "6.02e23" );
    ReadToken( &lexer, token_kind_t::INTEGER, "1_000" );
    ReadToken( &lexer, token_kind_t::INTEGER, "0b1010" );
    ReadToken( &lexer, token_kind_t::INTEGER, "0o77" );
}

TEST_CASE( "Lexer rejects malformed numeric tokens at the lexical boundary",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_SIGN_IS_NUMBER_PART;
    token_t token{};

    REQUIRE( ReadFirstStatus( "0x", rules, &token ) == lexer_status_t::INVALID_NUMBER );
    REQUIRE( ReadFirstStatus( "0b2", rules, &token ) == lexer_status_t::INVALID_NUMBER );
    REQUIRE( ReadFirstStatus( "1__0", rules, &token ) == lexer_status_t::INVALID_NUMBER );
    REQUIRE( ReadFirstStatus( "1._2", rules, &token ) == lexer_status_t::INVALID_NUMBER );
    REQUIRE( ReadFirstStatus( "1e+", rules, &token ) == lexer_status_t::INVALID_NUMBER );
    REQUIRE( ReadFirstStatus( "12name", rules, &token ) == lexer_status_t::INVALID_NUMBER );
}

TEST_CASE( "Lexer enforces bounded token sizes and keeps errors sticky",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.cchMaxToken = 3u;

    lexer_t lexer{};
    REQUIRE( Lexer_Init( &lexer, StringView_FromCString( "abcd" ), rules ) );

    token_t token{};
    REQUIRE( Lexer_Read( &lexer, &token ) == lexer_status_t::TOKEN_TOO_LONG );
    REQUIRE( token.kind == token_kind_t::ERROR );
    REQUIRE( token.lexeme.cchLength == 4u );
    REQUIRE( Lexer_Read( &lexer, &token ) == lexer_status_t::TOKEN_TOO_LONG );

    Lexer_Reset( &lexer );
    REQUIRE( Lexer_Location( &lexer ).iByte == 0u );
    REQUIRE( Lexer_Location( &lexer ).nLine == 1u );
    REQUIRE( Lexer_Location( &lexer ).nColumn == 1u );
}

TEST_CASE( "Lexer checkpoints restore token lookahead state",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_t lexer{};
    REQUIRE( Lexer_Init(
        &lexer,
        StringView_FromCString( "one two" ),
        Lexer_DefaultRules() ) );

    ReadToken( &lexer, token_kind_t::IDENTIFIER, "one" );
    const lexer_checkpoint_t checkpoint = Lexer_Save( &lexer );
    REQUIRE( checkpoint.iByte == 3u );

    ReadToken( &lexer, token_kind_t::IDENTIFIER, "two" );
    token_t end{};
    REQUIRE( Lexer_Read( &lexer, &end ) == lexer_status_t::END_OF_INPUT );

    Lexer_Restore( &lexer, checkpoint );
    REQUIRE( Lexer_Location( &lexer ).iByte == checkpoint.iByte );
    ReadToken( &lexer, token_kind_t::IDENTIFIER, "two" );
}

TEST_CASE( "Lexer rejects structurally invalid source and rule contracts",
           "[CypherCommon][Tier1][Lexer]" )
{
    lexer_t lexer{};
    lexer_rules_t rules = Lexer_DefaultRules();

    REQUIRE_FALSE( Lexer_Init( &lexer, { nullptr, 1u }, rules ) );
    REQUIRE( lexer.status == lexer_status_t::INVALID_ARGUMENT );

    rules = Lexer_DefaultRules();
    rules.nPunctuationCount = 1u;
    REQUIRE_FALSE( Lexer_Init( &lexer, {}, rules ) );

    rules = Lexer_DefaultRules();
    rules.lineCommentBegin = {};
    REQUIRE_FALSE( Lexer_Init( &lexer, {}, rules ) );

    rules = Lexer_DefaultRules();
    rules.blockCommentEnd = {};
    REQUIRE_FALSE( Lexer_Init( &lexer, {}, rules ) );

    rules = Lexer_DefaultRules();
    REQUIRE( Lexer_Init( &lexer, {}, rules ) );
    token_t token{};
    REQUIRE( Lexer_Read( &lexer, &token ) == lexer_status_t::END_OF_INPUT );
    REQUIRE( token.kind == token_kind_t::END_OF_INPUT );
}
