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

const char *Cy_ModuleStateName( module_state_t state )
{
    switch ( state ) {
        case module_state_t::Unloaded: return "Unloaded";
        case module_state_t::Loaded: return "Loaded";
        case module_state_t::Initialized: return "Initialized";
        case module_state_t::Shutdown: return "Shutdown";
    }

    return "Unknown";
}

bool_t Cy_ModuleVersionCompatible( const module_version_t &required, const module_version_t &provided )
{
    if ( provided.major != required.major ) {
        return CY_FALSE;
    }

    if ( provided.minor < required.minor ) {
        return CY_FALSE;
    }

    if ( provided.minor == required.minor && provided.patch < required.patch ) {
        return CY_FALSE;
    }

    return CY_TRUE;
}

} // namespace cypher::common
