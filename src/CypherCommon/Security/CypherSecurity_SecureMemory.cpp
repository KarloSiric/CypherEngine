//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_SecureMemory.cpp
//  Purpose: Implements guarded storage for cryptographic secrets.
//  Details: Allocation, page locking, access protection, and destruction stay
//           behind Cypher contracts while libsodium provides the platform layer.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity_SecureMemory.h"

#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"

#include <sodium.h>

namespace cypher::security
{

namespace
{

CYPHER_NODISCARD bool_t SecureMemory_LockPolicyIsValid(
    secure_memory_lock_policy_t lockPolicy ) noexcept
{
    return lockPolicy == secure_memory_lock_policy_t::BEST_EFFORT ||
           lockPolicy == secure_memory_lock_policy_t::REQUIRE_LOCKED;
}

CYPHER_NODISCARD bool_t SecureMemory_AccessIsActive(
    secure_memory_access_t access ) noexcept
{
    return access == secure_memory_access_t::READ_WRITE ||
           access == secure_memory_access_t::READ_ONLY ||
           access == secure_memory_access_t::NO_ACCESS;
}

CYPHER_NODISCARD bool_t SecureMemory_IsInactive(
    const secure_memory_t &memory ) noexcept
{
    return memory.pMemory == nullptr &&
           memory.cbMemory == 0u &&
           memory.access == secure_memory_access_t::INACTIVE &&
           !memory.bLocked;
}

void SecureMemory_ResetState( secure_memory_t &memory ) noexcept
{
    memory.pMemory = nullptr;
    memory.cbMemory = 0u;
    memory.access = secure_memory_access_t::INACTIVE;
    memory.bLocked = CY_FALSE;
}

CYPHER_NODISCARD security_status_t SecureMemory_SetAccess(
    secure_memory_t *pMemory,
    secure_memory_access_t access ) noexcept
{
    const bool_t bValidMemory = SecureMemory_IsValid( pMemory );
    CY_ASSERT_MSG(
        bValidMemory,
        "SecureMemory access protection requires an active allocation." );
    if ( !bValidMemory ) {
        return security_status_t::INVALID_STATE;
    }
    if ( pMemory->access == access ) {
        return security_status_t::OK;
    }

    int result = -1;
    switch ( access ) {
        case secure_memory_access_t::READ_WRITE:
            result = sodium_mprotect_readwrite( pMemory->pMemory );
            break;
        case secure_memory_access_t::READ_ONLY:
            result = sodium_mprotect_readonly( pMemory->pMemory );
            break;
        case secure_memory_access_t::NO_ACCESS:
            result = sodium_mprotect_noaccess( pMemory->pMemory );
            break;
        case secure_memory_access_t::INACTIVE:
        default:
            CY_ASSERT_MSG( false, "SecureMemory cannot protect to an inactive state." );
            return security_status_t::INVALID_ARGUMENT;
    }

    if ( result != 0 ) {
        return security_status_t::PROTECTION_FAILED;
    }

    // Update the tracked state only after the operating system accepts it.
    pMemory->access = access;
    return security_status_t::OK;
}

} // namespace

security_status_t SecureMemory_Create(
    usize cbMemory,
    secure_memory_lock_policy_t lockPolicy,
    secure_memory_t *pMemoryOut ) noexcept
{
    const bool_t bValidOutput = pMemoryOut != nullptr;
    const bool_t bValidSize = cbMemory > 0u;
    const bool_t bValidPolicy = SecureMemory_LockPolicyIsValid( lockPolicy );
    CY_ASSERT_MSG( bValidOutput, "SecureMemory_Create requires output storage." );
    CY_ASSERT_MSG( bValidSize, "SecureMemory_Create requires a non-zero size." );
    CY_ASSERT_MSG( bValidPolicy, "SecureMemory_Create received an invalid lock policy." );
    if ( !bValidOutput || !bValidSize || !bValidPolicy ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    const bool_t bInactiveOutput = SecureMemory_IsInactive( *pMemoryOut );
    CY_ASSERT_MSG(
        bInactiveOutput,
        "SecureMemory_Create requires an inactive destination." );
    if ( !bInactiveOutput ) {
        return security_status_t::INVALID_STATE;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    void *pAllocation = sodium_malloc( cbMemory );
    if ( pAllocation == nullptr ) {
        return security_status_t::OUT_OF_MEMORY;
    }

    sodium_memzero( pAllocation, cbMemory );
    const bool_t bLocked = sodium_mlock( pAllocation, cbMemory ) == 0;
    if ( !bLocked &&
         lockPolicy == secure_memory_lock_policy_t::REQUIRE_LOCKED ) {
        sodium_free( pAllocation );
        return security_status_t::PROTECTION_FAILED;
    }

    pMemoryOut->pMemory = pAllocation;
    pMemoryOut->cbMemory = cbMemory;
    pMemoryOut->access = secure_memory_access_t::READ_WRITE;
    pMemoryOut->bLocked = bLocked;
    return security_status_t::OK;
}

void SecureMemory_Destroy( secure_memory_t *pMemory ) noexcept
{
    if ( pMemory == nullptr ) {
        return;
    }
    if ( pMemory->pMemory != nullptr ) {
        // sodium_free restores access when needed, verifies the canary, clears
        // the allocation, unlocks it, and releases its guarded pages.
        sodium_free( pMemory->pMemory );
    }
    SecureMemory_ResetState( *pMemory );
}

security_status_t SecureMemory_Zero(
    secure_memory_t *pMemory ) noexcept
{
    const bool_t bValidMemory = SecureMemory_IsValid( pMemory );
    CY_ASSERT_MSG( bValidMemory, "SecureMemory_Zero requires an active allocation." );
    if ( !bValidMemory ) {
        return security_status_t::INVALID_STATE;
    }

    const secure_memory_access_t previousAccess = pMemory->access;
    const security_status_t writableResult = SecureMemory_SetAccess(
        pMemory,
        secure_memory_access_t::READ_WRITE );
    if ( writableResult != security_status_t::OK ) {
        return writableResult;
    }

    sodium_memzero( pMemory->pMemory, pMemory->cbMemory );
    if ( previousAccess == secure_memory_access_t::READ_WRITE ) {
        return security_status_t::OK;
    }
    return SecureMemory_SetAccess( pMemory, previousAccess );
}

security_status_t SecureMemory_SetReadWrite(
    secure_memory_t *pMemory ) noexcept
{
    return SecureMemory_SetAccess(
        pMemory,
        secure_memory_access_t::READ_WRITE );
}

security_status_t SecureMemory_SetReadOnly(
    secure_memory_t *pMemory ) noexcept
{
    return SecureMemory_SetAccess(
        pMemory,
        secure_memory_access_t::READ_ONLY );
}

security_status_t SecureMemory_SetNoAccess(
    secure_memory_t *pMemory ) noexcept
{
    return SecureMemory_SetAccess(
        pMemory,
        secure_memory_access_t::NO_ACCESS );
}

byte *SecureMemory_Data(
    secure_memory_t *pMemory ) noexcept
{
    const bool_t bWritable =
        SecureMemory_IsValid( pMemory ) &&
        pMemory->access == secure_memory_access_t::READ_WRITE;
    CY_ASSERT_MSG(
        bWritable,
        "SecureMemory_Data requires readable and writable access." );
    return bWritable ? static_cast<byte *>( pMemory->pMemory ) : nullptr;
}

const byte *SecureMemory_ConstData(
    const secure_memory_t *pMemory ) noexcept
{
    const bool_t bReadable =
        SecureMemory_IsValid( pMemory ) &&
        pMemory->access != secure_memory_access_t::NO_ACCESS;
    CY_ASSERT_MSG(
        bReadable,
        "SecureMemory_ConstData requires readable access." );
    return bReadable
        ? static_cast<const byte *>( pMemory->pMemory )
        : nullptr;
}

bool_t SecureMemory_IsValid(
    const secure_memory_t *pMemory ) noexcept
{
    return pMemory != nullptr &&
           pMemory->pMemory != nullptr &&
           pMemory->cbMemory > 0u &&
           SecureMemory_AccessIsActive( pMemory->access );
}

bool_t SecureMemory_IsLocked(
    const secure_memory_t *pMemory ) noexcept
{
    return SecureMemory_IsValid( pMemory ) && pMemory->bLocked;
}

usize SecureMemory_Size(
    const secure_memory_t *pMemory ) noexcept
{
    return SecureMemory_IsValid( pMemory ) ? pMemory->cbMemory : 0u;
}

secure_memory_access_t SecureMemory_GetAccess(
    const secure_memory_t *pMemory ) noexcept
{
    return SecureMemory_IsValid( pMemory )
        ? pMemory->access
        : secure_memory_access_t::INACTIVE;
}

secure_memory_t::~secure_memory_t() noexcept
{
    SecureMemory_Destroy( this );
}

} // namespace cypher::security
