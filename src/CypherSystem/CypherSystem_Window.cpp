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
Sys_CreateWindow

Creates the SDL window used later by the renderer backend.
================
*/
sys_error_t Sys_CreateWindow( const window_desc_t &windowDescription, window_t &windowOut )
{
    SDL_WindowFlags flags{};
    SDL_Window *sdlWindow{ nullptr };

    if ( windowDescription.title == nullptr || windowDescription.title[0] == '\0' ) {
        LOG_ERROR( log::channel_t::PLATFORM, "window creation failed: invalid title." );
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( windowDescription.width == 0u || windowDescription.height == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "window creation failed: invalid dimensions %ux%u.", windowDescription.width, windowDescription.height );
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( windowOut.nativeWindow != nullptr ) {
        LOG_WARNING( log::channel_t::PLATFORM, "window creation requested while output window is already initialized." );
        return sys_error_t::ERR_IS_INIT;
    }

    if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) ) {
        LOG_ERROR( log::channel_t::PLATFORM, "SDL video subsystem init failed: %s.", SDL_GetError() );
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if ( windowDescription.fullscreen ) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    sdlWindow = SDL_CreateWindow( windowDescription.title, static_cast<int>( windowDescription.width ), static_cast<int>( windowDescription.height ), flags );

    if ( sdlWindow == nullptr ) {
        LOG_ERROR( log::channel_t::PLATFORM, "SDL window creation failed: title='%s', size=%ux%u, fullscreen=%u: %s.", windowDescription.title, windowDescription.width, windowDescription.height, windowDescription.fullscreen ? 1u : 0u, SDL_GetError() );
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    windowOut.nativeWindow = sdlWindow;
    windowOut.fullscreen = windowDescription.fullscreen;
    windowOut.width = windowDescription.width;
    windowOut.height = windowDescription.height;
    windowOut.vsync = windowDescription.vsync;
    windowOut.shouldClose = false;
    windowOut.valid = true;

    LOG_INFO( log::channel_t::PLATFORM, "window created: title='%s', size=%ux%u, fullscreen=%u, vsync=%u.", windowDescription.title, windowOut.width, windowOut.height, windowOut.fullscreen ? 1u : 0u, windowOut.vsync ? 1u : 0u );

    return sys_error_t::OK;
}

/*
================
Sys_DestroyWindow
================
*/
void Sys_DestroyWindow( window_t &window ) {
    SDL_Window *sdlWindow{ nullptr };

    if ( window.nativeWindow == nullptr ) {
        LOG_DEBUG( log::channel_t::PLATFORM, "destroy window skipped: no native window." );
        return ;
    }

    sdlWindow = static_cast<SDL_Window *>( window.nativeWindow );

    SDL_DestroyWindow( sdlWindow );
    SDL_QuitSubSystem( SDL_INIT_VIDEO );

    LOG_INFO( log::channel_t::PLATFORM, "window destroyed." );

    window = {};

    return ;
}

/*
================
Sys_PollWindowEvents

Updates window state from SDL events.
================
*/
void Sys_PollWindowEvents( window_t &window ) {
    SDL_Event event{};

    if ( window.nativeWindow == nullptr ) {
        return ;
    }

    while ( SDL_PollEvent( &event ) ) {
        switch( event.type ) {
            case SDL_EVENT_QUIT:
                window.shouldClose = true;
                LOG_INFO( log::channel_t::PLATFORM, "SDL quit event received." );
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                window.shouldClose = true;
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
Sys_WindowShouldClose
================
*/
bool Sys_WindowShouldClose( const window_t &window ) {
    return window.shouldClose;
}

}       // namespace cypher::engine::sys
