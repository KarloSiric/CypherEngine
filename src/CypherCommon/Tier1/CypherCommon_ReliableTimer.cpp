//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ReliableTimer.cpp
//  Purpose: Implements reliable-message retransmission timing.
//  Details: RTT smoothing follows RFC 6298's alpha and beta updates, with caller-
//           supplied monotonic time and bounded exponential timeout backoff.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ReliableTimer.h"

#include <cmath>

namespace cypher::common
{

namespace
{

f64 ClampRto( const reliable_timer_config_t &config, f64 flRto ) noexcept
{
    if ( flRto < config.flMinRtoSeconds ) {
        return config.flMinRtoSeconds;
    }
    if ( flRto > config.flMaxRtoSeconds ) {
        return config.flMaxRtoSeconds;
    }
    return flRto;
}

bool_t ReliableTimerConfigIsValid(
    const reliable_timer_config_t &config ) noexcept
{
    return std::isfinite( config.flInitialRtoSeconds ) &&
           std::isfinite( config.flMinRtoSeconds ) &&
           std::isfinite( config.flMaxRtoSeconds ) &&
           std::isfinite( config.flClockGranularitySeconds ) &&
           config.flMinRtoSeconds > 0.0 &&
           config.flMaxRtoSeconds >= config.flMinRtoSeconds &&
           config.flInitialRtoSeconds >= config.flMinRtoSeconds &&
           config.flInitialRtoSeconds <= config.flMaxRtoSeconds &&
           config.flClockGranularitySeconds >= 0.0;
}

} // namespace

bool_t ReliableTimer_Init(
    reliable_timer_t *pTimer,
    const reliable_timer_config_t &config ) noexcept
{
    if ( pTimer == nullptr || !ReliableTimerConfigIsValid( config ) ) {
        return CY_FALSE;
    }
    *pTimer = {};
    pTimer->config = config;
    pTimer->flRtoSeconds = config.flInitialRtoSeconds;
    return CY_TRUE;
}

void ReliableTimer_Reset( reliable_timer_t *pTimer ) noexcept
{
    CY_ASSERT_MSG( pTimer != nullptr, "ReliableTimer_Reset requires state." );
    if ( pTimer == nullptr ) {
        return;
    }
    const reliable_timer_config_t config = pTimer->config;
    *pTimer = {};
    pTimer->config = config;
    pTimer->flRtoSeconds = config.flInitialRtoSeconds;
}

bool_t ReliableTimer_AddRttSample(
    reliable_timer_t *pTimer,
    f64 flRttSeconds ) noexcept
{
    if ( pTimer == nullptr || !ReliableTimerConfigIsValid( pTimer->config ) ||
         !std::isfinite( flRttSeconds ) || flRttSeconds <= 0.0 ) {
        return CY_FALSE;
    }

    if ( !pTimer->bHasSample ) {
        // RFC 6298 initializes SRTT from the first sample and RTTVAR to half
        // that sample; later updates use alpha=1/8 and beta=1/4.
        pTimer->flSmoothedRttSeconds = flRttSeconds;
        pTimer->flRttVariationSeconds = flRttSeconds * 0.5;
        pTimer->bHasSample = CY_TRUE;
    } else {
        const f64 flError = std::fabs(
            pTimer->flSmoothedRttSeconds - flRttSeconds );
        pTimer->flRttVariationSeconds =
            0.75 * pTimer->flRttVariationSeconds + 0.25 * flError;
        pTimer->flSmoothedRttSeconds =
            0.875 * pTimer->flSmoothedRttSeconds + 0.125 * flRttSeconds;
    }

    // The safety margin is four times variation, but never below the clock's
    // measurable granularity.
    const f64 flVariationTerm = 4.0 * pTimer->flRttVariationSeconds;
    const f64 flSafetyMargin = flVariationTerm > pTimer->config.flClockGranularitySeconds
        ? flVariationTerm
        : pTimer->config.flClockGranularitySeconds;
    pTimer->flRtoSeconds = ClampRto(
        pTimer->config,
        pTimer->flSmoothedRttSeconds + flSafetyMargin );
    pTimer->nBackoffCount = 0u;
    return CY_TRUE;
}

void ReliableTimer_Arm(
    reliable_timer_t *pTimer,
    f64 flNowSeconds ) noexcept
{
    const bool_t bValid = pTimer != nullptr && std::isfinite( flNowSeconds );
    CY_ASSERT_MSG( bValid, "ReliableTimer_Arm requires finite monotonic time." );
    if ( !bValid ) {
        return;
    }
    pTimer->flDeadlineSeconds = flNowSeconds + pTimer->flRtoSeconds;
    pTimer->bArmed = CY_TRUE;
}

void ReliableTimer_Disarm( reliable_timer_t *pTimer ) noexcept
{
    CY_ASSERT_MSG( pTimer != nullptr, "ReliableTimer_Disarm requires state." );
    if ( pTimer != nullptr ) {
        pTimer->bArmed = CY_FALSE;
        pTimer->flDeadlineSeconds = 0.0;
    }
}

bool_t ReliableTimer_HasExpired(
    const reliable_timer_t *pTimer,
    f64 flNowSeconds ) noexcept
{
    return pTimer != nullptr && pTimer->bArmed &&
           std::isfinite( flNowSeconds ) &&
           flNowSeconds >= pTimer->flDeadlineSeconds;
}

void ReliableTimer_OnTimeout(
    reliable_timer_t *pTimer,
    f64 flNowSeconds ) noexcept
{
    const bool_t bValid = pTimer != nullptr && std::isfinite( flNowSeconds );
    CY_ASSERT_MSG(
        bValid,
        "ReliableTimer_OnTimeout requires state and finite monotonic time." );
    if ( !bValid ) {
        return;
    }

    // Saturating before multiplication avoids an intermediate overflow while
    // implementing exponential retransmission backoff.
    pTimer->flRtoSeconds = pTimer->flRtoSeconds >= pTimer->config.flMaxRtoSeconds * 0.5
        ? pTimer->config.flMaxRtoSeconds
        : pTimer->flRtoSeconds * 2.0;
    pTimer->flRtoSeconds = ClampRto( pTimer->config, pTimer->flRtoSeconds );
    if ( pTimer->nBackoffCount < CY_U32_MAX ) {
        ++pTimer->nBackoffCount;
    }
    pTimer->flDeadlineSeconds = flNowSeconds + pTimer->flRtoSeconds;
    pTimer->bArmed = CY_TRUE;
}

} // namespace cypher::common
