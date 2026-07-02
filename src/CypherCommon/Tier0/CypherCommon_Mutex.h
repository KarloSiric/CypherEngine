//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Mutex.h
//  Purpose: Declares CypherCommon Tier0 Mutex support.
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

#ifndef CYPHER_COMMON_TIER0_MUTEX_H
#define CYPHER_COMMON_TIER0_MUTEX_H
#pragma once

/*
================
CypherCommon Mutex

Low-level mutex declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct mutex_t;
struct recursive_mutex_t;

bool_t Mutex_Init( mutex_t *pMutex );
void Mutex_Shutdown( mutex_t *pMutex );
void Mutex_Lock( mutex_t *pMutex );
bool_t Mutex_TryLock( mutex_t *pMutex );
void Mutex_Unlock( mutex_t *pMutex );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MUTEX_H
