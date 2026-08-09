//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_KeyExchange.cpp
//  Purpose: Implements directional X25519 session-key derivation.
//  Details: Secret and session material is written directly into guarded memory.
//           Failed derivations destroy every partially initialized output.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity_KeyExchange.h"

#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"

#include <sodium.h>

namespace cypher::security
{

namespace
{

CYPHER_NODISCARD security_status_t KeyExchangeKeyPair_Finalize(
    key_exchange_keypair_t *pKeyPair ) noexcept
{
    const security_status_t result =
        SecureMemory_SetReadOnly( &pKeyPair->secretKey );
    if ( result != security_status_t::OK ) {
        KeyExchangeKeyPair_Destroy( pKeyPair );
    }
    return result;
}

CYPHER_NODISCARD security_status_t KeyExchangeSessionKeys_Create(
    secure_memory_lock_policy_t lockPolicy,
    key_exchange_session_keys_t *pSessionKeys ) noexcept
{
    security_status_t result = SecureMemory_Create(
        CY_SECURITY_KX_SESSION_KEY_SIZE,
        lockPolicy,
        &pSessionKeys->receiveKey );
    if ( result != security_status_t::OK ) {
        return result;
    }
    result = SecureMemory_Create(
        CY_SECURITY_KX_SESSION_KEY_SIZE,
        lockPolicy,
        &pSessionKeys->transmitKey );
    if ( result != security_status_t::OK ) {
        KeyExchangeSessionKeys_Destroy( pSessionKeys );
    }
    return result;
}

CYPHER_NODISCARD security_status_t KeyExchangeSessionKeys_Finalize(
    key_exchange_session_keys_t *pSessionKeys ) noexcept
{
    security_status_t result =
        SecureMemory_SetReadOnly( &pSessionKeys->receiveKey );
    if ( result == security_status_t::OK ) {
        result = SecureMemory_SetReadOnly( &pSessionKeys->transmitKey );
    }
    if ( result != security_status_t::OK ) {
        KeyExchangeSessionKeys_Destroy( pSessionKeys );
    }
    return result;
}

using derive_session_keys_fn_t = int (*)(
    unsigned char *,
    unsigned char *,
    const unsigned char *,
    const unsigned char *,
    const unsigned char * );

CYPHER_NODISCARD security_status_t KeyExchange_DeriveSessionKeys(
    const key_exchange_keypair_t *pLocalKeyPair,
    const key_exchange_public_key_t &peerPublicKey,
    secure_memory_lock_policy_t lockPolicy,
    key_exchange_session_keys_t *pSessionKeysOut,
    derive_session_keys_fn_t pfnDerive ) noexcept
{
    const bool_t bValidLocalKey =
        KeyExchangeKeyPair_IsValid( pLocalKeyPair );
    const bool_t bValidOutput = pSessionKeysOut != nullptr;
    CY_ASSERT_MSG( bValidLocalKey, "Key exchange requires a valid local key pair." );
    CY_ASSERT_MSG( bValidOutput, "Key exchange requires session-key output storage." );
    if ( !bValidLocalKey || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_status_t result =
        KeyExchangeSessionKeys_Create( lockPolicy, pSessionKeysOut );
    if ( result != security_status_t::OK ) {
        return result;
    }

    const int deriveResult = pfnDerive(
        SecureMemory_Data( &pSessionKeysOut->receiveKey ),
        SecureMemory_Data( &pSessionKeysOut->transmitKey ),
        pLocalKeyPair->publicKey.bytes,
        SecureMemory_ConstData( &pLocalKeyPair->secretKey ),
        peerPublicKey.bytes );
    if ( deriveResult != 0 ) {
        KeyExchangeSessionKeys_Destroy( pSessionKeysOut );
        return security_status_t::PEER_KEY_REJECTED;
    }
    return KeyExchangeSessionKeys_Finalize( pSessionKeysOut );
}

} // namespace

static_assert(
    CY_SECURITY_KX_PUBLIC_KEY_SIZE == crypto_kx_PUBLICKEYBYTES &&
    CY_SECURITY_KX_SECRET_KEY_SIZE == crypto_kx_SECRETKEYBYTES &&
    CY_SECURITY_KX_SEED_SIZE == crypto_kx_SEEDBYTES &&
    CY_SECURITY_KX_SESSION_KEY_SIZE == crypto_kx_SESSIONKEYBYTES,
    "Cypher key-exchange contracts must match libsodium." );

security_status_t KeyExchangeKeyPair_Generate(
    secure_memory_lock_policy_t lockPolicy,
    key_exchange_keypair_t *pKeyPairOut ) noexcept
{
    const bool_t bValidOutput = pKeyPairOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Key generation requires output storage." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_KX_SECRET_KEY_SIZE,
        lockPolicy,
        &pKeyPairOut->secretKey );
    if ( result != security_status_t::OK ) {
        return result;
    }
    if ( crypto_kx_keypair(
             pKeyPairOut->publicKey.bytes,
             SecureMemory_Data( &pKeyPairOut->secretKey ) ) != 0 ) {
        KeyExchangeKeyPair_Destroy( pKeyPairOut );
        return security_status_t::OPERATION_FAILED;
    }
    return KeyExchangeKeyPair_Finalize( pKeyPairOut );
}

security_status_t KeyExchangeKeyPair_FromSeed(
    binary_block_t seed,
    secure_memory_lock_policy_t lockPolicy,
    key_exchange_keypair_t *pKeyPairOut ) noexcept
{
    const bool_t bValidSeed =
        common::BinaryBlock_IsValid( seed ) &&
        seed.cbSize == CY_SECURITY_KX_SEED_SIZE;
    const bool_t bValidOutput = pKeyPairOut != nullptr;
    CY_ASSERT_MSG( bValidSeed, "Key-exchange seed requires exactly 32 bytes." );
    CY_ASSERT_MSG( bValidOutput, "Seed key generation requires output storage." );
    if ( !bValidSeed || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_KX_SECRET_KEY_SIZE,
        lockPolicy,
        &pKeyPairOut->secretKey );
    if ( result != security_status_t::OK ) {
        return result;
    }
    if ( crypto_kx_seed_keypair(
             pKeyPairOut->publicKey.bytes,
             SecureMemory_Data( &pKeyPairOut->secretKey ),
             seed.pData ) != 0 ) {
        KeyExchangeKeyPair_Destroy( pKeyPairOut );
        return security_status_t::OPERATION_FAILED;
    }
    return KeyExchangeKeyPair_Finalize( pKeyPairOut );
}

void KeyExchangeKeyPair_Destroy(
    key_exchange_keypair_t *pKeyPair ) noexcept
{
    if ( pKeyPair == nullptr ) {
        return;
    }
    SecureMemory_Destroy( &pKeyPair->secretKey );
    Security_ZeroMemory(
        pKeyPair->publicKey.bytes,
        sizeof( pKeyPair->publicKey.bytes ) );
}

bool_t KeyExchangeKeyPair_IsValid(
    const key_exchange_keypair_t *pKeyPair ) noexcept
{
    return pKeyPair != nullptr &&
           SecureMemory_IsValid( &pKeyPair->secretKey ) &&
           SecureMemory_Size( &pKeyPair->secretKey ) ==
               CY_SECURITY_KX_SECRET_KEY_SIZE &&
           SecureMemory_GetAccess( &pKeyPair->secretKey ) ==
               secure_memory_access_t::READ_ONLY;
}

security_status_t KeyExchangePublicKey_FromBytes(
    binary_block_t keyBytes,
    key_exchange_public_key_t *pKeyOut ) noexcept
{
    const bool_t bValidInput =
        common::BinaryBlock_IsValid( keyBytes ) &&
        keyBytes.cbSize == CY_SECURITY_KX_PUBLIC_KEY_SIZE;
    const bool_t bValidOutput = pKeyOut != nullptr;
    CY_ASSERT_MSG( bValidInput, "Peer public key requires exactly 32 bytes." );
    CY_ASSERT_MSG( bValidOutput, "Peer public key requires output storage." );
    if ( !bValidInput || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    key_exchange_public_key_t key{};
    common::Cy_MemCopy( key.bytes, keyBytes.pData, keyBytes.cbSize );
    *pKeyOut = key;
    return security_status_t::OK;
}

security_status_t KeyExchange_ClientSessionKeys(
    const key_exchange_keypair_t *pClientKeyPair,
    const key_exchange_public_key_t &serverPublicKey,
    secure_memory_lock_policy_t lockPolicy,
    key_exchange_session_keys_t *pSessionKeysOut ) noexcept
{
    return KeyExchange_DeriveSessionKeys(
        pClientKeyPair,
        serverPublicKey,
        lockPolicy,
        pSessionKeysOut,
        crypto_kx_client_session_keys );
}

security_status_t KeyExchange_ServerSessionKeys(
    const key_exchange_keypair_t *pServerKeyPair,
    const key_exchange_public_key_t &clientPublicKey,
    secure_memory_lock_policy_t lockPolicy,
    key_exchange_session_keys_t *pSessionKeysOut ) noexcept
{
    return KeyExchange_DeriveSessionKeys(
        pServerKeyPair,
        clientPublicKey,
        lockPolicy,
        pSessionKeysOut,
        crypto_kx_server_session_keys );
}

void KeyExchangeSessionKeys_Destroy(
    key_exchange_session_keys_t *pSessionKeys ) noexcept
{
    if ( pSessionKeys == nullptr ) {
        return;
    }
    SecureMemory_Destroy( &pSessionKeys->transmitKey );
    SecureMemory_Destroy( &pSessionKeys->receiveKey );
}

bool_t KeyExchangeSessionKeys_AreValid(
    const key_exchange_session_keys_t *pSessionKeys ) noexcept
{
    return pSessionKeys != nullptr &&
           SecureMemory_IsValid( &pSessionKeys->receiveKey ) &&
           SecureMemory_IsValid( &pSessionKeys->transmitKey ) &&
           SecureMemory_Size( &pSessionKeys->receiveKey ) ==
               CY_SECURITY_KX_SESSION_KEY_SIZE &&
           SecureMemory_Size( &pSessionKeys->transmitKey ) ==
               CY_SECURITY_KX_SESSION_KEY_SIZE &&
           SecureMemory_GetAccess( &pSessionKeys->receiveKey ) ==
               secure_memory_access_t::READ_ONLY &&
           SecureMemory_GetAccess( &pSessionKeys->transmitKey ) ==
               secure_memory_access_t::READ_ONLY;
}

binary_block_t KeyExchangeSessionKeys_ReceiveBlock(
    const key_exchange_session_keys_t *pSessionKeys ) noexcept
{
    return KeyExchangeSessionKeys_AreValid( pSessionKeys )
        ? common::BinaryBlock_FromData(
              SecureMemory_ConstData( &pSessionKeys->receiveKey ),
              CY_SECURITY_KX_SESSION_KEY_SIZE )
        : binary_block_t{};
}

binary_block_t KeyExchangeSessionKeys_TransmitBlock(
    const key_exchange_session_keys_t *pSessionKeys ) noexcept
{
    return KeyExchangeSessionKeys_AreValid( pSessionKeys )
        ? common::BinaryBlock_FromData(
              SecureMemory_ConstData( &pSessionKeys->transmitKey ),
              CY_SECURITY_KX_SESSION_KEY_SIZE )
        : binary_block_t{};
}

} // namespace cypher::security
