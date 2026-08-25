//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Window.h
//  Purpose: Declares the CypherSystem System Window module.
//  Details: This file owns platform-facing system, window, and graphics context
//           boundaries. Keep OS-specific code isolated enough that higher-level
//           runtime code remains portable.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_SYSTEM_WINDOW_H
#define CYPHER_ENGINE_SYSTEM_WINDOW_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"
#include "CypherSystem_Error.h"

namespace cypher::engine::sys
{

/*
================
System Window Types

The public engine-facing wrapper around native SDL window state.
================
*/
struct window_desc_t {
    const char *title{ common::COM_GAME_INFO.name }; // Borrowed UTF-8 title used during creation.
    common::u32 width{ 1280u };                     // Requested drawable width in pixels.
    common::u32 height{ 720u };                     // Requested drawable height in pixels.
    bool fullscreen{ false };                       // Creates a fullscreen window when true.
    bool vsync{ true };                             // Requests synchronized buffer presentation.
};

struct window_t {
    void *nativeWindow{ nullptr };                  // Opaque SDL_Window pointer owned by this wrapper.

    common::u32 width{ 0u };                        // Last known drawable width in pixels.
    common::u32 height{ 0u };                       // Last known drawable height in pixels.

    bool fullscreen{ false };                       // Current fullscreen policy.
    bool vsync{ true };                             // Current presentation interval policy.
    bool shouldClose{ false };                     // Set after a platform close request.
    bool valid{ false };                            // Native window was created and has not been destroyed.
};

/*
================
System Window API
================
*/
sys_error_t Sys_CreateWindow( const window_desc_t &windowDescription, window_t &windowOut );

void Sys_DestroyWindow( window_t &window );

void Sys_PollWindowEvents( window_t &window );

bool Sys_WindowShouldClose( const window_t &window );

}       // namespace cypher::engine::sys

#endif // CYPHER_ENGINE_SYSTEM_WINDOW_H
