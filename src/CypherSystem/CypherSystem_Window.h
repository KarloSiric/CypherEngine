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
    const char *title{ common::COM_GAME_INFO.name };
    common::u32 width{ 1280u };
    common::u32 height{ 720u };
    bool fullscreen{ false };
    bool vsync{ true };
};

struct window_t {
    void *pNativeWindow{ nullptr };

    common::u32 width{ 0u };
    common::u32 height{ 0u };

    bool fullscreen{ false};
    bool vsync{ true };
    bool bShouldClose{ false };
    bool valid{ false };
};

/*
================
System Window API
================
*/
sys_error_t CypherSystem_CreateWindow( const window_desc_t &szWindowDescription, window_t &windowOut );

void CypherSystem_DestroyWindow( window_t &window );

void CypherSystem_PollWindowEvents( window_t &window );

bool CypherSystem_WindowShouldClose( const window_t &window );

}       // namespace cypher::engine::sys

#endif // CYPHER_ENGINE_SYSTEM_WINDOW_H
