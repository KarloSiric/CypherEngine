//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherHost/CypherHost_Types.h
//  Purpose: Declares the CypherHost Host Types module.
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

#ifndef CYPHER_ENGINE_HOST_TYPES_H
#define CYPHER_ENGINE_HOST_TYPES_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon.h"
#include "CypherSystem_Window.h"

namespace cypher::engine::host
{

/*
================
Host Defaults
================
*/
constexpr common::u32 HOST_DEFAULT_VIEWPORT_WIDTH  = 1280u;
constexpr common::u32 HOST_DEFAULT_VIEWPORT_HEIGHT = 720u;
constexpr common::u32 HOST_DEFAULT_TARGET_FPS      = 60u;
constexpr const char *HOST_DEFAULT_WINDOW_TITLE         = "REAP";

/*
================
Host State Types

The host owns high-level engine lifecycle and keeps subsystem-facing config.
================
*/
enum class stage_t : common::u8 {
    UNINITIALIZED,
    INITIALIZING,
    RUNNING,
    PAUSED,
    SHUTTINGDOWN,
    SHUTDOWN
};

enum class build_config_t : common::u8 {
    UNKNOWN,
    DEBUG,
    RELEASE,
    DISTRIBUTION
};

/*
================
Host Runtime Data
================
*/
struct viewport_t {
    common::u32 width{ HOST_DEFAULT_VIEWPORT_WIDTH };
    common::u32 height{ HOST_DEFAULT_VIEWPORT_HEIGHT };
};

struct window_config_t {
    viewport_t viewport{};
    const char *title{ HOST_DEFAULT_WINDOW_TITLE };
    bool fullscreen{false};
    bool vsync{true};
    common::u32 nTargetFps{ HOST_DEFAULT_TARGET_FPS };
};

struct frame_t {
    common::frame_index_t index{};

    common::f64 nPreviousTimeSeconds{};
    common::f64 nCurrentTimeSeconds{};

    common::f32 nDeltaTimeSeconds{};
    common::f32 nRealTimeSeconds{};
    common::f32 nSimulationTimeSeconds{};
};

struct config_t {
    int argc{ 0 };
    const char *const *argv{ nullptr };

    build_config_t buildConfig{ build_config_t::UNKNOWN };
    window_config_t pWindowConfig{};
};

struct state_t {
    stage_t stage{ stage_t::UNINITIALIZED };
    config_t config{};
    sys::window_t window{};
    bool running{ false };
    bool bHasFocus{ false };
    frame_t frame{};
};

}       // namespace cypher::engine::host

#endif // CYPHER_ENGINE_HOST_TYPES_H
