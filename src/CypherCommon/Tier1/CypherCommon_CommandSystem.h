//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandSystem.h
//  Purpose: Declares CypherCommon Tier1 CommandSystem support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_COMMANDSYSTEM_H
#define CYPHER_COMMON_TIER1_COMMANDSYSTEM_H
#pragma once

/*
================
CypherCommon Command System

Shared command registry declarations for console commands, config execution,
debug tools and editor command routing.
================
*/

#include "CypherCommon_Tier0.h"
#include "CypherCommon_ConCommand.h"
#include "CypherCommon_ConVar.h"

namespace cypher::common
{

struct command_system_t;

struct command_system_desc_t {
    u32 maxCommands;
    u32 maxConVars;
    void *pUserData;
};

bool_t CommandSystem_Init( command_system_t *pSystem, const command_system_desc_t &desc );
void CommandSystem_Shutdown( command_system_t *pSystem );
bool_t CommandSystem_RegisterCommand( command_system_t *pSystem, const concommand_desc_t &desc );
bool_t CommandSystem_RegisterConVar( command_system_t *pSystem, const convar_desc_t &desc );
bool_t CommandSystem_ExecuteLine( command_system_t *pSystem, const char *pCommandLine );
bool_t CommandSystem_FindCommand( command_system_t *pSystem, const char *pName, concommand_desc_t *pOutDesc );
bool_t CommandSystem_FindConVar( command_system_t *pSystem, const char *pName, convar_desc_t *pOutDesc );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMMANDSYSTEM_H
