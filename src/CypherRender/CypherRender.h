//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender.h
//  Purpose: Declares the CypherRender Render module.
//  Details: This file participates in the renderer bootstrap and draw path. Keep API
//           boundaries clear so the renderer can grow from simple OpenGL startup into
//           a fuller rendering backend.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_H
#define CYPHER_ENGINE_RENDER_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherHost_Types.h"
#include "CypherRender_Camera.h"
#include "CypherRender_Draw.h"
#include "CypherRender_Error.h"
#include "CypherRender_GL.h"
#include "CypherRender_Mesh.h"
#include "CypherRender_Shader.h"

namespace cypher::engine::render
{

/*
================
Renderer Runtime State

High-level renderer state. The embedded OpenGL state is transitional; public
renderer contracts must eventually expose backend-neutral handles only.
================
*/
struct render_runtime_state_t {
    bool initialized{ false };                              // Renderer and selected backend completed initialization.
    bool inFrame{ false };                                  // BeginFrame succeeded and EndFrame is still required.

    const sys::window_t *window{ nullptr };                 // Borrowed presentation window; System owns its lifetime.

    camera_t activeCamera{};                                // Camera used to build view/projection state this frame.

    draw_list_t mainDrawList{};                             // Transient submissions consumed before EndFrame.

    common::u32 nViewportWidth{ 0u };                       // Current drawable width in pixels.
    common::u32 nViewportHeight{ 0u };                      // Current drawable height in pixels.

    gl_state_t pGlState{};                                  // OpenGL backend state for the Cypher 1 renderer.
    shader_registry_t szShaderRegistry{};                   // Shaders owned by the current renderer instance.
};

/*
================
Renderer API
================
*/
render_error_t CypherRender_Init( const sys::window_t &window, const host::window_config_t &pWindowConfig );

void CypherRender_Shutdown();

render_error_t CypherRender_BeginFrame( const common::f32 nDeltaTimeSeconds );

render_error_t CypherRender_RenderFrame();

render_error_t CypherRender_EndFrame();

render_error_t CypherRender_SubmitDrawItem( const draw_item_t &drawItem );

bool CypherRender_IsInitialized();

}

#endif // CYPHER_ENGINE_RENDER_H
