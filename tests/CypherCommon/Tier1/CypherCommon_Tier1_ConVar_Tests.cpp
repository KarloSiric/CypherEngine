//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ConVar_Tests.cpp
//  Purpose: Tests typed console-variable parsing and descriptor contracts.
//  Details: Covers primitive conversion, range validation, transactional failure,
//           borrowed strings, and locale-independent round-trip formatting.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ConVar.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;

namespace
{

convar_desc_t NumericDesc()
{
    convar_desc_t desc{};
    desc.name = StringView_FromCString( "player.speed" );
    desc.help = StringView_FromCString( "Player movement speed." );
    desc.type = convar_type_t::F64;
    desc.defaultValue = StringView_FromCString( "320.0" );
    desc.minValue = StringView_FromCString( "1.0" );
    desc.maxValue = StringView_FromCString( "1000.0" );
    return desc;
}

} // namespace

TEST_CASE( "ConVar shares the strict console identifier grammar", "[CypherCommon][Tier1][ConVar]" )
{
    REQUIRE( ConVar_IsValidName( StringView_FromCString( "r_wireframe" ) ) );
    REQUIRE( ConVar_IsValidName( StringView_FromCString( "audio.volume-master" ) ) );
    REQUIRE_FALSE( ConVar_IsValidName( StringView_FromCString( "1volume" ) ) );
    REQUIRE_FALSE( ConVar_IsValidName( StringView_FromCString( "audio/volume" ) ) );
}

TEST_CASE( "ConVar parses Boolean integer float and borrowed string values", "[CypherCommon][Tier1][ConVar]" )
{
    convar_value_t value{};

    REQUIRE( ConVar_ParseSucceeded( ConVar_ParseValue(
        convar_type_t::BOOL,
        StringView_FromCString( " FALSE " ),
        &value ) ) );
    REQUIRE( value.value.type == variant_type_t::BOOL );
    REQUIRE_FALSE( value.value.data.bValue );

    REQUIRE( ConVar_ParseSucceeded( ConVar_ParseValue(
        convar_type_t::I64,
        StringView_FromCString( "-0x7f" ),
        &value ) ) );
    REQUIRE( value.value.data.iValue == -127 );

    REQUIRE( ConVar_ParseSucceeded( ConVar_ParseValue(
        convar_type_t::U64,
        StringView_FromCString( "1_000_000" ),
        &value ) ) );
    REQUIRE( value.value.data.uValue == 1000000u );

    REQUIRE( ConVar_ParseSucceeded( ConVar_ParseValue(
        convar_type_t::F64,
        StringView_FromCString( " 3.125 " ),
        &value ) ) );
    REQUIRE( value.value.data.flValue == Catch::Approx( 3.125 ) );

    const char text[] = "facility sector";
    REQUIRE( ConVar_ParseSucceeded( ConVar_ParseValue(
        convar_type_t::STRING,
        StringView_FromCString( text ),
        &value ) ) );
    REQUIRE( value.value.data.stringValue.pData == text );
    REQUIRE( value.value.data.stringValue.cchLength == 15u );
}

TEST_CASE( "ConVar preserves output and exposes scalar failures", "[CypherCommon][Tier1][ConVar]" )
{
    convar_value_t value{ Variant_FromU64( 77u ) };
    const convar_parse_result_t result = ConVar_ParseValue(
        convar_type_t::I64,
        StringView_FromCString( "12bad" ),
        &value );

    REQUIRE( result.status == convar_parse_status_t::INVALID_VALUE );
    REQUIRE( result.scalarResult.status == string_parse_status_t::TRAILING_CHARACTERS );
    REQUIRE( value.value.type == variant_type_t::U64 );
    REQUIRE( value.value.data.uValue == 77u );

    REQUIRE( ConVar_ParseValue(
        convar_type_t::F64,
        StringView_FromCString( "nan" ),
        &value ).status == convar_parse_status_t::INVALID_VALUE );
    REQUIRE( ConVar_ParseValue(
        convar_type_t::U64,
        StringView_FromCString( "-1" ),
        &value ).status == convar_parse_status_t::INVALID_VALUE );

    const char textWithNull[] = { 'a', '\0', 'b' };
    REQUIRE( ConVar_ParseValue(
        convar_type_t::STRING,
        StringView_FromRange( textWithNull, sizeof( textWithNull ) ),
        &value ).status == convar_parse_status_t::EMBEDDED_NULL );
}

TEST_CASE( "ConVar validates coherent defaults and numeric bounds", "[CypherCommon][Tier1][ConVar]" )
{
    convar_desc_t desc = NumericDesc();
    REQUIRE( ConVar_ValidateDesc( desc ) );

    desc.minValue = StringView_FromCString( "500" );
    desc.maxValue = StringView_FromCString( "100" );
    REQUIRE_FALSE( ConVar_ValidateDesc( desc ) );

    desc = NumericDesc();
    desc.defaultValue = StringView_FromCString( "2000" );
    REQUIRE_FALSE( ConVar_ValidateDesc( desc ) );

    desc = NumericDesc();
    desc.minValue = StringView_FromCString( "not-a-number" );
    REQUIRE_FALSE( ConVar_ValidateDesc( desc ) );

    desc = {};
    desc.name = StringView_FromCString( "game.enabled" );
    desc.type = convar_type_t::BOOL;
    desc.defaultValue = StringView_FromCString( "true" );
    REQUIRE( ConVar_ValidateDesc( desc ) );
    desc.minValue = StringView_FromCString( "false" );
    REQUIRE_FALSE( ConVar_ValidateDesc( desc ) );

    desc = NumericDesc();
    desc.flags = CYPHER_BIT32( 31 );
    REQUIRE_FALSE( ConVar_ValidateDesc( desc ) );

    desc = NumericDesc();
    desc.flags = CONVAR_FLAG_READ_ONLY |
                 CONVAR_FLAG_REMOTE_WRITE_ALLOWED;
    REQUIRE_FALSE( ConVar_ValidateDesc( desc ) );
}

TEST_CASE( "ConVar descriptor parsing rejects values outside bounds", "[CypherCommon][Tier1][ConVar]" )
{
    const convar_desc_t desc = NumericDesc();
    convar_value_t value{ Variant_FromF64( 99.0 ) };

    REQUIRE( ConVar_ParseValueForDesc(
        desc,
        StringView_FromCString( "0.5" ),
        &value ).status == convar_parse_status_t::BELOW_MINIMUM );
    REQUIRE( value.value.data.flValue == Catch::Approx( 99.0 ) );

    REQUIRE( ConVar_ParseValueForDesc(
        desc,
        StringView_FromCString( "1001" ),
        &value ).status == convar_parse_status_t::ABOVE_MAXIMUM );
    REQUIRE( value.value.data.flValue == Catch::Approx( 99.0 ) );

    REQUIRE( ConVar_ParseSucceeded( ConVar_ParseValueForDesc(
        desc,
        StringView_FromCString( "640" ),
        &value ) ) );
    REQUIRE( value.value.data.flValue == Catch::Approx( 640.0 ) );
}

TEST_CASE( "ConVar identifies exact variant types", "[CypherCommon][Tier1][ConVar]" )
{
    REQUIRE( ConVar_ValueMatchesType(
        convar_type_t::BOOL,
        { Variant_FromBool( CY_TRUE ) } ) );
    REQUIRE( ConVar_ValueMatchesType(
        convar_type_t::STRING,
        { Variant_FromString( StringView_FromCString( "value" ) ) } ) );
    REQUIRE_FALSE( ConVar_ValueMatchesType(
        convar_type_t::I64,
        { Variant_FromU64( 1u ) } ) );
}

TEST_CASE( "ConVar formats values with required-length and round-trip behavior", "[CypherCommon][Tier1][ConVar]" )
{
    char text[128]{};
    REQUIRE( ConVar_FormatValue(
        { Variant_FromBool( CY_TRUE ) },
        text,
        sizeof( text ) ) == 4u );
    REQUIRE( std::string( text ) == "true" );

    REQUIRE( ConVar_FormatValue(
        { Variant_FromI64( CY_I64_MIN ) },
        text,
        sizeof( text ) ) == 20u );
    REQUIRE( std::string( text ) == "-9223372036854775808" );

    const f64 source = 1.0 / 10.0;
    REQUIRE( ConVar_FormatValue(
        { Variant_FromF64( source ) },
        text,
        sizeof( text ) ) > 0u );
    convar_value_t parsed{};
    REQUIRE( ConVar_ParseSucceeded( ConVar_ParseValue(
        convar_type_t::F64,
        StringView_FromCString( text ),
        &parsed ) ) );
    REQUIRE( parsed.value.data.flValue == source );

    char small[4]{};
    REQUIRE( ConVar_FormatValue(
        { Variant_FromString( StringView_FromCString( "abcdef" ) ) },
        small,
        sizeof( small ) ) == 6u );
    REQUIRE( std::string( small ) == "abc" );
}

TEST_CASE( "ConVar exposes stable parse status names", "[CypherCommon][Tier1][ConVar]" )
{
    REQUIRE( std::string( ConVar_ParseStatusName( convar_parse_status_t::OK ) ) == "OK" );
    REQUIRE( std::string( ConVar_ParseStatusName(
        static_cast<convar_parse_status_t>( 0xFFu ) ) ) ==
        "UNKNOWN_CONVAR_PARSE_STATUS" );
}
