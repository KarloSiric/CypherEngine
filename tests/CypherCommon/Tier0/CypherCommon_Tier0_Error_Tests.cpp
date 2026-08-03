//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Error_Tests.cpp
//  Purpose: Tests Tier0 packed error behavior.
//  Details: This file guards domain packing, local code extraction, lookup
//           tables, and compatibility aliases used by subsystem diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-07-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon/Tier0/CypherCommon_Error.h"

#include <string_view>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Errors pack domains and local codes into stable u32 values", "[CypherCommon][Tier0][Error]" )
{
    constexpr error_code_t filesystemError = Cy_ErrorMake( error_domain_t::FILESYSTEM, 0x1234u );
    STATIC_REQUIRE( filesystemError == CY_ERROR( FILESYSTEM, 0x1234u ) );
    REQUIRE( Cy_ErrorDomain( filesystemError ) == error_domain_t::FILESYSTEM );
    REQUIRE( Cy_ErrorLocalCode( filesystemError ) == 0x1234u );

    const error_code_t maxLocalError = Cy_ErrorMake( error_domain_t::NETWORK, 0xFFFFu );
    REQUIRE( Cy_ErrorDomain( maxLocalError ) == error_domain_t::NETWORK );
    REQUIRE( Cy_ErrorLocalCode( maxLocalError ) == 0xFFFFu );
    STATIC_REQUIRE( Cy_ErrorMake( common_error_t::OK ) == CY_ERROR_OK );
    STATIC_REQUIRE( CY_ERROR_COMMON( ERR_TIMEOUT ) == Cy_ErrorMake( common_error_t::ERR_TIMEOUT ) );
}

TEST_CASE( "Errors require a registered domain and zero local code for success", "[CypherCommon][Tier0][Error]" )
{
    const error_code_t commonOk = Cy_ErrorMake( error_domain_t::COMMON, 0u );
    const error_code_t renderOk = Cy_ErrorMake( error_domain_t::RENDER, 0u );
    const error_code_t renderFailed = Cy_ErrorMake( error_domain_t::RENDER, 1u );
    const error_code_t countDomain = Cy_ErrorMake( error_domain_t::COUNT, 0u );
    const error_code_t invalidDomain = Cy_ErrorMake( error_domain_t::INVALID, 0u );

    STATIC_REQUIRE( Cy_ErrorDomainIsValid( error_domain_t::COMMON ) );
    STATIC_REQUIRE( Cy_ErrorDomainIsValid( error_domain_t::REFLECTION ) );
    STATIC_REQUIRE_FALSE( Cy_ErrorDomainIsValid( error_domain_t::COUNT ) );
    STATIC_REQUIRE_FALSE( Cy_ErrorDomainIsValid( error_domain_t::INVALID ) );
    REQUIRE( Cy_ErrorSucceeded( commonOk ) );
    REQUIRE( Cy_ErrorSucceeded( renderOk ) );
    REQUIRE_FALSE( Cy_ErrorFailed( renderOk ) );
    REQUIRE( Cy_ErrorFailed( renderFailed ) );
    REQUIRE( Cy_ErrorFailed( countDomain ) );
    REQUIRE( Cy_ErrorFailed( invalidDomain ) );
}

TEST_CASE( "Errors expose common names and descriptions through the common table", "[CypherCommon][Tier0][Error]" )
{
    const error_code_t timeout = Cy_ErrorMake(
        error_domain_t::COMMON,
        static_cast<u16>( common_error_t::ERR_TIMEOUT ) );

    const error_table_t *pTable = Cy_CommonErrorTable();
    REQUIRE( pTable != nullptr );
    REQUIRE( pTable->domain == error_domain_t::COMMON );
    REQUIRE( Cy_ErrorFindDesc( *pTable, timeout ) != nullptr );
    REQUIRE( Cy_ErrorFindName( *pTable, timeout ) == Cy_CommonErrorName( common_error_t::ERR_TIMEOUT ) );
    REQUIRE( Cy_ErrorFindDescription( *pTable, timeout ) == Cy_CommonErrorDescription( common_error_t::ERR_TIMEOUT ) );
}

TEST_CASE( "Errors reject table lookups from the wrong domain", "[CypherCommon][Tier0][Error]" )
{
    const error_code_t filesystemError = Cy_ErrorMake( error_domain_t::FILESYSTEM, 1u );
    const error_table_t *pTable = Cy_CommonErrorTable();

    REQUIRE( pTable != nullptr );
    REQUIRE( Cy_ErrorFindDesc( *pTable, filesystemError ) == nullptr );
    REQUIRE( std::string_view( Cy_ErrorFindName( *pTable, filesystemError ) ) == "ERR_UNKNOWN" );
    REQUIRE( std::string_view( Cy_ErrorFindDescription( *pTable, filesystemError ) ) == "Unknown error." );
}

TEST_CASE( "Errors reject malformed and unknown table entries", "[CypherCommon][Tier0][Error]" )
{
    const error_code_t unknownCommon = Cy_ErrorMake(
        error_domain_t::COMMON,
        static_cast<u16>( common_error_t::COUNT ) );
    const error_table_t emptyTable{ error_domain_t::COMMON, nullptr, 0u };

    REQUIRE( Cy_ErrorFindDesc( emptyTable, unknownCommon ) == nullptr );
    REQUIRE( std::string_view( Cy_ErrorFindName( emptyTable, unknownCommon ) ) == "ERR_UNKNOWN" );
    REQUIRE( std::string_view( Cy_ErrorFindDescription( emptyTable, unknownCommon ) ) == "Unknown error." );
    REQUIRE( std::string_view( Cy_CommonErrorName( common_error_t::COUNT ) ) == "ERR_UNKNOWN" );
    REQUIRE( std::string_view( Cy_ErrorDomainName( error_domain_t::INVALID ) ) == "Unknown" );
}

TEST_CASE( "Errors keep compatibility aliases mapped to their canonical domains", "[CypherCommon][Tier0][Error]" )
{
    REQUIRE( error_domain_t::COM_DOMAIN_COMMON == error_domain_t::COMMON );
    REQUIRE( error_domain_t::COM_DOMAIN_FILESYSTEM == error_domain_t::FILESYSTEM );
    REQUIRE( error_domain_t::COM_DOMAIN_TOOLS == error_domain_t::TOOLS );
    REQUIRE( error_domain_t::COM_DOMAIN_ASSET == error_domain_t::ASSET );
    REQUIRE( error_domain_t::COM_DOMAIN_RESOURCE == error_domain_t::RESOURCE );
    REQUIRE( error_domain_t::COM_DOMAIN_REFLECTION == error_domain_t::REFLECTION );

    REQUIRE( std::string_view( Cy_ErrorDomainName( error_domain_t::COM_DOMAIN_ASSET ) ) == "Asset" );
    REQUIRE( std::string_view( Cy_ErrorDomainName( error_domain_t::COM_DOMAIN_REFLECTION ) ) == "Reflection" );
}
