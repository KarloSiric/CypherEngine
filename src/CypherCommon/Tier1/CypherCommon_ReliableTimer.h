//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ReliableTimer.h
//  Purpose: Declares retransmission-timeout estimation for reliable messaging.
//  Details: ReliableTimer tracks smoothed RTT, variation, clamped RTO, and exponential
//           timeout backoff. Time values are monotonic seconds supplied by the caller.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RELIABLETIMER_H
#define CYPHER_COMMON_TIER1_RELIABLETIMER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct reliable_timer_config_t {
    f64 flInitialRtoSeconds{ 1.0 };
    f64 flMinRtoSeconds{ 0.050 };
    f64 flMaxRtoSeconds{ 10.0 };
    f64 flClockGranularitySeconds{ 0.001 };
};

struct reliable_timer_t {
    reliable_timer_config_t config{};
    f64 flSmoothedRttSeconds{ 0.0 };
    f64 flRttVariationSeconds{ 0.0 };
    f64 flRtoSeconds{ 1.0 };
    f64 flDeadlineSeconds{ 0.0 };
    u32 nBackoffCount{ 0u };
    bool_t bHasSample{ CY_FALSE };
    bool_t bArmed{ CY_FALSE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ReliableTimer_Init(
    reliable_timer_t *pTimer,
    const reliable_timer_config_t &config ) noexcept;

CYPHER_COMMON_API void ReliableTimer_Reset( reliable_timer_t *pTimer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ReliableTimer_AddRttSample(
    reliable_timer_t *pTimer,
    f64 flRttSeconds ) noexcept;

CYPHER_COMMON_API void ReliableTimer_Arm(
    reliable_timer_t *pTimer,
    f64 flNowSeconds ) noexcept;

CYPHER_COMMON_API void ReliableTimer_Disarm(
    reliable_timer_t *pTimer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ReliableTimer_HasExpired(
    const reliable_timer_t *pTimer,
    f64 flNowSeconds ) noexcept;

CYPHER_COMMON_API void ReliableTimer_OnTimeout(
    reliable_timer_t *pTimer,
    f64 flNowSeconds ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RELIABLETIMER_H
