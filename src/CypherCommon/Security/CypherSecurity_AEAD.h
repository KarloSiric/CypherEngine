//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_AEAD.h
//  Purpose: Declares authenticated symmetric encryption and nonce policy.
//  Details: XChaCha20-Poly1305 encrypts payloads and authenticates both payloads
//           and optional public metadata. Explicit nonce APIs make reuse policy
//           visible at every protocol boundary.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_AEAD_H
#define CYPHER_SECURITY_AEAD_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_SecureMemory.h"
#include "CypherCommon_BinaryBlock.h"

namespace cypher::security
{

using common::binary_block_t;
using common::u64;

inline constexpr usize CY_SECURITY_AEAD_KEY_SIZE = 32u;
inline constexpr usize CY_SECURITY_AEAD_NONCE_SIZE = 24u;
inline constexpr usize CY_SECURITY_AEAD_NONCE_PREFIX_SIZE = 16u;
inline constexpr usize CY_SECURITY_AEAD_TAG_SIZE = 16u;

struct aead_key_t {
    secure_memory_t memory{}; // Guarded CY_SECURITY_AEAD_KEY_SIZE-byte key storage.
    aead_key_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( aead_key_t );
};

struct aead_nonce_t {
    byte bytes[CY_SECURITY_AEAD_NONCE_SIZE]{}; // Public XChaCha20 nonce bytes.
};

// A sequence combines a random 128-bit prefix with a 64-bit counter. One
// sequence must never be reused with the same key after restart or reset.
struct aead_nonce_sequence_t {
    byte prefix[CY_SECURITY_AEAD_NONCE_PREFIX_SIZE]{}; // Random per-sequence nonce domain.
    u64 nNextCounter{ 0u };                            // Counter assigned by the next call.
    bool_t bInitialized{ CY_FALSE };                   // Prefix and counter are usable.
    bool_t bExhausted{ CY_FALSE };                     // Counter wrapped; no nonce may be emitted.
};

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t AeadKey_Generate(
    secure_memory_lock_policy_t lockPolicy,
    aead_key_t *pKeyOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t AeadKey_Import(
    binary_block_t keyBytes,
    secure_memory_lock_policy_t lockPolicy,
    aead_key_t *pKeyOut ) noexcept;

CYPHER_SECURITY_API void AeadKey_Destroy(
    aead_key_t *pKey ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t AeadKey_IsValid(
    const aead_key_t *pKey ) noexcept;

// Generates a full random XChaCha20 nonce suitable for an independent message.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t AeadNonce_Generate(
    aead_nonce_t *pNonceOut ) noexcept;

// Initializes a new random-prefix sequence at the requested first counter.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t AeadNonceSequence_Init(
    u64 nFirstCounter,
    aead_nonce_sequence_t *pSequenceOut ) noexcept;

// Restores a sequence from a persisted 16-byte prefix and next counter.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t AeadNonceSequence_InitFromPrefix(
    binary_block_t prefix,
    u64 nFirstCounter,
    aead_nonce_sequence_t *pSequenceOut ) noexcept;

// Emits the current nonce and advances the counter exactly once.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t AeadNonceSequence_Next(
    aead_nonce_sequence_t *pSequence,
    aead_nonce_t *pNonceOut ) noexcept;

CYPHER_SECURITY_API void AeadNonceSequence_Reset(
    aead_nonce_sequence_t *pSequence ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t Aead_CiphertextSize(
    usize cbPlaintext,
    usize *pSizeOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t Aead_PlaintextSize(
    usize cbCiphertext,
    usize *pSizeOut ) noexcept;

// Encrypts plaintext and appends its authentication tag. Plaintext,
// authenticated data, and output storage must not overlap.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t Aead_Encrypt(
    binary_block_t plaintext,
    binary_block_t authenticatedData,
    const aead_nonce_t &nonce,
    const aead_key_t *pKey,
    void *pCiphertextOut,
    usize cbCiphertextCapacity,
    usize *pCiphertextSizeOut ) noexcept;

// Authenticates before returning plaintext. Authentication failure clears the
// candidate plaintext range and never reports output bytes.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t Aead_Decrypt(
    binary_block_t ciphertext,
    binary_block_t authenticatedData,
    const aead_nonce_t &nonce,
    const aead_key_t *pKey,
    void *pPlaintextOut,
    usize cbPlaintextCapacity,
    usize *pPlaintextSizeOut ) noexcept;

} // namespace cypher::security

#endif // CYPHER_SECURITY_AEAD_H
