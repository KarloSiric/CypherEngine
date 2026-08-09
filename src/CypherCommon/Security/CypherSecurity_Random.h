//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_Random.h
//  Purpose: Declares cryptographically secure random-data services.
//  Details: Random data comes from libsodium's operating-system-backed CSPRNG.
//           This API is reserved for keys, nonces, salts, challenges, and tokens.
//           Gameplay and simulation randomness require a separate deterministic API.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_RANDOM_H
#define CYPHER_SECURITY_RANDOM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_Types.h"

namespace cypher::security
{

using common::u32;
using common::u64;

// Fills a caller-owned memory range with cryptographically secure random bytes.
// A null output pointer is valid only when cbOutput is zero.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityRandom_Fill(
    void *pOutput,
    usize cbOutput ) noexcept;

// Generates a uniformly distributed value across the complete u32 range.
// The output remains unchanged when the operation fails.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityRandom_U32(
    u32 *pValueOut ) noexcept;

// Generates a uniformly distributed value across the complete u64 range.
// The output remains unchanged when the operation fails.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityRandom_U64(
    u64 *pValueOut ) noexcept;

// Generates an unbiased value in the range [0, nUpperBoundExclusive).
// An upper bound of zero is invalid; an upper bound of one always produces zero.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityRandom_UniformU32(
    u32 nUpperBoundExclusive,
    u32 *pValueOut ) noexcept;

// Generates an unbiased value in the range [0, nUpperBoundExclusive).
// An upper bound of zero is invalid; an upper bound of one always produces zero.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityRandom_UniformU64(
    u64 nUpperBoundExclusive,
    u64 *pValueOut ) noexcept;

} // namespace cypher::security

#endif // CYPHER_SECURITY_RANDOM_H
