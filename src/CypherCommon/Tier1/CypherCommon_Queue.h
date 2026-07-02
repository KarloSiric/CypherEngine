//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Queue.h
//  Purpose: Declares CypherCommon Tier1 Queue support.
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

#ifndef CYPHER_COMMON_TIER1_QUEUE_H
#define CYPHER_COMMON_TIER1_QUEUE_H
#pragma once

/*
================
CypherCommon Queue

FIFO container declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
struct queue_t;

template <typename type_t>
bool_t Queue_Push( queue_t<type_t> *pQueue, const type_t &value );

template <typename type_t>
bool_t Queue_Pop( queue_t<type_t> *pQueue, type_t *pOutValue );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_QUEUE_H
