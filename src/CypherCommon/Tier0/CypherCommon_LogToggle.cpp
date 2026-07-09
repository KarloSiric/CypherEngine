//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_LogToggle.cpp
//  Purpose: Implements CypherCommon Tier0 log category toggles.
//  Details: Category masks let tools and runtime diagnostics filter low-level
//           logging without depending on the higher engine log subsystem.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_LogToggle.h"

#include <atomic>

namespace cypher::common
{
namespace
{

std::atomic<log_category_mask_t> g_logCategoryMask = CY_U64_MAX;

} // namespace

void LogToggle_Enable( log_category_mask_t category_mask )
{
    g_logCategoryMask.fetch_or( category_mask, std::memory_order_relaxed );
}

void LogToggle_Disable( log_category_mask_t category_mask )
{
    g_logCategoryMask.fetch_and( ~category_mask, std::memory_order_relaxed );
}

bool_t LogToggle_IsEnabled( log_category_mask_t category_mask )
{
    return ( g_logCategoryMask.load( std::memory_order_relaxed ) & category_mask ) != 0u;
}

} // namespace cypher::common
