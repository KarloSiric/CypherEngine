//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Validator_Tests.cpp
//  Purpose: Tests Tier0 recoverable validation reporting.
//  Details: These checks protect structured records, callback context,
//           registration replacement, null normalization, and severity names.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Validator.h"

#include <cstring>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct validation_capture_t {
    u32 count{ 0u };
    validation_record_t record{};
};

void CaptureValidation( const validation_record_t &record, void *pUserData ) noexcept
{
    auto *pCapture = static_cast<validation_capture_t *>( pUserData );
    ++pCapture->count;
    pCapture->record = record;
}

void ReplaceValidation( const validation_record_t &, void * ) noexcept
{
    Cy_ValidatorSetCallback( nullptr, nullptr );
}

} // namespace

TEST_CASE( "Validator forwards structured recoverable issues", "[CypherCommon][Tier0][Validator]" )
{
    validation_capture_t capture{};
    Cy_ValidatorSetCallback( CaptureValidation, &capture );

    const error_code_t errorCode = CY_ERROR( ASSET, 9u );
    const source_location_t location{ "asset.cpp", "ValidateAsset", 18u, 2u };
    Cy_ValidatorReportAt(
        validator_severity_t::Warning,
        errorCode,
        "missing optional field",
        location );

    REQUIRE( capture.count == 1u );
    REQUIRE( capture.record.severity == validator_severity_t::Warning );
    REQUIRE( capture.record.errorCode == errorCode );
    REQUIRE( capture.record.location.line == 18u );
    REQUIRE( std::strcmp( capture.record.pMessage, "missing optional field" ) == 0 );

    Cy_ValidatorSetCallback( nullptr, nullptr );
}

TEST_CASE( "Validator callbacks may change registration", "[CypherCommon][Tier0][Validator]" )
{
    Cy_ValidatorSetCallback( ReplaceValidation, nullptr );
    Cy_ValidatorReport( validator_severity_t::Info, "replace" );

    validator_callback_t pCallback = ReplaceValidation;
    void *pUserData = reinterpret_cast<void *>( static_cast<uintptr>( 1u ) );
    Cy_ValidatorGetCallback( &pCallback, &pUserData );
    REQUIRE( pCallback == nullptr );
    REQUIRE( pUserData == nullptr );
}

TEST_CASE( "Validator normalizes null messages and names sentinels", "[CypherCommon][Tier0][Validator]" )
{
    validation_capture_t capture{};
    Cy_ValidatorSetCallback( CaptureValidation, &capture );
    Cy_ValidatorReport( validator_severity_t::Error, nullptr );

    REQUIRE( capture.record.pMessage != nullptr );
    REQUIRE( capture.record.pMessage[0] == '\0' );
    REQUIRE( std::strcmp( Cy_ValidatorSeverityName( validator_severity_t::Fatal ), "Fatal" ) == 0 );
    REQUIRE( std::strcmp( Cy_ValidatorSeverityName( validator_severity_t::Count ), "Unknown" ) == 0 );

    Cy_ValidatorSetCallback( nullptr, nullptr );
}

