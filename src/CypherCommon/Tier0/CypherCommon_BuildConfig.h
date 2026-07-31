//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_BuildConfig.h
//  Purpose: Declares CypherCommon Tier0 BuildConfig support.
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

#ifndef CYPHER_COMMON_TIER0_BUILDCONFIG_H
#define CYPHER_COMMON_TIER0_BUILDCONFIG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Build Config

Build configuration declarations shared by every module.
================
*/

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"

namespace cypher::common
{

enum class build_config_t : u32 {
    Unknown = 0u,
    Debug,
    Development,
    Release,
    Shipping
};

// Returns the active compile configuration.
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

// Returns a stable human-readable name for a build configuration.
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

// Returns whether the supplied build configuration is recognized.
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

// Returns true when the current translation unit is compiled as Debug.
CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsDebug() noexcept
{
    return CYPHER_CONFIG_DEBUG != 0;
}

// Returns true when the current translation unit is compiled as Development.
CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsDevelopment() noexcept
{
    return CYPHER_CONFIG_DEVELOPMENT != 0;
}

// Returns true when the current translation unit is compiled as Release.
CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsRelease() noexcept
{
    return CYPHER_CONFIG_RELEASE != 0;
}

// Returns true when the current translation unit is compiled as Shipping.
CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsShipping() noexcept
{
    return CYPHER_CONFIG_SHIPPING != 0;
}

// Returns true for any non-Debug configuration that enables optimization.
CYPHER_NODISCARD constexpr bool_t Cy_BuildConfigIsOptimized() noexcept
{
    return CYPHER_BUILD_OPTIMIZED != 0;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_BUILDCONFIG_H
