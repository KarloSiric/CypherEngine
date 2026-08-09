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
    OK = 0u,
    INVALID_ARGUMENT,
    BACKEND_UNAVAILABLE,
    OPERATION_FAILED,
    INVALID_ENCODING,
    AUTHENTICATION_FAILED,
    OUT_OF_MEMORY,
    INVALID_STATE,
    PROTECTION_FAILED,
    BUFFER_TOO_SMALL,
    COUNTER_EXHAUSTED,
    PEER_KEY_REJECTED
};

} // namespace cypher::security

#endif // CYPHER_SECURITY_TYPES_H
