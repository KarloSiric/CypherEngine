//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_DynamicLibrary.h
//  Purpose: Declares CypherCommon Tier0 DynamicLibrary support.
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

#ifndef CYPHER_COMMON_TIER0_DYNAMICLIBRARY_H
#define CYPHER_COMMON_TIER0_DYNAMICLIBRARY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Dynamic Library

Runtime shared library declarations.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"

namespace cypher::common
{

constexpr usize CY_DYNAMIC_LIBRARY_ERROR_MAX = 512u;

enum cy_dynamic_library_flags_t : flags32_t {
    CY_DYNAMIC_LIBRARY_NONE = 0u,
    CY_DYNAMIC_LIBRARY_RESOLVE_LAZY = CYPHER_BIT32( 0 ),
    CY_DYNAMIC_LIBRARY_GLOBAL_SYMBOLS = CYPHER_BIT32( 1 )
};

struct dynamic_library_t {
    void *pHandle = nullptr;
    char szLastError[CY_DYNAMIC_LIBRARY_ERROR_MAX] = {};
};

// Initializes an unloaded library handle.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_DynamicLibraryInit(
    dynamic_library_t *pLibrary ) noexcept;

// Loads with immediate symbol resolution and local symbol visibility.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_DynamicLibraryLoad(
    dynamic_library_t *pLibrary,
    const char *pszPath ) noexcept;

// Loads with explicit resolution and symbol visibility flags.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_DynamicLibraryLoadEx(
    dynamic_library_t *pLibrary,
    const char *pszPath,
    flags32_t flags ) noexcept;

// Unloads a library. Calling this on an unloaded initialized handle succeeds.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_DynamicLibraryUnload(
    dynamic_library_t *pLibrary ) noexcept;

[[nodiscard]] CYPHER_COMMON_API bool_t Cy_DynamicLibraryIsLoaded(
    const dynamic_library_t *pLibrary ) noexcept;

// Resolves one exported symbol and records platform diagnostics on failure.
[[nodiscard]] CYPHER_COMMON_API void *Cy_DynamicLibraryGetSymbol(
    dynamic_library_t *pLibrary,
    const char *pszSymbolName ) noexcept;

[[nodiscard]] CYPHER_COMMON_API const char *Cy_DynamicLibraryGetLastError(
    const dynamic_library_t *pLibrary ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_DYNAMICLIBRARY_H
