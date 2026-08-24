//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_SecretStream.h
//  Purpose: Declares authenticated streaming encryption for large data.
//  Details: XChaCha20-Poly1305 records authenticate ordering, boundaries, tags,
//           and optional metadata. Push and pull state cannot be interchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_SECRETSTREAM_H
#define CYPHER_SECURITY_SECRETSTREAM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_SecureMemory.h"
#include "CypherCommon_BinaryBlock.h"

namespace cypher::security
{

using common::binary_block_t;

inline constexpr usize CY_SECURITY_SECRET_STREAM_KEY_SIZE = 32u;
inline constexpr usize CY_SECURITY_SECRET_STREAM_HEADER_SIZE = 24u;
inline constexpr usize CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE = 17u;
inline constexpr usize CY_SECURITY_SECRET_STREAM_STATE_STORAGE_SIZE = 128u;
inline constexpr usize CY_SECURITY_SECRET_STREAM_STATE_ALIGNMENT = 64u;

enum class secret_stream_tag_t : u8 {
    MESSAGE = 0u, // Ordinary record; the stream remains open.
    PUSH = 1u,    // Logical boundary within the stream.
    REKEY = 2u,   // Record also rotates the stream key.
    FINAL = 3u    // Last record; state is destroyed after processing.
};

struct secret_stream_key_t {
    secure_memory_t memory{}; // Guarded secretstream key material.
    secret_stream_key_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( secret_stream_key_t );
};

struct secret_stream_header_t {
    byte bytes[CY_SECURITY_SECRET_STREAM_HEADER_SIZE]{}; // Public stream initialization header.
};

struct secret_stream_push_t {
    secret_stream_push_t() noexcept = default;
    CYPHER_SECURITY_API ~secret_stream_push_t() noexcept;
    CYPHER_NO_COPY_MOVE( secret_stream_push_t );

    alignas( CY_SECURITY_SECRET_STREAM_STATE_ALIGNMENT )
        byte storage[CY_SECURITY_SECRET_STREAM_STATE_STORAGE_SIZE]{}; // Opaque encryption state.
    bool_t bActive{ CY_FALSE }; // Begin succeeded and FINAL has not been emitted.
};

struct secret_stream_pull_t {
    secret_stream_pull_t() noexcept = default;
    CYPHER_SECURITY_API ~secret_stream_pull_t() noexcept;
    CYPHER_NO_COPY_MOVE( secret_stream_pull_t );

    alignas( CY_SECURITY_SECRET_STREAM_STATE_ALIGNMENT )
        byte storage[CY_SECURITY_SECRET_STREAM_STATE_STORAGE_SIZE]{}; // Opaque decryption state.
    bool_t bActive{ CY_FALSE }; // Begin succeeded and FINAL has not been consumed.
};

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecretStreamKey_Generate(
    secure_memory_lock_policy_t lockPolicy,
    secret_stream_key_t *pKeyOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecretStreamKey_Import(
    binary_block_t keyBytes,
    secure_memory_lock_policy_t lockPolicy,
    secret_stream_key_t *pKeyOut ) noexcept;

CYPHER_SECURITY_API void SecretStreamKey_Destroy(
    secret_stream_key_t *pKey ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecretStreamKey_IsValid(
    const secret_stream_key_t *pKey ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecretStream_CiphertextSize(
    usize cbPlaintext,
    usize *pSizeOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecretStream_PlaintextSize(
    usize cbCiphertext,
    usize *pSizeOut ) noexcept;

// Starts an encryption stream and emits the public header required by Pull_Begin.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecretStreamPush_Begin(
    const secret_stream_key_t *pKey,
    secret_stream_push_t *pStreamOut,
    secret_stream_header_t *pHeaderOut ) noexcept;

// Encrypts one ordered record. FINAL closes and clears the push state.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecretStreamPush_Message(
    secret_stream_push_t *pStream,
    binary_block_t plaintext,
    binary_block_t authenticatedData,
    secret_stream_tag_t tag,
    void *pCiphertextOut,
    usize cbCiphertextCapacity,
    usize *pCiphertextSizeOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecretStreamPush_Rekey(
    secret_stream_push_t *pStream ) noexcept;

CYPHER_SECURITY_API void SecretStreamPush_Cancel(
    secret_stream_push_t *pStream ) noexcept;

// Starts decryption using the exact header produced by the sender.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecretStreamPull_Begin(
    const secret_stream_key_t *pKey,
    const secret_stream_header_t &header,
    secret_stream_pull_t *pStreamOut ) noexcept;

// Authenticates and decrypts one record. Failure or FINAL closes the stream.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecretStreamPull_Message(
    secret_stream_pull_t *pStream,
    binary_block_t ciphertext,
    binary_block_t authenticatedData,
    void *pPlaintextOut,
    usize cbPlaintextCapacity,
    usize *pPlaintextSizeOut,
    secret_stream_tag_t *pTagOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecretStreamPull_Rekey(
    secret_stream_pull_t *pStream ) noexcept;

CYPHER_SECURITY_API void SecretStreamPull_Cancel(
    secret_stream_pull_t *pStream ) noexcept;

} // namespace cypher::security

#endif // CYPHER_SECURITY_SECRETSTREAM_H
