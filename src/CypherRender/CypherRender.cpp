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
CypherRender_Init

Validates host/window config, starts the GL backend and initializes renderer state.
================
*/
render_error_t CypherRender_Init( const sys::window_t &window, const host::window_config_t &pWindowConfig ) {
	if ( CypherRender_IsInitialized() ) {
		LOG_WARNING( log::channel_t::RENDER, "renderer already initialized." );
		return render_error_t::ERR_IS_INIT;
	}

	if ( !window.valid || window.pNativeWindow == nullptr ) {
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

    CypherRender_DrawListInit(
        s_RenderRuntimeState.mainDrawList,
        s_MainDrawItems,
        CYPHER_RENDER_DRAW_ITEMS_LIST_MAX );

	LOG_INFO(
		log::channel_t::RENDER,
		"renderer initialized with viewport %ux%u.",
		s_RenderRuntimeState.nViewportWidth,
		s_RenderRuntimeState.nViewportHeight );

	const auto glResult = CypherRenderGL_Init( window, pWindowConfig.vsync, s_RenderRuntimeState.pGlState );
	if ( glResult != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "renderer backend initialization failed: %s.", CypherRender_ErrorDesc( glResult ) );
		s_RenderRuntimeState = {};
		return glResult;
	}

	CypherRender_ShaderRegistryInit( s_RenderRuntimeState.szShaderRegistry );

    camera_desc_t cameraDesc{};
    cameraDesc.cameraProjectionMode = camera_projection_mode_t::PERSPECTIVE;
    cameraDesc.aspectRatio    = static_cast<common::f32>( pWindowConfig.viewport.width ) /
                                  static_cast<common::f32>( pWindowConfig.viewport.height );
    cameraDesc.fovYRadians   = CYPHER_RENDER_DEFAULT_FOV_Y_RADIANS;
    cameraDesc.farZ           = CYPHER_RENDER_DEFAULT_FAR_Z;
    cameraDesc.nearZ          = CYPHER_RENDER_DEFAULT_NEAR_Z;
    CypherRender_CameraInit( s_RenderRuntimeState.activeCamera, cameraDesc );

    s_RenderRuntimeState.initialized = true;

	return render_error_t::OK;
}

/*
================
CypherRender_Shutdown
================
*/
void CypherRender_Shutdown() {
	if ( !CypherRender_IsInitialized() ) {
		LOG_INFO( log::channel_t::RENDER, "renderer was not initialized; nothing to shutdown." );
		return;
	}

	CypherRender_ShaderRegistryShutdown( s_RenderRuntimeState.szShaderRegistry );
	CypherRenderGL_Shutdown( s_RenderRuntimeState.pGlState );

	s_RenderRuntimeState = {};

	LOG_INFO( log::channel_t::RENDER, "renderer shutdown complete." );
}

/*
================
CypherRender_BeginFrame

Opens a render frame and prepares the backend for drawing.
================
*/
render_error_t CypherRender_BeginFrame( const common::f32 nDeltaTimeSeconds ) {
	if ( !CypherRender_IsInitialized() ) {
        LOG_ERROR( log::channel_t::RENDER, "begin frame failed: renderer is not initialized." );
		return render_error_t::ERR_NOT_INIT;
	}

	if ( s_RenderRuntimeState.inFrame ) {
        LOG_ERROR( log::channel_t::RENDER, "begin frame failed: frame is already active." );
		return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
	}

	const auto glResult = CypherRenderGL_BeginFrame( *s_RenderRuntimeState.window );
	if ( glResult != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "begin frame failed: GL begin failed: %s.", CypherRender_ErrorDesc( glResult ) );
		return render_error_t::ERR_BEGIN_DRAW;
	}

    CypherRender_DrawListClear( s_RenderRuntimeState.mainDrawList );

	(void)nDeltaTimeSeconds;
	s_RenderRuntimeState.inFrame = true;

	return render_error_t::OK;
}

/*
================
CypherRender_RenderFrame

Draws the items submitted by world/game/editor systems for the active frame.
================
*/
render_error_t CypherRender_RenderFrame() {
	if ( !CypherRender_IsInitialized() ) {
        LOG_ERROR( log::channel_t::RENDER, "render frame failed: renderer is not initialized." );
		return render_error_t::ERR_NOT_INIT;
	}

	if ( !s_RenderRuntimeState.inFrame ) {
        LOG_ERROR( log::channel_t::RENDER, "render frame failed: frame is not active." );
		return render_error_t::ERR_FRAME_NOT_ACTIVE;
	}

    return CypherRender_DrawListDraw(
        s_RenderRuntimeState.mainDrawList,
        s_RenderRuntimeState.activeCamera );
}

/*
================
CypherRender_EndFrame

Closes the render frame and presents the back buffer.
================
*/
render_error_t CypherRender_EndFrame() {
	if ( !CypherRender_IsInitialized() ) {
        LOG_ERROR( log::channel_t::RENDER, "end frame failed: renderer is not initialized." );
		return render_error_t::ERR_NOT_INIT;
	}

	if ( !s_RenderRuntimeState.inFrame ) {
        LOG_ERROR( log::channel_t::RENDER, "end frame failed: frame is not active." );
		return render_error_t::ERR_FRAME_NOT_ACTIVE;
	}

	const auto glResult = CypherRenderGL_EndFrame( *s_RenderRuntimeState.window );
	if ( glResult != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "end frame failed: GL end failed: %s.", CypherRender_ErrorDesc( glResult ) );
		return render_error_t::ERR_END_DRAW;
	}

	s_RenderRuntimeState.inFrame = false;

	return render_error_t::OK;
}

/*
================
CypherRender_SubmitDrawItem

Submits one draw item for the current frame. Game, editor and ECS layers use
this as the public doorway into the renderer draw list.
================
*/
render_error_t CypherRender_SubmitDrawItem( const draw_item_t &drawItem )
{
    if ( !CypherRender_IsInitialized() ) {
        LOG_ERROR( log::channel_t::RENDER, "submit draw item failed: renderer is not initialized." );
        return render_error_t::ERR_NOT_INIT;
    }

    if ( !s_RenderRuntimeState.inFrame ) {
        LOG_ERROR( log::channel_t::RENDER, "submit draw item failed: frame is not active." );
        return render_error_t::ERR_FRAME_NOT_ACTIVE;
    }

    return CypherRender_DrawListSubmit( s_RenderRuntimeState.mainDrawList, drawItem );
}

/*
================
CypherRender_IsInitialized
================
*/
bool CypherRender_IsInitialized() {
	return s_RenderRuntimeState.initialized;
}

} // namespace cypher::engine::render
