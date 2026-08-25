//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherHost/CypherHost.cpp
//  Purpose: Implements the CypherHost Host module.
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

#include "CypherCommand.h"
#include "CypherCVar.h"
#include "Engine/CypherCommon_Print.h"
#include "CypherHost.h"
#include "CypherConfig.h"
#include "CypherFileSystem.h"
#include "CypherLog.h"
#include "CypherMemory.h"
#include "CypherRender.h"
#include "CypherSystem_Platform.h"

#include <cstring>     // strncpy for log path cvars.

namespace rc = cypher::engine::common;
namespace mem = cypher::engine::memory;

namespace {

void CypherHost_PrintArenaStats( const mem::arena_stats_t &arenaStats )
{
    COM_PRINTF(
        "%-16s used=%zu committed=%zu capacity=%zu peak=%zu allocs=%llu failed=%llu\n",
        arenaStats.name ? arenaStats.name : "<unnamed>",
        arenaStats.used,
        arenaStats.committed,
        arenaStats.capacity,
        arenaStats.nPeakUsed,
        static_cast<unsigned long long>( arenaStats.nAllocationCount ),
        static_cast<unsigned long long>( arenaStats.nFailedAllocationCount ) );
}

/*
================
Host Builtin Commands
================
*/
void CypherHost_CmdEcho( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )pExtraData;
    for ( rc::u32 i = 1u; i < argc; ++i ) {
        COM_PRINTF( "%s%s", argv[i], ( i + 1u < argc ) ? " " : "\n" );
    }
    if ( argc <= 1u ) {
        COM_PRINTF( "\n" );
    }
}

void CypherHost_CmdVersion( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )pExtraData;
    ( void )argc;
    ( void )argv;

    const rc::version_t &nEngineVersion = rc::COM_ENGINE_INFO.version;
    const rc::version_t &nGameVersion = rc::COM_GAME_INFO.version;

    COM_PRINTF(
        "%s %u.%u.%u.%u | %s %u.%u.%u.%u\n",
        rc::COM_ENGINE_INFO.name,
        nEngineVersion.major,
        nEngineVersion.minor,
        nEngineVersion.patch,
        nEngineVersion.build,
        rc::COM_GAME_INFO.name,
        nGameVersion.major,
        nGameVersion.minor,
        nGameVersion.patch,
        nGameVersion.build );
}

void CypherHost_CmdQuit( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )argc;
    ( void )argv;

    cypher::engine::host::state_t *pHostState = static_cast<cypher::engine::host::state_t *>( pExtraData );

    if ( pHostState == nullptr ) {
        return ;
    }

    CypherHost_RequestShutdown( *pHostState );
}

void CypherHost_CmdLogApply( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )pExtraData;
    ( void )argc;
    ( void )argv;

    const auto result = cypher::engine::host::CypherHost_ApplyLogCvars();

    if ( result != cypher::engine::host::host_error_t::OK ) {
        COM_ERRORF(
            cypher::engine::host::CypherHost_ErrorCode( result ),
            "log_apply failed." );
        return;
    }

    COM_PRINTF( "log config applied.\n" );
}

void CypherHost_CmdLogConfig( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )pExtraData;
    ( void )argc;
    ( void )argv;

    const auto &config = cypher::engine::log::CypherLog_GetConfig();

    COM_PRINTF(
        "log: global=%s terminal=%u/%s engine_file=%u/%s error_file=%u/%s\n",
        cypher::engine::log::CypherLog_LevelName( config.nMinLevel ),
        config.terminal.enabled ? 1u : 0u,
        cypher::engine::log::CypherLog_LevelName( config.terminal.nMinLevel ),
        config.engineFile.enabled ? 1u : 0u,
        cypher::engine::log::CypherLog_LevelName( config.engineFile.nMinLevel ),
        config.errorFile.enabled ? 1u : 0u,
        cypher::engine::log::CypherLog_LevelName( config.errorFile.nMinLevel ) );
}

void CypherHost_CmdMemReport( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )pExtraData;
    ( void )argc;
    ( void )argv;

    if ( !mem::CypherMemory_IsInitialized() ) {
        COM_PRINTF( "memory system is not initialized.\n" );
        return;
    }

    const mem::memory_stats_t stats = mem::CypherMemory_Stats();

    COM_PRINTF(
        "memory: used=%zu committed=%zu capacity=%zu peak=%zu\n",
        stats.nTotalUsed,
        stats.totalCommitted,
        stats.nTotalCapacity,
        stats.nPeakUsed );

    CypherHost_PrintArenaStats( stats.permanentStats );
    CypherHost_PrintArenaStats( stats.frameStats );
    CypherHost_PrintArenaStats( stats.scratchStats );
    CypherHost_PrintArenaStats( stats.resourceStats );
    CypherHost_PrintArenaStats( stats.worldStats );
    CypherHost_PrintArenaStats( stats.renderStats );
    CypherHost_PrintArenaStats( stats.editorStats );
}

}       // namespace

namespace cypher::engine::host {

/*
================
CypherHost_PrepareStateForInit
================
*/
void CypherHost_PrepareStateForInit( state_t &pHostState ) {
    pHostState.stage = stage_t::INITIALIZING;
    pHostState.running = false;
    pHostState.bHasFocus = true;
    pHostState.frame = {};
}

/*
================
CypherHost_InitCoreEngineSystems

Brings up low-level systems in dependency order.
================
*/
host_error_t CypherHost_InitCoreEngineSystems( state_t &pHostState ) {
    sys::init_info_t sysInfo {
        .argc = pHostState.config.argc,
        .argv = pHostState.config.argv,
        .appName = common::COM_GAME_INFO.szInternalName,
        .organizationName = common::COM_GAME_INFO.szOrganizationName
    };

    const auto sysResult = sys::Sys_Init( sysInfo );
    if ( sysResult != sys::sys_error_t::OK ) {
        COM_ERRORF( Sys_ErrorCode( sysResult ) , "CypherHost_Init: Sys_Init failed: %s", sys::Sys_ErrorDesc( sysResult ) );

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    const auto logResult = log::CypherLog_Init();
    if ( logResult != log::log_error_t::OK ) {
        COM_ERRORF( log::CypherLog_ErrorCode( logResult ), "CypherHost_Init: CypherLog_Init failed: %s", log::CypherLog_ErrorDesc( logResult ) );

        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    LOG_INFO( log::channel_t::HOST, "%s startup begin.", common::COM_ENGINE_INFO.name );
    LOG_INFO( log::channel_t::SYSTEM, "system initialized: platform=%s, compiler=%s.", sys::Sys_PlatformName( sys::Sys_PlatformType() ), sys::Sys_CompilerName( sys::Sys_CompilerType() ) );
    LOG_INFO( log::channel_t::SYSTEM, "paths: base='%s', user='%s', executable='%s'.", sys::Sys_Paths().basePath, sys::Sys_Paths().userPath, sys::Sys_Paths().executablePath );

    const auto memoryResult = mem::CypherMemory_Init( mem::CypherMemory_DefaultConfig() );
    if ( memoryResult != mem::mem_error_t::OK ) {
        LOG_ERROR( log::channel_t::MEMORY, "memory system initialization failed: %s.", mem::CypherMemory_ErrorDesc( memoryResult ) );
        COM_ERRORF( mem::CypherMemory_ErrorCode( memoryResult ), "CypherHost_Init: CypherMemory_Init failed: %s", mem::CypherMemory_ErrorDesc( memoryResult ) );
        log::CypherLog_Shutdown();
        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    const auto fsResult = fs::CypherFileSystem_Init();
    if( fsResult != fs::fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::FS, "filesystem initialization failed: %s.", fs::CypherFileSystem_ErrorDesc( fsResult ) );
        COM_ERRORF( CypherFileSystem_ErrorCode( fsResult ), "CypherHost_Init: CypherFileSystem_Init failed: %s", fs::CypherFileSystem_ErrorDesc( fsResult ) );
        mem::CypherMemory_Shutdown();
        log::CypherLog_Shutdown();
        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }
    const auto cmdResult = cmd::CypherCommand_Init();
    if ( cmdResult != cmd::cmd_error_t::OK )
    {
        LOG_ERROR( log::channel_t::CMD, "command system initialization failed: %s.", CypherCommand_ErrorDesc( cmdResult ) );
        COM_ERRORF( CypherCommand_ErrorCode( cmdResult ), "CypherHost_Init: CypherCommand_Init failed: %s", CypherCommand_ErrorDesc( cmdResult ) );

        fs::CypherFileSystem_Shutdown();
        mem::CypherMemory_Shutdown();
        log::CypherLog_Shutdown();
        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    const auto cvarResult = cvar::CypherCVar_Init();

    if ( cvarResult != cvar::cvar_error_t::OK )
    {
        LOG_ERROR( log::channel_t::CVAR, "cvar system initialization failed: %s.", cvar::CypherCVar_ErrorDesc( cvarResult ) );
        COM_ERRORF( CypherCVar_ErrorCode( cvarResult ), "CypherHost_Init: CypherCVar_Init failed: %s", cvar::CypherCVar_ErrorDesc( cvarResult ) );

        cmd::CypherCommand_Shutdown();
        fs::CypherFileSystem_Shutdown();
        mem::CypherMemory_Shutdown();
        log::CypherLog_Shutdown();
        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    const auto cfgResult = cfg::CypherConfig_Init();

    if ( cfgResult != cfg::cfg_error_t::OK )
    {
        LOG_ERROR( log::channel_t::CFG, "config system initialization failed: %s.", cfg::CypherConfig_ErrorDesc( cfgResult ) );
        COM_ERRORF( CypherConfig_ErrorCode( cfgResult ), "CypherHost_Init: CypherConfig_Init failed: %s", cfg::CypherConfig_ErrorDesc( cfgResult ) );

        cvar::CypherCVar_Shutdown();
        cmd::CypherCommand_Shutdown();
        fs::CypherFileSystem_Shutdown();
        mem::CypherMemory_Shutdown();
        log::CypherLog_Shutdown();
        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    LOG_INFO( log::channel_t::HOST, "core engine systems initialized." );
    return host_error_t::OK;
}

/*
================
CypherHost_MountFileSystem
================
*/
host_error_t CypherHost_MountFileSystem( void ) {
    const sys::paths_t &paths = sys::Sys_Paths();

    const auto baseMountResult = fs::CypherFileSystem_MountDirectory(
        "",
        paths.basePath,
        fs::CYPHER_FILESYSTEM_MOUNT_READ_ONLY,
        0u );

    if ( baseMountResult != fs::fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::FS, "filesystem base mount failed: base='%s', error=%s.", paths.basePath, fs::CypherFileSystem_ErrorDesc( baseMountResult ) );
        COM_ERRORF(
            fs::CypherFileSystem_ErrorCode( baseMountResult ),
            "CypherHost_Init: filesystem base mount failed: %s",
            fs::CypherFileSystem_ErrorDesc( baseMountResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    const auto writePathResult = fs::CypherFileSystem_SetWritePath( paths.userPath );
    if ( writePathResult != fs::fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::FS, "filesystem write path failed: user='%s', error=%s.", paths.userPath, fs::CypherFileSystem_ErrorDesc( writePathResult ) );
        COM_ERRORF(
            fs::CypherFileSystem_ErrorCode( writePathResult ),
            "CypherHost_Init: filesystem write path failed: %s",
            fs::CypherFileSystem_ErrorDesc( writePathResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    LOG_INFO( log::channel_t::FS, "filesystem mounted: base='%s', write='%s'.", paths.basePath, paths.userPath );
    return host_error_t::OK;
}

/*
================
CypherHost_RegisterBuiltinCvars
================
*/
host_error_t CypherHost_RegisterBuiltinCvars( void ) {
    struct builtin_cvar_t {
        const char *name;
        const char *defaultValue;
        cvar::flags_t flags;
    };

	    const builtin_cvar_t builtinCvars[] = {
        { "r_width", "1280", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_height", "720", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_fullscreen", "0", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_vsync", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_fov", "90", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_near", "0.1", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_far", "1000", cvar::CYPHER_CVAR_ARCHIVE },

        { "host_target_fps", "60", cvar::CYPHER_CVAR_ARCHIVE },
        { "host_timescale", "1.0", cvar::CYPHER_CVAR_ARCHIVE },
        { "host_max_delta_time", "0.25", cvar::CYPHER_CVAR_ARCHIVE },

        { "developer", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "con_show", "0", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_global_level", "trace", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_terminal", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_terminal_level", "info", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_terminal_color", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_terminal_timestamps", "0", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_engine_file", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_engine_file_level", "trace", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_engine_file_path", "CypherEngine.log", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_error_file", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_error_file_level", "warning", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_error_file_path", "CypherEngine_errors.log", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_console_file", "0", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_console_file_level", "info", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_console_file_path", "Console.log", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_editor_file", "0", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_editor_file_level", "info", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_editor_file_path", "Editor.log", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_game_file", "0", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_game_file_level", "info", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_game_file_path", "Game.log", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_file_timestamps", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_file_source", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_file_function", "1", cvar::CYPHER_CVAR_ARCHIVE },

        { "sys_app_name", rc::COM_GAME_INFO.szInternalName, cvar::CYPHER_CVAR_READONLY },
    };

    for ( const builtin_cvar_t &builtinCvar : builtinCvars ) {
        const auto result = cvar::CypherCVar_Register(
            builtinCvar.name,
            builtinCvar.defaultValue,
            builtinCvar.flags );

        if ( result != cvar::cvar_error_t::OK ) {
            COM_ERRORF(
                cvar::CypherCVar_ErrorCode( result ),
                "CypherHost_Init: failed to register cvar '%s': %s",
                builtinCvar.name,
                cvar::CypherCVar_ErrorDesc( result ) );
            return host_error_t::ERR_INITIALIZING;
        }
    }

    LOG_INFO( log::channel_t::CVAR, "registered %zu builtin cvars.", sizeof( builtinCvars ) / sizeof( builtinCvars[0] ) );
    return host_error_t::OK;
}

/*
================
CypherHost_RegisterBuiltinCommands
================
*/
host_error_t CypherHost_RegisterBuiltinCommands( state_t &pHostState ) {
    struct builtin_command_t {
        const char *name;
        cmd::command_fn_t callback;
        void *pExtraData;
        const char *description;
    };

    const builtin_command_t builtinCommands[] = {
        { "echo", CypherHost_CmdEcho, nullptr, "prints text to the engine console" },
        { "version", CypherHost_CmdVersion, nullptr, "prints engine and game version information" },
        { "quit", CypherHost_CmdQuit, &pHostState, "requests engine shutdown." },
        { "log_apply", CypherHost_CmdLogApply, nullptr, "applies log cvars to active log sinks" },
        { "log_config", CypherHost_CmdLogConfig, nullptr, "prints active log sink configuration" },
        { "mem_report", CypherHost_CmdMemReport, nullptr, "prints memory arena usage" }
    };

    for ( const builtin_command_t &builtinCommand : builtinCommands ) {
        const auto result = cmd::CypherCommand_Register(
            builtinCommand.name,
            builtinCommand.callback,
            builtinCommand.pExtraData,
            builtinCommand.description );

        if ( result != cmd::cmd_error_t::OK ) {
            COM_ERRORF(
                cmd::CypherCommand_ErrorCode( result ),
                "CypherHost_Init: failed to register command '%s': %s",
                builtinCommand.name,
                cmd::CypherCommand_ErrorDesc( result ) );
            return host_error_t::ERR_INITIALIZING;
        }
    }

    LOG_INFO( log::channel_t::CMD, "registered %zu builtin commands.", sizeof( builtinCommands ) / sizeof( builtinCommands[0] ) );
    return host_error_t::OK;
}

/*
================
CypherHost_LoadStartupConfig
================
*/
host_error_t CypherHost_LoadStartupConfig( void ) {
    const auto defaultResult = cfg::CypherConfig_LoadFile( "config/default.cfg", false );
    if ( defaultResult != cfg::cfg_error_t::OK ) {
        LOG_ERROR( log::channel_t::CFG, "default startup config failed: %s.", cfg::CypherConfig_ErrorDesc( defaultResult ) );
        COM_ERRORF(
            cfg::CypherConfig_ErrorCode( defaultResult ),
            "CypherHost_Init: default config load failed: %s",
            cfg::CypherConfig_ErrorDesc( defaultResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    const auto autoexecResult = cfg::CypherConfig_LoadAutoexec();
    if ( autoexecResult != cfg::cfg_error_t::OK ) {
        LOG_ERROR( log::channel_t::CFG, "autoexec startup config failed: %s.", cfg::CypherConfig_ErrorDesc( autoexecResult ) );
        COM_ERRORF(
            cfg::CypherConfig_ErrorCode( autoexecResult ),
            "CypherHost_Init: autoexec config load failed: %s",
            cfg::CypherConfig_ErrorDesc( autoexecResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    LOG_INFO( log::channel_t::CFG, "startup configs loaded." );
    return host_error_t::OK;
}

/*
================
CypherHost_LogLevelFromCvar
================
*/
log::level_t CypherHost_LogLevelFromCvar( const char *szCvarName, const log::level_t fallback )
{
    log::level_t parsedLevel = fallback;
    const char *szLevelName = cvar::CypherCVar_GetString( szCvarName );
    const auto parseResult = log::CypherLog_LevelFromString( szLevelName, parsedLevel );

    if ( parseResult != log::log_error_t::OK ) {
        LOG_WARNING( log::channel_t::CFG, "invalid log level cvar '%s'='%s'; keeping '%s'.", szCvarName, szLevelName ? szLevelName : "<null>", log::CypherLog_LevelName( fallback ) );
        return fallback;
    }

    return parsedLevel;
}

/*
================
CypherHost_CopyLogPathFromCvar
================
*/
void CypherHost_CopyLogPathFromCvar( char *outPath, const common::usize outPathSize, const char *szCvarName, const char *fallback )
{
    if ( outPath == nullptr || outPathSize == 0u ) {
        return;
    }

    const char *path = cvar::CypherCVar_GetString( szCvarName );

    if ( path == nullptr || path[0] == '\0' ) {
        path = fallback;
    }

    std::strncpy( outPath, path, outPathSize - 1u );
    outPath[outPathSize - 1u] = '\0';
}

/*
================
CypherHost_ApplyLogCvars

Converts registered log cvars into the active logger sink configuration.
================
*/
host_error_t CypherHost_ApplyLogCvars( void )
{
    log::config_t logConfig = log::CypherLog_GetConfig();

    logConfig.nMinLevel = CypherHost_LogLevelFromCvar( "log_global_level", logConfig.nMinLevel );

    logConfig.terminal.enabled = cvar::CypherCVar_GetBool( "log_terminal" );
    logConfig.terminal.nMinLevel = CypherHost_LogLevelFromCvar( "log_terminal_level", logConfig.terminal.nMinLevel );
    logConfig.terminal.format = log::format_mode_t::COMPACT;
    logConfig.terminal.bIncludeTimestamps = cvar::CypherCVar_GetBool( "log_terminal_timestamps" );
    logConfig.terminal.bIncludeSourceLocation = false;
    logConfig.terminal.bIncludeFunctionName = false;
    logConfig.terminal.bColorEnabled = cvar::CypherCVar_GetBool( "log_terminal_color" );

    logConfig.engineFile.enabled = cvar::CypherCVar_GetBool( "log_engine_file" );
    logConfig.engineFile.nMinLevel = CypherHost_LogLevelFromCvar( "log_engine_file_level", logConfig.engineFile.nMinLevel );
    logConfig.engineFile.format = log::format_mode_t::DETAILED;
    logConfig.engineFile.bIncludeTimestamps = cvar::CypherCVar_GetBool( "log_file_timestamps" );
    logConfig.engineFile.bIncludeSourceLocation = cvar::CypherCVar_GetBool( "log_file_source" );
    logConfig.engineFile.bIncludeFunctionName = cvar::CypherCVar_GetBool( "log_file_function" );
    logConfig.engineFile.bColorEnabled = false;
    CypherHost_CopyLogPathFromCvar( logConfig.engineFile.path, sizeof( logConfig.engineFile.path ), "log_engine_file_path", "CypherEngine.log" );

    logConfig.errorFile.enabled = cvar::CypherCVar_GetBool( "log_error_file" );
    logConfig.errorFile.nMinLevel = CypherHost_LogLevelFromCvar( "log_error_file_level", logConfig.errorFile.nMinLevel );
    logConfig.errorFile.format = log::format_mode_t::DETAILED;
    logConfig.errorFile.bIncludeTimestamps = cvar::CypherCVar_GetBool( "log_file_timestamps" );
    logConfig.errorFile.bIncludeSourceLocation = cvar::CypherCVar_GetBool( "log_file_source" );
    logConfig.errorFile.bIncludeFunctionName = cvar::CypherCVar_GetBool( "log_file_function" );
    logConfig.errorFile.bColorEnabled = false;
    CypherHost_CopyLogPathFromCvar( logConfig.errorFile.path, sizeof( logConfig.errorFile.path ), "log_error_file_path", "CypherEngine_errors.log" );

    logConfig.consoleFile.enabled = cvar::CypherCVar_GetBool( "log_console_file" );
    logConfig.consoleFile.nMinLevel = CypherHost_LogLevelFromCvar( "log_console_file_level", logConfig.consoleFile.nMinLevel );
    logConfig.consoleFile.format = log::format_mode_t::COMPACT;
    logConfig.consoleFile.bIncludeTimestamps = true;
    logConfig.consoleFile.bIncludeSourceLocation = false;
    logConfig.consoleFile.bIncludeFunctionName = false;
    logConfig.consoleFile.bColorEnabled = false;
    CypherHost_CopyLogPathFromCvar( logConfig.consoleFile.path, sizeof( logConfig.consoleFile.path ), "log_console_file_path", "Console.log" );

    logConfig.editorFile.enabled = cvar::CypherCVar_GetBool( "log_editor_file" );
    logConfig.editorFile.nMinLevel = CypherHost_LogLevelFromCvar( "log_editor_file_level", logConfig.editorFile.nMinLevel );
    logConfig.editorFile.format = log::format_mode_t::DETAILED;
    logConfig.editorFile.bIncludeTimestamps = cvar::CypherCVar_GetBool( "log_file_timestamps" );
    logConfig.editorFile.bIncludeSourceLocation = cvar::CypherCVar_GetBool( "log_file_source" );
    logConfig.editorFile.bIncludeFunctionName = cvar::CypherCVar_GetBool( "log_file_function" );
    logConfig.editorFile.bColorEnabled = false;
    CypherHost_CopyLogPathFromCvar( logConfig.editorFile.path, sizeof( logConfig.editorFile.path ), "log_editor_file_path", "Editor.log" );

    logConfig.gameFile.enabled = cvar::CypherCVar_GetBool( "log_game_file" );
    logConfig.gameFile.nMinLevel = CypherHost_LogLevelFromCvar( "log_game_file_level", logConfig.gameFile.nMinLevel );
    logConfig.gameFile.format = log::format_mode_t::DETAILED;
    logConfig.gameFile.bIncludeTimestamps = cvar::CypherCVar_GetBool( "log_file_timestamps" );
    logConfig.gameFile.bIncludeSourceLocation = cvar::CypherCVar_GetBool( "log_file_source" );
    logConfig.gameFile.bIncludeFunctionName = cvar::CypherCVar_GetBool( "log_file_function" );
    logConfig.gameFile.bColorEnabled = false;
    CypherHost_CopyLogPathFromCvar( logConfig.gameFile.path, sizeof( logConfig.gameFile.path ), "log_game_file_path", "Game.log" );

    const auto setResult = log::CypherLog_SetConfig( logConfig );

    if ( setResult != log::log_error_t::OK ) {
        COM_ERRORF( log::CypherLog_ErrorCode( setResult ), "CypherHost_ApplyLogCvars: CypherLog_SetConfig failed: %s", log::CypherLog_ErrorDesc( setResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    LOG_INFO( log::channel_t::CFG, "log cvars applied: terminal=%u, engine_file=%u, error_file=%u.", logConfig.terminal.enabled ? 1u : 0u, logConfig.engineFile.enabled ? 1u : 0u, logConfig.errorFile.enabled ? 1u : 0u );

    return host_error_t::OK;
}

/*
================
CypherHost_ApplyCvarsToConfig
================
*/
host_error_t CypherHost_ApplyCvarsToConfig( state_t &pHostState ) {
    host::window_config_t &pWindowConfig = pHostState.config.pWindowConfig;

    const common::u32 width = cvar::CypherCVar_GetInt( "r_width" );
    const common::u32 height = cvar::CypherCVar_GetInt( "r_height" );
    const common::u32 nTargetFps = cvar::CypherCVar_GetInt( "host_target_fps" );

    if ( width != 0u ) {
        pWindowConfig.viewport.width = width;
    }

    if ( height != 0u ) {
        pWindowConfig.viewport.height = height;
    }

    if ( nTargetFps != 0u ) {
        pWindowConfig.nTargetFps = nTargetFps;
    }

    pWindowConfig.fullscreen = cvar::CypherCVar_GetBool( "r_fullscreen" );
    pWindowConfig.vsync = cvar::CypherCVar_GetBool( "r_vsync" );

    LOG_INFO( log::channel_t::HOST, "applied startup cvars: viewport=%ux%u, fullscreen=%u, vsync=%u, target_fps=%u.", pWindowConfig.viewport.width, pWindowConfig.viewport.height, pWindowConfig.fullscreen ? 1u : 0u, pWindowConfig.vsync ? 1u : 0u, pWindowConfig.nTargetFps );

    return host_error_t::OK;
}

/*
================
CypherHost_CreateWindow
================
*/
host_error_t CypherHost_CreateWindow( state_t &pHostState )
{
    sys::window_desc_t windowDescription{};

    windowDescription.title        = pHostState.config.pWindowConfig.title;
    windowDescription.width        = pHostState.config.pWindowConfig.viewport.width;
    windowDescription.height       = pHostState.config.pWindowConfig.viewport.height;
    windowDescription.fullscreen   = pHostState.config.pWindowConfig.fullscreen;
    windowDescription.vsync        = pHostState.config.pWindowConfig.vsync;

    const auto windowResult = sys::Sys_CreateWindow( windowDescription, pHostState.window );
    if ( windowResult != sys::sys_error_t::OK ) {
        LOG_ERROR( log::channel_t::HOST, "window creation failed: %s.", sys::Sys_ErrorDesc( windowResult ) );
        COM_ERRORF( sys::Sys_ErrorCode( windowResult ), "CypherHost_CreateWindow: Sys_CreateWindow failed: %s", sys::Sys_ErrorDesc( windowResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    return host_error_t::OK;
}

/*
================
CypherHost_InitRenderer
================
*/
host_error_t CypherHost_InitRenderer( state_t &pHostState ) {
    const auto renderResult = render::CypherRender_Init( pHostState.window, pHostState.config.pWindowConfig );
    if ( renderResult != render::render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "renderer initialization failed." );
        COM_ERRORF(
            render::CypherRender_ErrorCode( renderResult ),
            "CypherHost_Init: renderer initialization failed." );
        return host_error_t::ERR_INITIALIZING;
    }

    return host_error_t::OK;
}

/*
================
CypherHost_FinishInit
================
*/
host_error_t CypherHost_FinishInit( state_t &pHostState ) {
    pHostState.running = true;
    pHostState.stage = stage_t::RUNNING;

    const common::f64 now = sys::Sys_TimeNowSeconds();

    pHostState.frame.nCurrentTimeSeconds = now;
    pHostState.frame.nPreviousTimeSeconds = now;

    LOG_INFO( log::channel_t::HOST, "%s startup complete.", common::COM_ENGINE_INFO.name );

    return host_error_t::OK;
}

/*
================
CypherHost_RequestShutdown
================
*/
void CypherHost_RequestShutdown( state_t &pHostState )
{
    if ( pHostState.stage == stage_t::SHUTDOWN ) {
        return ;
    }

    pHostState.running = false;
    pHostState.stage = stage_t::SHUTTINGDOWN;

    LOG_INFO( log::channel_t::HOST, "shutdown requested." );

    return ;
}

/*
================
CypherHost_Init

Main engine startup sequence.
================
*/
host_error_t CypherHost_Init( state_t &pHostState ) {
    host_error_t result{};

    CypherHost_PrepareStateForInit( pHostState );

    result = CypherHost_InitCoreEngineSystems( pHostState );
    if ( result != host_error_t::OK ) {
        return result;
    }

    result = CypherHost_MountFileSystem();
    if ( result != host_error_t::OK ) {
        CypherHost_Shutdown( pHostState );
        return result;
    }

    result = CypherHost_RegisterBuiltinCvars();
    if ( result != host_error_t::OK ) {
        CypherHost_Shutdown( pHostState );
        return result;
    }

    result = CypherHost_RegisterBuiltinCommands( pHostState );
    if ( result != host_error_t::OK ) {
        CypherHost_Shutdown( pHostState );
        return result;
    }

    result = CypherHost_LoadStartupConfig();
    if ( result != host_error_t::OK ) {
        CypherHost_Shutdown( pHostState );
        return result;
    }

    result = CypherHost_ApplyLogCvars();
    if ( result != host_error_t::OK ) {
        CypherHost_Shutdown( pHostState );
        return result;
    }

    result = CypherHost_ApplyCvarsToConfig( pHostState );
    if ( result != host_error_t::OK ) {
        CypherHost_Shutdown( pHostState );
        return result;
    }

    result = CypherHost_CreateWindow( pHostState );
    if ( result != host_error_t::OK ) {
        CypherHost_Shutdown( pHostState );
        return result;
    }

    result = CypherHost_InitRenderer( pHostState );
    if ( result != host_error_t::OK ) {
        CypherHost_Shutdown( pHostState );
        return result;
    }

    result = CypherHost_FinishInit( pHostState );
    if ( result != host_error_t::OK ) {
        CypherHost_Shutdown( pHostState );
        return result;
    }

    return result;
}

/*
================
CypherHost_Shutdown
================
*/
void CypherHost_Shutdown( state_t &pHostState ) {
    LOG_INFO( log::channel_t::HOST, "%s shutdown begin.", common::COM_ENGINE_INFO.name );

	pHostState.running = false;
	pHostState.stage = stage_t::SHUTDOWN;

    render::CypherRender_Shutdown();
    sys::Sys_DestroyWindow( pHostState.window );
    cfg::CypherConfig_Shutdown();
    cvar::CypherCVar_Shutdown();
    cmd::CypherCommand_Shutdown();
    fs::CypherFileSystem_Shutdown();
    mem::CypherMemory_Shutdown();
    LOG_INFO( log::channel_t::HOST, "%s shutdown complete.", common::COM_ENGINE_INFO.name );
    log::CypherLog_Shutdown();
    sys::Sys_Shutdown();
}

/*
================
CypherHost_BeginFrame

Updates frame timing and opens the renderer frame.
================
*/
void CypherHost_BeginFrame( state_t &pHostState ) {
	if ( pHostState.stage == stage_t::SHUTDOWN ) {
		return;
	}

    mem::CypherMemory_BeginFrame();

	frame_t &frame = pHostState.frame;

	frame.nPreviousTimeSeconds = frame.nCurrentTimeSeconds;
	frame.nCurrentTimeSeconds = sys::Sys_TimeNowSeconds();

    const common::f32 nRawDeltaTimeSeconds = static_cast<common::f32>( frame.nCurrentTimeSeconds - frame.nPreviousTimeSeconds );

    const common::f32 nMaxDeltaTimeSeconds = cvar::CypherCVar_GetFloat( "host_max_delta_time" );
    const common::f32 timescale = cvar::CypherCVar_GetFloat( "host_timescale" );

    const common::f32 flSafeMaxDeltaTimeSeconds = ( nMaxDeltaTimeSeconds > 0.0f ) ? nMaxDeltaTimeSeconds : 0.25f;
    const common::f32 flSafeTimescale = ( timescale >= 0.0f ) ? timescale : 1.0f;

    frame.nDeltaTimeSeconds = ( nRawDeltaTimeSeconds > flSafeMaxDeltaTimeSeconds )   ? flSafeMaxDeltaTimeSeconds : nRawDeltaTimeSeconds;

    frame.nRealTimeSeconds += nRawDeltaTimeSeconds;

	if ( pHostState.stage == stage_t::RUNNING ) {
		frame.nSimulationTimeSeconds += frame.nDeltaTimeSeconds * flSafeTimescale;
	}

	frame.index++;

	const auto renderResult = render::CypherRender_BeginFrame( frame.nDeltaTimeSeconds );

	if ( renderResult != render::render_error_t::OK ) {
		COM_ERRORF(
			render::CypherRender_ErrorCode( renderResult ),
			"CypherHost_BeginFrame: renderer begin-frame failed." );

		pHostState.running = false;
		pHostState.stage = stage_t::SHUTTINGDOWN;
        mem::CypherMemory_EndFrame();
		return;
	}
}

/*
================
CypherHost_Update

Polls platform events and advances runtime systems.
================
*/
void CypherHost_Update( state_t &pHostState ) {
	if ( pHostState.stage != stage_t::RUNNING ) {
		return;
	}

	if ( !pHostState.running ) {
		return;
	}

    sys::Sys_PollWindowEvents( pHostState.window );

    if ( sys::Sys_WindowShouldClose( pHostState.window ) ) {
        CypherHost_RequestShutdown( pHostState );
        return ;
    }

    /*
    Future order:
    CypherHost_UpdateInput -> CypherHost_UpdateConsole -> CypherHost_UpdateGame -> CypherHost_UpdateAudio.
     */
}

/*
================
CypherHost_Render
================
*/
void CypherHost_Render( state_t &pHostState ) {
	if ( pHostState.stage != stage_t::RUNNING || !pHostState.running ) {
		return;
	}

	const auto renderResult = render::CypherRender_RenderFrame();

	if ( renderResult != render::render_error_t::OK ) {
		COM_ERRORF(
			render::CypherRender_ErrorCode( renderResult ),
			"CypherHost_Render: renderer frame submission failed." );

		pHostState.running = false;
		pHostState.stage = stage_t::SHUTTINGDOWN;
	}
}

/*
================
CypherHost_EndFrame
================
*/
void CypherHost_EndFrame( state_t &pHostState ) {
	if ( pHostState.stage == stage_t::SHUTDOWN ) {
		return;
	}

	const auto renderResult = render::CypherRender_EndFrame();

	if ( renderResult != render::render_error_t::OK ) {
		COM_ERRORF(
			render::CypherRender_ErrorCode( renderResult ),
			"CypherHost_EndFrame: renderer end-frame failed." );

		pHostState.running = false;
		pHostState.stage = stage_t::SHUTDOWN;
        mem::CypherMemory_EndFrame();
		return;
	}

	if ( pHostState.stage == stage_t::SHUTTINGDOWN ) {
		pHostState.running = false;
		pHostState.stage = stage_t::SHUTDOWN;
	}

    mem::CypherMemory_EndFrame();
}

/*
================
CypherHost_IsRunning
================
*/
bool CypherHost_IsRunning( state_t &pHostState ) {
	return pHostState.running && ( pHostState.stage != stage_t::SHUTTINGDOWN && pHostState.stage != stage_t::SHUTDOWN );
}

}       // namespace cypher::engine::host
