//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_Hash.cpp
//  Purpose: Implements cryptographic digest and keyed short-hash services.
//  Details: Third-party types remain private while all input, output, state, and
//           constant-time comparison contracts are enforced at the Cypher boundary.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity_Hash.h"
#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"

#include <new>
#include <sodium.h>

namespace cypher::security
{

namespace
{

constexpr byte g_emptySecurityInput = 0u;

CYPHER_NODISCARD const byte *SecurityHash_Input(
    binary_block_t data ) noexcept
{
    return data.cbSize == 0u ? &g_emptySecurityInput : data.pData;
}

CYPHER_NODISCARD bool_t SecurityDigestSizeIsValid(
    usize cbDigest ) noexcept
{
    return cbDigest >= CY_SECURITY_DIGEST_MIN_SIZE &&
           cbDigest <= CY_SECURITY_DIGEST_MAX_SIZE;
}

CYPHER_NODISCARD bool_t SecurityDigestKeyIsValid(
    binary_block_t key ) noexcept
{
    return BinaryBlock_IsValid( key ) &&
           ( key.cbSize == 0u ||
             ( key.cbSize >= CY_SECURITY_DIGEST_KEY_MIN_SIZE &&
               key.cbSize <= CY_SECURITY_DIGEST_KEY_MAX_SIZE ) );
}

CYPHER_NODISCARD crypto_generichash_state *SecurityDigest_State(
    security_digest_stream_t *pStream ) noexcept
{
    return std::launder(
        reinterpret_cast<crypto_generichash_state *>( pStream->storage ) );
}

} // namespace

static_assert(
    sizeof( crypto_generichash_state ) <= CY_SECURITY_DIGEST_STREAM_STORAGE_SIZE,
    "libsodium digest state exceeds Cypher's opaque storage." );
static_assert(
    alignof( crypto_generichash_state ) <=
        CY_SECURITY_DIGEST_STREAM_STORAGE_ALIGNMENT,
    "libsodium digest state exceeds Cypher's opaque alignment." );
static_assert(
    CY_SECURITY_DIGEST_MIN_SIZE >= crypto_generichash_BYTES_MIN &&
    CY_SECURITY_DIGEST_MAX_SIZE <= crypto_generichash_BYTES_MAX,
    "Cypher digest sizes must remain inside libsodium's supported range." );
static_assert(
    CY_SECURITY_SHORT_HASH_KEY_SIZE == crypto_shorthash_KEYBYTES &&
    CY_SECURITY_SHORT_HASH_SIZE == crypto_shorthash_BYTES,
    "Cypher SipHash storage must match libsodium." );

security_status_t SecurityDigest_Data(
    binary_block_t data,
    binary_block_t key,
    usize cbDigest,
    security_digest_t *pDigestOut ) noexcept
{
    const bool_t bValidData = BinaryBlock_IsValid( data );
    const bool_t bValidKey = SecurityDigestKeyIsValid( key );
    const bool_t bValidSize = SecurityDigestSizeIsValid( cbDigest );
    const bool_t bValidOutput = pDigestOut != nullptr;
    CY_ASSERT_MSG( bValidData, "Security digest requires a valid data range." );
    CY_ASSERT_MSG( bValidKey, "Security digest key length is invalid." );
    CY_ASSERT_MSG( bValidSize, "Security digest output length is invalid." );
    CY_ASSERT_MSG( bValidOutput, "Security digest requires output storage." );
    if ( !bValidData || !bValidKey || !bValidSize || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_digest_t digest{};
    const int result = crypto_generichash(
        digest.bytes,
        cbDigest,
        SecurityHash_Input( data ),
        static_cast<unsigned long long>( data.cbSize ),
        key.cbSize == 0u ? nullptr : key.pData,
        key.cbSize );
    if ( result != 0 ) {
        return security_status_t::OPERATION_FAILED;
    }
    digest.cbSize = cbDigest;
    *pDigestOut = digest;
    return security_status_t::OK;
}

security_status_t SecurityDigest_Begin(
    security_digest_stream_t *pStream,
    binary_block_t key,
    usize cbDigest ) noexcept
{
    const bool_t bValidStream = pStream != nullptr;
    const bool_t bValidKey = SecurityDigestKeyIsValid( key );
    const bool_t bValidSize = SecurityDigestSizeIsValid( cbDigest );
    CY_ASSERT_MSG( bValidStream, "Security digest stream requires storage." );
    CY_ASSERT_MSG( bValidKey, "Security digest stream key length is invalid." );
    CY_ASSERT_MSG( bValidSize, "Security digest stream output length is invalid." );
    if ( !bValidStream || !bValidKey || !bValidSize ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( pStream->bActive ) {
        return security_status_t::INVALID_STATE;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    ::new ( static_cast<void *>( pStream->storage ) ) crypto_generichash_state{};
    const int result = crypto_generichash_init(
        SecurityDigest_State( pStream ),
        key.cbSize == 0u ? nullptr : key.pData,
        key.cbSize,
        cbDigest );
    if ( result != 0 ) {
        Security_ZeroMemory( pStream->storage, sizeof( pStream->storage ) );
        pStream->cbDigest = 0u;
        return security_status_t::OPERATION_FAILED;
    }

    pStream->cbDigest = cbDigest;
    pStream->bActive = CY_TRUE;
    return security_status_t::OK;
}

security_status_t SecurityDigest_Update(
    security_digest_stream_t *pStream,
    binary_block_t data ) noexcept
{
    const bool_t bValidStream = pStream != nullptr;
    const bool_t bActive = bValidStream && pStream->bActive;
    const bool_t bValidData = BinaryBlock_IsValid( data );
    CY_ASSERT_MSG( bValidStream, "Security digest update requires stream storage." );
    CY_ASSERT_MSG( bValidData, "Security digest update requires valid data." );
    if ( !bValidStream || !bValidData ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !bActive ) {
        return security_status_t::INVALID_STATE;
    }

    return crypto_generichash_update(
               SecurityDigest_State( pStream ),
               SecurityHash_Input( data ),
               static_cast<unsigned long long>( data.cbSize ) ) == 0
        ? security_status_t::OK
        : security_status_t::OPERATION_FAILED;
}

void SecurityDigest_Cancel(
    security_digest_stream_t *pStream ) noexcept
{
    if ( pStream == nullptr ) {
        return;
    }

    Security_ZeroMemory( pStream->storage, sizeof( pStream->storage ) );
    pStream->cbDigest = 0u;
    pStream->bActive = CY_FALSE;
}

security_status_t SecurityDigest_End(
    security_digest_stream_t *pStream,
    security_digest_t *pDigestOut ) noexcept
{
    const bool_t bValidStream = pStream != nullptr;
    const bool_t bActive = bValidStream && pStream->bActive;
    const bool_t bValidOutput = pDigestOut != nullptr;
    CY_ASSERT_MSG( bValidStream, "Security digest finalization requires stream storage." );
    CY_ASSERT_MSG( bValidOutput, "Security digest finalization requires output storage." );
    if ( !bValidStream || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !bActive ) {
        return security_status_t::INVALID_STATE;
    }

    security_digest_t digest{};
    const int result = crypto_generichash_final(
        SecurityDigest_State( pStream ),
        digest.bytes,
        pStream->cbDigest );
    digest.cbSize = result == 0 ? pStream->cbDigest : 0u;
    SecurityDigest_Cancel( pStream );
    if ( result != 0 ) {
        return security_status_t::OPERATION_FAILED;
    }

    *pDigestOut = digest;
    return security_status_t::OK;
}

bool_t SecurityDigest_IsValid(
    const security_digest_t &digest ) noexcept
{
    return SecurityDigestSizeIsValid( digest.cbSize );
}

bool_t SecurityDigest_Equals(
    const security_digest_t &left,
    const security_digest_t &right ) noexcept
{
    if ( !SecurityDigest_IsValid( left ) ||
         !SecurityDigest_IsValid( right ) ||
         left.cbSize != right.cbSize ) {
        return CY_FALSE;
    }
    return Security_ConstantTimeEquals(
        left.bytes,
        right.bytes,
        left.cbSize );
}

security_status_t SecurityShortHash_GenerateKey(
    security_short_hash_key_t *pKeyOut ) noexcept
{
    const bool_t bValidOutput = pKeyOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "SipHash key generation requires output storage." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    randombytes_buf( pKeyOut->bytes, sizeof( pKeyOut->bytes ) );
    return security_status_t::OK;
}

security_status_t SecurityShortHash_Data(
    binary_block_t data,
    const security_short_hash_key_t &key,
    security_short_hash_t *pHashOut ) noexcept
{
    const bool_t bValidData = BinaryBlock_IsValid( data );
    const bool_t bValidOutput = pHashOut != nullptr;
    CY_ASSERT_MSG( bValidData, "SipHash requires a valid data range." );
    CY_ASSERT_MSG( bValidOutput, "SipHash requires output storage." );
    if ( !bValidData || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    return crypto_shorthash(
               pHashOut->bytes,
               SecurityHash_Input( data ),
               static_cast<unsigned long long>( data.cbSize ),
               key.bytes ) == 0
        ? security_status_t::OK
        : security_status_t::OPERATION_FAILED;
}

bool_t SecurityShortHash_Equals(
    const security_short_hash_t &left,
    const security_short_hash_t &right ) noexcept
{
    return Security_ConstantTimeEquals(
        left.bytes,
        right.bytes,
        sizeof( left.bytes ) );
}

hash64_t SecurityShortHash_ToU64(
    const security_short_hash_t &hash ) noexcept
{
    hash64_t value = 0u;
    for ( usize iByte = 0u; iByte < sizeof( hash.bytes ); ++iByte ) {
        value |= static_cast<hash64_t>( hash.bytes[iByte] ) <<
                 static_cast<common::u32>( iByte * 8u );
    }
    return value;
}

security_digest_stream_t::~security_digest_stream_t() noexcept
{
    SecurityDigest_Cancel( this );
}

} // namespace cypher::security
