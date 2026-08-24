//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Module.cpp
//  Purpose: Implements CypherCommon Tier0 module metadata helpers.
//  Details: Module helpers are used by engine libraries, game modules, tools,
//           and future editor plugins.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Module.h"

namespace cypher::common
{

//-----------------------------------------------------------------------------
// Module contract validation
//
// Compatibility is checked before state transitions so a module never reaches
// initialization with a mismatched descriptor or API version.
//-----------------------------------------------------------------------------
namespace
{

bool_t Module_IsValidState( module_state_t state ) noexcept
{
    switch ( state ) {
        case module_state_t::Unloaded:
        case module_state_t::Loaded:
        case module_state_t::Initialized:
        case module_state_t::Shutdown:
            return CY_TRUE;
    }
    return CY_FALSE;
}

} // namespace

const char *Cy_ModuleStateName( module_state_t state ) noexcept
{
    switch ( state ) {
        case module_state_t::Unloaded: return "Unloaded";
        case module_state_t::Loaded: return "Loaded";
        case module_state_t::Initialized: return "Initialized";
        case module_state_t::Shutdown: return "Shutdown";
    }

    return "Unknown";
}

bool_t Cy_ModuleVersionCompatible(
    const module_version_t &required,
    const module_version_t &provided ) noexcept
{
    if ( provided.nMajor != required.nMajor ) {
        return CY_FALSE;
    }

    if ( provided.nMinor < required.nMinor ) {
        return CY_FALSE;
    }

    if ( provided.nMinor == required.nMinor &&
         provided.nPatch < required.nPatch ) {
        return CY_FALSE;
    }

    return CY_TRUE;
}

bool_t Cy_ModuleApiVersionCompatible(
    u32 nRequiredApiVersion,
    u32 nProvidedApiVersion ) noexcept
{
    return nRequiredApiVersion != 0u &&
           nRequiredApiVersion == nProvidedApiVersion;
}

bool_t Cy_ModuleDescriptorIsValid(
    const module_desc_t *pDescriptor ) noexcept
{
    return pDescriptor != nullptr &&
           pDescriptor->pszName != nullptr &&
           pDescriptor->pszName[0] != '\0' &&
           pDescriptor->pszInternalName != nullptr &&
           pDescriptor->pszInternalName[0] != '\0' &&
           pDescriptor->nApiVersion != 0u;
}

bool_t Cy_ModuleCanTransition(
    module_state_t from,
    module_state_t to ) noexcept
{
    if ( !Module_IsValidState( from ) || !Module_IsValidState( to ) ) {
        return CY_FALSE;
    }
    if ( from == to ) {
        return CY_TRUE;
    }

    switch ( from ) {
        case module_state_t::Unloaded:
            return to == module_state_t::Loaded;
        case module_state_t::Loaded:
            return to == module_state_t::Initialized ||
                   to == module_state_t::Unloaded;
        case module_state_t::Initialized:
            return to == module_state_t::Shutdown;
        case module_state_t::Shutdown:
            return to == module_state_t::Loaded ||
                   to == module_state_t::Unloaded;
    }
    return CY_FALSE;
}

} // namespace cypher::common
