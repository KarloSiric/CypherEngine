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

/*
================
Reliable Timer Contract

Timer comparisons use wrap-safe integer differences so long-running sessions do not fail when
the underlying counter rolls over.
================
*/

#ifndef CYPHER_COMMON_TIER1_RELIABLETIMER_H
#define CYPHER_COMMON_TIER1_RELIABLETIMER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct reliable_timer_config_t {
    f64 flInitialRtoSeconds{ 1.0 };        // RTO before the first RTT sample.
    f64 flMinRtoSeconds{ 0.050 };          // Inclusive lower clamp for computed RTO.
    f64 flMaxRtoSeconds{ 10.0 };           // Inclusive upper clamp for computed RTO.
    f64 flClockGranularitySeconds{ 0.001 }; // Minimum measurable timing variation.
};

struct reliable_timer_t {
    reliable_timer_config_t config{};       // Validated policy snapshot.
    f64 flSmoothedRttSeconds{ 0.0 };         // RFC-style smoothed round-trip time.
    f64 flRttVariationSeconds{ 0.0 };        // Smoothed absolute RTT deviation.
    f64 flRtoSeconds{ 1.0 };                 // Current retransmission timeout.
    f64 flDeadlineSeconds{ 0.0 };            // Absolute deadline while armed.
    u32 nBackoffCount{ 0u };                 // Consecutive timeout exponent count.
    bool_t bHasSample{ CY_FALSE };            // At least one RTT sample was accepted.
    bool_t bArmed{ CY_FALSE };                // Deadline is currently active.
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
