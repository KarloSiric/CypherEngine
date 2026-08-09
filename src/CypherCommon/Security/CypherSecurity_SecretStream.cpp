//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_SecretStream.cpp
//  Purpose: Implements authenticated streaming encryption for large data.
//  Details: Capacity and overlap checks happen before stream mutation. Pull
//           state is abandoned after failed authentication because it is no
//           longer safe for the caller to continue the record sequence.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity_SecretStream.h"

#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"
#include "CypherCommon_MemoryOps.h"

#include <new>
#include <sodium.h>

namespace cypher::security
{

namespace
{

using native_stream_state_t =
    crypto_secretstream_xchacha20poly1305_state;

constexpr byte g_emptyStreamInput = 0u;

CYPHER_NODISCARD const byte *SecretStream_Input(
    binary_block_t bytes ) noexcept
{
    return bytes.cbSize == 0u ? &g_emptyStreamInput : bytes.pData;
}

CYPHER_NODISCARD native_stream_state_t *SecretStreamPush_State(
    secret_stream_push_t *pStream ) noexcept
{
    return std::launder(
        reinterpret_cast<native_stream_state_t *>( pStream->storage ) );
}

CYPHER_NODISCARD native_stream_state_t *SecretStreamPull_State(
    secret_stream_pull_t *pStream ) noexcept
{
    return std::launder(
        reinterpret_cast<native_stream_state_t *>( pStream->storage ) );
}

CYPHER_NODISCARD bool_t SecretStream_TagIsValid(
    secret_stream_tag_t tag ) noexcept
{
    return tag == secret_stream_tag_t::MESSAGE ||
           tag == secret_stream_tag_t::PUSH ||
           tag == secret_stream_tag_t::REKEY ||
           tag == secret_stream_tag_t::FINAL;
}

} // namespace

static_assert(
    CY_SECURITY_SECRET_STREAM_KEY_SIZE ==
        crypto_secretstream_xchacha20poly1305_KEYBYTES &&
    CY_SECURITY_SECRET_STREAM_HEADER_SIZE ==
        crypto_secretstream_xchacha20poly1305_HEADERBYTES &&
    CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE ==
        crypto_secretstream_xchacha20poly1305_ABYTES,
    "Cypher secret-stream contracts must match libsodium." );
static_assert(
    sizeof( native_stream_state_t ) <=
        CY_SECURITY_SECRET_STREAM_STATE_STORAGE_SIZE &&
    alignof( native_stream_state_t ) <=
        CY_SECURITY_SECRET_STREAM_STATE_ALIGNMENT,
    "libsodium secret-stream state exceeds Cypher opaque storage." );
static_assert(
    static_cast<u8>( secret_stream_tag_t::MESSAGE ) ==
        crypto_secretstream_xchacha20poly1305_TAG_MESSAGE &&
    static_cast<u8>( secret_stream_tag_t::PUSH ) ==
        crypto_secretstream_xchacha20poly1305_TAG_PUSH &&
    static_cast<u8>( secret_stream_tag_t::REKEY ) ==
        crypto_secretstream_xchacha20poly1305_TAG_REKEY &&
    static_cast<u8>( secret_stream_tag_t::FINAL ) ==
        crypto_secretstream_xchacha20poly1305_TAG_FINAL,
    "Cypher stream tags must match libsodium." );

security_status_t SecretStreamKey_Generate(
    secure_memory_lock_policy_t lockPolicy,
    secret_stream_key_t *pKeyOut ) noexcept
{
    const bool_t bValidOutput = pKeyOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Stream key generation requires output storage." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_SECRET_STREAM_KEY_SIZE,
        lockPolicy,
        &pKeyOut->memory );
    if ( result != security_status_t::OK ) {
        return result;
    }
    crypto_secretstream_xchacha20poly1305_keygen(
        SecureMemory_Data( &pKeyOut->memory ) );
    result = SecureMemory_SetReadOnly( &pKeyOut->memory );
    if ( result != security_status_t::OK ) {
        SecretStreamKey_Destroy( pKeyOut );
    }
    return result;
}

security_status_t SecretStreamKey_Import(
    binary_block_t keyBytes,
    secure_memory_lock_policy_t lockPolicy,
    secret_stream_key_t *pKeyOut ) noexcept
{
    const bool_t bValidKey =
        common::BinaryBlock_IsValid( keyBytes ) &&
        keyBytes.cbSize == CY_SECURITY_SECRET_STREAM_KEY_SIZE;
    const bool_t bValidOutput = pKeyOut != nullptr;
    CY_ASSERT_MSG( bValidKey, "Stream key import requires exactly 32 bytes." );
    CY_ASSERT_MSG( bValidOutput, "Stream key import requires output storage." );
    if ( !bValidKey || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_SECRET_STREAM_KEY_SIZE,
        lockPolicy,
        &pKeyOut->memory );
    if ( result != security_status_t::OK ) {
        return result;
    }
    common::Cy_MemCopy(
        SecureMemory_Data( &pKeyOut->memory ),
        keyBytes.pData,
        keyBytes.cbSize );
    result = SecureMemory_SetReadOnly( &pKeyOut->memory );
    if ( result != security_status_t::OK ) {
        SecretStreamKey_Destroy( pKeyOut );
    }
    return result;
}

void SecretStreamKey_Destroy(
    secret_stream_key_t *pKey ) noexcept
{
    if ( pKey != nullptr ) {
        SecureMemory_Destroy( &pKey->memory );
    }
}

bool_t SecretStreamKey_IsValid(
    const secret_stream_key_t *pKey ) noexcept
{
    return pKey != nullptr &&
           SecureMemory_IsValid( &pKey->memory ) &&
           SecureMemory_Size( &pKey->memory ) ==
               CY_SECURITY_SECRET_STREAM_KEY_SIZE &&
           SecureMemory_GetAccess( &pKey->memory ) ==
               secure_memory_access_t::READ_ONLY;
}

bool_t SecretStream_CiphertextSize(
    usize cbPlaintext,
    usize *pSizeOut ) noexcept
{
    if ( pSizeOut == nullptr ||
         cbPlaintext >
             static_cast<usize>(
                 crypto_secretstream_xchacha20poly1305_MESSAGEBYTES_MAX ) ||
         cbPlaintext >
             common::CY_USIZE_MAX - CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE ) {
        return CY_FALSE;
    }
    *pSizeOut = cbPlaintext + CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE;
    return CY_TRUE;
}

bool_t SecretStream_PlaintextSize(
    usize cbCiphertext,
    usize *pSizeOut ) noexcept
{
    if ( pSizeOut == nullptr ||
         cbCiphertext < CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE ) {
        return CY_FALSE;
    }
    *pSizeOut = cbCiphertext - CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE;
    return CY_TRUE;
}

security_status_t SecretStreamPush_Begin(
    const secret_stream_key_t *pKey,
    secret_stream_push_t *pStreamOut,
    secret_stream_header_t *pHeaderOut ) noexcept
{
    const bool_t bValidKey = SecretStreamKey_IsValid( pKey );
    const bool_t bValidStream = pStreamOut != nullptr;
    const bool_t bValidHeader = pHeaderOut != nullptr;
    CY_ASSERT_MSG( bValidKey, "Push stream requires a valid key." );
    CY_ASSERT_MSG( bValidStream, "Push stream requires state storage." );
    CY_ASSERT_MSG( bValidHeader, "Push stream requires header output storage." );
    if ( !bValidKey || !bValidStream || !bValidHeader ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( pStreamOut->bActive ) {
        return security_status_t::INVALID_STATE;
    }

    ::new ( static_cast<void *>( pStreamOut->storage ) ) native_stream_state_t{};
    secret_stream_header_t header{};
    if ( crypto_secretstream_xchacha20poly1305_init_push(
             SecretStreamPush_State( pStreamOut ),
             header.bytes,
             SecureMemory_ConstData( &pKey->memory ) ) != 0 ) {
        SecretStreamPush_Cancel( pStreamOut );
        return security_status_t::OPERATION_FAILED;
    }
    pStreamOut->bActive = CY_TRUE;
    *pHeaderOut = header;
    return security_status_t::OK;
}

security_status_t SecretStreamPush_Message(
    secret_stream_push_t *pStream,
    binary_block_t plaintext,
    binary_block_t authenticatedData,
    secret_stream_tag_t tag,
    void *pCiphertextOut,
    usize cbCiphertextCapacity,
    usize *pCiphertextSizeOut ) noexcept
{
    const bool_t bValidStream = pStream != nullptr;
    const bool_t bValidPlaintext = common::BinaryBlock_IsValid( plaintext );
    const bool_t bValidAd = common::BinaryBlock_IsValid( authenticatedData );
    const bool_t bValidTag = SecretStream_TagIsValid( tag );
    CY_ASSERT_MSG( bValidStream, "Stream push requires state storage." );
    CY_ASSERT_MSG( bValidPlaintext, "Stream push requires valid plaintext." );
    CY_ASSERT_MSG( bValidAd, "Stream push requires valid authenticated data." );
    CY_ASSERT_MSG( bValidTag, "Stream push requires a recognized record tag." );
    if ( !bValidStream || !bValidPlaintext || !bValidAd || !bValidTag ||
         pCiphertextSizeOut == nullptr ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !pStream->bActive ) {
        return security_status_t::INVALID_STATE;
    }

    usize cbRequired = 0u;
    if ( !SecretStream_CiphertextSize( plaintext.cbSize, &cbRequired ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( pCiphertextOut == nullptr || cbCiphertextCapacity < cbRequired ) {
        return security_status_t::BUFFER_TOO_SMALL;
    }
    if ( common::Cy_MemRangesOverlap(
             pCiphertextOut,
             cbRequired,
             plaintext.pData,
             plaintext.cbSize ) ||
         common::Cy_MemRangesOverlap(
             pCiphertextOut,
             cbRequired,
             authenticatedData.pData,
             authenticatedData.cbSize ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    unsigned long long cbWritten = 0u;
    const int result = crypto_secretstream_xchacha20poly1305_push(
        SecretStreamPush_State( pStream ),
        static_cast<byte *>( pCiphertextOut ),
        &cbWritten,
        SecretStream_Input( plaintext ),
        static_cast<unsigned long long>( plaintext.cbSize ),
        SecretStream_Input( authenticatedData ),
        static_cast<unsigned long long>( authenticatedData.cbSize ),
        static_cast<byte>( tag ) );
    if ( result != 0 || cbWritten != cbRequired ) {
        Security_ZeroMemory( pCiphertextOut, cbRequired );
        SecretStreamPush_Cancel( pStream );
        return security_status_t::OPERATION_FAILED;
    }
    *pCiphertextSizeOut = static_cast<usize>( cbWritten );
    if ( tag == secret_stream_tag_t::FINAL ) {
        SecretStreamPush_Cancel( pStream );
    }
    return security_status_t::OK;
}

security_status_t SecretStreamPush_Rekey(
    secret_stream_push_t *pStream ) noexcept
{
    if ( pStream == nullptr ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !pStream->bActive ) {
        return security_status_t::INVALID_STATE;
    }
    crypto_secretstream_xchacha20poly1305_rekey(
        SecretStreamPush_State( pStream ) );
    return security_status_t::OK;
}

void SecretStreamPush_Cancel(
    secret_stream_push_t *pStream ) noexcept
{
    if ( pStream == nullptr ) {
        return;
    }
    Security_ZeroMemory( pStream->storage, sizeof( pStream->storage ) );
    pStream->bActive = CY_FALSE;
}

security_status_t SecretStreamPull_Begin(
    const secret_stream_key_t *pKey,
    const secret_stream_header_t &header,
    secret_stream_pull_t *pStreamOut ) noexcept
{
    const bool_t bValidKey = SecretStreamKey_IsValid( pKey );
    const bool_t bValidStream = pStreamOut != nullptr;
    CY_ASSERT_MSG( bValidKey, "Pull stream requires a valid key." );
    CY_ASSERT_MSG( bValidStream, "Pull stream requires state storage." );
    if ( !bValidKey || !bValidStream ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( pStreamOut->bActive ) {
        return security_status_t::INVALID_STATE;
    }

    ::new ( static_cast<void *>( pStreamOut->storage ) ) native_stream_state_t{};
    if ( crypto_secretstream_xchacha20poly1305_init_pull(
             SecretStreamPull_State( pStreamOut ),
             header.bytes,
             SecureMemory_ConstData( &pKey->memory ) ) != 0 ) {
        SecretStreamPull_Cancel( pStreamOut );
        return security_status_t::OPERATION_FAILED;
    }
    pStreamOut->bActive = CY_TRUE;
    return security_status_t::OK;
}

security_status_t SecretStreamPull_Message(
    secret_stream_pull_t *pStream,
    binary_block_t ciphertext,
    binary_block_t authenticatedData,
    void *pPlaintextOut,
    usize cbPlaintextCapacity,
    usize *pPlaintextSizeOut,
    secret_stream_tag_t *pTagOut ) noexcept
{
    const bool_t bValidStream = pStream != nullptr;
    const bool_t bValidCiphertext = common::BinaryBlock_IsValid( ciphertext );
    const bool_t bValidAd = common::BinaryBlock_IsValid( authenticatedData );
    CY_ASSERT_MSG( bValidStream, "Stream pull requires state storage." );
    CY_ASSERT_MSG( bValidCiphertext, "Stream pull requires valid ciphertext." );
    CY_ASSERT_MSG( bValidAd, "Stream pull requires valid authenticated data." );
    if ( !bValidStream || !bValidCiphertext || !bValidAd ||
         pPlaintextSizeOut == nullptr || pTagOut == nullptr ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !pStream->bActive ) {
        return security_status_t::INVALID_STATE;
    }

    usize cbRequired = 0u;
    if ( !SecretStream_PlaintextSize( ciphertext.cbSize, &cbRequired ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( cbPlaintextCapacity < cbRequired ||
         ( cbRequired > 0u && pPlaintextOut == nullptr ) ) {
        return security_status_t::BUFFER_TOO_SMALL;
    }
    if ( common::Cy_MemRangesOverlap(
             pPlaintextOut,
             cbRequired,
             ciphertext.pData,
             ciphertext.cbSize ) ||
         common::Cy_MemRangesOverlap(
             pPlaintextOut,
             cbRequired,
             authenticatedData.pData,
             authenticatedData.cbSize ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    unsigned long long cbWritten = 0u;
    byte nativeTag = 0u;
    const int result = crypto_secretstream_xchacha20poly1305_pull(
        SecretStreamPull_State( pStream ),
        static_cast<byte *>( pPlaintextOut ),
        &cbWritten,
        &nativeTag,
        ciphertext.pData,
        static_cast<unsigned long long>( ciphertext.cbSize ),
        SecretStream_Input( authenticatedData ),
        static_cast<unsigned long long>( authenticatedData.cbSize ) );
    if ( result != 0 ) {
        Security_ZeroMemory( pPlaintextOut, cbRequired );
        SecretStreamPull_Cancel( pStream );
        return security_status_t::AUTHENTICATION_FAILED;
    }
    if ( cbWritten != cbRequired || nativeTag > static_cast<byte>( secret_stream_tag_t::FINAL ) ) {
        Security_ZeroMemory( pPlaintextOut, cbRequired );
        SecretStreamPull_Cancel( pStream );
        return security_status_t::OPERATION_FAILED;
    }

    const secret_stream_tag_t tag =
        static_cast<secret_stream_tag_t>( nativeTag );
    *pPlaintextSizeOut = static_cast<usize>( cbWritten );
    *pTagOut = tag;
    if ( tag == secret_stream_tag_t::FINAL ) {
        SecretStreamPull_Cancel( pStream );
    }
    return security_status_t::OK;
}

security_status_t SecretStreamPull_Rekey(
    secret_stream_pull_t *pStream ) noexcept
{
    if ( pStream == nullptr ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !pStream->bActive ) {
        return security_status_t::INVALID_STATE;
    }
    crypto_secretstream_xchacha20poly1305_rekey(
        SecretStreamPull_State( pStream ) );
    return security_status_t::OK;
}

void SecretStreamPull_Cancel(
    secret_stream_pull_t *pStream ) noexcept
{
    if ( pStream == nullptr ) {
        return;
    }
    Security_ZeroMemory( pStream->storage, sizeof( pStream->storage ) );
    pStream->bActive = CY_FALSE;
}

secret_stream_push_t::~secret_stream_push_t() noexcept
{
    SecretStreamPush_Cancel( this );
}

secret_stream_pull_t::~secret_stream_pull_t() noexcept
{
    SecretStreamPull_Cancel( this );
}

} // namespace cypher::security
