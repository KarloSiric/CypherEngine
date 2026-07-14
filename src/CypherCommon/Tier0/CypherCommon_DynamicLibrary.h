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

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct dynamic_library_t {
    void *pHandle;
};

bool_t DynamicLibrary_Load( dynamic_library_t *pLibrary, const char *pPath );
void DynamicLibrary_Unload( dynamic_library_t *pLibrary );
void *DynamicLibrary_GetSymbol( dynamic_library_t *pLibrary, const char *pSymbolName );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_DYNAMICLIBRARY_H
