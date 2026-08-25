//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender.cpp
//  Purpose: Implements the CypherRender Render module.
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

#include "CypherRender.h"
#include "CypherLog.h"
#include "CypherRender_Camera.h"
#include "CypherRender_Draw.h"
#include "CypherRender_GL.h"

namespace cypher::engine::render {

// @NOTE will be replaced by arena allocators soon
namespace
{
draw_item_t s_MainDrawItems[CYPHER_RENDER_DRAW_ITEMS_LIST_MAX]{};
}

static render_runtime_state_t s_RenderRuntimeState{};

/*
================
R_Init

Validates host/window config, starts the GL backend and initializes renderer state.
================
*/
render_error_t R_Init( const sys::window_t &window, const host::window_config_t &pWindowConfig ) {
	if ( R_IsInitialized() ) {
		LOG_WARNING( log::channel_t::RENDER, "renderer already initialized." );
		return render_error_t::ERR_IS_INIT;
	}

	if ( !window.valid || window.nativeWindow == nullptr ) {
		LOG_ERROR( log::channel_t::RENDER, "invalid sys window." );
		return render_error_t::ERR_INVALID_WINDOW_CFG;
	}

	if ( pWindowConfig.viewport.width == 0u || pWindowConfig.viewport.height == 0u ) {
		LOG_ERROR(
			log::channel_t::RENDER,
			"invalid viewport %ux%u (both dimensions must be > 0).",
			pWindowConfig.viewport.width,
			pWindowConfig.viewport.height );
		return render_error_t::ERR_INVALID_VIEWPORT;
	}

	s_RenderRuntimeState.window = &window;
	s_RenderRuntimeState.nViewportWidth = pWindowConfig.viewport.width;
	s_RenderRuntimeState.nViewportHeight = pWindowConfig.viewport.height;
	s_RenderRuntimeState.pGlState = {};
	s_RenderRuntimeState.szShaderRegistry = {};
	s_RenderRuntimeState.inFrame = false;

    R_DrawListInit(
        s_RenderRuntimeState.mainDrawList,
        s_MainDrawItems,
        CYPHER_RENDER_DRAW_ITEMS_LIST_MAX );

	LOG_INFO(
		log::channel_t::RENDER,
		"renderer initialized with viewport %ux%u.",
		s_RenderRuntimeState.nViewportWidth,
		s_RenderRuntimeState.nViewportHeight );

	const auto glResult = GL_Init( window, pWindowConfig.vsync, s_RenderRuntimeState.pGlState );
	if ( glResult != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "renderer backend initialization failed: %s.", R_ErrorDesc( glResult ) );
		s_RenderRuntimeState = {};
		return glResult;
	}

	R_ShaderRegistryInit( s_RenderRuntimeState.szShaderRegistry );

    camera_desc_t cameraDesc{};
    cameraDesc.cameraProjectionMode = camera_projection_mode_t::PERSPECTIVE;
    cameraDesc.aspectRatio    = static_cast<common::f32>( pWindowConfig.viewport.width ) /
                                  static_cast<common::f32>( pWindowConfig.viewport.height );
    cameraDesc.fovYRadians   = CYPHER_RENDER_DEFAULT_FOV_Y_RADIANS;
    cameraDesc.farZ           = CYPHER_RENDER_DEFAULT_FAR_Z;
    cameraDesc.nearZ          = CYPHER_RENDER_DEFAULT_NEAR_Z;
    R_CameraInit( s_RenderRuntimeState.activeCamera, cameraDesc );

    s_RenderRuntimeState.initialized = true;

	return render_error_t::OK;
}

/*
================
R_Shutdown
================
*/
void R_Shutdown() {
	if ( !R_IsInitialized() ) {
		LOG_INFO( log::channel_t::RENDER, "renderer was not initialized; nothing to shutdown." );
		return;
	}

	R_ShaderRegistryShutdown( s_RenderRuntimeState.szShaderRegistry );
	GL_Shutdown( s_RenderRuntimeState.pGlState );

	s_RenderRuntimeState = {};

	LOG_INFO( log::channel_t::RENDER, "renderer shutdown complete." );
}

/*
================
R_BeginFrame

Opens a render frame and prepares the backend for drawing.
================
*/
render_error_t R_BeginFrame( const common::f32 nDeltaTimeSeconds ) {
	if ( !R_IsInitialized() ) {
        LOG_ERROR( log::channel_t::RENDER, "begin frame failed: renderer is not initialized." );
		return render_error_t::ERR_NOT_INIT;
	}

	if ( s_RenderRuntimeState.inFrame ) {
        LOG_ERROR( log::channel_t::RENDER, "begin frame failed: frame is already active." );
		return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
	}

	const auto glResult = GL_BeginFrame( *s_RenderRuntimeState.window );
	if ( glResult != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "begin frame failed: GL begin failed: %s.", R_ErrorDesc( glResult ) );
		return render_error_t::ERR_BEGIN_DRAW;
	}

    R_DrawListClear( s_RenderRuntimeState.mainDrawList );

	(void)nDeltaTimeSeconds;
	s_RenderRuntimeState.inFrame = true;

	return render_error_t::OK;
}

/*
================
R_RenderFrame

Draws the items submitted by world/game/editor systems for the active frame.
================
*/
render_error_t R_RenderFrame() {
	if ( !R_IsInitialized() ) {
        LOG_ERROR( log::channel_t::RENDER, "render frame failed: renderer is not initialized." );
		return render_error_t::ERR_NOT_INIT;
	}

	if ( !s_RenderRuntimeState.inFrame ) {
        LOG_ERROR( log::channel_t::RENDER, "render frame failed: frame is not active." );
		return render_error_t::ERR_FRAME_NOT_ACTIVE;
	}

    return R_DrawListDraw(
        s_RenderRuntimeState.mainDrawList,
        s_RenderRuntimeState.activeCamera );
}

/*
================
R_EndFrame

Closes the render frame and presents the back buffer.
================
*/
render_error_t R_EndFrame() {
	if ( !R_IsInitialized() ) {
        LOG_ERROR( log::channel_t::RENDER, "end frame failed: renderer is not initialized." );
		return render_error_t::ERR_NOT_INIT;
	}

	if ( !s_RenderRuntimeState.inFrame ) {
        LOG_ERROR( log::channel_t::RENDER, "end frame failed: frame is not active." );
		return render_error_t::ERR_FRAME_NOT_ACTIVE;
	}

	const auto glResult = GL_EndFrame( *s_RenderRuntimeState.window );
	if ( glResult != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "end frame failed: GL end failed: %s.", R_ErrorDesc( glResult ) );
		return render_error_t::ERR_END_DRAW;
	}

	s_RenderRuntimeState.inFrame = false;

	return render_error_t::OK;
}

/*
================
R_SubmitDrawItem

Submits one draw item for the current frame. Game, editor and ECS layers use
this as the public doorway into the renderer draw list.
================
*/
render_error_t R_SubmitDrawItem( const draw_item_t &drawItem )
{
    if ( !R_IsInitialized() ) {
        LOG_ERROR( log::channel_t::RENDER, "submit draw item failed: renderer is not initialized." );
        return render_error_t::ERR_NOT_INIT;
    }

    if ( !s_RenderRuntimeState.inFrame ) {
        LOG_ERROR( log::channel_t::RENDER, "submit draw item failed: frame is not active." );
        return render_error_t::ERR_FRAME_NOT_ACTIVE;
    }

    return R_DrawListSubmit( s_RenderRuntimeState.mainDrawList, drawItem );
}

/*
================
R_IsInitialized
================
*/
bool R_IsInitialized() {
	return s_RenderRuntimeState.initialized;
}

} // namespace cypher::engine::render
