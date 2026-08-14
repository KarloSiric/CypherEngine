//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Window.cpp
//  Purpose: Implements the CypherSystem System Window module.
//  Details: This file owns platform-facing system, window, and graphics context
//           boundaries. Keep OS-specific code isolated enough that higher-level
//           runtime code remains portable.
//
//  History:
//  - Created by Karlo Siric on 2026-05-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_Window.h"
#include "Engine/CypherCommon_Print.h"
#include "CypherLog.h"

#include <SDL3/SDL.h>      // Cross-platform window and event API.

namespace cypher::engine::sys
{

/*
================
CypherSystem_CreateWindow

Creates the SDL window used later by the renderer backend.
================
*/
sys_error_t CypherSystem_CreateWindow( const window_desc_t &szWindowDescription, window_t &windowOut )
{
    SDL_WindowFlags flags{};
    SDL_Window *pSdlWindow{ nullptr };

    if ( szWindowDescription.title == nullptr || szWindowDescription.title[0] == '\0' ) {
        LOG_ERROR( log::channel_t::PLATFORM, "window creation failed: invalid title." );
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( szWindowDescription.width == 0u || szWindowDescription.height == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "window creation failed: invalid dimensions %ux%u.", szWindowDescription.width, szWindowDescription.height );
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( windowOut.pNativeWindow != nullptr ) {
        LOG_WARNING( log::channel_t::PLATFORM, "window creation requested while output window is already initialized." );
        return sys_error_t::ERR_IS_INIT;
    }

    if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) ) {
        LOG_ERROR( log::channel_t::PLATFORM, "SDL video subsystem init failed: %s.", SDL_GetError() );
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if ( szWindowDescription.fullscreen ) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    pSdlWindow = SDL_CreateWindow( szWindowDescription.title, static_cast<int>( szWindowDescription.width ), static_cast<int>( szWindowDescription.height ), flags );

    if ( pSdlWindow == nullptr ) {
        LOG_ERROR( log::channel_t::PLATFORM, "SDL window creation failed: title='%s', size=%ux%u, fullscreen=%u: %s.", szWindowDescription.title, szWindowDescription.width, szWindowDescription.height, szWindowDescription.fullscreen ? 1u : 0u, SDL_GetError() );
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    windowOut.pNativeWindow = pSdlWindow;
    windowOut.fullscreen = szWindowDescription.fullscreen;
    windowOut.width = szWindowDescription.width;
    windowOut.height = szWindowDescription.height;
    windowOut.vsync = szWindowDescription.vsync;
    windowOut.bShouldClose = false;
    windowOut.valid = true;

    LOG_INFO( log::channel_t::PLATFORM, "window created: title='%s', size=%ux%u, fullscreen=%u, vsync=%u.", szWindowDescription.title, windowOut.width, windowOut.height, windowOut.fullscreen ? 1u : 0u, windowOut.vsync ? 1u : 0u );

    return sys_error_t::OK;
}

/*
================
CypherSystem_DestroyWindow
================
*/
void CypherSystem_DestroyWindow( window_t &window ) {
    SDL_Window *pSdlWindow{ nullptr };

    if ( window.pNativeWindow == nullptr ) {
        LOG_DEBUG( log::channel_t::PLATFORM, "destroy window skipped: no native window." );
        return ;
    }

    pSdlWindow = static_cast<SDL_Window *>( window.pNativeWindow );

    SDL_DestroyWindow( pSdlWindow );
    SDL_QuitSubSystem( SDL_INIT_VIDEO );

    LOG_INFO( log::channel_t::PLATFORM, "window destroyed." );

    window = {};

    return ;
}

/*
================
CypherSystem_PollWindowEvents

Updates window state from SDL events.
================
*/
void CypherSystem_PollWindowEvents( window_t &window ) {
    SDL_Event event{};

    if ( window.pNativeWindow == nullptr ) {
        return ;
    }

    while ( SDL_PollEvent( &event ) ) {
        switch( event.type ) {
            case SDL_EVENT_QUIT:
                window.bShouldClose = true;
                LOG_INFO( log::channel_t::PLATFORM, "SDL quit event received." );
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                window.bShouldClose = true;
                LOG_INFO( log::channel_t::PLATFORM, "window close requested." );
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                if ( event.window.data1 > 0 && event.window.data2 > 0 ) {
                    window.width = static_cast<common::u32>( event.window.data1 );
                    window.height = static_cast<common::u32>( event.window.data2 );
                    LOG_DEBUG( log::channel_t::PLATFORM, "window resized: %ux%u.", window.width, window.height );
                }
                break;
            default:
                break;
        }
    }
}

/*
================
CypherSystem_WindowShouldClose
================
*/
bool CypherSystem_WindowShouldClose( const window_t &window ) {
    return window.bShouldClose;
}

}       // namespace cypher::engine::sys
