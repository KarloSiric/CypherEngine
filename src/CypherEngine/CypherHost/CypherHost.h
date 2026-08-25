//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherHost/CypherHost.h
//  Purpose: Declares the CypherHost Host module.
//  Details: This file participates in engine host startup, frame flow, and shutdown
//           ordering. Keep it thin enough that subsystem initialization remains
//           visible and debuggable.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_HOST_H
#define CYPHER_ENGINE_HOST_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherHost_Error.h"
#include "CypherHost_Types.h"

namespace cypher::engine::host
{

/*
================
Host Initialization Steps

Kept separate so startup order stays readable and failure cleanup is explicit.
================
*/
void Host_PrepareStateForInit( state_t &pHostState );

host_error_t Host_InitCoreEngineSystems( state_t &pHostState );

host_error_t Host_MountFileSystem( void );

host_error_t Host_RegisterBuiltinCvars( void );

host_error_t Host_RegisterBuiltinCommands( state_t &pHostState );

host_error_t Host_LoadStartupConfig( void );

host_error_t Host_ApplyLogCvars( void );

host_error_t Host_ApplyCvarsToConfig( state_t &pHostState );

host_error_t Host_CreateWindow( state_t &pHostState );

host_error_t Host_InitRenderer( state_t &pHostState );

host_error_t Host_FinishInit( state_t &pHostState );

/*
================
Host Runtime API
================
*/
host_error_t Host_Init( state_t &pHostState );

void Host_RequestShutdown( state_t &pHostState );

void Host_Shutdown( state_t &pHostState );

void Host_BeginFrame( state_t &pHostState );

void Host_Update( state_t &pHostState );

void Host_Render( state_t &pHostState );

void Host_EndFrame( state_t &pHostState );

bool Host_IsRunning( state_t &pHostState );

}

#endif // CYPHER_ENGINE_HOST_H
