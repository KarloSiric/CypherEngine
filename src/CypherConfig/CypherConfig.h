//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherConfig/CypherConfig.h
//  Purpose: Declares the CypherConfig Config module.
//  Details: This file participates in configuration loading and runtime settings.
//           Keep file format handling strict and predictable so startup failures are
//           easy to diagnose.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_CONFIG_H
#define CYPHER_ENGINE_CONFIG_H


#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherConfig_Error.h"

namespace cypher::engine::cfg {

/*
================
Config Limits
================
*/
constexpr common::u32 CYPHER_CONFIG_MAX_LINE_LENGTH = 1024u;    // Command line bytes including terminator.
constexpr common::u32 CYPHER_CONFIG_MAX_PATH_LENGTH = 260u;     // Virtual config path bytes including terminator.
constexpr common::u64 CYPHER_CONFIG_MAX_FILE_SIZE = 64u * 1024u; // Defensive maximum text configuration size.
constexpr common::u32 CYPHER_CONFIG_MAX_EXEC_DEPTH = 8u;       // Includes the outermost file; bounds recursive exec.

/*
================
Config API

Loads cfg files and routes each line into cvars or command execution. The service
is single-threaded. Lines and tokens are rejected instead of truncated; exec
nesting is bounded and each exec/set/seta accepts exactly its required arguments.
================
*/
cfg_error_t Cfg_Init();
cfg_error_t Cfg_Shutdown();

// loadedOut distinguishes a successfully processed file from an optional path
// that was absent. Only ERR_PATH_NOT_FOUND is skippable; malformed, denied, and
// other failed opens remain errors. The output is always initialized.
cfg_error_t Cfg_LoadFile( const char *path, bool required = false, bool *loadedOut = nullptr );
cfg_error_t Cfg_LoadDefault();
cfg_error_t Cfg_LoadAutoexec();

cfg_error_t Cfg_ExecuteLine( const char *nCommandLine );

}

#endif // CYPHER_ENGINE_CONFIG_H
