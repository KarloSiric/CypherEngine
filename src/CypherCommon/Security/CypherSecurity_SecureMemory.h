//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_SecureMemory.h
//  Purpose: Declares guarded storage for cryptographic secrets.
//  Details: SecureMemory owns fixed-size, zero-on-release storage backed by
//           libsodium guard pages. Explicit access states reduce the time keys
//           remain readable or writable while preserving a C-style API.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_SECUREMEMORY_H
#define CYPHER_SECURITY_SECUREMEMORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_Types.h"
#include "CypherCommon_Defines.h"

namespace cypher::security
{

/*
================
Secure Memory Rules

- Storage is fixed-size and intended only for keys and comparable secrets.
- Separate objects may be used concurrently; one object requires external synchronization.
- Borrowed pointers expire when access protection changes or the object is destroyed.
- Memory locking reduces swap and dump exposure but cannot protect stack or register copies.
================
*/

// Memory locking is defense in depth: operating-system limits may prevent it.
enum class secure_memory_lock_policy_t : u8 {
    BEST_EFFORT = 0u, // Continue when the OS refuses to pin the pages.
    REQUIRE_LOCKED    // Fail creation unless the pages can be pinned.
};

// INACTIVE denotes an object that currently owns no guarded allocation.
enum class secure_memory_access_t : u8 {
    INACTIVE = 0u, // No allocation is owned.
    READ_WRITE,    // Secret bytes may be read and changed.
    READ_ONLY,     // Secret bytes may be read but not changed.
    NO_ACCESS      // Guarded pages reject both reads and writes.
};

// Owns one fixed-size guarded allocation. The public fields are implementation
// state and must not be changed directly; use the SecureMemory functions below.
struct secure_memory_t {
    secure_memory_t() noexcept = default;
    CYPHER_SECURITY_API ~secure_memory_t() noexcept;
    CYPHER_NO_COPY_MOVE( secure_memory_t );

    void *pMemory{ nullptr }; // Start of the guarded backend allocation.
    usize cbMemory{ 0u };     // Usable secret-byte count.
    secure_memory_access_t access{ secure_memory_access_t::INACTIVE }; // Current page protection.
    bool_t bLocked{ CY_FALSE }; // True when the OS pinned the pages against swapping.
};

// Allocates zeroed guarded storage in READ_WRITE state. Creation is
// transactional: the destination remains unchanged whenever it fails.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecureMemory_Create(
    usize cbMemory,
    secure_memory_lock_policy_t lockPolicy,
    secure_memory_t *pMemoryOut ) noexcept;

// Securely clears and releases the allocation. Repeated destruction is safe.
CYPHER_SECURITY_API void SecureMemory_Destroy(
    secure_memory_t *pMemory ) noexcept;

// Clears every secret byte while preserving the current access state. A failed
// protection restore leaves the allocation readable and writable and reports it.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecureMemory_Zero(
    secure_memory_t *pMemory ) noexcept;

// Changes a valid allocation to readable and writable storage.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecureMemory_SetReadWrite(
    secure_memory_t *pMemory ) noexcept;

// Changes a valid allocation to read-only storage.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecureMemory_SetReadOnly(
    secure_memory_t *pMemory ) noexcept;

// Makes a valid allocation inaccessible without destroying its contents.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecureMemory_SetNoAccess(
    secure_memory_t *pMemory ) noexcept;

// Borrows writable bytes until the next access-state change or destruction.
// Returns nullptr unless the allocation is currently READ_WRITE.
CYPHER_NODISCARD CYPHER_SECURITY_API
byte *SecureMemory_Data(
    secure_memory_t *pMemory ) noexcept;

// Borrows readable bytes until the next access-state change or destruction.
// Returns nullptr while the allocation is inactive or NO_ACCESS.
CYPHER_NODISCARD CYPHER_SECURITY_API
const byte *SecureMemory_ConstData(
    const secure_memory_t *pMemory ) noexcept;

// Returns true only for a complete, internally consistent active allocation.
CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecureMemory_IsValid(
    const secure_memory_t *pMemory ) noexcept;

// Reports whether explicit operating-system memory locking succeeded.
CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecureMemory_IsLocked(
    const secure_memory_t *pMemory ) noexcept;

// Returns the allocation size, or zero for a null or inactive object.
CYPHER_NODISCARD CYPHER_SECURITY_API
usize SecureMemory_Size(
    const secure_memory_t *pMemory ) noexcept;

// Returns INACTIVE for a null or inactive object.
CYPHER_NODISCARD CYPHER_SECURITY_API
secure_memory_access_t SecureMemory_GetAccess(
    const secure_memory_t *pMemory ) noexcept;

} // namespace cypher::security

#endif // CYPHER_SECURITY_SECUREMEMORY_H
