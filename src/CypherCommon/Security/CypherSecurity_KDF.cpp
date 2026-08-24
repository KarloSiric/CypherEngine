//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_KDF.cpp
//  Purpose: Implements domain-separated cryptographic key derivation.
//  Details: BLAKE2b KDF output is written directly into guarded memory so master
//           and derived key material never require ordinary heap storage.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity_KDF.h"

#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"
#include "CypherCommon_MemoryOps.h"

#include <sodium.h>

namespace cypher::security
{

namespace
{

CYPHER_NODISCARD bool_t SecurityKdf_SubkeySizeIsValid(
    usize cbSubkey ) noexcept
{
    return cbSubkey >= CY_SECURITY_KDF_SUBKEY_MIN_SIZE &&
           cbSubkey <= CY_SECURITY_KDF_SUBKEY_MAX_SIZE;
}

CYPHER_NODISCARD security_status_t SecurityKdf_FinalizeKey(
    secure_memory_t *pMemory ) noexcept
{
    // Key creation occurs in writable guarded memory. Publish it read-only;
    // if protection fails, destroy the bytes instead of returning mutable secret state.
    const security_status_t result = SecureMemory_SetReadOnly( pMemory );
    if ( result != security_status_t::OK ) {
        SecureMemory_Destroy( pMemory );
    }
    return result;
}

} // namespace

static_assert(
    CY_SECURITY_KDF_CONTEXT_SIZE == crypto_kdf_CONTEXTBYTES &&
    CY_SECURITY_KDF_MASTER_KEY_SIZE == crypto_kdf_KEYBYTES &&
    CY_SECURITY_KDF_SUBKEY_MIN_SIZE == crypto_kdf_BYTES_MIN &&
    CY_SECURITY_KDF_SUBKEY_MAX_SIZE == crypto_kdf_BYTES_MAX,
    "Cypher KDF contracts must match libsodium." );

security_status_t SecurityKdf_ContextFromBytes(
    const char *pContext,
    usize cchContext,
    kdf_context_t *pContextOut ) noexcept
{
    const bool_t bValidInput = pContext != nullptr;
    const bool_t bValidSize = cchContext == CY_SECURITY_KDF_CONTEXT_SIZE;
    const bool_t bValidOutput = pContextOut != nullptr;
    CY_ASSERT_MSG( bValidInput, "KDF context requires source bytes." );
    CY_ASSERT_MSG( bValidSize, "KDF context must contain exactly eight bytes." );
    CY_ASSERT_MSG( bValidOutput, "KDF context requires output storage." );
    if ( !bValidInput || !bValidSize || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    // libsodium uses an exact eight-byte context as the domain separator. It is
    // a fixed binary tag, not a null-terminated or variable-length string.
    kdf_context_t context{};
    common::Cy_MemCopy( context.bytes, pContext, sizeof( context.bytes ) );
    *pContextOut = context;
    return security_status_t::OK;
}

security_status_t SecurityKdf_GenerateMasterKey(
    secure_memory_lock_policy_t lockPolicy,
    kdf_master_key_t *pKeyOut ) noexcept
{
    const bool_t bValidOutput = pKeyOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "KDF master-key generation requires output storage." );
    if ( !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_KDF_MASTER_KEY_SIZE,
        lockPolicy,
        &pKeyOut->memory );
    if ( result != security_status_t::OK ) {
        return result;
    }

    // Generate directly into guarded storage so no ordinary heap copy of the
    // master key is created.
    crypto_kdf_keygen( SecureMemory_Data( &pKeyOut->memory ) );
    return SecurityKdf_FinalizeKey( &pKeyOut->memory );
}

security_status_t SecurityKdf_ImportMasterKey(
    binary_block_t keyBytes,
    secure_memory_lock_policy_t lockPolicy,
    kdf_master_key_t *pKeyOut ) noexcept
{
    const bool_t bValidInput =
        common::BinaryBlock_IsValid( keyBytes ) &&
        keyBytes.cbSize == CY_SECURITY_KDF_MASTER_KEY_SIZE;
    const bool_t bValidOutput = pKeyOut != nullptr;
    CY_ASSERT_MSG( bValidInput, "KDF master-key import requires exactly 32 bytes." );
    CY_ASSERT_MSG( bValidOutput, "KDF master-key import requires output storage." );
    if ( !bValidInput || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    security_status_t result = SecureMemory_Create(
        CY_SECURITY_KDF_MASTER_KEY_SIZE,
        lockPolicy,
        &pKeyOut->memory );
    if ( result != security_status_t::OK ) {
        return result;
    }
    common::Cy_MemCopy(
        SecureMemory_Data( &pKeyOut->memory ),
        keyBytes.pData,
        keyBytes.cbSize );
    return SecurityKdf_FinalizeKey( &pKeyOut->memory );
}

void SecurityKdf_DestroyMasterKey(
    kdf_master_key_t *pKey ) noexcept
{
    if ( pKey != nullptr ) {
        SecureMemory_Destroy( &pKey->memory );
    }
}

bool_t SecurityKdf_MasterKeyIsValid(
    const kdf_master_key_t *pKey ) noexcept
{
    return pKey != nullptr &&
           SecureMemory_IsValid( &pKey->memory ) &&
           SecureMemory_Size( &pKey->memory ) ==
               CY_SECURITY_KDF_MASTER_KEY_SIZE &&
           SecureMemory_GetAccess( &pKey->memory ) ==
               secure_memory_access_t::READ_ONLY;
}

security_status_t SecurityKdf_DeriveSubkey(
    const kdf_master_key_t *pMasterKey,
    const kdf_context_t &context,
    u64 nSubkeyId,
    usize cbSubkey,
    secure_memory_lock_policy_t lockPolicy,
    kdf_subkey_t *pSubkeyOut ) noexcept
{
    const bool_t bValidMaster = SecurityKdf_MasterKeyIsValid( pMasterKey );
    const bool_t bValidSize = SecurityKdf_SubkeySizeIsValid( cbSubkey );
    const bool_t bValidOutput = pSubkeyOut != nullptr;
    CY_ASSERT_MSG( bValidMaster, "KDF derivation requires a valid master key." );
    CY_ASSERT_MSG( bValidSize, "KDF subkey size must be between 16 and 64 bytes." );
    CY_ASSERT_MSG( bValidOutput, "KDF derivation requires output storage." );
    if ( !bValidMaster || !bValidSize || !bValidOutput ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    security_status_t result = SecureMemory_Create(
        cbSubkey,
        lockPolicy,
        &pSubkeyOut->memory );
    if ( result != security_status_t::OK ) {
        return result;
    }

    // Context separates subsystems; the 64-bit identifier separates keys within
    // that subsystem. Reusing an ID is safe only for the same intended purpose.
    const int deriveResult = crypto_kdf_derive_from_key(
        SecureMemory_Data( &pSubkeyOut->memory ),
        cbSubkey,
        nSubkeyId,
        context.bytes,
        SecureMemory_ConstData( &pMasterKey->memory ) );
    if ( deriveResult != 0 ) {
        SecureMemory_Destroy( &pSubkeyOut->memory );
        return security_status_t::OPERATION_FAILED;
    }
    return SecurityKdf_FinalizeKey( &pSubkeyOut->memory );
}

void SecurityKdf_DestroySubkey(
    kdf_subkey_t *pSubkey ) noexcept
{
    if ( pSubkey != nullptr ) {
        SecureMemory_Destroy( &pSubkey->memory );
    }
}

bool_t SecurityKdf_SubkeyIsValid(
    const kdf_subkey_t *pSubkey ) noexcept
{
    return pSubkey != nullptr &&
           SecureMemory_IsValid( &pSubkey->memory ) &&
           SecurityKdf_SubkeySizeIsValid(
               SecureMemory_Size( &pSubkey->memory ) ) &&
           SecureMemory_GetAccess( &pSubkey->memory ) ==
               secure_memory_access_t::READ_ONLY;
}

binary_block_t SecurityKdf_SubkeyBlock(
    const kdf_subkey_t *pSubkey ) noexcept
{
    if ( !SecurityKdf_SubkeyIsValid( pSubkey ) ) {
        return {};
    }
    return common::BinaryBlock_FromData(
        SecureMemory_ConstData( &pSubkey->memory ),
        SecureMemory_Size( &pSubkey->memory ) );
}

} // namespace cypher::security
