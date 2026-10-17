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

#include <string>

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
    CHECK( result.languageVersionLocation.nLine == 1u );
    CHECK( result.languageVersionLocation.nColumn == 7u );
    CHECK( result.schemaIdLocation.nLine == 2u );
    CHECK( result.schemaIdLocation.nColumn == 9u );
    CHECK( result.schemaVersionLocation.nLine == 2u );
    CHECK( result.schemaVersionLocation.nColumn == 23u );

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
        "@cykv 3\n@schema \"cypher.test\" 1\n{}" ) ==
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

TEST_CASE( "CYKV authored keys stay exact-case with folded destination lookup",
           "[CypherCommon][Tier1][CYKV][Conformance][Case]" )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument({
        nullptr,
        128u,
        8u * CY_KIB,
        CY_TRUE
    });
    REQUIRE( pDocument != nullptr );

    constexpr const char *pSource =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "{ Value = 1 value = 2 }";
    const key_value_parse_result_t result = KeyValue_ParseText(
        StringView_FromCString( pSource ),
        {},
        pDocument );
    REQUIRE( result.status == key_value_parse_status_t::OK );
    key_value_t *pRoot = KeyValue_Root( pDocument );
    REQUIRE( KeyValue_ChildCount( pRoot ) == 2u );
    CHECK( StringView_Equals(
        KeyValue_Name( KeyValue_ChildAt( pRoot, 0u ) ),
        StringView_FromCString( "Value" ) ) );
    CHECK( StringView_Equals(
        KeyValue_Name( KeyValue_ChildAt( pRoot, 1u ) ),
        StringView_FromCString( "value" ) ) );
    // The caller's post-publication lookup preference remains intact.
    CHECK( KeyValue_Find(
        pRoot,
        StringView_FromCString( "VALUE" ) ) != nullptr );

    constexpr const char *pExactDuplicate =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "{ Value = 1 Value = 2 }";
    const key_value_parse_result_t duplicate = KeyValue_ParseText(
        StringView_FromCString( pExactDuplicate ),
        {},
        pDocument );
    CHECK( duplicate.status == key_value_parse_status_t::DUPLICATE_KEY );
    // Failed parsing remains transactional.
    CHECK( KeyValue_ChildCount( KeyValue_Root( pDocument ) ) == 2u );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV 2 expands immutable typed local definitions",
           "[CypherCommon][Tier1][CYKV][Definitions]" )
{
    constexpr const char *pSource = R"cykv(@cykv 2
@schema "cypher.test" 1
#define ENABLED true
#define COUNT 42u
#define RATIO 1.5
#define TITLE "local value"
#define PAYLOAD hex"00ff80"
#define RECORD {
    title = $TITLE
    payload = $PAYLOAD
    values = [null, $COUNT, $RATIO]
}
#define RECORD_ALIAS $RECORD
{
    enabled = $ENABLED
    count = $COUNT
    first = $RECORD
    second = $RECORD_ALIAS
}
)cykv";

    key_value_document_t *pDocument = CreateDocument();
    const key_value_parse_result_t result = KeyValue_ParseText(
        StringView_FromCString( pSource ),
        {},
        pDocument );
    REQUIRE( result.status == key_value_parse_status_t::OK );
    REQUIRE( KeyValue_DocumentHeader( pDocument ).nLanguageVersion ==
             CYKV_LANGUAGE_VERSION_2 );

    key_value_t *pRoot = KeyValue_Root( pDocument );
    REQUIRE( KeyValue_ChildCount( pRoot ) == 4u );
    bool_t enabled = CY_FALSE;
    REQUIRE( KeyValue_GetBool(
        FindRequired( pRoot, "enabled" ),
        &enabled ) );
    CHECK( enabled );
    u64 count = 0u;
    REQUIRE( KeyValue_GetU64( FindRequired( pRoot, "count" ), &count ) );
    CHECK( count == 42u );

    key_value_t *pFirst = FindRequired( pRoot, "first" );
    key_value_t *pSecond = FindRequired( pRoot, "second" );
    REQUIRE( pFirst != pSecond );
    REQUIRE( KeyValue_Type( pFirst ) == key_value_type_t::OBJECT );
    REQUIRE( KeyValue_Type( pSecond ) == key_value_type_t::OBJECT );
    REQUIRE( KeyValue_ChildCount( pFirst ) == 3u );
    REQUIRE( KeyValue_ChildCount( pSecond ) == 3u );

    binary_block_t payload{};
    REQUIRE( KeyValue_GetBinary(
        FindRequired( pFirst, "payload" ),
        &payload ) );
    REQUIRE( payload.cbSize == 3u );
    CHECK( payload.pData[0] == 0x00u );
    CHECK( payload.pData[1] == 0xFFu );
    CHECK( payload.pData[2] == 0x80u );

    key_value_t *pValues = FindRequired( pFirst, "values" );
    REQUIRE( KeyValue_Type( pValues ) == key_value_type_t::ARRAY );
    REQUIRE( KeyValue_ChildCount( pValues ) == 3u );
    REQUIRE( KeyValue_Type( KeyValue_ChildAt( pValues, 0u ) ) ==
             key_value_type_t::NULL_VALUE );
    REQUIRE( KeyValue_GetU64( KeyValue_ChildAt( pValues, 1u ), &count ) );
    CHECK( count == 42u );
    f64 ratio = 0.0;
    REQUIRE( KeyValue_GetF64( KeyValue_ChildAt( pValues, 2u ), &ratio ) );
    CHECK( ratio == 1.5 );

    // Each expansion owns a deep copy; editing one result cannot mutate another.
    REQUIRE( KeyValue_SetString(
        pDocument,
        FindRequired( pFirst, "title" ),
        StringView_FromCString( "changed" ) ) );
    string_view_t secondTitle{};
    REQUIRE( KeyValue_GetString(
        FindRequired( pSecond, "title" ),
        &secondTitle ) );
    CHECK( StringView_Equals(
        secondTitle,
        StringView_FromCString( "local value" ) ) );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV 2 definitions are ordered unique and bounded",
           "[CypherCommon][Tier1][CYKV][Definitions][Limits]" )
{
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n{}" ) ==
        key_value_parse_status_t::OK );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define ROOT { value = 1 }\n$ROOT" ) ==
        key_value_parse_status_t::OK );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define ROOT [1, 2]\n$ROOT" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define A $B\n#define B 1\n{ value = $A }" ) ==
        key_value_parse_status_t::UNDEFINED_DEFINITION );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define A $A\n{ value = $A }" ) ==
        key_value_parse_status_t::UNDEFINED_DEFINITION );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define A 1\n#define A 2\n{ value = $A }" ) ==
        key_value_parse_status_t::DUPLICATE_DEFINITION );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n{ value = $MISSING }" ) ==
        key_value_parse_status_t::UNDEFINED_DEFINITION );

    key_value_parse_options_t options{};
    REQUIRE( options.nMaxDefinitions == 256u );
    options.nMaxDefinitions = 1u;
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define A 1\n#define B 2\n{ value = $A }",
        options ) == key_value_parse_status_t::DEFINITION_LIMIT );

    options = {};
    options.nMaxDefinitions = 0u;
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n{}",
        options ) == key_value_parse_status_t::OK );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define A 1\n{ value = $A }",
        options ) == key_value_parse_status_t::DEFINITION_LIMIT );

    options = {};
    options.nMaxNodes = 6u;
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define RECORD { a = 1 b = 2 }\n{ copy = $RECORD }",
        options ) == key_value_parse_status_t::NODE_LIMIT );

    options = {};
    options.cbMaxStringData = 31u;
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define TEXT \"1234\"\n{ copy = $TEXT }",
        options ) == key_value_parse_status_t::STRING_LIMIT );

    options = {};
    options.nMaxDepth = 1u;
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define NESTED { child = { value = 1 } }\n"
        "{ outer = { copy = $NESTED } }",
        options ) == key_value_parse_status_t::DEPTH_LIMIT );
}

TEST_CASE( "CYKV 2 local definitions do not enable token macros",
           "[CypherCommon][Tier1][CYKV][Definitions][Conformance]" )
{
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n"
        "#define A 1\n{ value = 1 }" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 1\n@schema \"cypher.test\" 1\n{ value = $A }" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define F(x) 1\n{ value = 1 }" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "# define A 1\n{ value = 1 }" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define A\n1\n{ value = $A }" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define A 1\n#define B $A##suffix\n{ value = $B }" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define KEY \"field\"\n{ $KEY = 1 }" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#include \"other.cykv\"\n{}" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#base \"base.cykv\"\n{}" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#define A 1\n{ value = $ A }" ) ==
        key_value_parse_status_t::SYNTAX_ERROR );
    REQUIRE( ParseStatus(
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "{}\n#define LATE 1" ) ==
        key_value_parse_status_t::TRAILING_INPUT );
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
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#define ALLOCATED { nested = \"temporary\" }\n"
            "{ broken = $MISSING }" ),
        {},
        pDocument );
    REQUIRE( result.status ==
             key_value_parse_status_t::UNDEFINED_DEFINITION );

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
    REQUIRE( StringView_Equals(
        StringView_FromCString( KeyValue_ParseStatusName(
            key_value_parse_status_t::UNDEFINED_DEFINITION ) ),
        StringView_FromCString( "UNDEFINED_DEFINITION" ) ) );
}

// ---------------------------------------------------------------------------
// CYKV 2 @when build conditionals
// ---------------------------------------------------------------------------

namespace
{

key_value_build_property_t StringProperty( const char *pName, const char *pValue )
{
    return { StringView_FromCString( pName ), key_value_build_value_kind_t::STRING, CY_FALSE, 0, StringView_FromCString( pValue ) };
}

key_value_build_property_t BoolProperty( const char *pName, bool_t bValue )
{
    return { StringView_FromCString( pName ), key_value_build_value_kind_t::BOOL, bValue, 0, {} };
}

key_value_build_property_t IntegerProperty( const char *pName, i64 value )
{
    return { StringView_FromCString( pName ), key_value_build_value_kind_t::INTEGER, CY_FALSE, value, {} };
}

std::string StringMember( key_value_t *pObject, const char *pName )
{
    string_view_t text{};
    REQUIRE( KeyValue_GetString( FindRequired( pObject, pName ), &text ) );
    return std::string( text.pData, text.cchLength );
}

i64 IntegerMember( key_value_t *pObject, const char *pName )
{
    i64 value = 0;
    REQUIRE( KeyValue_GetI64( FindRequired( pObject, pName ), &value ) );
    return value;
}

} // namespace

TEST_CASE( "CYKV 2 @when picks members for the build context",
           "[CypherCommon][Tier1][CYKV][Conditional]" )
{
    constexpr const char *pSource = R"cykv(@cykv 2
@schema "cypher.test" 1
{
    name = "panel"
    @when target.platform == "windows" { format = "bc7" }
    @else_when target.platform == "macos" and not build.debug { format = "astc" }
    @else { format = "rgba8" }
    @when build.debug { overlay = true }
    @when quality != 3 and ( target.platform == "linux" or quality == 2 ) { lod = 1 }
}
)cykv";
    const auto parse = [&]( bool_t bDebug, key_value_document_t *pDocument ) {
        const key_value_build_property_t properties[]{
            StringProperty( "target.platform", "macos" ), BoolProperty( "build.debug", bDebug ), IntegerProperty( "quality", 2 )
        };
        const key_value_build_context_t context{ properties, 3u };
        key_value_parse_options_t options{};
        options.pBuildContext = &context;
        return KeyValue_ParseText( StringView_FromCString( pSource ), options, pDocument ).status;
    };

    key_value_document_t *pDebug = CreateDocument();
    REQUIRE( parse( CY_TRUE, pDebug ) == key_value_parse_status_t::OK );
    key_value_t *pRoot = KeyValue_Root( pDebug );
    CHECK( StringMember( pRoot, "format" ) == "rgba8" ); // macOS, but debug: the second branch fails.
    CHECK( KeyValue_Find( pRoot, StringView_FromCString( "overlay" ) ) != nullptr );
    CHECK( IntegerMember( pRoot, "lod" ) == 1 );
    CHECK( KeyValue_ChildCount( pRoot ) == 4u );
    KeyValue_DestroyDocument( pDebug );

    key_value_document_t *pRelease = CreateDocument();
    REQUIRE( parse( CY_FALSE, pRelease ) == key_value_parse_status_t::OK );
    pRoot = KeyValue_Root( pRelease );
    CHECK( StringMember( pRoot, "format" ) == "astc" );
    CHECK( KeyValue_Find( pRoot, StringView_FromCString( "overlay" ) ) == nullptr );
    KeyValue_DestroyDocument( pRelease );

    // The running build's own context is there without being passed.
    const key_value_build_context_t *pHost = KeyValue_HostBuildContext();
    REQUIRE( pHost->nProperties >= 3u );
    CHECK( StringView_Equals( pHost->pProperties[0].name, StringView_FromCString( "target.platform" ) ) );
    CHECK( ParseStatus( "@cykv 2\n@schema \"cypher.test\" 1\n{ @when build.debug { a = 1 } @else { a = 2 } }" ) == key_value_parse_status_t::OK );
}

TEST_CASE( "CYKV 2 @when rejects unknown properties, kind mismatches, and broken branches",
           "[CypherCommon][Tier1][CYKV][Conditional]" )
{
    const key_value_build_property_t properties[]{ StringProperty( "target.platform", "macos" ), BoolProperty( "build.debug", CY_TRUE ) };
    const key_value_build_context_t context{ properties, 2u };
    key_value_parse_options_t options{};
    options.pBuildContext = &context;
    const auto status = [&]( const char *pBody ) {
        const std::string source = std::string( "@cykv 2\n@schema \"cypher.test\" 1\n{ " ) + pBody + " }";
        return ParseStatus( source.c_str(), options );
    };
    // A misspelt property is found even in a branch that is never reached.
    CHECK( status( "@when build.debug { a = 1 } @else_when target.platfrom == \"linux\" { a = 2 }" ) ==
           key_value_parse_status_t::UNKNOWN_CONDITION_PROPERTY );
    CHECK( status( "@when target.platform { a = 1 }" ) == key_value_parse_status_t::CONDITION_TYPE_MISMATCH );
    CHECK( status( "@when build.debug == \"yes\" { a = 1 }" ) == key_value_parse_status_t::CONDITION_TYPE_MISMATCH );
    CHECK( status( "@when target.platform == 3 { a = 1 }" ) == key_value_parse_status_t::CONDITION_TYPE_MISMATCH );
    // A branch adds members; it never silently replaces one.
    CHECK( status( "a = 0 @when build.debug { a = 1 }" ) == key_value_parse_status_t::DUPLICATE_KEY );
    // A branch not taken must still be valid CYKV.
    CHECK( status( "@when not build.debug { a = }" ) == key_value_parse_status_t::SYNTAX_ERROR );
    CHECK( status( "@else { a = 1 }" ) == key_value_parse_status_t::SYNTAX_ERROR );
    CHECK( status( "@when build.debug a = 1" ) == key_value_parse_status_t::SYNTAX_ERROR );
    CHECK( ParseStatus( "@cykv 1\n@schema \"cypher.test\" 1\n{ @when build.debug { a = 1 } }", options ) ==
           key_value_parse_status_t::SYNTAX_ERROR );
}

// ---------------------------------------------------------------------------
// CYKV 2 #extends templates and the #delete / += operators
// ---------------------------------------------------------------------------

TEST_CASE( "CYKV 2 #extends builds objects from templates with #delete and +=",
           "[CypherCommon][Tier1][CYKV][Extends]" )
{
    constexpr const char *pSource = R"cykv(@cykv 2
@schema "cypher.test" 1
#define RIFLE { damage = 24 fire_rate = 8 tags = ["bullet"] scope = { zoom = 2.0 mount = "rail" } ammo = "556" }
#define SILENCED { noise = 0.2 ammo = "subsonic" }
#define SMG {
    #extends $RIFLE
    fire_rate = 12
    tags += ["automatic"]
    scope = #delete
}
{
    rifle = { #extends $RIFLE }
    smg = $SMG
    marksman = {
        #extends $RIFLE
        #extends $SILENCED
        scope = { zoom = 6.0 }
    }
}
)cykv";
    key_value_document_t *pDocument = CreateDocument();
    REQUIRE( KeyValue_ParseText( StringView_FromCString( pSource ), {}, pDocument ).status == key_value_parse_status_t::OK );
    key_value_t *pRoot = KeyValue_Root( pDocument );

    key_value_t *pRifle = FindRequired( pRoot, "rifle" );
    CHECK( IntegerMember( pRifle, "damage" ) == 24 );
    CHECK( KeyValue_ChildCount( pRifle ) == 5u );

    key_value_t *pSmg = FindRequired( pRoot, "smg" );
    CHECK( IntegerMember( pSmg, "damage" ) == 24 );
    CHECK( IntegerMember( pSmg, "fire_rate" ) == 12 );
    CHECK( KeyValue_Find( pSmg, StringView_FromCString( "scope" ) ) == nullptr ); // Deleted, not left as null.
    key_value_t *pTags = FindRequired( pSmg, "tags" );
    REQUIRE( KeyValue_ChildCount( pTags ) == 2u );
    string_view_t tag{};
    REQUIRE( KeyValue_GetString( KeyValue_ChildAt( pTags, 0u ), &tag ) );
    CHECK( StringView_Equals( tag, StringView_FromCString( "bullet" ) ) ); // Inherited first, then the appended ones.
    REQUIRE( KeyValue_GetString( KeyValue_ChildAt( pTags, 1u ), &tag ) );
    CHECK( StringView_Equals( tag, StringView_FromCString( "automatic" ) ) );

    key_value_t *pMarksman = FindRequired( pRoot, "marksman" );
    CHECK( StringMember( pMarksman, "ammo" ) == "556" ); // The earlier template wins.
    CHECK( KeyValue_Find( pMarksman, StringView_FromCString( "noise" ) ) != nullptr );
    key_value_t *pScope = FindRequired( pMarksman, "scope" );
    f64 zoom = 0.0;
    REQUIRE( KeyValue_GetF64( FindRequired( pScope, "zoom" ), &zoom ) );
    CHECK( zoom == 6.0 );
    CHECK( StringMember( pScope, "mount" ) == "rail" ); // Objects merge member by member.
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV 2 #extends and operators fail loudly when misused",
           "[CypherCommon][Tier1][CYKV][Extends]" )
{
    const auto status = [&]( const char *pBody ) {
        const std::string source = std::string( "@cykv 2\n@schema \"cypher.test\" 1\n"
                                                "#define BASE { list = [1] count = 2 nested = { a = 1 } }\n"
                                                "#define NUMBER 7\n{ " ) + pBody + " }";
        return ParseStatus( source.c_str() );
    };
    CHECK( status( "x = { #extends $BASE missing = #delete }" ) == key_value_parse_status_t::INHERITED_VALUE_MISSING );
    CHECK( status( "x = { #extends $BASE count += [3] }" ) == key_value_parse_status_t::APPEND_TYPE_MISMATCH );
    CHECK( status( "x = { #extends $BASE list += 3 }" ) == key_value_parse_status_t::APPEND_TYPE_MISMATCH );
    CHECK( status( "x = { count = #delete }" ) == key_value_parse_status_t::OPERATOR_WITHOUT_BASE );
    CHECK( status( "x = { list += [1] }" ) == key_value_parse_status_t::OPERATOR_WITHOUT_BASE );
    CHECK( status( "x = { #extends $NOPE }" ) == key_value_parse_status_t::UNDEFINED_DEFINITION );
    CHECK( status( "x = { #extends $NUMBER }" ) == key_value_parse_status_t::MERGE_CONFLICT );
    CHECK( status( "x = { #extends $BASE count = \"two\" }" ) == key_value_parse_status_t::MERGE_CONFLICT );
    CHECK( status( "x = { count = 1 #extends $BASE }" ) == key_value_parse_status_t::SYNTAX_ERROR );
    CHECK( status( "x = { #extends $BASE nested = { b = #delete } }" ) == key_value_parse_status_t::INHERITED_VALUE_MISSING );
    CHECK( status( "x = { #extends $BASE nested = { a = #delete } }" ) == key_value_parse_status_t::OK );
    CHECK( ParseStatus( "@cykv 1\n@schema \"cypher.test\" 1\n{ x = { #extends $BASE } }" ) == key_value_parse_status_t::SYNTAX_ERROR );
}
