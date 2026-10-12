//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherHost/CypherHost.h
//  Purpose: Declares the CypherHost Host module.
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

#ifndef CYPHER_ENGINE_HOST_H
#define CYPHER_ENGINE_HOST_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherHost_Error.h"
#include "CypherHost_Types.h"
#include "CypherRender/CypherRender_Error.h"

namespace cypher::engine::host
{

/*
================
Host Runtime API
================
*/
CYPHER_NODISCARD host_error_t Host_Init( state_t &hostState );

void Host_RequestShutdown( state_t &hostState );

void Host_Shutdown( state_t &hostState );

void Host_BeginFrame( state_t &hostState );

void Host_Update( state_t &hostState );

// Called once between R_BeginFrame and R_EndFrame. The Host borrows userData
// only for this call; submission may use public renderer draw commands, but
// must not begin or end the frame itself. An error requests shutdown and is
// returned to the caller so the process can report a failed run.
using host_render_submit_fn = render::render_error_t (*)(
    const render::render_frame_info_t &frameInfo,
    void *userData ) noexcept;

CYPHER_NODISCARD render::render_error_t Host_Render(
    state_t &hostState,
    host_render_submit_fn submit = nullptr,
    void *userData = nullptr );

void Host_EndFrame( state_t &hostState );

CYPHER_NODISCARD bool Host_IsRunning( const state_t &hostState );

}

#endif // CYPHER_ENGINE_HOST_H
