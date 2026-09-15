//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherHost/CypherHost_Local.h
//  Purpose: Declares private Host startup and integration helpers.
//  Details: Only CypherHost implementation files include this header. External
//           engine code enters Host through CypherHost.h and cannot invoke
//           individual initialization stages out of order.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_HOST_LOCAL_H
#define CYPHER_ENGINE_HOST_LOCAL_H
#pragma once

#include "CypherHost.h"

namespace cypher::engine::host
{

/*
===============================================================================

    Private Host initialization stages

Host_Init owns their ordering and rollback. They remain separate functions so
startup failures are easy to identify and test, but they are not public engine
entry points.

===============================================================================
*/
void Host_PrepareStateForInit( state_t &hostState );

CYPHER_NODISCARD host_error_t Host_InitCoreEngineSystems(
    state_t &hostState );

CYPHER_NODISCARD host_error_t Host_MountFileSystem();
CYPHER_NODISCARD host_error_t Host_RegisterBuiltinCvars();
CYPHER_NODISCARD host_error_t Host_RegisterBuiltinCommands(
    state_t &hostState );
CYPHER_NODISCARD host_error_t Host_LoadStartupConfig();
CYPHER_NODISCARD host_error_t Host_ApplyLogCvars();
CYPHER_NODISCARD host_error_t Host_ApplyCvarsToConfig(
    state_t &hostState );
CYPHER_NODISCARD host_error_t Host_CreateWindow( state_t &hostState );
CYPHER_NODISCARD host_error_t Host_FinishInit( state_t &hostState );

} // namespace cypher::engine::host

#endif // CYPHER_ENGINE_HOST_LOCAL_H
