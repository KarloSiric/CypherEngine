//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Log.h
//  Purpose: Defines the allocation-free logging record and process-wide sink.
//  Details: Producers submit borrowed strings synchronously; callbacks may run on
//           multiple producer threads and must not retain record pointers.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_LOG_H
#define CYPHER_COMMON_TIER0_LOG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_API.h"
#include "CypherCommon_Error.h"
#include "CypherCommon_SourceLocation.h"

namespace cypher::common
{

enum class log_level_t : u8 {
    Trace = 0u,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
    Count // Sentinel; never emitted as a level.
};

enum class log_channel_t : u16 {
    Common = 0u,
    Memory,
    FileSystem,
    Pak,
    Render,
    Audio,
    Network,
    Physics,
    Game,
    Tools,
    Editor,
    System,
    Host,
    Command,
    CVar,
    Config,
    Input,
    Asset,
    Resource,
    World,
    Entity,
    Animation,
    AI,
    Script,
    Material,
    Texture,
    Gui,
    Job,
    Serialization,
    Reflection,
    Count // Sentinel; never emitted as a channel.
};

struct log_record_t {
    log_level_t level{ log_level_t::Info };          // Severity used for filtering and presentation.
    log_channel_t channel{ log_channel_t::Common };  // Subsystem that produced the record.
    error_code_t errorCode{ CY_ERROR_OK };           // Optional packed error; zero means no failure.
    source_location_t location{};                    // Call site; invalid for non-At entry points.
    const char *pMessage{ "" };                      // Borrowed for the duration of the callback only.
};

using log_callback_t = void ( * )(
    const log_record_t &record,
    void *pUserData ) noexcept; // pUserData is the opaque value registered with the callback.

// Write functions invoke the currently installed sink synchronously.
CYPHER_COMMON_API void Cy_LogWrite(
    log_level_t level,
    log_channel_t channel,
    const char *pMessage ) noexcept;

CYPHER_COMMON_API void Cy_LogWriteError(
    log_level_t level,
    log_channel_t channel,
    error_code_t errorCode,
    const char *pMessage ) noexcept;

CYPHER_COMMON_API void Cy_LogWriteAt(
    log_level_t level,
    log_channel_t channel,
    const char *pMessage,
    source_location_t location ) noexcept;

CYPHER_COMMON_API void Cy_LogWriteErrorAt(
    log_level_t level,
    log_channel_t channel,
    error_code_t errorCode,
    const char *pMessage,
    source_location_t location ) noexcept;

// Installs the process-wide Tier0 sink. Callbacks may execute concurrently on
// producer threads. Replacement does not drain calls already in flight, so the
// caller must keep callback code and user data alive until producers quiesce.
CYPHER_COMMON_API void Cy_LogSetCallback(
    log_callback_t pCallback,
    void *pUserData ) noexcept;

// Returns a consistent snapshot of the current callback registration.
CYPHER_COMMON_API void Cy_LogGetCallback(
    log_callback_t *pCallbackOut,
    void **ppUserDataOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_LogLevelName(
    log_level_t level ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_LogChannelName(
    log_channel_t channel ) noexcept;

} // namespace cypher::common

#define CY_LOG_WRITE( level, channel, message )                                           \
    ::cypher::common::Cy_LogWriteAt(                                                      \
        ::cypher::common::log_level_t::level,                                             \
        ::cypher::common::log_channel_t::channel,                                         \
        message,                                                                          \
        CY_SOURCE_LOCATION )

#define CY_LOG_WRITE_ERROR( level, channel, errorCode, message )                          \
    ::cypher::common::Cy_LogWriteErrorAt(                                                 \
        ::cypher::common::log_level_t::level,                                             \
        ::cypher::common::log_channel_t::channel,                                         \
        errorCode,                                                                        \
        message,                                                                          \
        CY_SOURCE_LOCATION )

#endif // CYPHER_COMMON_TIER0_LOG_H
