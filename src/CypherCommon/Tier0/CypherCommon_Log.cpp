//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Log.cpp
//  Purpose: Implements CypherCommon Tier0 low-level logging.
//  Details: This layer provides callback-based logging before full engine logging
//           exists. Higher systems can install a callback and route records.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Log.h"

#include "CypherCommon_LogToggle.h"

#include <cstdio>
#include <mutex>

namespace cypher::common
{
namespace
{

std::mutex g_logMutex;
log_callback_t g_logCallback = nullptr;
void *g_logUserData = nullptr;
thread_local u32 g_logCallbackDepth = 0u;

struct log_callback_scope_t {
    log_callback_scope_t()
    {
        ++g_logCallbackDepth;
    }

    ~log_callback_scope_t()
    {
        --g_logCallbackDepth;
    }
};

void Log_WriteFallback( const log_record_t &record ) noexcept
{
    const char *pLevel = Cy_LogLevelName( record.level );
    const char *pChannel = Cy_LogChannelName( record.channel );

    if ( record.errorCode != CY_ERROR_OK && Cy_SourceLocation_IsValid( record.location ) ) {
        std::fprintf( stderr,
                      "[%s][%s][%s:%u] %s (%s:%u)\n",
                      pLevel,
                      pChannel,
                      Cy_ErrorDomainName( Cy_ErrorDomain( record.errorCode ) ),
                      static_cast<unsigned int>( Cy_ErrorLocalCode( record.errorCode ) ),
                      record.pMessage,
                      record.location.pFile,
                      static_cast<unsigned int>( record.location.line ) );
    } else if ( record.errorCode != CY_ERROR_OK ) {
        std::fprintf( stderr,
                      "[%s][%s][%s:%u] %s\n",
                      pLevel,
                      pChannel,
                      Cy_ErrorDomainName( Cy_ErrorDomain( record.errorCode ) ),
                      static_cast<unsigned int>( Cy_ErrorLocalCode( record.errorCode ) ),
                      record.pMessage );
    } else if ( Cy_SourceLocation_IsValid( record.location ) ) {
        std::fprintf( stderr,
                      "[%s][%s] %s (%s:%u)\n",
                      pLevel,
                      pChannel,
                      record.pMessage,
                      record.location.pFile,
                      static_cast<unsigned int>( record.location.line ) );
    } else {
        std::fprintf( stderr, "[%s][%s] %s\n", pLevel, pChannel, record.pMessage );
    }

    std::fflush( stderr );
}

} // namespace

void Cy_LogWrite(
    log_level_t level,
    log_channel_t channel,
    const char *pMessage ) noexcept
{
    Cy_LogWriteErrorAt( level, channel, CY_ERROR_OK, pMessage, {} );
}

void Cy_LogWriteError(
    log_level_t level,
    log_channel_t channel,
    error_code_t errorCode,
    const char *pMessage ) noexcept
{
    Cy_LogWriteErrorAt( level, channel, errorCode, pMessage, {} );
}

void Cy_LogWriteAt(
    log_level_t level,
    log_channel_t channel,
    const char *pMessage,
    source_location_t location ) noexcept
{
    Cy_LogWriteErrorAt( level, channel, CY_ERROR_OK, pMessage, location );
}

void Cy_LogWriteErrorAt(
    log_level_t level,
    log_channel_t channel,
    error_code_t errorCode,
    const char *pMessage,
    source_location_t location ) noexcept
{
    const log_category_mask_t channelMask = Cy_LogChannelMask( channel );
    if ( channelMask != 0u && !Cy_LogToggleAllEnabled( channelMask ) ) {
        return;
    }

    log_record_t record{};
    record.level = level;
    record.channel = channel;
    record.errorCode = errorCode;
    record.location = location;
    record.pMessage = pMessage != nullptr ? pMessage : "";

    log_callback_t pCallback = nullptr;
    void *pUserData = nullptr;
    {
        std::lock_guard<std::mutex> lock( g_logMutex );
        pCallback = g_logCallback;
        pUserData = g_logUserData;
    }

    if ( pCallback != nullptr && g_logCallbackDepth == 0u ) {
        const log_callback_scope_t scope;
        pCallback( record, pUserData );
        return;
    }

    Log_WriteFallback( record );
}

void Cy_LogSetCallback(
    log_callback_t pCallback,
    void *pUserData ) noexcept
{
    std::lock_guard<std::mutex> lock( g_logMutex );
    g_logCallback = pCallback;
    g_logUserData = pUserData;
}

void Cy_LogGetCallback(
    log_callback_t *pCallbackOut,
    void **ppUserDataOut ) noexcept
{
    std::lock_guard<std::mutex> lock( g_logMutex );
    if ( pCallbackOut != nullptr ) {
        *pCallbackOut = g_logCallback;
    }
    if ( ppUserDataOut != nullptr ) {
        *ppUserDataOut = g_logUserData;
    }
}

const char *Cy_LogLevelName( log_level_t level ) noexcept
{
    switch ( level ) {
        case log_level_t::Trace: return "Trace";
        case log_level_t::Debug: return "Debug";
        case log_level_t::Info: return "Info";
        case log_level_t::Warning: return "Warning";
        case log_level_t::Error: return "Error";
        case log_level_t::Fatal: return "Fatal";
        case log_level_t::Count:
            break;
    }

    return "Unknown";
}

const char *Cy_LogChannelName( log_channel_t channel ) noexcept
{
    switch ( channel ) {
        case log_channel_t::Common: return "Common";
        case log_channel_t::Memory: return "Memory";
        case log_channel_t::FileSystem: return "FileSystem";
        case log_channel_t::Pak: return "Pak";
        case log_channel_t::Render: return "Render";
        case log_channel_t::Audio: return "Audio";
        case log_channel_t::Network: return "Network";
        case log_channel_t::Physics: return "Physics";
        case log_channel_t::Game: return "Game";
        case log_channel_t::Tools: return "Tools";
        case log_channel_t::Editor: return "Editor";
        case log_channel_t::System: return "System";
        case log_channel_t::Host: return "Host";
        case log_channel_t::Command: return "Command";
        case log_channel_t::CVar: return "CVar";
        case log_channel_t::Config: return "Config";
        case log_channel_t::Input: return "Input";
        case log_channel_t::Asset: return "Asset";
        case log_channel_t::Resource: return "Resource";
        case log_channel_t::World: return "World";
        case log_channel_t::Entity: return "Entity";
        case log_channel_t::Animation: return "Animation";
        case log_channel_t::AI: return "AI";
        case log_channel_t::Script: return "Script";
        case log_channel_t::Material: return "Material";
        case log_channel_t::Texture: return "Texture";
        case log_channel_t::Gui: return "Gui";
        case log_channel_t::Job: return "Job";
        case log_channel_t::Serialization: return "Serialization";
        case log_channel_t::Reflection: return "Reflection";
        case log_channel_t::Count:
            break;
    }

    return "Unknown";
}

} // namespace cypher::common
