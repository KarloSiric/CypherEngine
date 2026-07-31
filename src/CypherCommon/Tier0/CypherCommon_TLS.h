//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_TLS.h
//  Purpose: Declares CypherCommon Tier0 TLS support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_TLS_H
#define CYPHER_COMMON_TIER0_TLS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon TLS

Thread-local storage slots used by low-level systems that need per-thread
context without passing a pointer through every call.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using tls_slot_t = u32;

constexpr tls_slot_t CY_TLS_INVALID_SLOT = CY_U32_MAX;
constexpr u32 CY_TLS_MAX_SLOT_COUNT = 256u;

using tls_destructor_t = void ( * )( void *pValue ) noexcept;

// Creates a generational TLS slot handle, or CY_TLS_INVALID_SLOT on failure.
CYPHER_NODISCARD CYPHER_COMMON_API tls_slot_t Cy_TLSCreateSlot(
    tls_destructor_t pDestructor = nullptr ) noexcept;

// Destroys a slot. Call only after other threads have cleared or exited.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_TLSDestroySlot( tls_slot_t slot ) noexcept;

// Returns true when the slot currently refers to a live TLS slot generation.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_TLSIsValidSlot(
    tls_slot_t slot ) noexcept;

// Stores a pointer value for the current thread and slot.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_TLSSetValue(
    tls_slot_t slot,
    void *pValue ) noexcept;

// Reads the pointer value for the current thread and slot.
CYPHER_NODISCARD CYPHER_COMMON_API void *Cy_TLSGetValue(
    tls_slot_t slot ) noexcept;

// Clears the pointer without invoking the optional thread-exit destructor.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_TLSClearValue( tls_slot_t slot ) noexcept;

// Returns the number of currently allocated slot handles.
CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_TLSGetAllocatedSlotCount() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_TLS_H
