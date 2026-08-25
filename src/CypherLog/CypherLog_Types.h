//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherLog/CypherLog_Types.h
//  Purpose: Declares the CypherLog Log Types module.
//  Details: This file participates in engine logging and formatted diagnostic output.
//           Keep it usable from early startup and failure paths without introducing
//           fragile dependencies.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_LOG_TYPES_H
#define CYPHER_ENGINE_LOG_TYPES_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"

#include <ctime>       // std::time_t timestamps.

namespace cypher::engine::log {

constexpr common::usize CYPHER_LOG_MESSAGE_MAX = 1024u;       // Formatted message storage including terminator.

constexpr common::usize CYPHER_LOG_FILE_PATH_MAX = 512u;      // Sink path storage including terminator.

/*
================
Log Types

Severity, output policy, channel and record descriptions for the log system.
================
*/
enum class level_t : common::u8 {
		TRACE,      // High-volume execution details for focused diagnosis.
		DEBUG,      // Developer-facing state useful during ordinary debugging.
		INFO,       // Normal lifecycle and operational milestones.
		WARNING,    // Recoverable condition requiring attention.
		ERR,        // Operation failed but process may continue.
		FATAL,      // Process or subsystem cannot safely continue.
    COUNT       // Number of valid severity levels; never emitted.
};

enum class file_mode_t : common::u8 {
    TRUNCATE,   // Start each logging session with a new file.
    APPEND      // Preserve previous sessions and append new records.
};

enum class flush_policy_t : common::u8 {
    NEVER,              // Rely on stream buffering until shutdown.
    ERRORS_AND_ABOVE,   // Flush immediately for error and fatal records.
    EVERY_MESSAGE       // Flush each record for maximum crash survivability.
};

enum class source_path_mode_t : common::u8 {
    BASENAME,   // Store only the final source filename in formatted output.
    FULL_PATH   // Preserve the complete compiler-provided source path.
};

enum class sink_flag_t : common::u32 {
    NONE         = 0u,        // Route to no sink.
    TERMINAL     = 1u << 0u,  // Human-readable stdout/stderr output.
    ENGINE_FILE  = 1u << 1u,  // General engine session log.
    ERROR_FILE   = 1u << 2u,  // Warning-and-error focused file.
    CONSOLE_FILE = 1u << 3u,  // Interactive console transcript.
    EDITOR_FILE  = 1u << 4u,  // Editor/tool host log.
    GAME_FILE    = 1u << 5u,  // Game/runtime-specific log.
    CRASH_BUFFER = 1u << 6u,  // Reserved in-memory crash-report history.
    ALL          = 0xFFFFFFFFu // Selects every present and future sink bit.
};

enum class format_mode_t : common::u8 {
    COMPACT,    // Severity, channel, and message only unless enabled otherwise.
    DETAILED    // Full timestamp and source-oriented diagnostic format.
};

enum class channel_t : common::u8 {
    NONE = 0,   // No subsystem channel; normally filtered out.
    CORE,       // Shared low-level runtime support.
    HOST,       // Engine lifecycle and frame orchestration.
    SYSTEM,     // Process and operating-system services.
    PLATFORM,   // Window, input-device, and platform backend details.
    MEMORY,     // Allocators, arenas, pools, and memory diagnostics.
    RENDER,     // Renderer frontend and graphics backends.
    PHYSICS,    // Collision and rigid-body simulation.
    NET,        // Transport, net channel, and replication.
    ECS,        // Entity/component storage and scheduling.
    CFG,        // Configuration parsing and execution.
    CMD,        // Runtime command registration and dispatch.
    CVAR,       // Console-variable registration and mutation.
    FS,         // Virtual filesystem and package mounts.
    INPUT,      // User input collection and command generation.
    WORLD,      // World representation and scene runtime.
    RESOURCE,   // Resource identity, loading, and lifetime.
    SCRIPT,     // Lua and script-host integration.
    ANIMATION,  // Skeletal, sequence, and animation-graph runtime.
    AI,         // Navigation, perception, and behavior runtime.
    CONSOLE,    // Interactive console presentation and history.
    PROFILE,    // Profiling and telemetry systems.
    GAME,       // Game-module logic.
    EDITOR,     // Authoring application behavior.
    TOOLS,      // Offline compiler and utility behavior.
    CLIENT,     // Client simulation and presentation.
    SERVER,     // Authoritative server simulation.
    AUDIO,      // Sound loading, mixing, and playback.
    ASSETS,     // Source-asset import and cooking.
    MATH,       // Math validation and numerical diagnostics.
    COUNT       // Number of channel values; never emitted.
};

/*
================
Log Runtime Records
================
*/
struct record_t {
		level_t level{ level_t::INFO };                   // Severity controlling filtering and flush policy.
		channel_t channel{ channel_t::CORE };             // Subsystem category controlling channel filtering.
    common::u32 nSinkMask{ 0u };                      // Explicit sink routing selected for this record.

		const char *file{ "" };                          // Borrowed compiler source path.
		const char *function{ "" };                      // Borrowed compiler function name.
		common::i32 line{ 0 };                            // One-based source line, or zero when unknown.

    std::time_t timestamp{};                          // Wall-clock time captured at emission.
		char message[CYPHER_LOG_MESSAGE_MAX]{};           // Owned, null-terminated formatted text.
};

struct sink_config_t {
    bool enabled{ false };                            // Sink accepts records when true.
    level_t nMinLevel{ level_t::INFO };               // Lowest severity accepted by this sink.
    format_mode_t format{ format_mode_t::COMPACT };   // Text layout used by this sink.
    flush_policy_t flush{ flush_policy_t::ERRORS_AND_ABOVE }; // Stream flush frequency.
    file_mode_t file{ file_mode_t::TRUNCATE };        // File-open behavior for file-backed sinks.

    bool bIncludeTimestamps{ false };                 // Prefix formatted records with local wall time.
    bool bIncludeSourceLocation{ false };             // Include source filename and line.
    bool bIncludeFunctionName{ false };               // Include source function when location is present.
    bool bColorEnabled{ false };                      // Emit terminal ANSI severity colors.

    char path[CYPHER_LOG_FILE_PATH_MAX]{};            // File path for file-backed sinks.
};

struct config_t {
    level_t nMinLevel{ level_t::TRACE };              // Global severity gate applied before sink routing.
    common::u32 nChannelMask{ 0xFFFFFFFFu };          // Enabled channel bits applied before sink routing.
    source_path_mode_t szSourcePath{ source_path_mode_t::BASENAME }; // Source path formatting policy.

    sink_config_t terminal{
        true,
        level_t::INFO,
        format_mode_t::COMPACT,
        flush_policy_t::ERRORS_AND_ABOVE,
        file_mode_t::TRUNCATE,
        false,
        false,
        false,
        true,
        ""
    };

    sink_config_t engineFile{
        true,
        level_t::TRACE,
        format_mode_t::DETAILED,
        flush_policy_t::ERRORS_AND_ABOVE,
        file_mode_t::TRUNCATE,
        true,
        true,
        true,
        false,
        "CypherEngine.log"
    };

    sink_config_t errorFile{
        true,
        level_t::WARNING,
        format_mode_t::DETAILED,
        flush_policy_t::EVERY_MESSAGE,
        file_mode_t::TRUNCATE,
        true,
        true,
        true,
        false,
        "CypherEngine_errors.log"
    };

    sink_config_t consoleFile{};                      // Optional interactive-console transcript sink.
    sink_config_t editorFile{};                       // Optional editor session sink.
    sink_config_t gameFile{};                         // Optional game-module session sink.
};

/*
================
Log Name Helpers
================
*/
constexpr inline const char *Log_LevelName( const level_t logLevel ) {
	switch ( logLevel ) {
        case level_t::TRACE:        return "TRACE";
        case level_t::DEBUG:        return "DEBUG";
        case level_t::INFO:         return "INFO";
        case level_t::WARNING:      return "WARNING";
        case level_t::ERR:          return "ERROR";
        case level_t::FATAL:        return "FATAL";
        default:                    return "UNKNOWN";
	}
}

constexpr inline const char *Log_ChannelName( const channel_t channel ) {
    switch ( channel ) {
        case channel_t::CORE:      return "CORE";
        case channel_t::HOST:      return "HOST";
        case channel_t::SYSTEM:    return "SYSTEM";
        case channel_t::PLATFORM:  return "PLATFORM";
        case channel_t::MEMORY:    return "MEMORY";
        case channel_t::RENDER:    return "RENDER";
        case channel_t::PHYSICS:   return "PHYSICS";
        case channel_t::NET:       return "NET";
        case channel_t::ECS:       return "ECS";
        case channel_t::CFG:       return "CFG";
        case channel_t::CMD:       return "CMD";
        case channel_t::CVAR:      return "CVAR";
        case channel_t::FS:        return "FS";
        case channel_t::INPUT:     return "INPUT";
        case channel_t::WORLD:     return "WORLD";
        case channel_t::RESOURCE:  return "RESOURCE";
        case channel_t::SCRIPT:    return "SCRIPT";
        case channel_t::ANIMATION: return "ANIMATION";
        case channel_t::AI:        return "AI";
        case channel_t::CONSOLE:   return "CONSOLE";
        case channel_t::PROFILE:   return "PROFILE";
        case channel_t::GAME:      return "GAME";
        case channel_t::EDITOR:    return "EDITOR";
        case channel_t::TOOLS:     return "TOOLS";
        case channel_t::CLIENT:    return "CLIENT";
        case channel_t::SERVER:    return "SERVER";
        case channel_t::AUDIO:     return "AUDIO";
        case channel_t::ASSETS:    return "ASSETS";
        case channel_t::MATH:      return "MATH";
        default:                   return "CORE";
    }
}

constexpr inline common::u32 Log_ChannelBit( const channel_t channel )
{
    return 1u << static_cast<common::u32>( channel );
}

/*
 * Helpers for the sink masking and bit masking.
 */

constexpr inline common::u32 Log_SinkBit( sink_flag_t pSinkFlag )
{
    return static_cast<common::u32>( pSinkFlag );
}

constexpr inline bool Log_SinkMaskHas( common::u32 nSinkMask, sink_flag_t pSinkFlag )
{
    return ( ( nSinkMask & Log_SinkBit( pSinkFlag ) ) != 0u );
}

constexpr inline common::u32 Log_SinkMaskAdd( common::u32 nSinkMask, sink_flag_t pSinkFlag )
{
    return nSinkMask | Log_SinkBit( pSinkFlag );
}

constexpr inline common::u32 Log_SinkMaskRemove( common::u32 nSinkMask, sink_flag_t pSinkFlag )
{
    return nSinkMask & ~Log_SinkBit( pSinkFlag );
}

constexpr inline bool Log_LevelPasses( level_t level, level_t nMinLevel )
{
    return static_cast<common::u8>( level ) >= static_cast<common::u8>( nMinLevel );
}

constexpr inline common::u32 Log_DefaultSinkMaskForLevel( level_t level )
{
    common::u32 mask = 0u;
    mask = Log_SinkMaskAdd( mask, sink_flag_t::TERMINAL );
    mask = Log_SinkMaskAdd( mask, sink_flag_t::ENGINE_FILE );

    if ( Log_LevelPasses( level, level_t::WARNING ) ) {
        mask = Log_SinkMaskAdd( mask, sink_flag_t::ERROR_FILE );
    }

    if ( Log_LevelPasses( level, level_t::ERR ) ) {
        mask = Log_SinkMaskAdd( mask, sink_flag_t::CRASH_BUFFER );
    }

    return mask;
}

}       // namespace cypher::engine::log

#endif // CYPHER_ENGINE_LOG_TYPES_H
