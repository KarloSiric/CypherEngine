//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_KeyExchange.h
//  Purpose: Declares directional channel key-agreement primitives.
//  Details: X25519 derives independent receive and transmit keys for client and
//           server roles. This primitive does not authenticate peer identity;
//           protocols must bind public keys to trusted identities separately.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_KEYEXCHANGE_H
#define CYPHER_SECURITY_KEYEXCHANGE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_SecureMemory.h"
#include "CypherCommon_BinaryBlock.h"

namespace cypher::security
{

using common::binary_block_t;

inline constexpr usize CY_SECURITY_KX_PUBLIC_KEY_SIZE = 32u;
inline constexpr usize CY_SECURITY_KX_SECRET_KEY_SIZE = 32u;
inline constexpr usize CY_SECURITY_KX_SEED_SIZE = 32u;
inline constexpr usize CY_SECURITY_KX_SESSION_KEY_SIZE = 32u;

struct key_exchange_public_key_t {
    byte bytes[CY_SECURITY_KX_PUBLIC_KEY_SIZE]{}; // Public X25519 key safe to transmit.
};

struct key_exchange_keypair_t {
    key_exchange_public_key_t publicKey{}; // Public identity used by the peer.
    secure_memory_t secretKey{};           // Guarded private X25519 key material.

    key_exchange_keypair_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( key_exchange_keypair_t );
};

// Directional keys prevent one direction's ciphertext from being accepted in
// the opposite direction when the surrounding protocol uses them correctly.
struct key_exchange_session_keys_t {
    secure_memory_t receiveKey{};  // Key reserved for peer-to-local traffic.
    secure_memory_t transmitKey{}; // Key reserved for local-to-peer traffic.

    key_exchange_session_keys_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( key_exchange_session_keys_t );
};

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t KeyExchangeKeyPair_Generate(
    secure_memory_lock_policy_t lockPolicy,
    key_exchange_keypair_t *pKeyPairOut ) noexcept;

// Deterministically recreates a key pair from exactly 32 secret seed bytes.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t KeyExchangeKeyPair_FromSeed(
    binary_block_t seed,
    secure_memory_lock_policy_t lockPolicy,
    key_exchange_keypair_t *pKeyPairOut ) noexcept;

CYPHER_SECURITY_API void KeyExchangeKeyPair_Destroy(
    key_exchange_keypair_t *pKeyPair ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t KeyExchangeKeyPair_IsValid(
    const key_exchange_keypair_t *pKeyPair ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t KeyExchangePublicKey_FromBytes(
    binary_block_t keyBytes,
    key_exchange_public_key_t *pKeyOut ) noexcept;

// Derives the client's receive/transmit keys using the server public key.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t KeyExchange_ClientSessionKeys(
    const key_exchange_keypair_t *pClientKeyPair,
    const key_exchange_public_key_t &serverPublicKey,
    secure_memory_lock_policy_t lockPolicy,
    key_exchange_session_keys_t *pSessionKeysOut ) noexcept;

// Derives the server's receive/transmit keys using the client public key.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t KeyExchange_ServerSessionKeys(
    const key_exchange_keypair_t *pServerKeyPair,
    const key_exchange_public_key_t &clientPublicKey,
    secure_memory_lock_policy_t lockPolicy,
    key_exchange_session_keys_t *pSessionKeysOut ) noexcept;

CYPHER_SECURITY_API void KeyExchangeSessionKeys_Destroy(
    key_exchange_session_keys_t *pSessionKeys ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t KeyExchangeSessionKeys_AreValid(
    const key_exchange_session_keys_t *pSessionKeys ) noexcept;

// Returned views remain valid only until the session keys are destroyed.
CYPHER_NODISCARD CYPHER_SECURITY_API
binary_block_t KeyExchangeSessionKeys_ReceiveBlock(
    const key_exchange_session_keys_t *pSessionKeys ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
binary_block_t KeyExchangeSessionKeys_TransmitBlock(
    const key_exchange_session_keys_t *pSessionKeys ) noexcept;

} // namespace cypher::security

#endif // CYPHER_SECURITY_KEYEXCHANGE_H
