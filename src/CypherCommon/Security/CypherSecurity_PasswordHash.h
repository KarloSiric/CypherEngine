//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_PasswordHash.h
//  Purpose: Declares Argon2id password hashing and verification.
//  Details: Encoded strings carry algorithm, salt, and work parameters. Passwords
//           must never pass through xxHash, CRC, FNV, or a custom engine digest.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_PASSWORDHASH_H
#define CYPHER_SECURITY_PASSWORDHASH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_Types.h"
#include "CypherCommon_StringView.h"

namespace cypher::security
{

using common::string_view_t;

inline constexpr usize CY_PASSWORD_HASH_STRING_CAPACITY = 128u;

enum class password_hash_profile_t : u8 {
    INTERACTIVE = 0u,
    MODERATE,
    SENSITIVE
};

struct password_hash_t {
    char encoded[CY_PASSWORD_HASH_STRING_CAPACITY]{};
};

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t PasswordHash_Create(
    string_view_t password,
    password_hash_profile_t profile,
    password_hash_t *pHashOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t PasswordHash_Verify(
    string_view_t password,
    const password_hash_t &hash ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t PasswordHash_CheckRehash(
    const password_hash_t &hash,
    password_hash_profile_t profile,
    bool_t *pNeedsRehashOut ) noexcept;

} // namespace cypher::security

#endif // CYPHER_SECURITY_PASSWORDHASH_H
