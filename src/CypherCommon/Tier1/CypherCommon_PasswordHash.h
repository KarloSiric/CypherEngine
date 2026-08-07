//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_PasswordHash.h
//  Purpose: Declares the libsodium-backed password hashing adapter.
//  Details: Password hashing must use the audited third-party implementation. This
//           API must never be replaced by engine hash, checksum, or custom crypto code.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_PASSWORDHASH_H
#define CYPHER_COMMON_TIER1_PASSWORDHASH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

constexpr usize CY_PASSWORD_HASH_STRING_CAPACITY = 128u;

enum class password_hash_profile_t : u8 {
    INTERACTIVE = 0u,
    MODERATE,
    SENSITIVE
};

enum class password_hash_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    BACKEND_UNAVAILABLE,
    OUT_OF_MEMORY,
    HASH_FAILED,
    INVALID_HASH,
    MISMATCH
};

struct password_hash_t {
    char encoded[CY_PASSWORD_HASH_STRING_CAPACITY]{};
};

CYPHER_NODISCARD CYPHER_COMMON_API
password_hash_status_t PasswordHash_Create(
    string_view_t password,
    password_hash_profile_t profile,
    password_hash_t *pHashOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
password_hash_status_t PasswordHash_Verify(
    string_view_t password,
    const password_hash_t &hash ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t PasswordHash_NeedsRehash(
    const password_hash_t &hash,
    password_hash_profile_t profile ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_PASSWORDHASH_H
