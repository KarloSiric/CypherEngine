//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier2/CypherCommon_Tier2_FontDefinition_Tests.cpp
//  Purpose: Contract tests for `.cyfont` family definitions.
//  Details: Unusable faces and fallbacks are skipped and reported; only a
//           family with no usable face fails.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_FontDefinition.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

key_value_document_t *ParseFont( const char *pSource )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    REQUIRE( KeyValue_ParseText( StringView_FromCString( pSource ), {}, pDocument ).status ==
             key_value_parse_status_t::OK );
    return pDocument;
}

bool_t Equals( string_view_t view, const char *pText )
{
    return StringView_Equals( view, StringView_FromCString( pText ) );
}

} // namespace

TEST_CASE( "Font definitions decode faces, fallbacks, and monospace",
           "[CypherCommon][Tier2][FontDefinition]" )
{
    key_value_document_t *pDocument = ParseFont( R"cykv(@cykv 1
@schema "cypher.font" 1
{
    name = "JetBrains Mono"
    faces = [
        { file = "fonts/jbmono/jbmono-regular.ttf" },
        { file = "fonts/jbmono/jbmono-bold.otf" weight = 700u italic = false },
        { file = "fonts/jbmono/jbmono-italic.ttf" weight = 400 italic = true }
    ]
    fallbacks = [ "fonts/noto/noto_mono.cyfont", "system:Menlo" ]
    monospace = true
    atlas_hints = { size = 32 }
}
)cykv" );
    font_definition_view_t font{};
    const font_definition_decode_result_t result = FontDefinition_Decode( pDocument, nullptr, 0u, &font );
    REQUIRE( result.status == font_definition_status_t::OK );
    REQUIRE( result.nProblemsRequired == 0u );
    REQUIRE( Equals( font.name, "JetBrains Mono" ) );
    REQUIRE( font.nFaces == 3u );
    REQUIRE( font.faces[0].nWeight == CY_FONT_WEIGHT_DEFAULT );
    REQUIRE( font.faces[1].nWeight == 700u );
    REQUIRE( font.faces[2].bItalic == CY_TRUE );
    REQUIRE( font.nFallbacks == 2u );
    REQUIRE( Equals( font.fallbacks[1], "system:Menlo" ) );
    REQUIRE( font.bMonospace == CY_TRUE );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Font definitions skip unusable entries and report them",
           "[CypherCommon][Tier2][FontDefinition]" )
{
    key_value_document_t *pDocument = ParseFont( R"cykv(@cykv 1
@schema "cypher.font" 1
{
    name = "Inter"
    faces = [
        { file = "fonts/Inter/Inter-Regular.ttf" },
        { file = "fonts/inter/inter-regular.woff" },
        { file = "fonts/inter/inter-heavy.ttf" weight = 1200 },
        { file = "fonts/inter/inter-regular.ttf" },
        "not a face"
    ]
    fallbacks = [ "system:", "fonts/noto/noto.ttf", 5, "system:Helvetica" ]
    monospace = "no"
}
)cykv" );
    font_problem_t problems[16]{};
    font_definition_view_t font{};
    const font_definition_decode_result_t result = FontDefinition_Decode( pDocument, problems, 16u, &font );
    REQUIRE( result.status == font_definition_status_t::OK );
    REQUIRE( font.nFaces == 1u );
    REQUIRE( Equals( font.faces[0].file, "fonts/inter/inter-regular.ttf" ) );
    REQUIRE( font.nFallbacks == 1u );
    REQUIRE( Equals( font.fallbacks[0], "system:Helvetica" ) );
    REQUIRE( font.bMonospace == CY_FALSE );
    REQUIRE( result.nProblemsRequired == 8u );
    REQUIRE( problems[0].code == font_problem_code_t::BAD_FACE_FILE ); // Upper case is not canonical.
    REQUIRE( problems[0].iElement == 0u );
    REQUIRE( problems[1].code == font_problem_code_t::BAD_FACE_FILE );
    REQUIRE( problems[2].code == font_problem_code_t::BAD_FACE_WEIGHT );
    REQUIRE( problems[3].code == font_problem_code_t::BAD_FACE );
    REQUIRE( problems[7].code == font_problem_code_t::BAD_MONOSPACE );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Font definitions fail only without a name, a usable face, or the right header",
           "[CypherCommon][Tier2][FontDefinition]" )
{
    struct case_t {
        const char *pSource;
        font_definition_status_t expected;
    };
    const case_t cases[]{
        { "@cykv 1\n@schema \"cypher.font\" 1\n{ faces = [ { file = \"fonts/a.ttf\" } ] }", font_definition_status_t::INVALID_NAME },
        { "@cykv 1\n@schema \"cypher.font\" 1\n{ name = \"A\" faces = [] }", font_definition_status_t::NO_USABLE_FACE },
        { "@cykv 1\n@schema \"cypher.font\" 1\n{ name = \"A\" faces = [ { file = \"a.png\" } ] }", font_definition_status_t::NO_USABLE_FACE },
        { "@cykv 1\n@schema \"cypher.font\" 2\n{ name = \"A\" faces = [ { file = \"fonts/a.ttf\" } ] }", font_definition_status_t::INVALID_HEADER },
        { "@cykv 1\n@schema \"cypher.theme\" 1\n{ name = \"A\" faces = [ { file = \"fonts/a.ttf\" } ] }", font_definition_status_t::INVALID_HEADER },
    };
    for ( const case_t &c : cases ) {
        key_value_document_t *pDocument = ParseFont( c.pSource );
        font_definition_view_t font{};
        font.nFaces = 9u;
        CAPTURE( c.pSource );
        REQUIRE( FontDefinition_Decode( pDocument, nullptr, 0u, &font ).status == c.expected );
        REQUIRE( font.nFaces == 9u );
        KeyValue_DestroyDocument( pDocument );
    }
    REQUIRE( FontDefinition_Identity().nCurrentVersion == CY_FONT_SCHEMA_VERSION );
}
