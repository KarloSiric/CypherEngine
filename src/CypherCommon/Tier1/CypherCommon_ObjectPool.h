//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ObjectPool.h
//  Purpose: Declares CypherCommon Tier1 ObjectPool support.
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

#ifndef CYPHER_COMMON_TIER1_OBJECTPOOL_H
#define CYPHER_COMMON_TIER1_OBJECTPOOL_H
#pragma once

/*
================
CypherCommon Object Pool

Typed object pool declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
struct object_pool_t;

template <typename type_t>
type_t *ObjectPool_Alloc( object_pool_t<type_t> *pPool );

template <typename type_t>
void ObjectPool_Free( object_pool_t<type_t> *pPool, type_t *pObject );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_OBJECTPOOL_H
