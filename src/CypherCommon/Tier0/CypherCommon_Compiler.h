//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Compiler.h
//  Purpose: Declares CypherCommon Tier0 Compiler support.
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

#ifndef CYPHER_COMMON_TIER0_COMPILER_H
#define CYPHER_COMMON_TIER0_COMPILER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Compiler

Compiler identity and feature declarations.
================
*/

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"

namespace cypher::common
{

enum class compiler_type_t : u8 {
    Unknown = 0u,
    Msvc,
    Clang,
    Gcc
};

struct compiler_info_t {
    compiler_type_t type = compiler_type_t::Unknown;
    const char *pName = "Unknown";

    u32 version = 0u;
    u32 versionMajor = 0u;
    u32 versionMinor = 0u;
    u32 versionPatch = 0u;

    bool_t isClangCl = CY_FALSE;
    bool_t usesMsvcAbi = CY_FALSE;
    bool_t hasExceptions = CY_FALSE;
    bool_t hasRtti = CY_FALSE;
};

// Returns the normalized compiler frontend selected by platform detection.
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

// Returns the compiler identity detected by CypherCommon_Platform.h.
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

// Returns the normalized compiler name string.
CYPHER_NODISCARD constexpr const char *Cy_CompilerGetName() noexcept
{
    return CYPHER_COMPILER_NAME;
}

// Returns a packed compiler version value.
CYPHER_NODISCARD constexpr u32 Cy_CompilerGetVersion() noexcept
{
    return static_cast<u32>( CYPHER_COMPILER_VERSION );
}

// Returns the compiler frontend's major version component.
CYPHER_NODISCARD constexpr u32 Cy_CompilerGetVersionMajor() noexcept
{
    return static_cast<u32>( CYPHER_COMPILER_VERSION_MAJOR );
}

// Returns the compiler frontend's minor version component.
CYPHER_NODISCARD constexpr u32 Cy_CompilerGetVersionMinor() noexcept
{
    return static_cast<u32>( CYPHER_COMPILER_VERSION_MINOR );
}

// Returns the compiler frontend's patch or toolset build component.
CYPHER_NODISCARD constexpr u32 Cy_CompilerGetVersionPatch() noexcept
{
    return static_cast<u32>( CYPHER_COMPILER_VERSION_PATCH );
}

// Returns whether the frontend is Clang operating in clang-cl mode.
CYPHER_NODISCARD constexpr bool_t Cy_CompilerIsClangCl() noexcept
{
    return CYPHER_COMPILER_CLANG_CL != 0;
}

// Returns whether the compiler targets the Microsoft C++ ABI.
CYPHER_NODISCARD constexpr bool_t Cy_CompilerUsesMsvcAbi() noexcept
{
    return CYPHER_COMPILER_MSVC_ABI != 0;
}

// Returns whether language-level C++ exceptions are enabled.
CYPHER_NODISCARD constexpr bool_t Cy_CompilerHasExceptions() noexcept
{
    return CYPHER_CPP_EXCEPTIONS != 0;
}

// Returns whether C++ runtime type information is enabled.
CYPHER_NODISCARD constexpr bool_t Cy_CompilerHasRtti() noexcept
{
    return CYPHER_CPP_RTTI != 0;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_COMPILER_H
