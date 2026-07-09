//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Minidump.h
//  Purpose: Declares CypherCommon Tier0 Minidump support.
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

#ifndef CYPHER_COMMON_TIER0_MINIDUMP_H
#define CYPHER_COMMON_TIER0_MINIDUMP_H
#pragma once

/*
================
CypherCommon Minidump

Portable diagnostic dump declarations for crash and validation reporting.
This Tier0 layer writes deterministic text diagnostics; native OS dump backends
can be layered behind the same call later.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct minidump_info_t {
    const char *pApplicationName;
    const char *pVersion;
    const char *pOutputPath;
};

bool_t Minidump_Write( const minidump_info_t &info );
void Minidump_SetOutputPath( const char *pPath );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MINIDUMP_H
