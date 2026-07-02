//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandLine.h
//  Purpose: Declares CypherCommon Tier1 CommandLine support.
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

#ifndef CYPHER_COMMON_TIER1_COMMANDLINE_H
#define CYPHER_COMMON_TIER1_COMMANDLINE_H
#pragma once

/*
================
CypherCommon Command Line

Command-line parser declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct command_line_t;

bool_t CommandLine_Init( command_line_t *pCommandLine, i32 argc, const char **ppArgv );
bool_t CommandLine_HasSwitch( const command_line_t *pCommandLine, const char *pName );
const char *CommandLine_GetValue( const command_line_t *pCommandLine, const char *pName );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMMANDLINE_H
