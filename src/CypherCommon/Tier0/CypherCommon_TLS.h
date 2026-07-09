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
#pragma once

/*
================
CypherCommon TLS

Thread-local storage slots used by low-level systems that need per-thread
context without passing a pointer through every call.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using tls_slot_t = u32;

constexpr tls_slot_t CY_TLS_INVALID_SLOT = CY_U32_MAX;

// Creates a generational TLS slot handle, or CY_TLS_INVALID_SLOT on failure.
tls_slot_t Cy_TLSCreateSlot();

// Destroys a TLS slot. Existing values for that old generation become invalid.
void Cy_TLSDestroySlot( tls_slot_t slot );

// Returns true when the slot currently refers to a live TLS slot generation.
bool_t Cy_TLSIsValidSlot( tls_slot_t slot );

// Stores a pointer value for the current thread and slot.
bool_t Cy_TLSSetValue( tls_slot_t slot, void *pValue );

// Reads the pointer value for the current thread and slot.
void *Cy_TLSGetValue( tls_slot_t slot );

// Clears the pointer value for the current thread and slot.
void Cy_TLSClearValue( tls_slot_t slot );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_TLS_H
