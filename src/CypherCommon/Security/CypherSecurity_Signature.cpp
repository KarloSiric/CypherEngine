//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_Signature.cpp
//  Purpose: Implements Ed25519 signing and verification services.
//  Details: Secret material is generated directly into guarded memory and every
//           multipart state is cleared after completion, cancellation, or scope exit.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity_Signature.h"

#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"
#include "CypherCommon_MemoryOps.h"

#include <new>
#include <sodium.h>

namespace cypher::security
{

namespace
{

constexpr byte g_emptySignatureInput = 0u;

CYPHER_NODISCARD const byte *Signature_Input(
    binary_block_t message ) noexcept
{
    return message.cbSize == 0u ? &g_emptySignatureInput : message.pData;
}

CYPHER_NODISCARD crypto_sign_state *SignatureStream_State(
    signature_stream_t *pStream ) noexcept
{
    // Backend state remains private behind fixed, aligned public storage.
    return std::launder(
        reinterpret_cast<crypto_sign_state *>( pStream->storage ) );
}

CYPHER_NODISCARD security_status_t SignatureKeyPair_Finalize(
    signature_keypair_t *pKeyPair ) noexcept
{
    const security_status_t result =
        SecureMemory_SetReadOnly( &pKeyPair->secretKey );
    if ( result != security_status_t::OK ) {
        SignatureKeyPair_Destroy( pKeyPair );
    }
    return result;
}

} // namespace

static_assert(
    CY_SECURITY_SIGNATURE_SIZE == crypto_sign_BYTES &&
    CY_SECURITY_SIGN_PUBLIC_KEY_SIZE == crypto_sign_PUBLICKEYBYTES &&
    CY_SECURITY_SIGN_SECRET_KEY_SIZE == crypto_sign_SECRETKEYBYTES &&
    CY_SECURITY_SIGN_SEED_SIZE == crypto_sign_SEEDBYTES,
    "Cypher signature contracts must match libsodium." );
static_assert(
    sizeof( crypto_sign_state ) <= CY_SECURITY_SIGN_STREAM_STORAGE_SIZE &&
    alignof( crypto_sign_state ) <= CY_SECURITY_SIGN_STREAM_STORAGE_ALIGNMENT,
    "libsodium signature state exceeds Cypher opaque storage." );

security_status_t SignatureKeyPair_Generate(
    secure_memory_lock_policy_t lockPolicy,
    signature_keypair_t *pKeyPairOut ) noexcept
{
    const bool_t bValidOutput = pKeyPairOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Signature key generation requires output storage." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_SIGN_SECRET_KEY_SIZE,
        lockPolicy,
        &pKeyPairOut->secretKey );
    if ( result != security_status_t::OK ) {
        return result;
    }
    const int keyResult = crypto_sign_keypair(
        pKeyPairOut->publicKey.bytes,
        SecureMemory_Data( &pKeyPairOut->secretKey ) );
    if ( keyResult != 0 ) {
        SignatureKeyPair_Destroy( pKeyPairOut );
        return security_status_t::OPERATION_FAILED;
    }
    return SignatureKeyPair_Finalize( pKeyPairOut );
}

security_status_t SignatureKeyPair_FromSeed(
    binary_block_t seed,
    secure_memory_lock_policy_t lockPolicy,
    signature_keypair_t *pKeyPairOut ) noexcept
{
    const bool_t bValidSeed =
        common::BinaryBlock_IsValid( seed ) &&
        seed.cbSize == CY_SECURITY_SIGN_SEED_SIZE;
    const bool_t bValidOutput = pKeyPairOut != nullptr;
    CY_ASSERT_MSG( bValidSeed, "Signature seed requires exactly 32 bytes." );
    CY_ASSERT_MSG( bValidOutput, "Signature seed import requires output storage." );
    if ( !bValidSeed || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_SIGN_SECRET_KEY_SIZE,
        lockPolicy,
        &pKeyPairOut->secretKey );
    if ( result != security_status_t::OK ) {
        return result;
    }
    const int keyResult = crypto_sign_seed_keypair(
        pKeyPairOut->publicKey.bytes,
        SecureMemory_Data( &pKeyPairOut->secretKey ),
        seed.pData );
    if ( keyResult != 0 ) {
        SignatureKeyPair_Destroy( pKeyPairOut );
        return security_status_t::OPERATION_FAILED;
    }
    return SignatureKeyPair_Finalize( pKeyPairOut );
}

security_status_t SignatureKeyPair_ImportSecret(
    binary_block_t secretKey,
    secure_memory_lock_policy_t lockPolicy,
    signature_keypair_t *pKeyPairOut ) noexcept
{
    const bool_t bValidSecret =
        common::BinaryBlock_IsValid( secretKey ) &&
        secretKey.cbSize == CY_SECURITY_SIGN_SECRET_KEY_SIZE;
    const bool_t bValidOutput = pKeyPairOut != nullptr;
    CY_ASSERT_MSG( bValidSecret, "Signature secret import requires exactly 64 bytes." );
    CY_ASSERT_MSG( bValidOutput, "Signature secret import requires output storage." );
    if ( !bValidSecret || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_SIGN_SECRET_KEY_SIZE,
        lockPolicy,
        &pKeyPairOut->secretKey );
    if ( result != security_status_t::OK ) {
        return result;
    }
    // Re-derive the canonical keypair from the embedded seed, then compare the
    // complete secret key. This rejects inconsistent or corrupted imports.
    byte seed[CY_SECURITY_SIGN_SEED_SIZE]{};
    const int seedResult = crypto_sign_ed25519_sk_to_seed(
        seed,
        secretKey.pData );
    byte *pSecret = SecureMemory_Data( &pKeyPairOut->secretKey );
    const int keyResult = seedResult == 0
        ? crypto_sign_seed_keypair(
              pKeyPairOut->publicKey.bytes,
              pSecret,
              seed )
        : -1;
    Security_ZeroMemory( seed, sizeof( seed ) );
    if ( keyResult != 0 ) {
        SignatureKeyPair_Destroy( pKeyPairOut );
        return security_status_t::OPERATION_FAILED;
    }
    if ( !Security_ConstantTimeEquals(
             pSecret,
             secretKey.pData,
             CY_SECURITY_SIGN_SECRET_KEY_SIZE ) ) {
        SignatureKeyPair_Destroy( pKeyPairOut );
        return security_status_t::INVALID_ARGUMENT;
    }
    return SignatureKeyPair_Finalize( pKeyPairOut );
}

void SignatureKeyPair_Destroy(
    signature_keypair_t *pKeyPair ) noexcept
{
    if ( pKeyPair == nullptr ) {
        return;
    }
    SecureMemory_Destroy( &pKeyPair->secretKey );
    Security_ZeroMemory(
        pKeyPair->publicKey.bytes,
        sizeof( pKeyPair->publicKey.bytes ) );
}

bool_t SignatureKeyPair_IsValid(
    const signature_keypair_t *pKeyPair ) noexcept
{
    return pKeyPair != nullptr &&
           SecureMemory_IsValid( &pKeyPair->secretKey ) &&
           SecureMemory_Size( &pKeyPair->secretKey ) ==
               CY_SECURITY_SIGN_SECRET_KEY_SIZE &&
           SecureMemory_GetAccess( &pKeyPair->secretKey ) ==
               secure_memory_access_t::READ_ONLY;
}

security_status_t SignaturePublicKey_FromBytes(
    binary_block_t keyBytes,
    signature_public_key_t *pKeyOut ) noexcept
{
    const bool_t bValidInput =
        common::BinaryBlock_IsValid( keyBytes ) &&
        keyBytes.cbSize == CY_SECURITY_SIGN_PUBLIC_KEY_SIZE;
    const bool_t bValidOutput = pKeyOut != nullptr;
    CY_ASSERT_MSG( bValidInput, "Signature public key requires exactly 32 bytes." );
    CY_ASSERT_MSG( bValidOutput, "Signature public key requires output storage." );
    if ( !bValidInput || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    signature_public_key_t key{};
    common::Cy_MemCopy( key.bytes, keyBytes.pData, keyBytes.cbSize );
    *pKeyOut = key;
    return security_status_t::OK;
}

security_status_t Signature_Sign(
    binary_block_t message,
    const signature_keypair_t *pKeyPair,
    signature_t *pSignatureOut ) noexcept
{
    const bool_t bValidMessage = common::BinaryBlock_IsValid( message );
    const bool_t bValidKey = SignatureKeyPair_IsValid( pKeyPair );
    const bool_t bValidOutput = pSignatureOut != nullptr;
    CY_ASSERT_MSG( bValidMessage, "Signature requires a valid message range." );
    CY_ASSERT_MSG( bValidKey, "Signature requires a valid private key." );
    CY_ASSERT_MSG( bValidOutput, "Signature requires output storage." );
    if ( !bValidMessage || !bValidKey || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    // Produce a detached signature: the message remains external and only the
    // fixed-size authenticator is returned.
    signature_t signature{};
    unsigned long long cbSignature = 0u;
    const int result = crypto_sign_detached(
        signature.bytes,
        &cbSignature,
        Signature_Input( message ),
        static_cast<unsigned long long>( message.cbSize ),
        SecureMemory_ConstData( &pKeyPair->secretKey ) );
    if ( result != 0 || cbSignature != CY_SECURITY_SIGNATURE_SIZE ) {
        return security_status_t::OPERATION_FAILED;
    }
    *pSignatureOut = signature;
    return security_status_t::OK;
}

security_status_t Signature_Verify(
    binary_block_t message,
    const signature_t &signature,
    const signature_public_key_t &publicKey ) noexcept
{
    const bool_t bValidMessage = common::BinaryBlock_IsValid( message );
    CY_ASSERT_MSG( bValidMessage, "Signature verification requires a valid message." );
    if ( !bValidMessage ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    return crypto_sign_verify_detached(
               signature.bytes,
               Signature_Input( message ),
               static_cast<unsigned long long>( message.cbSize ),
               publicKey.bytes ) == 0
        ? security_status_t::OK
        : security_status_t::AUTHENTICATION_FAILED;
}

bool_t Signature_Equals(
    const signature_t &left,
    const signature_t &right ) noexcept
{
    return Security_ConstantTimeEquals(
        left.bytes,
        right.bytes,
        sizeof( left.bytes ) );
}

security_status_t SignatureStream_Begin(
    signature_stream_t *pStream ) noexcept
{
    const bool_t bValidStream = pStream != nullptr;
    CY_ASSERT_MSG( bValidStream, "Signature stream requires state storage." );
    if ( !bValidStream ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( pStream->bActive ) {
        return security_status_t::INVALID_STATE;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    // libsodium's multipart API implements Ed25519ph. Its signatures must be
    // verified with this same streaming API, not the one-shot Ed25519 function.
    ::new ( static_cast<void *>( pStream->storage ) ) crypto_sign_state{};
    if ( crypto_sign_init( SignatureStream_State( pStream ) ) != 0 ) {
        SignatureStream_Cancel( pStream );
        return security_status_t::OPERATION_FAILED;
    }
    pStream->bActive = CY_TRUE;
    return security_status_t::OK;
}

security_status_t SignatureStream_Update(
    signature_stream_t *pStream,
    binary_block_t messagePart ) noexcept
{
    const bool_t bValidStream = pStream != nullptr;
    const bool_t bActive = bValidStream && pStream->bActive;
    const bool_t bValidMessage = common::BinaryBlock_IsValid( messagePart );
    CY_ASSERT_MSG( bValidStream, "Signature stream update requires state storage." );
    CY_ASSERT_MSG( bValidMessage, "Signature stream update requires valid bytes." );
    if ( !bValidStream || !bValidMessage ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !bActive ) {
        return security_status_t::INVALID_STATE;
    }
    if ( messagePart.cbSize == 0u ) {
        return security_status_t::OK;
    }

    return crypto_sign_update(
               SignatureStream_State( pStream ),
               messagePart.pData,
               static_cast<unsigned long long>( messagePart.cbSize ) ) == 0
        ? security_status_t::OK
        : security_status_t::OPERATION_FAILED;
}

security_status_t SignatureStream_EndSign(
    signature_stream_t *pStream,
    const signature_keypair_t *pKeyPair,
    signature_t *pSignatureOut ) noexcept
{
    const bool_t bValidStream = pStream != nullptr;
    const bool_t bActive = bValidStream && pStream->bActive;
    const bool_t bValidKey = SignatureKeyPair_IsValid( pKeyPair );
    const bool_t bValidOutput = pSignatureOut != nullptr;
    CY_ASSERT_MSG( bValidStream, "Signature finalization requires state storage." );
    CY_ASSERT_MSG( bValidKey, "Signature finalization requires a valid key." );
    CY_ASSERT_MSG( bValidOutput, "Signature finalization requires output storage." );
    if ( !bValidStream || !bValidKey || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !bActive ) {
        return security_status_t::INVALID_STATE;
    }

    signature_t signature{};
    unsigned long long cbSignature = 0u;
    const int result = crypto_sign_final_create(
        SignatureStream_State( pStream ),
        signature.bytes,
        &cbSignature,
        SecureMemory_ConstData( &pKeyPair->secretKey ) );
    SignatureStream_Cancel( pStream );
    if ( result != 0 || cbSignature != CY_SECURITY_SIGNATURE_SIZE ) {
        return security_status_t::OPERATION_FAILED;
    }
    *pSignatureOut = signature;
    return security_status_t::OK;
}

security_status_t SignatureStream_EndVerify(
    signature_stream_t *pStream,
    const signature_t &signature,
    const signature_public_key_t &publicKey ) noexcept
{
    const bool_t bValidStream = pStream != nullptr;
    const bool_t bActive = bValidStream && pStream->bActive;
    CY_ASSERT_MSG( bValidStream, "Signature verification requires state storage." );
    if ( !bValidStream ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !bActive ) {
        return security_status_t::INVALID_STATE;
    }

    const int result = crypto_sign_final_verify(
        SignatureStream_State( pStream ),
        signature.bytes,
        publicKey.bytes );
    SignatureStream_Cancel( pStream );
    return result == 0
        ? security_status_t::OK
        : security_status_t::AUTHENTICATION_FAILED;
}

void SignatureStream_Cancel(
    signature_stream_t *pStream ) noexcept
{
    if ( pStream == nullptr ) {
        return;
    }
    // Always erase accumulated hash state on completion, cancellation, or destruction.
    Security_ZeroMemory( pStream->storage, sizeof( pStream->storage ) );
    pStream->bActive = CY_FALSE;
}

signature_stream_t::~signature_stream_t() noexcept
{
    SignatureStream_Cancel( this );
}

} // namespace cypher::security
