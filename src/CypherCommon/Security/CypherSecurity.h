//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity.h
//  Purpose: Declares CypherSecurity initialization and umbrella access.
//  Details: The subsystem wraps audited cryptographic primitives behind stable
//           Cypher contracts; custom engine hashes must never replace this layer.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_H
#define CYPHER_SECURITY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_Types.h"

namespace cypher::security
{

// Initializes libsodium. Repeated and concurrent calls are supported.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t Security_Init() noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t Security_IsReady() noexcept;

// Clears sensitive bytes through a call that the optimizer may not remove.
CYPHER_SECURITY_API void Security_ZeroMemory(
    void *pMemory,
    usize cbMemory ) noexcept;

// Compares equally-sized secret buffers in constant time.
CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t Security_ConstantTimeEquals(
    const void *pLeft,
    const void *pRight,
    usize cbData ) noexcept;

} // namespace cypher::security

#include "CypherSecurity_AEAD.h"
#include "CypherSecurity_Encoding.h"
#include "CypherSecurity_Hash.h"
#include "CypherSecurity_KDF.h"
#include "CypherSecurity_KeyExchange.h"
#include "CypherSecurity_PasswordHash.h"
#include "CypherSecurity_Random.h"
#include "CypherSecurity_SecureMemory.h"
#include "CypherSecurity_SecretStream.h"
#include "CypherSecurity_Signature.h"

#endif // CYPHER_SECURITY_H
