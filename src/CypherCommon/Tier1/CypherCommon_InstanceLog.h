//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_InstanceLog.h
//  Purpose: Declares CypherCommon Tier1 InstanceLog support.
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

#ifndef CYPHER_COMMON_TIER1_INSTANCELOG_H
#define CYPHER_COMMON_TIER1_INSTANCELOG_H
#pragma once

/*
================
CypherCommon Instance Log

Per-instance event log declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct instance_log_t;

void InstanceLog_Add( instance_log_t *pLog, const char *pMessage );
void InstanceLog_Clear( instance_log_t *pLog );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_INSTANCELOG_H
