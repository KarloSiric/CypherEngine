//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_Random.cpp
//  Purpose: Implements cryptographically secure random-data services.
//  Details: The implementation validates Cypher contracts before delegating
//           entropy generation and unbiased bounded sampling to libsodium.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity_Random.h"

#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"

#include <sodium.h>

namespace cypher::security
{

security_status_t SecurityRandom_Fill(
    void *pOutput,
    usize cbOutput ) noexcept
{
    const bool_t bValidOutput = ( pOutput != nullptr || cbOutput == 0u );
    CY_ASSERT_MSG( bValidOutput, "SecurityRandom_Fill requires output storage for a non-empty range." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( cbOutput == 0u ) {
        return security_status_t::OK;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }
    randombytes_buf( pOutput, cbOutput );

    return security_status_t::OK;
}

security_status_t SecurityRandom_U32(
    u32 *pValueOut ) noexcept
{
    const bool_t bValidOutput = pValueOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "SecurityRandom_U32 requires output storage." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    u32 nValue = 0u;
    const security_status_t result = SecurityRandom_Fill( &nValue, sizeof( nValue ) );
    if ( result != security_status_t::OK ) {
        return result;
    }

    *pValueOut = nValue;
    return security_status_t::OK;
}

security_status_t SecurityRandom_U64(
    u64 *pValueOut ) noexcept
{
    const bool_t bValidOutput = pValueOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "SecurityRandom_U64 requires output storage." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    u64 nValue = 0u;
    const security_status_t result = SecurityRandom_Fill( &nValue, sizeof( nValue ) );
    if ( result != security_status_t::OK ) {
        return result;
    }

    *pValueOut = nValue;
    return security_status_t::OK;
}

security_status_t SecurityRandom_UniformU32(
    u32 nUpperBoundExclusive,
    u32 *pValueOut ) noexcept
{
    const bool_t bValidOutput = pValueOut != nullptr;
    const bool_t bValidBound = nUpperBoundExclusive > 0u;

    CY_ASSERT_MSG( bValidOutput, "SecurityRandom_UniformU32 requires output storage." );
    CY_ASSERT_MSG( bValidBound, "SecurityRandom_UniformU32 requires a non-zero upper bound." );

    if ( !bValidOutput || !bValidBound ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( nUpperBoundExclusive == 1u ) {
        *pValueOut = 0u;
        return security_status_t::OK;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }
    const u32 nValue = randombytes_uniform( nUpperBoundExclusive );
    *pValueOut = nValue;

    return security_status_t::OK;
}

security_status_t SecurityRandom_UniformU64(
    u64 nUpperBoundExclusive,
    u64 *pValueOut ) noexcept
{
    const bool_t bValidOutput = pValueOut != nullptr;
    const bool_t bValidBound = nUpperBoundExclusive > 0u;

    CY_ASSERT_MSG( bValidOutput, "SecurityRandom_UniformU64 requires output storage." );
    CY_ASSERT_MSG( bValidBound, "SecurityRandom_UniformU64 requires a non-zero upper bound." );

    if ( !bValidOutput || !bValidBound ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    if ( nUpperBoundExclusive == 1u ) {
        *pValueOut = 0u;
        return security_status_t::OK;
    }

    // Unsigned wrap computes 2^64 modulo the requested bound.
    const u64 nThreshold = ( ( static_cast<u64>( 0u ) - nUpperBoundExclusive ) % nUpperBoundExclusive );
    u64 nRandomValue = 0u;

    do {
        const security_status_t result = SecurityRandom_U64( &nRandomValue );
        if ( result != security_status_t::OK ) {
            return result;
        }
    } while ( nRandomValue < nThreshold );

    *pValueOut = nRandomValue % nUpperBoundExclusive;

    return security_status_t::OK;
}

} // namespace cypher::security
