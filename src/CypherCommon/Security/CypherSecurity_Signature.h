//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_Signature.h
//  Purpose: Declares Ed25519 signing and verification services.
//  Details: Public keys and signatures remain portable fixed-size bytes while
//           private keys live in guarded read-only memory. Multipart signatures
//           support large package and asset streams without buffering them whole.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_SIGNATURE_H
#define CYPHER_SECURITY_SIGNATURE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_SecureMemory.h"
#include "CypherCommon_BinaryBlock.h"

namespace cypher::security
{

using common::binary_block_t;

inline constexpr usize CY_SECURITY_SIGNATURE_SIZE = 64u;
inline constexpr usize CY_SECURITY_SIGN_PUBLIC_KEY_SIZE = 32u;
inline constexpr usize CY_SECURITY_SIGN_SECRET_KEY_SIZE = 64u;
inline constexpr usize CY_SECURITY_SIGN_SEED_SIZE = 32u;
inline constexpr usize CY_SECURITY_SIGN_STREAM_STORAGE_SIZE = 512u;
inline constexpr usize CY_SECURITY_SIGN_STREAM_STORAGE_ALIGNMENT = 64u;

struct signature_public_key_t {
    byte bytes[CY_SECURITY_SIGN_PUBLIC_KEY_SIZE]{}; // Public Ed25519 verification key.
};

struct signature_t {
    byte bytes[CY_SECURITY_SIGNATURE_SIZE]{}; // Detached Ed25519 signature bytes.
};

struct signature_keypair_t {
    signature_public_key_t publicKey{}; // Public half safe to distribute.
    secure_memory_t secretKey{};        // Guarded private signing key.

    signature_keypair_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( signature_keypair_t );
};

// Multipart signing uses Ed25519ph and is intentionally not interchangeable
// with one-shot Ed25519 signatures over the same bytes.
struct signature_stream_t {
    signature_stream_t() noexcept = default;
    CYPHER_SECURITY_API ~signature_stream_t() noexcept;
    CYPHER_NO_COPY_MOVE( signature_stream_t );

    alignas( CY_SECURITY_SIGN_STREAM_STORAGE_ALIGNMENT )
        byte storage[CY_SECURITY_SIGN_STREAM_STORAGE_SIZE]{}; // Opaque Ed25519ph state.
    bool_t bActive{ CY_FALSE };                               // Multipart operation is open.
};

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SignatureKeyPair_Generate(
    secure_memory_lock_policy_t lockPolicy,
    signature_keypair_t *pKeyPairOut ) noexcept;

// Deterministically recreates a key pair from exactly 32 secret seed bytes.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SignatureKeyPair_FromSeed(
    binary_block_t seed,
    secure_memory_lock_policy_t lockPolicy,
    signature_keypair_t *pKeyPairOut ) noexcept;

// Imports libsodium's complete 64-byte Ed25519 secret representation and
// derives its public key.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SignatureKeyPair_ImportSecret(
    binary_block_t secretKey,
    secure_memory_lock_policy_t lockPolicy,
    signature_keypair_t *pKeyPairOut ) noexcept;

CYPHER_SECURITY_API void SignatureKeyPair_Destroy(
    signature_keypair_t *pKeyPair ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SignatureKeyPair_IsValid(
    const signature_keypair_t *pKeyPair ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SignaturePublicKey_FromBytes(
    binary_block_t keyBytes,
    signature_public_key_t *pKeyOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t Signature_Sign(
    binary_block_t message,
    const signature_keypair_t *pKeyPair,
    signature_t *pSignatureOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t Signature_Verify(
    binary_block_t message,
    const signature_t &signature,
    const signature_public_key_t &publicKey ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t Signature_Equals(
    const signature_t &left,
    const signature_t &right ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SignatureStream_Begin(
    signature_stream_t *pStream ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SignatureStream_Update(
    signature_stream_t *pStream,
    binary_block_t messagePart ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SignatureStream_EndSign(
    signature_stream_t *pStream,
    const signature_keypair_t *pKeyPair,
    signature_t *pSignatureOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SignatureStream_EndVerify(
    signature_stream_t *pStream,
    const signature_t &signature,
    const signature_public_key_t &publicKey ) noexcept;

// Abandons a multipart operation and clears all pre-hash state.
CYPHER_SECURITY_API void SignatureStream_Cancel(
    signature_stream_t *pStream ) noexcept;

} // namespace cypher::security

#endif // CYPHER_SECURITY_SIGNATURE_H
