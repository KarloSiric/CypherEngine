//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_LogToggle.h
//  Purpose: Declares CypherCommon Tier0 LogToggle support.
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

#ifndef CYPHER_COMMON_TIER0_LOGTOGGLE_H
#define CYPHER_COMMON_TIER0_LOGTOGGLE_H
#pragma once

/*
================
CypherCommon Log Toggle

Compile-time log category toggle declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using log_category_mask_t = u64;

void LogToggle_Enable( log_category_mask_t category_mask );
void LogToggle_Disable( log_category_mask_t category_mask );
bool_t LogToggle_IsEnabled( log_category_mask_t category_mask );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_LOGTOGGLE_H
