//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_BuildConfig.h
//  Purpose: Exposes the active build configuration as a typed compile-time value.
//  Details: The configuration selectors are normalized by CypherCommon_Platform.h.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_BUILDCONFIG_H
#define CYPHER_COMMON_TIER0_BUILDCONFIG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"

namespace cypher::common
{

enum class build_config_t : u32 {
    Unknown = 0u,
    Debug,       // Diagnostics on; optimization normally off.
    Development, // Diagnostics on; optimization on.
    Release,     // Production performance with non-shipping facilities retained.
    Shipping     // Final player-facing build with developer facilities removed.
};

// Compile-time configuration queries; no runtime state is consulted.
CYPHER_NODISCARD constexpr build_config_t Cy_BuildConfigGetCurrent() noexcept
{
#if CYPHER_CONFIG_DEBUG
    return build_config_t::Debug;
#elif CYPHER_CONFIG_DEVELOPMENT
    return build_config_t::Development;
#elif CYPHER_CONFIG_RELEASE
    return build_config_t::Release;
#elif CYPHER_CONFIG_SHIPPING
    return build_config_t::Shipping;
#else
    return build_config_t::Unknown;
#endif
}

CYPHER_NODISCARD constexpr const char *Cy_BuildConfigGetName( build_config_t config ) noexcept
{
    switch ( config ) {
        case build_config_t::Debug:
            return "Debug";
        case build_config_t::Development:
            return "Development";
        case build_config_t::Release:
            return "Release";
        case build_config_t::Shipping:
            return "Shipping";
        case build_config_t::Unknown:
        default:
            return "Unknown";
    }
}

CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsKnown( build_config_t config ) noexcept
{
    switch ( config ) {
        case build_config_t::Debug:
        case build_config_t::Development:
        case build_config_t::Release:
        case build_config_t::Shipping:
            return CY_TRUE;
        case build_config_t::Unknown:
            return CY_FALSE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsDebug() noexcept
{
    return CYPHER_CONFIG_DEBUG != 0;
}

CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsDevelopment() noexcept
{
    return CYPHER_CONFIG_DEVELOPMENT != 0;
}

CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsRelease() noexcept
{
    return CYPHER_CONFIG_RELEASE != 0;
}

CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsShipping() noexcept
{
    return CYPHER_CONFIG_SHIPPING != 0;
}

CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsOptimized() noexcept
{
    return CYPHER_BUILD_OPTIMIZED != 0;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_BUILDCONFIG_H
