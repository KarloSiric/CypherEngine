//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Module.h
//  Purpose: Declares binary-module metadata and legal lifecycle transitions.
//  Details: This is the ABI-facing contract used by optional engine/game/tool
//           modules. It does not load libraries; DynamicLibrary owns that step.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_MODULE_H
#define CYPHER_COMMON_TIER0_MODULE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Module

Runtime module metadata and lifecycle declarations for engine libraries, game
DLLs, tools and editor plugins.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Error.h"

namespace cypher::common
{

enum class module_state_t : u8 {
    Unloaded = 0u, // No native library or exports are available.
    Loaded,        // Binary is loaded and its descriptor may be inspected.
    Initialized,   // Module initialization completed successfully.
    Shutdown       // Shutdown callback ran; binary may now be unloaded.
};

struct module_version_t {
    u32 nMajor; // Compatibility-breaking generation.
    u32 nMinor; // Backward-compatible feature generation.
    u32 nPatch; // Corrective generation.
    u32 nBuild; // Informational build metadata.
};

struct module_desc_t {
    const char *pszName;         // User-facing module name.
    const char *pszInternalName; // Stable machine-facing identifier.
    const char *pszDescription;  // Optional diagnostic description.
    module_version_t version;    // Product compatibility version.
    u32 nApiVersion;             // Exact function-table layout version.
};

// Module callbacks must not allow C++ exceptions to cross binary boundaries.
using module_init_fn_t =
    error_code_t ( CYPHER_CALL * )( void *pUserData ) noexcept;
using module_shutdown_fn_t =
    void ( CYPHER_CALL * )( void *pUserData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API const char *Cy_ModuleStateName(
    module_state_t state ) noexcept;

// Requires the same major version and an equal-or-newer minor/patch version.
// Build metadata is intentionally not part of compatibility.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ModuleVersionCompatible(
    const module_version_t &required,
    const module_version_t &provided ) noexcept;

// Binary API tables require an exact version match.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ModuleApiVersionCompatible(
    u32 nRequiredApiVersion,
    u32 nProvidedApiVersion ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ModuleDescriptorIsValid(
    const module_desc_t *pDescriptor ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ModuleCanTransition(
    module_state_t from,
    module_state_t to ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MODULE_H
