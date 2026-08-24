//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_KDF.h
//  Purpose: Declares domain-separated cryptographic key derivation.
//  Details: One guarded master key can derive independent fixed-purpose subkeys.
//           Context and identifier separation prevents accidental key reuse across
//           engine protocols, packages, tools, and persistent data.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_KDF_H
#define CYPHER_SECURITY_KDF_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_SecureMemory.h"
#include "CypherCommon_BinaryBlock.h"

namespace cypher::security
{

using common::binary_block_t;
using common::u64;

inline constexpr usize CY_SECURITY_KDF_CONTEXT_SIZE = 8u;
inline constexpr usize CY_SECURITY_KDF_MASTER_KEY_SIZE = 32u;
inline constexpr usize CY_SECURITY_KDF_SUBKEY_MIN_SIZE = 16u;
inline constexpr usize CY_SECURITY_KDF_SUBKEY_MAX_SIZE = 64u;

struct kdf_context_t {
    char bytes[CY_SECURITY_KDF_CONTEXT_SIZE]{}; // Exact public domain-separation label.
};

struct kdf_master_key_t {
    secure_memory_t memory{}; // Guarded root key used only for subkey derivation.
    kdf_master_key_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( kdf_master_key_t );
};

struct kdf_subkey_t {
    secure_memory_t memory{}; // Guarded derived key for one context and identifier.
    kdf_subkey_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( kdf_subkey_t );
};

// Creates the exact eight-byte public context used to separate key domains.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityKdf_ContextFromBytes(
    const char *pContext,
    usize cchContext,
    kdf_context_t *pContextOut ) noexcept;

// Generates a new high-entropy master key in guarded read-only storage.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityKdf_GenerateMasterKey(
    secure_memory_lock_policy_t lockPolicy,
    kdf_master_key_t *pKeyOut ) noexcept;

// Imports exactly CY_SECURITY_KDF_MASTER_KEY_SIZE bytes into guarded storage.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityKdf_ImportMasterKey(
    binary_block_t keyBytes,
    secure_memory_lock_policy_t lockPolicy,
    kdf_master_key_t *pKeyOut ) noexcept;

CYPHER_SECURITY_API void SecurityKdf_DestroyMasterKey(
    kdf_master_key_t *pKey ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecurityKdf_MasterKeyIsValid(
    const kdf_master_key_t *pKey ) noexcept;

// Derives one deterministic subkey. Reusing an identifier is safe only when the
// context intentionally names the same key purpose.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityKdf_DeriveSubkey(
    const kdf_master_key_t *pMasterKey,
    const kdf_context_t &context,
    u64 nSubkeyId,
    usize cbSubkey,
    secure_memory_lock_policy_t lockPolicy,
    kdf_subkey_t *pSubkeyOut ) noexcept;

CYPHER_SECURITY_API void SecurityKdf_DestroySubkey(
    kdf_subkey_t *pSubkey ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecurityKdf_SubkeyIsValid(
    const kdf_subkey_t *pSubkey ) noexcept;

// Borrows derived key bytes while the subkey remains alive and read-only.
CYPHER_NODISCARD CYPHER_SECURITY_API
binary_block_t SecurityKdf_SubkeyBlock(
    const kdf_subkey_t *pSubkey ) noexcept;

} // namespace cypher::security

#endif // CYPHER_SECURITY_KDF_H
