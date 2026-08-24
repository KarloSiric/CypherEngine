//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_AEAD.cpp
//  Purpose: Implements authenticated symmetric encryption and nonce policy.
//  Details: XChaCha20-Poly1305 remains inside libsodium while Cypher validates
//           buffers, prevents accidental overlap, and owns nonce sequencing.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity_AEAD.h"

#include "CypherSecurity.h"
#include "CypherSecurity_Random.h"
#include "CypherCommon_Assert.h"
#include "CypherCommon_Endian.h"
#include "CypherCommon_MemoryOps.h"

#include <sodium.h>

namespace cypher::security
{

namespace
{

CYPHER_NODISCARD security_status_t AeadKey_Finalize(
    secure_memory_t *pMemory ) noexcept
{
    const security_status_t result = SecureMemory_SetReadOnly( pMemory );
    if ( result != security_status_t::OK ) {
        SecureMemory_Destroy( pMemory );
    }
    return result;
}

CYPHER_NODISCARD bool_t Aead_OutputDoesNotOverlap(
    const void *pOutput,
    usize cbOutput,
    binary_block_t first,
    binary_block_t second ) noexcept
{
    // Cypher exposes a simple out-of-place contract even if a backend happens
    // to support selected in-place layouts. This prevents ambiguous aliasing.
    return !common::Cy_MemRangesOverlap(
               pOutput,
               cbOutput,
               first.pData,
               first.cbSize ) &&
           !common::Cy_MemRangesOverlap(
               pOutput,
               cbOutput,
               second.pData,
               second.cbSize );
}

} // namespace

static_assert(
    CY_SECURITY_AEAD_KEY_SIZE ==
        crypto_aead_xchacha20poly1305_ietf_KEYBYTES &&
    CY_SECURITY_AEAD_NONCE_SIZE ==
        crypto_aead_xchacha20poly1305_ietf_NPUBBYTES &&
    CY_SECURITY_AEAD_TAG_SIZE ==
        crypto_aead_xchacha20poly1305_ietf_ABYTES,
    "Cypher AEAD contracts must match libsodium." );

security_status_t AeadKey_Generate(
    secure_memory_lock_policy_t lockPolicy,
    aead_key_t *pKeyOut ) noexcept
{
    const bool_t bValidOutput = pKeyOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "AEAD key generation requires output storage." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_AEAD_KEY_SIZE,
        lockPolicy,
        &pKeyOut->memory );
    if ( result != security_status_t::OK ) {
        return result;
    }
    crypto_aead_xchacha20poly1305_ietf_keygen(
        SecureMemory_Data( &pKeyOut->memory ) );
    return AeadKey_Finalize( &pKeyOut->memory );
}

security_status_t AeadKey_Import(
    binary_block_t keyBytes,
    secure_memory_lock_policy_t lockPolicy,
    aead_key_t *pKeyOut ) noexcept
{
    const bool_t bValidInput =
        common::BinaryBlock_IsValid( keyBytes ) &&
        keyBytes.cbSize == CY_SECURITY_AEAD_KEY_SIZE;
    const bool_t bValidOutput = pKeyOut != nullptr;
    CY_ASSERT_MSG( bValidInput, "AEAD key import requires exactly 32 bytes." );
    CY_ASSERT_MSG( bValidOutput, "AEAD key import requires output storage." );
    if ( !bValidInput || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_AEAD_KEY_SIZE,
        lockPolicy,
        &pKeyOut->memory );
    if ( result != security_status_t::OK ) {
        return result;
    }
    common::Cy_MemCopy(
        SecureMemory_Data( &pKeyOut->memory ),
        keyBytes.pData,
        keyBytes.cbSize );
    return AeadKey_Finalize( &pKeyOut->memory );
}

void AeadKey_Destroy( aead_key_t *pKey ) noexcept
{
    if ( pKey != nullptr ) {
        SecureMemory_Destroy( &pKey->memory );
    }
}

bool_t AeadKey_IsValid( const aead_key_t *pKey ) noexcept
{
    return pKey != nullptr &&
           SecureMemory_IsValid( &pKey->memory ) &&
           SecureMemory_Size( &pKey->memory ) == CY_SECURITY_AEAD_KEY_SIZE &&
           SecureMemory_GetAccess( &pKey->memory ) ==
               secure_memory_access_t::READ_ONLY;
}

security_status_t AeadNonce_Generate( aead_nonce_t *pNonceOut ) noexcept
{
    const bool_t bValidOutput = pNonceOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "AEAD nonce generation requires output storage." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    aead_nonce_t nonce{};
    const security_status_t result = SecurityRandom_Fill(
        nonce.bytes,
        sizeof( nonce.bytes ) );
    if ( result == security_status_t::OK ) {
        *pNonceOut = nonce;
    }
    return result;
}

security_status_t AeadNonceSequence_Init(
    u64 nFirstCounter,
    aead_nonce_sequence_t *pSequenceOut ) noexcept
{
    const bool_t bValidOutput = pSequenceOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "AEAD nonce sequence requires output storage." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    const bool_t bInactiveOutput = !pSequenceOut->bInitialized;
    CY_ASSERT_MSG(
        bInactiveOutput,
        "AEAD nonce sequence initialization requires an inactive destination." );
    if ( !bInactiveOutput ) {
        return security_status_t::INVALID_STATE;
    }

    // A random 128-bit prefix identifies this sequence; the remaining 64 bits
    // are a monotonic counter. A key must never reuse the same prefix/counter pair.
    aead_nonce_sequence_t sequence{};
    const security_status_t result = SecurityRandom_Fill(
        sequence.prefix,
        sizeof( sequence.prefix ) );
    if ( result != security_status_t::OK ) {
        return result;
    }
    sequence.nNextCounter = nFirstCounter;
    sequence.bInitialized = CY_TRUE;
    *pSequenceOut = sequence;
    return security_status_t::OK;
}

security_status_t AeadNonceSequence_InitFromPrefix(
    binary_block_t prefix,
    u64 nFirstCounter,
    aead_nonce_sequence_t *pSequenceOut ) noexcept
{
    const bool_t bValidPrefix =
        common::BinaryBlock_IsValid( prefix ) &&
        prefix.cbSize == CY_SECURITY_AEAD_NONCE_PREFIX_SIZE;
    const bool_t bValidOutput = pSequenceOut != nullptr;
    CY_ASSERT_MSG( bValidPrefix, "AEAD nonce prefix requires exactly 16 bytes." );
    CY_ASSERT_MSG( bValidOutput, "AEAD nonce sequence requires output storage." );
    if ( !bValidPrefix || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    const bool_t bInactiveOutput = !pSequenceOut->bInitialized;
    CY_ASSERT_MSG(
        bInactiveOutput,
        "AEAD nonce sequence initialization requires an inactive destination." );
    if ( !bInactiveOutput ) {
        return security_status_t::INVALID_STATE;
    }

    aead_nonce_sequence_t sequence{};
    common::Cy_MemCopy(
        sequence.prefix,
        prefix.pData,
        sizeof( sequence.prefix ) );
    sequence.nNextCounter = nFirstCounter;
    sequence.bInitialized = CY_TRUE;
    *pSequenceOut = sequence;
    return security_status_t::OK;
}

security_status_t AeadNonceSequence_Next(
    aead_nonce_sequence_t *pSequence,
    aead_nonce_t *pNonceOut ) noexcept
{
    const bool_t bValidSequence =
        pSequence != nullptr && pSequence->bInitialized;
    const bool_t bValidOutput = pNonceOut != nullptr;
    CY_ASSERT_MSG( bValidSequence, "AEAD nonce sequence is not initialized." );
    CY_ASSERT_MSG( bValidOutput, "AEAD nonce generation requires output storage." );
    if ( !bValidSequence || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( pSequence->bExhausted ) {
        return security_status_t::COUNTER_EXHAUSTED;
    }

    aead_nonce_t nonce{};
    common::Cy_MemCopy(
        nonce.bytes,
        pSequence->prefix,
        sizeof( pSequence->prefix ) );
    common::Cy_WriteLittle64(
        nonce.bytes + CY_SECURITY_AEAD_NONCE_PREFIX_SIZE,
        pSequence->nNextCounter );

    // Emit the maximum counter once, then permanently exhaust the sequence so
    // wraparound cannot repeat a nonce under the same key.
    if ( pSequence->nNextCounter == common::CY_U64_MAX ) {
        pSequence->bExhausted = CY_TRUE;
    } else {
        ++pSequence->nNextCounter;
    }
    *pNonceOut = nonce;
    return security_status_t::OK;
}

void AeadNonceSequence_Reset(
    aead_nonce_sequence_t *pSequence ) noexcept
{
    if ( pSequence != nullptr ) {
        Security_ZeroMemory( pSequence, sizeof( *pSequence ) );
    }
}

bool_t Aead_CiphertextSize(
    usize cbPlaintext,
    usize *pSizeOut ) noexcept
{
    if ( pSizeOut == nullptr ||
         cbPlaintext > common::CY_USIZE_MAX - CY_SECURITY_AEAD_TAG_SIZE ) {
        return CY_FALSE;
    }
    *pSizeOut = cbPlaintext + CY_SECURITY_AEAD_TAG_SIZE;
    return CY_TRUE;
}

bool_t Aead_PlaintextSize(
    usize cbCiphertext,
    usize *pSizeOut ) noexcept
{
    if ( pSizeOut == nullptr || cbCiphertext < CY_SECURITY_AEAD_TAG_SIZE ) {
        return CY_FALSE;
    }
    *pSizeOut = cbCiphertext - CY_SECURITY_AEAD_TAG_SIZE;
    return CY_TRUE;
}

security_status_t Aead_Encrypt(
    binary_block_t plaintext,
    binary_block_t authenticatedData,
    const aead_nonce_t &nonce,
    const aead_key_t *pKey,
    void *pCiphertextOut,
    usize cbCiphertextCapacity,
    usize *pCiphertextSizeOut ) noexcept
{
    usize cbRequired = 0u;
    const bool_t bValidPlaintext = common::BinaryBlock_IsValid( plaintext );
    const bool_t bValidAuthenticatedData =
        common::BinaryBlock_IsValid( authenticatedData );
    const bool_t bValidKey = AeadKey_IsValid( pKey );
    const bool_t bValidSize = Aead_CiphertextSize( plaintext.cbSize, &cbRequired );
    const bool_t bValidOutput =
        pCiphertextOut != nullptr && pCiphertextSizeOut != nullptr;
    CY_ASSERT_MSG( bValidPlaintext, "AEAD encryption requires valid plaintext." );
    CY_ASSERT_MSG( bValidAuthenticatedData, "AEAD authenticated data is invalid." );
    CY_ASSERT_MSG( bValidKey, "AEAD encryption requires a valid key." );
    CY_ASSERT_MSG( bValidOutput, "AEAD encryption requires output storage." );
    if ( !bValidPlaintext || !bValidAuthenticatedData || !bValidKey ||
         !bValidSize || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( cbCiphertextCapacity < cbRequired ) {
        return security_status_t::BUFFER_TOO_SMALL;
    }

    const bool_t bNonOverlapping = Aead_OutputDoesNotOverlap(
        pCiphertextOut,
        cbRequired,
        plaintext,
        authenticatedData );
    CY_ASSERT_MSG( bNonOverlapping, "AEAD encryption buffers must not overlap." );
    if ( !bNonOverlapping ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    // Authenticated data is covered by the tag but is not copied into the
    // ciphertext. Protocol headers can therefore remain visible and tamper-evident.
    unsigned long long cbProduced = 0u;
    const int result = crypto_aead_xchacha20poly1305_ietf_encrypt(
        static_cast<byte *>( pCiphertextOut ),
        &cbProduced,
        plaintext.pData,
        static_cast<unsigned long long>( plaintext.cbSize ),
        authenticatedData.pData,
        static_cast<unsigned long long>( authenticatedData.cbSize ),
        nullptr,
        nonce.bytes,
        SecureMemory_ConstData( &pKey->memory ) );
    if ( result != 0 || cbProduced != cbRequired ) {
        Security_ZeroMemory( pCiphertextOut, cbRequired );
        return security_status_t::OPERATION_FAILED;
    }

    *pCiphertextSizeOut = static_cast<usize>( cbProduced );
    return security_status_t::OK;
}

security_status_t Aead_Decrypt(
    binary_block_t ciphertext,
    binary_block_t authenticatedData,
    const aead_nonce_t &nonce,
    const aead_key_t *pKey,
    void *pPlaintextOut,
    usize cbPlaintextCapacity,
    usize *pPlaintextSizeOut ) noexcept
{
    usize cbRequired = 0u;
    const bool_t bValidCiphertext = common::BinaryBlock_IsValid( ciphertext );
    const bool_t bValidAuthenticatedData =
        common::BinaryBlock_IsValid( authenticatedData );
    const bool_t bValidKey = AeadKey_IsValid( pKey );
    const bool_t bValidSize = Aead_PlaintextSize( ciphertext.cbSize, &cbRequired );
    const bool_t bValidOutput =
        ( pPlaintextOut != nullptr || cbRequired == 0u ) &&
        pPlaintextSizeOut != nullptr;
    CY_ASSERT_MSG( bValidCiphertext, "AEAD decryption requires valid ciphertext." );
    CY_ASSERT_MSG( bValidAuthenticatedData, "AEAD authenticated data is invalid." );
    CY_ASSERT_MSG( bValidKey, "AEAD decryption requires a valid key." );
    CY_ASSERT_MSG( bValidSize, "AEAD ciphertext is shorter than its tag." );
    CY_ASSERT_MSG( bValidOutput, "AEAD decryption requires output storage." );
    if ( !bValidCiphertext || !bValidAuthenticatedData || !bValidKey ||
         !bValidSize || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( cbPlaintextCapacity < cbRequired ) {
        return security_status_t::BUFFER_TOO_SMALL;
    }

    const bool_t bNonOverlapping = Aead_OutputDoesNotOverlap(
        pPlaintextOut,
        cbRequired,
        ciphertext,
        authenticatedData );
    CY_ASSERT_MSG( bNonOverlapping, "AEAD decryption buffers must not overlap." );
    if ( !bNonOverlapping ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    unsigned long long cbProduced = 0u;
    const int result = crypto_aead_xchacha20poly1305_ietf_decrypt(
        static_cast<byte *>( pPlaintextOut ),
        &cbProduced,
        nullptr,
        ciphertext.pData,
        static_cast<unsigned long long>( ciphertext.cbSize ),
        authenticatedData.pData,
        static_cast<unsigned long long>( authenticatedData.cbSize ),
        nonce.bytes,
        SecureMemory_ConstData( &pKey->memory ) );
    if ( result != 0 ) {
        // Never expose unauthenticated plaintext. Scrub the complete expected
        // output range before reporting the authentication failure.
        Security_ZeroMemory( pPlaintextOut, cbRequired );
        return security_status_t::AUTHENTICATION_FAILED;
    }
    if ( cbProduced != cbRequired ) {
        Security_ZeroMemory( pPlaintextOut, cbRequired );
        return security_status_t::OPERATION_FAILED;
    }

    *pPlaintextSizeOut = static_cast<usize>( cbProduced );
    return security_status_t::OK;
}

} // namespace cypher::security
