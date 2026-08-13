//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier2/CypherCommon_Tier2_DataValidation_Tests.cpp
//  Purpose: Tests shared semantic validation for authored Cypher data.
//  Details: Covers stable identifiers, canonical VFS paths, traversal rejection,
//           extension policy, byte diagnostics, and explicit length bounds.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_DataValidation.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

data_validation_result_t CheckId( const char *pText )
{
    return DataValidation_CheckStableIdentifier(
        StringView_FromCString( pText ),
        64u );
}

data_validation_result_t CheckPath( const char *pText )
{
    return DataValidation_CheckCanonicalVirtualPath(
        StringView_FromCString( pText ),
        259u );
}

} // namespace

TEST_CASE( "Tier2 validates stable authored identifiers",
           "[CypherCommon][Tier2][DataValidation]" )
{
    REQUIRE( DataValidation_Succeeded( CheckId( "reap" ) ) );
    REQUIRE( DataValidation_Succeeded( CheckId( "world_shader-2" ) ) );
    REQUIRE( CheckId( "" ).status == data_validation_status_t::EMPTY_VALUE );
    REQUIRE( CheckId( "Reap" ).status ==
             data_validation_status_t::INVALID_IDENTIFIER_START );

    const data_validation_result_t invalid = CheckId( "reap.world" );
    REQUIRE( invalid.status ==
             data_validation_status_t::INVALID_IDENTIFIER_BYTE );
    REQUIRE( invalid.iByte == 4u );
}

TEST_CASE( "Tier2 validates canonical virtual paths",
           "[CypherCommon][Tier2][DataValidation]" )
{
    REQUIRE( DataValidation_Succeeded(
        CheckPath( "materials/facility/wall_01.cymat" ) ) );

    REQUIRE( CheckPath( "/materials/wall.cymat" ).status ==
             data_validation_status_t::NON_CANONICAL_PATH );
    REQUIRE( CheckPath( "Materials/wall.cymat" ).status ==
             data_validation_status_t::NON_CANONICAL_PATH );
    REQUIRE( CheckPath( "materials\\wall.cymat" ).status ==
             data_validation_status_t::NON_CANONICAL_PATH );
    REQUIRE( CheckPath( "materials//wall.cymat" ).status ==
             data_validation_status_t::NON_CANONICAL_PATH );
    REQUIRE( CheckPath( "materials/./wall.cymat" ).status ==
             data_validation_status_t::NON_CANONICAL_PATH );
    REQUIRE( CheckPath( "materials/../wall.cymat" ).status ==
             data_validation_status_t::PARENT_TRAVERSAL );
    REQUIRE( CheckPath( "materials/wall file.cymat" ).status ==
             data_validation_status_t::INVALID_PATH_BYTE );
}

TEST_CASE( "Tier2 validates case-sensitive ASCII identifiers",
           "[CypherCommon][Tier2][DataValidation]" )
{
    REQUIRE( DataValidation_Succeeded(
        DataValidation_CheckAsciiIdentifier(
            StringView_FromCString( "CY_WORLD_PASS_2" ),
            64u ) ) );
    REQUIRE( DataValidation_Succeeded(
        DataValidation_CheckAsciiIdentifier(
            StringView_FromCString( "_internal" ),
            64u ) ) );
    REQUIRE( DataValidation_CheckAsciiIdentifier(
                 StringView_FromCString( "2D" ),
                 64u ).status ==
             data_validation_status_t::INVALID_IDENTIFIER_START );
    REQUIRE( DataValidation_CheckAsciiIdentifier(
                 StringView_FromCString( "WORLD-PASS" ),
                 64u ).status ==
             data_validation_status_t::INVALID_IDENTIFIER_BYTE );
}

TEST_CASE( "Tier2 validates typed resource paths and bounds",
           "[CypherCommon][Tier2][DataValidation]" )
{
    const string_view_t material =
        StringView_FromCString( "materials/wall.cymat" );
    REQUIRE( DataValidation_Succeeded(
        DataValidation_CheckResourcePath(
            material,
            StringView_FromCString( ".cymat" ),
            259u ) ) );
    REQUIRE( DataValidation_CheckResourcePath(
                 material,
                 StringView_FromCString( ".cytex" ),
                 259u ).status ==
             data_validation_status_t::EXTENSION_MISMATCH );
    REQUIRE( DataValidation_CheckCanonicalVirtualPath(
                 material,
                 8u ).status ==
             data_validation_status_t::LENGTH_LIMIT );
    REQUIRE( DataValidation_CheckResourcePath(
                 material,
                 StringView_FromCString( "CYMAT" ),
                 259u ).status ==
             data_validation_status_t::INVALID_ARGUMENT );
    REQUIRE( StringView_Equals(
        StringView_FromCString( DataValidation_StatusName(
            data_validation_status_t::PARENT_TRAVERSAL ) ),
        StringView_FromCString( "PARENT_TRAVERSAL" ) ) );
}
