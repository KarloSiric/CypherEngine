<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/CYPHERENGINE_API_REFERENCE.md
//  Purpose: Documents CYPHERENGINE API REFERENCE.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine - API Reference

Complete reference for the current public headers, types, constants, and functions exposed by the CypherEngine runtime inside the CypherEngine project.

**Version:** 0.1.0  
**Last Updated:** April 2026

---

## Table of Contents

- [1. Introduction](#1-introduction)
- [2. Core Foundation Headers](#2-core-foundation-headers)
  - [com_foundation.h - Engine Foundation Types](#com_foundationh---engine-foundation-types)
  - [com_error.h - Common Error Surface](#com_errorh---common-error-surface)
  - [com_print.h - Common Print and Error Output](#com_printh---common-print-and-error-output)
- [3. Logging System](#3-logging-system)
  - [log_types.h - Log Types and Configuration](#log_typesh---log-types-and-configuration)
  - [log_main.h - Logger Runtime API](#log_mainh---logger-runtime-api)
- [4. Platform System](#4-platform-system)
  - [sys_platform.h - Platform and Timing API](#sys_platformh---platform-and-timing-api)
- [5. Host Runtime](#5-host-runtime)
  - [host_types.h - Runtime State and Configuration](#host_typesh---runtime-state-and-configuration)
  - [host_main.h - Host Lifecycle API](#host_mainh---host-lifecycle-api)
- [6. Renderer Contract](#6-renderer-contract)
  - [r_main.h - Renderer Lifecycle API](#r_mainh---renderer-lifecycle-api)
- [7. Command System](#7-command-system)
  - [cmd_main.h - Command Registry and Dispatch](#cmd_mainh---command-registry-and-dispatch)
- [8. Cvar System](#8-cvar-system)
  - [cvar_main.h - Console Variable Runtime](#cvar_mainh---console-variable-runtime)
- [9. Config System](#9-config-system)
  - [cfg_main.h - Config Loading and Execution](#cfg_mainh---config-loading-and-execution)
- [10. Error Headers](#10-error-headers)
- [11. Next Planned Public API](#11-next-planned-public-api)

---

## 1. Introduction

This document serves as the public API reference for CypherEngine.

Scope:

- documents public headers first
- describes types, constants, and function contracts
- stays focused on the visible API surface
- leaves deeper internal design and behavior to [CYPHERENGINE_API_IMPLEMENTATION.md](/Users/karlosiric/Documents/MyProjects/REAP/docs/CYPHERENGINE_API_IMPLEMENTATION.md)

Naming:

- `REAP` is the overall project/game
- `CypherEngine` is the engine runtime
- source currently lives under `src/CypherEngine/`

---

## 2. Core Foundation Headers

### com_foundation.h - Engine Foundation Types

**Location:** [src/CypherEngine/CypherCommon/CypherCommon.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCommon/CypherCommon.h)

Shared engine-owned foundational types and constants.

#### Type Aliases

| Alias Group | Examples | Description |
|-------------|----------|-------------|
| Signed integers | `i8`, `i16`, `i32`, `i64` | Fixed-width signed types |
| Unsigned integers | `u8`, `u16`, `u32`, `u64` | Fixed-width unsigned types |
| Floating point | `f32`, `f64` | Engine floating-point aliases |
| Size types | `usize` | Memory and container sizes |
| Runtime IDs | `frame_index_t`, `entity_id_t` | Engine runtime identifiers |

#### Constants

| Constant | Description |
|----------|-------------|
| `COM_INVALID_FRAME_INDEX` | Sentinel invalid frame index |
| `COM_INVALID_ENTITY_ID` | Sentinel invalid entity id |
| `COM_PI_F` | Pi as `f32` |
| `COM_TAU_F` | Tau as `f32` |
| `COM_DEG2RAD_F` | Degrees-to-radians multiplier |
| `COM_RAD2DEG_F` | Radians-to-degrees multiplier |
| `COM_EPSILON_F` | Small floating epsilon |
| `COM_INFINITY_F` | Floating infinity |

---

### com_error.h - Common Error Surface

**Location:** [src/CypherEngine/CypherCommon/CypherCommon_Error.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCommon/CypherCommon_Error.h)

Packed surfaced-error API used when subsystem-local typed errors need to cross subsystem boundaries.

#### Types

| Type | Description |
|------|-------------|
| `error_t` | Packed 32-bit surfaced error |
| `domain_t` | Subsystem/domain identifier |
| `error_code_t` | Canonical common error enum |

#### Important Functions

| Function | Description |
|----------|-------------|
| `CypherCommon_ErrorOk( code )` | True when code equals `OK` |
| `CypherCommon_ErrorFailed( code )` | True when code is not `OK` |
| `CypherCommon_ErrorName( code )` | String name of common error code |
| `CypherCommon_DomainName( domain )` | String name of domain |
| `CypherCommon_ErrorMake( domain, local_error_code )` | Pack domain and local code |
| `CypherCommon_ErrorDomain( error )` | Extract packed domain |
| `CypherCommon_ErrorCode( error )` | Extract packed local code |

---

### com_print.h - Common Print and Error Output

**Location:** [src/CypherEngine/CypherCommon/CypherCommon_Print.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCommon/CypherCommon_Print.h)

Formatted output helpers shared by subsystems.

#### Constants

| Constant | Description |
|----------|-------------|
| `COM_MSG_MAX` | Maximum formatted message size |

#### Functions

| Function | Description |
|----------|-------------|
| `CypherCommon_Printf( const char *message, ... )` | General formatted output |
| `CypherCommon_DPrintf( const char *message, ... )` | Debug-oriented formatted output |
| `CypherCommon_VPrintf( const char *message, va_list args )` | `va_list` print variant |
| `CypherCommon_Errorf( error_t error, const char *message, ... )` | Formatted surfaced error output |
| `CypherCommon_VErrorf( error_t error, const char *message, va_list args )` | `va_list` error variant |

---

## 3. Logging System

### log_types.h - Log Types and Configuration

**Location:** [src/CypherEngine/CypherLog/CypherLog_Types.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherLog/CypherLog_Types.h)

Defines log severities, channels, records, and runtime configuration.

#### Important Types

| Type | Description |
|------|-------------|
| `level_t` | Trace-to-fatal severity enum |
| `file_mode_t` | File open mode policy |
| `flush_policy_t` | Flush behavior policy |
| `source_path_mode_t` | Basename vs full source path mode |
| `channel_t` | Subsystem channel enum |
| `record_t` | Built log event payload |
| `config_t` | Runtime logger configuration |

#### Important Helpers

| Function | Description |
|----------|-------------|
| `CypherLog_LevelName( level )` | String name of log level |
| `CypherLog_ChannelName( channel )` | String name of log channel |
| `CypherLog_ChannelBit( channel )` | Bitmask for channel |

---

### log_main.h - Logger Runtime API

**Location:** [src/CypherEngine/CypherLog/CypherLog.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherLog/CypherLog.h)

Public logger lifecycle, configuration, and emission API.

#### Functions

| Function | Description |
|----------|-------------|
| `CypherLog_Init( const config_t &config = {} )` | Initialize logging subsystem |
| `CypherLog_Shutdown()` | Shutdown logger |
| `CypherLog_GetConfig()` | Get active config |
| `CypherLog_SetConfig( const config_t &config )` | Replace runtime config |
| `CypherLog_LevelEnabled( level, channel )` | Check if event would be accepted |
| `CypherLog_ChannelEnabled( channel_mask, channel )` | Check a channel bit in a mask |
| `CypherLog_Emit( const record_t &record )` | Emit fully-built record |
| `CypherLog_Emitf( ... )` | Build and emit formatted record |
| `CypherLog_Emitfv( ... )` | `va_list` formatted emit |

#### Macro Layer

| Macro | Description |
|-------|-------------|
| `CYPHER_LOG_TRACE` | Trace severity log |
| `CYPHER_LOG_DEBUG` | Debug severity log |
| `CYPHER_LOG_INFO` | Info severity log |
| `CYPHER_LOG_WARNING` | Warning severity log |
| `CYPHER_LOG_ERROR` | Error severity log |
| `CYPHER_LOG_FATAL` | Fatal severity log |
| `CYPHER_LOG_CHECK` | Condition-check logging helper |

---

## 4. Platform System

### sys_platform.h - Platform and Timing API

**Location:** [src/CypherEngine/CypherSystem/CypherSystem_Platform.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherSystem/CypherSystem_Platform.h)

Platform/compiler detection and timing/path utility API.

#### Types

| Type | Description |
|------|-------------|
| `platform_t` | Operating-system family enum |
| `compiler_t` | Compiler family enum |

#### Functions

| Function | Description |
|----------|-------------|
| `CypherSystem_PlatformType()` | Return build platform |
| `CypherSystem_CompilerType()` | Return build compiler |
| `CypherSystem_PlatformName( type )` | String name of platform |
| `CypherSystem_CompilerName( type )` | String name of compiler |
| `CypherSystem_PathBasename( const char *path )` | Basename view into path |
| `CypherSystem_TimeNowSeconds()` | Monotonic time in seconds |
| `CypherSystem_LocalTime( std::time_t time_value, std::tm &time_out )` | Local-time conversion helper |

---

## 5. Host Runtime

### host_types.h - Runtime State and Configuration

**Location:** [src/CypherEngine/CypherHost/CypherHost_Types.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherHost/CypherHost_Types.h)

Defines top-level runtime state and startup configuration types.

#### Important Types

| Type | Description |
|------|-------------|
| `stage_t` | High-level runtime lifecycle stage |
| `build_config_t` | Debug/release/distribution build mode |
| `viewport_t` | Width/height pair |
| `window_config_t` | Window startup configuration |
| `frame_t` | Per-frame timing state |
| `config_t` | Top-level host startup config |
| `state_t` | Mutable runtime host state |

---

### host_main.h - Host Lifecycle API

**Location:** [src/CypherEngine/CypherHost/CypherHost.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherHost/CypherHost.h)

Top-level runtime lifecycle API used by the executable entry point.

#### Functions

| Function | Description |
|----------|-------------|
| `CypherHost_Init( state_t &host_state, const config_t &host_config )` | Initialize host runtime |
| `CypherHost_Shutdown( state_t &host_state )` | Shutdown host runtime |
| `CypherHost_BeginFrame( state_t &host_state, f32 delta_time_seconds )` | Begin current frame |
| `CypherHost_Update( state_t &host_state )` | Update host simulation |
| `CypherHost_Render( state_t &host_state )` | Render current frame |
| `CypherHost_EndFrame( state_t &host_state )` | Finalize current frame |
| `CypherHost_IsRunning( state_t &host_state )` | Main-loop continuation query |

---

## 6. Renderer Contract

### r_main.h - Renderer Lifecycle API

**Location:** [src/CypherEngine/CypherRender/CypherRender.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherRender/CypherRender.h)

Defines the current renderer lifecycle contract.

#### Functions

| Function | Description |
|----------|-------------|
| `CypherRender_Init( const host::window_config_t &window_config )` | Initialize renderer |
| `CypherRender_Shutdown()` | Shutdown renderer |
| `CypherRender_BeginFrame( f32 delta_time_seconds )` | Begin a render frame |
| `CypherRender_RenderFrame()` | Submit/render current frame |
| `CypherRender_EndFrame()` | End current frame |
| `CypherRender_IsInitialized()` | Query init state |

---

## 7. Command System

### cmd_main.h - Command Registry and Dispatch

**Location:** [src/CypherEngine/CypherCommand/CypherCommand.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCommand/CypherCommand.h)

Fixed-registry command backend for textual command dispatch.

#### Constants

| Constant | Description |
|----------|-------------|
| `CYPHER_COMMAND_MAX_COMMANDS` | Maximum registered commands |
| `CYPHER_COMMAND_MAX_ARGUMENTS` | Maximum parsed command arguments |

#### Types

| Type | Description |
|------|-------------|
| `command_fn_t` | Command callback signature |
| `cmd_t` | Registered command entry |
| `registry_t` | Command registry state |

#### Functions

| Function | Description |
|----------|-------------|
| `CypherCommand_Init()` | Initialize command system |
| `CypherCommand_Shutdown()` | Shutdown command system |
| `CypherCommand_Register( const char *cmd_name, command_fn_t callback_fn, const char *cmd_description )` | Register command |
| `CypherCommand_Find( const char *cmd_name )` | Find command by name |
| `CypherCommand_Parse( char *command_line, u32 &argc, char **argv )` | Tokenize command line |
| `CypherCommand_Execute( const char *command_line )` | Parse and dispatch command |

---

## 8. Cvar System

### cvar_main.h - Console Variable Runtime

**Location:** [src/CypherEngine/CypherCVar/CypherCVar.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCVar/CypherCVar.h)

Fixed-registry console variable system with cached typed views.

#### Constants

| Constant | Description |
|----------|-------------|
| `CYPHER_CVAR_MAX_CVARS` | Maximum registered cvars |
| `CYPHER_CVAR_REGISTER_ALLOWED_FLAGS` | Flags permitted during registration |

#### Types

| Type | Description |
|------|-------------|
| `flags_t` | Cvar policy/state flags |
| `cvar_t` | Cvar entry |
| `registry_t` | Cvar registry state |

#### Flags

| Flag | Description |
|------|-------------|
| `CYPHER_CVAR_NONE` | No flags |
| `CYPHER_CVAR_ARCHIVE` | Persist/save candidate |
| `CYPHER_CVAR_READONLY` | Cannot be changed normally |
| `CYPHER_CVAR_CHEAT` | Cheat-protected cvar |
| `CYPHER_CVAR_DEV` | Development-oriented cvar |
| `CYPHER_CVAR_MODIFIED` | Changed during current session |

#### Functions

| Function | Description |
|----------|-------------|
| `CypherCVar_Init()` | Initialize cvar system |
| `CypherCVar_Register( const char *name, const char *default_value, flags_t flags )` | Register cvar |
| `CypherCVar_Set( const char *name, const char *value )` | Change cvar value |
| `CypherCVar_Shutdown()` | Shutdown cvar system |
| `CypherCVar_Find( const char *name )` | Find cvar by name |
| `CypherCVar_GetString( const char *name )` | Get string value |
| `CypherCVar_GetInt( const char *name )` | Get cached integer value |
| `CypherCVar_GetFloat( const char *name )` | Get cached float value |
| `CypherCVar_GetBool( const char *name )` | Get cached boolean value |

---

## 9. Config System

### cfg_main.h - Config Loading and Execution

**Location:** [src/CypherEngine/CypherConfig/CypherConfig.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherConfig/CypherConfig.h)

Config runtime used for loading startup/runtime config files and executing config-style lines.

#### Constants

| Constant | Description |
|----------|-------------|
| `CYPHER_CONFIG_MAX_LINE_LENGTH` | Maximum cfg line length |
| `CYPHER_CONFIG_MAX_PATH_LENGTH` | Maximum cfg path length |

#### Functions

| Function | Description |
|----------|-------------|
| `CypherConfig_Init()` | Initialize cfg subsystem |
| `CypherConfig_Shutdown()` | Shutdown cfg subsystem |
| `CypherConfig_LoadFile( const char *path, bool required = false )` | Load cfg file from path |
| `CypherConfig_LoadDefault()` | Load default startup cfg |
| `CypherConfig_LoadAutoexec()` | Load optional autoexec cfg |
| `CypherConfig_ExecuteLine( const char *command_line )` | Execute a single cfg-style command line |

#### Supported Line Forms

| Form | Behavior |
|------|----------|
| `exec <path>` | Load another cfg file |
| `set <name> <value>` | Set cvar value |
| `seta <name> <value>` | Set archive-oriented cvar value |
| `<command ...>` | Fallback to command execution |

---

## 10. Error Headers

Each major subsystem currently exposes a typed local error enum in its own header:

- [src/CypherEngine/CypherLog/CypherLog_Error.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherLog/CypherLog_Error.h)
- [src/CypherEngine/CypherHost/CypherHost_Error.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherHost/CypherHost_Error.h)
- [src/CypherEngine/CypherSystem/CypherSystem_Error.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherSystem/CypherSystem_Error.h)
- [src/CypherEngine/CypherRender/CypherRender_Error.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherRender/CypherRender_Error.h)
- [src/CypherEngine/CypherCommand/CypherCommand_Error.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCommand/CypherCommand_Error.h)
- [src/CypherEngine/CypherCVar/CypherCVar_Error.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCVar/CypherCVar_Error.h)
- [src/CypherEngine/CypherConfig/CypherConfig_Error.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherConfig/CypherConfig_Error.h)

Current design rule:

- use typed local error enums inside a subsystem
- convert to `common::error_t` when surfacing failures across subsystem boundaries

---

## 11. Next Planned Public API

The next major public API expected to be added is the filesystem layer.

Planned direction:

- `CypherFileSystem_Init()`
- `CypherFileSystem_Shutdown()`
- mount/search path registration
- engine-owned file open/read helpers
- later archive integration for `rpk` or another package format
