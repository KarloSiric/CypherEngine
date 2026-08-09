//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity.cpp
//  Purpose: Implements CypherSecurity initialization and low-level helpers.
//  Details: libsodium owns platform entropy and cryptographic implementation;
//           Cypher owns validation, type boundaries, and subsystem-level policy.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"

#include <sodium.h>

namespace cypher::security
{

security_status_t Security_Init() noexcept
{
    return sodium_init() >= 0
        ? security_status_t::OK
        : security_status_t::BACKEND_UNAVAILABLE;
}

bool_t Security_IsReady() noexcept
{
    return Security_Init() == security_status_t::OK;
}

void Security_ZeroMemory(
    void *pMemory,
    usize cbMemory ) noexcept
{
    const bool_t bValidMemory = pMemory != nullptr || cbMemory == 0u;
    CY_ASSERT_MSG(
        bValidMemory,
        "Security_ZeroMemory requires memory for a non-empty range." );
    if ( bValidMemory && cbMemory > 0u ) {
        sodium_memzero( pMemory, cbMemory );
    }
}

bool_t Security_ConstantTimeEquals(
    const void *pLeft,
    const void *pRight,
    usize cbData ) noexcept
{
    const bool_t bValidLeft = pLeft != nullptr || cbData == 0u;
    const bool_t bValidRight = pRight != nullptr || cbData == 0u;
    CY_ASSERT_MSG( bValidLeft, "Security comparison requires left-hand bytes." );
    CY_ASSERT_MSG( bValidRight, "Security comparison requires right-hand bytes." );
    if ( !bValidLeft || !bValidRight ) {
        return CY_FALSE;
    }
    return cbData == 0u || sodium_memcmp( pLeft, pRight, cbData ) == 0;
}

} // namespace cypher::security
