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

struct compiler_info_t {
    const char *pName;
    u32 version;
    bool_t has_exceptions;
    bool_t has_rtti;
};

// Returns the compiler identity detected by CypherCommon_Platform.h.
inline compiler_info_t Compiler_GetInfo()
{
    compiler_info_t info = {};
    info.pName = CYPHER_COMPILER_NAME;
    info.version = static_cast<u32>( CYPHER_COMPILER_VERSION );
    info.has_exceptions = CYPHER_CPP_EXCEPTIONS != 0;
    info.has_rtti = CYPHER_CPP_RTTI != 0;
    return info;
}

// Returns the normalized compiler name string.
inline const char *Compiler_GetName()
{
    return CYPHER_COMPILER_NAME;
}

// Returns a packed compiler version value.
inline u32 Compiler_GetVersion()
{
    return static_cast<u32>( CYPHER_COMPILER_VERSION );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_COMPILER_H
