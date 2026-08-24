//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_Types.h
//  Purpose: Declares shared CypherSecurity result types.
//  Details: A compact status vocabulary keeps cryptographic callers explicit
//           without exposing backend-specific integer return codes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Types Contract

Security types use fixed sizes and explicit validity rules. Secret buffers are not ordinary
strings and must never be logged, implicitly copied, or compared with early-exit equality.
================
*/

#ifndef CYPHER_SECURITY_TYPES_H
#define CYPHER_SECURITY_TYPES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::security
{

using common::bool_t;
using common::byte;
using common::hash64_t;
using common::u8;
using common::usize;
using common::CY_FALSE;
using common::CY_TRUE;

enum class security_status_t : u8 {
    OK = 0u,               // Operation completed successfully.
    INVALID_ARGUMENT,      // A pointer, size, policy, or value violates the contract.
    BACKEND_UNAVAILABLE,   // The cryptographic backend could not be initialized.
    OPERATION_FAILED,      // Backend operation failed without a narrower status.
    INVALID_ENCODING,      // Text or serialized cryptographic data is malformed.
    AUTHENTICATION_FAILED, // Tag, password, or signature verification failed.
    OUT_OF_MEMORY,         // Guarded or ordinary allocation could not be obtained.
    INVALID_STATE,         // Object lifecycle does not permit this operation.
    PROTECTION_FAILED,     // Required page locking or protection transition failed.
    BUFFER_TOO_SMALL,      // Caller-provided destination cannot hold the result.
    COUNTER_EXHAUSTED,     // A nonce counter reached its non-reusable terminal state.
    PEER_KEY_REJECTED      // Peer public key was invalid or produced an unsafe exchange.
};

} // namespace cypher::security

#endif // CYPHER_SECURITY_TYPES_H
