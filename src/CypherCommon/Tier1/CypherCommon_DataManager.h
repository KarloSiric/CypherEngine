//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_DataManager.h
//  Purpose: Declares CypherCommon Tier1 DataManager support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_DATAMANAGER_H
#define CYPHER_COMMON_TIER1_DATAMANAGER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Data Manager

Named data registry declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct data_manager_t;

bool_t DataManager_Register( data_manager_t *pManager, const char *pName, void *pData );
void *DataManager_Find( data_manager_t *pManager, const char *pName );
bool_t DataManager_Remove( data_manager_t *pManager, const char *pName );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_DATAMANAGER_H
