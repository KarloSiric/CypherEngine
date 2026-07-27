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

std::atomic<log_category_mask_t> g_logCategoryMask = CY_LOG_CATEGORY_ALL;

} // namespace

void Cy_LogToggleSetMask( log_category_mask_t categoryMask ) noexcept
{
    g_logCategoryMask.store( categoryMask, std::memory_order_relaxed );
}

log_category_mask_t Cy_LogToggleGetMask() noexcept
{
    return g_logCategoryMask.load( std::memory_order_relaxed );
}

void Cy_LogToggleEnable( log_category_mask_t categoryMask ) noexcept
{
    g_logCategoryMask.fetch_or( categoryMask, std::memory_order_relaxed );
}

void Cy_LogToggleDisable( log_category_mask_t categoryMask ) noexcept
{
    g_logCategoryMask.fetch_and( ~categoryMask, std::memory_order_relaxed );
}

bool_t Cy_LogToggleAnyEnabled( log_category_mask_t categoryMask ) noexcept
{
    return ( Cy_LogToggleGetMask() & categoryMask ) != 0u;
}

bool_t Cy_LogToggleAllEnabled( log_category_mask_t categoryMask ) noexcept
{
    return ( Cy_LogToggleGetMask() & categoryMask ) == categoryMask;
}

bool_t Cy_LogToggleChannelEnabled( log_channel_t channel ) noexcept
{
    const log_category_mask_t channelMask = Cy_LogChannelMask( channel );
    return channelMask != 0u && Cy_LogToggleAllEnabled( channelMask );
}

void Cy_LogToggleReset() noexcept
{
    Cy_LogToggleSetMask( CY_LOG_CATEGORY_ALL );
}

} // namespace cypher::common
