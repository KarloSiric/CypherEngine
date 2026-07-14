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

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Errors pack domains and local codes into stable u32 values", "[CypherCommon][Tier0][Error]" )
{
    const error_t filesystemError = Cy_ErrorMake( domain_t::FILESYSTEM, 0x1234u );
    REQUIRE( Cy_ErrorDomain( filesystemError ) == domain_t::FILESYSTEM );
    REQUIRE( Cy_ErrorLocalCode( filesystemError ) == 0x1234u );

    const error_t maxLocalError = Cy_ErrorMake( domain_t::NETWORK, 0xFFFFu );
    REQUIRE( Cy_ErrorDomain( maxLocalError ) == domain_t::NETWORK );
    REQUIRE( Cy_ErrorLocalCode( maxLocalError ) == 0xFFFFu );
}

TEST_CASE( "Errors treat zero local code as success for any domain", "[CypherCommon][Tier0][Error]" )
{
    const error_t commonOk = Cy_ErrorMake( domain_t::COMMON, 0u );
    const error_t renderOk = Cy_ErrorMake( domain_t::RENDER, 0u );
    const error_t renderFailed = Cy_ErrorMake( domain_t::RENDER, 1u );

    REQUIRE( Cy_ErrorSucceeded( commonOk ) );
    REQUIRE( Cy_ErrorSucceeded( renderOk ) );
    REQUIRE_FALSE( Cy_ErrorFailed( renderOk ) );
    REQUIRE( Cy_ErrorFailed( renderFailed ) );
}

TEST_CASE( "Errors expose common names and descriptions through the common table", "[CypherCommon][Tier0][Error]" )
{
    const error_t timeout = Cy_ErrorMake(
        domain_t::COMMON,
        static_cast<u16>( common_error_t::ERR_TIMEOUT ) );

    const error_table_t *pTable = Cy_CommonErrorTable();
    REQUIRE( pTable != nullptr );
    REQUIRE( pTable->domain == domain_t::COMMON );
    REQUIRE( Cy_ErrorFindDesc( *pTable, timeout ) != nullptr );
    REQUIRE( Cy_ErrorFindName( *pTable, timeout ) == Cy_CommonErrorName( common_error_t::ERR_TIMEOUT ) );
    REQUIRE( Cy_ErrorFindDescription( *pTable, timeout ) == Cy_CommonErrorDescription( common_error_t::ERR_TIMEOUT ) );
}

TEST_CASE( "Errors reject table lookups from the wrong domain", "[CypherCommon][Tier0][Error]" )
{
    const error_t filesystemError = Cy_ErrorMake( domain_t::FILESYSTEM, 1u );
    const error_table_t *pTable = Cy_CommonErrorTable();

    REQUIRE( pTable != nullptr );
    REQUIRE( Cy_ErrorFindDesc( *pTable, filesystemError ) == nullptr );
    REQUIRE( Cy_ErrorFindName( *pTable, filesystemError ) != nullptr );
    REQUIRE( Cy_ErrorFindDescription( *pTable, filesystemError ) != nullptr );
}

TEST_CASE( "Errors keep compatibility aliases mapped to their canonical domains", "[CypherCommon][Tier0][Error]" )
{
    REQUIRE( domain_t::COM_DOMAIN_COMMON == domain_t::COMMON );
    REQUIRE( domain_t::COM_DOMAIN_FILESYSTEM == domain_t::FILESYSTEM );
    REQUIRE( domain_t::COM_DOMAIN_TOOLS == domain_t::TOOLS );
    REQUIRE( domain_t::COM_DOMAIN_ASSET == domain_t::ASSET );
    REQUIRE( domain_t::COM_DOMAIN_RESOURCE == domain_t::RESOURCE );
    REQUIRE( domain_t::COM_DOMAIN_REFLECTION == domain_t::REFLECTION );

    REQUIRE( Cy_ErrorDomainName( domain_t::COM_DOMAIN_ASSET ) != nullptr );
    REQUIRE( Cy_ErrorDomainName( domain_t::COM_DOMAIN_REFLECTION ) != nullptr );
}
