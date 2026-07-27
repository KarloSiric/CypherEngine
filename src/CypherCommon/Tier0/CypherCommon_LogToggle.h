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
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Log Toggle

Runtime log category filtering shared by the Tier0 logger.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Log.h"

namespace cypher::common
{

using log_category_mask_t = u64;

constexpr log_category_mask_t CY_LOG_CATEGORY_ALL = CY_U64_MAX;

// Converts a valid log channel into its category bit.
[[nodiscard]] constexpr log_category_mask_t Cy_LogChannelMask( log_channel_t channel ) noexcept
{
    const u32 nChannel = static_cast<u32>( channel );
    return nChannel < static_cast<u32>( log_channel_t::Count )
        ? ( 1ull << nChannel )
        : 0ull;
}

CYPHER_COMMON_API void Cy_LogToggleSetMask( log_category_mask_t categoryMask ) noexcept;
[[nodiscard]] CYPHER_COMMON_API log_category_mask_t Cy_LogToggleGetMask() noexcept;
CYPHER_COMMON_API void Cy_LogToggleEnable( log_category_mask_t categoryMask ) noexcept;
CYPHER_COMMON_API void Cy_LogToggleDisable( log_category_mask_t categoryMask ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_LogToggleAnyEnabled(
    log_category_mask_t categoryMask ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_LogToggleAllEnabled(
    log_category_mask_t categoryMask ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_LogToggleChannelEnabled(
    log_channel_t channel ) noexcept;
CYPHER_COMMON_API void Cy_LogToggleReset() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_LOGTOGGLE_H
