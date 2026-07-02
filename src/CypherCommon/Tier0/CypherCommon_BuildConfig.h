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
#pragma once

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
    Release,
    Retail
};

// Returns the active compile configuration.
inline build_config_t BuildConfig_GetCurrent()
{
#if CYPHER_BUILD_DEBUG
    return build_config_t::Debug;
#else
    return build_config_t::Release;
#endif
}

// Returns a stable human-readable name for a build configuration.
inline const char *BuildConfig_GetName( build_config_t config )
{
    switch ( config ) {
        case build_config_t::Debug:
            return "Debug";
        case build_config_t::Release:
            return "Release";
        case build_config_t::Retail:
            return "Retail";
        case build_config_t::Unknown:
        default:
            return "Unknown";
    }
}

// Returns true when this translation unit is compiled as a debug build.
inline bool_t BuildConfig_IsDebug()
{
    return CYPHER_BUILD_DEBUG != 0;
}

// Returns true when this translation unit is compiled as a release-like build.
inline bool_t BuildConfig_IsRelease()
{
    return CYPHER_BUILD_RELEASE != 0;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_BUILDCONFIG_H
