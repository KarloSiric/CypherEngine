//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Compiler.h
//  Purpose: Exposes normalized compiler identity and enabled C++ runtime features.
//  Details: Values mirror CypherCommon_Platform.h and are available at compile time.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_COMPILER_H
#define CYPHER_COMMON_TIER0_COMPILER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"

namespace cypher::common
{

enum class compiler_type_t : u8 {
    Unknown = 0u,
    Msvc,  // Microsoft C/C++ frontend.
    Clang, // LLVM Clang, including clang-cl.
    Gcc    // GNU C++ frontend.
};

struct compiler_info_t {
    compiler_type_t type = compiler_type_t::Unknown; // Normalized frontend family.
    const char *pName = "Unknown";                    // Static compiler-owned name string.

    u32 version = 0u;      // Packed diagnostic value; do not use for ordering.
    u32 versionMajor = 0u; // Vendor major component.
    u32 versionMinor = 0u; // Vendor minor component.
    u32 versionPatch = 0u; // Vendor patch or toolset-build component.

    bool_t isClangCl = CY_FALSE;     // Clang frontend using the MSVC command-line mode.
    bool_t usesMsvcAbi = CY_FALSE;   // Target uses Microsoft's C++ ABI.
    bool_t hasExceptions = CY_FALSE; // Language-level exception support is enabled.
    bool_t hasRtti = CY_FALSE;       // C++ runtime type information is enabled.
};

// All accessors are compile-time views of the platform detection macros.
CYPHER_NODISCARD constexpr compiler_type_t Cy_CompilerGetType() noexcept
{
#if CYPHER_COMPILER_MSVC
    return compiler_type_t::Msvc;
#elif CYPHER_COMPILER_CLANG
    return compiler_type_t::Clang;
#elif CYPHER_COMPILER_GCC
    return compiler_type_t::Gcc;
#else
    return compiler_type_t::Unknown;
#endif
}

CYPHER_NODISCARD constexpr compiler_info_t Cy_CompilerGetInfo() noexcept
{
    compiler_info_t info = {};
    info.type = Cy_CompilerGetType();
    info.pName = CYPHER_COMPILER_NAME;
    info.version = static_cast<u32>( CYPHER_COMPILER_VERSION );
    info.versionMajor = static_cast<u32>( CYPHER_COMPILER_VERSION_MAJOR );
    info.versionMinor = static_cast<u32>( CYPHER_COMPILER_VERSION_MINOR );
    info.versionPatch = static_cast<u32>( CYPHER_COMPILER_VERSION_PATCH );
    info.isClangCl = CYPHER_COMPILER_CLANG_CL != 0;
    info.usesMsvcAbi = CYPHER_COMPILER_MSVC_ABI != 0;
    info.hasExceptions = CYPHER_CPP_EXCEPTIONS != 0;
    info.hasRtti = CYPHER_CPP_RTTI != 0;
    return info;
}

CYPHER_NODISCARD constexpr const char *Cy_CompilerGetName() noexcept
{
    return CYPHER_COMPILER_NAME;
}

CYPHER_NODISCARD constexpr u32 Cy_CompilerGetVersion() noexcept
{
    return static_cast<u32>( CYPHER_COMPILER_VERSION );
}

CYPHER_NODISCARD constexpr u32 Cy_CompilerGetVersionMajor() noexcept
{
    return static_cast<u32>( CYPHER_COMPILER_VERSION_MAJOR );
}

CYPHER_NODISCARD constexpr u32 Cy_CompilerGetVersionMinor() noexcept
{
    return static_cast<u32>( CYPHER_COMPILER_VERSION_MINOR );
}

CYPHER_NODISCARD constexpr u32 Cy_CompilerGetVersionPatch() noexcept
{
    return static_cast<u32>( CYPHER_COMPILER_VERSION_PATCH );
}

CYPHER_NODISCARD constexpr bool_t Cy_CompilerIsClangCl() noexcept
{
    return CYPHER_COMPILER_CLANG_CL != 0;
}

CYPHER_NODISCARD constexpr bool_t Cy_CompilerUsesMsvcAbi() noexcept
{
    return CYPHER_COMPILER_MSVC_ABI != 0;
}

CYPHER_NODISCARD constexpr bool_t Cy_CompilerHasExceptions() noexcept
{
    return CYPHER_CPP_EXCEPTIONS != 0;
}

CYPHER_NODISCARD constexpr bool_t Cy_CompilerHasRtti() noexcept
{
    return CYPHER_CPP_RTTI != 0;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_COMPILER_H
