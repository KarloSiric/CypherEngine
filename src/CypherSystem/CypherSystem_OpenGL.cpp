//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_OpenGL.cpp
//  Purpose: Implements the SDL-backed OpenGL context bridge.
//  Details: SDL remains private to CypherSystem. CypherRender receives only an
//           opaque context handle and a generic function pointer used by GLAD.
//
//  History:
//  - Created by Karlo Siric on 2026-08-31
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_OpenGL.h"
#include "CypherSystem_Local.h"
#include "CypherSystem_Window.h"

#include <SDL3/SDL.h> // Native OpenGL context, attribute, and presentation operations.

#include <limits> // Bounds checks while converting queried SDL values to engine types.

namespace cypher::engine::sys {

namespace {

struct glimp_state_t {
	SDL_GLContext context{ nullptr }; // One native OpenGL context is supported in System V1.
	SDL_Window *window{ nullptr }; // Native window associated with the active context.
	sys_window_id_t windowId{ SYS_INVALID_WINDOW_ID }; // Engine identity used for lifetime validation.
};

glimp_state_t glimpState{};

SDL_GLContext GLimp_GetNativeContext( const gl_context_t &context ) noexcept {
	return static_cast<SDL_GLContext>( context.nativeContext );
}

SDL_Window *GLimp_GetNativeWindow( window_t &window ) noexcept {
	return static_cast<SDL_Window *>( window.nativeWindow );
}

bool GLimp_IsCurrent() noexcept {
	return glimpState.context != nullptr && SDL_GL_GetCurrentContext() == glimpState.context && SDL_GL_GetCurrentWindow() == glimpState.window;
}

int GLimp_ProfileToSDL( const gl_profile_t profile ) noexcept {
	switch ( profile ) {
	case gl_profile_t::CORE:
		return SDL_GL_CONTEXT_PROFILE_CORE;
	case gl_profile_t::COMPATIBILITY:
		return SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
	case gl_profile_t::ES:
		return SDL_GL_CONTEXT_PROFILE_ES;
	case gl_profile_t::NONE:
	case gl_profile_t::COUNT:
	default:
		return 0;
	}
}

gl_profile_t GLimp_ProfileFromSDL( const int profile ) noexcept {
	switch ( profile ) {
	case SDL_GL_CONTEXT_PROFILE_CORE:
		return gl_profile_t::CORE;
	case SDL_GL_CONTEXT_PROFILE_COMPATIBILITY:
		return gl_profile_t::COMPATIBILITY;
	case SDL_GL_CONTEXT_PROFILE_ES:
		return gl_profile_t::ES;
	default:
		return gl_profile_t::NONE;
	}
}

int GLimp_ContextFlagsToSDL( const gl_context_flags_t flags ) noexcept {
	int nativeFlags = 0;
	if ( ( flags & GLIMP_CONTEXT_DEBUG ) != 0u ) {
		nativeFlags |= SDL_GL_CONTEXT_DEBUG_FLAG;
	}
	if ( ( flags & GLIMP_CONTEXT_FORWARD_COMPATIBLE ) != 0u ) {
		nativeFlags |= SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
	}
	if ( ( flags & GLIMP_CONTEXT_ROBUST_ACCESS ) != 0u ) {
		nativeFlags |= SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG;
	}
	if ( ( flags & GLIMP_CONTEXT_RESET_ISOLATION ) != 0u ) {
		nativeFlags |= SDL_GL_CONTEXT_RESET_ISOLATION_FLAG;
	}
	return nativeFlags;
}

gl_context_flags_t GLimp_ContextFlagsFromSDL( const int nativeFlags ) noexcept {
	gl_context_flags_t flags = GLIMP_CONTEXT_NONE;
	if ( ( nativeFlags & SDL_GL_CONTEXT_DEBUG_FLAG ) != 0 ) {
		flags |= GLIMP_CONTEXT_DEBUG;
	}
	if ( ( nativeFlags & SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG ) != 0 ) {
		flags |= GLIMP_CONTEXT_FORWARD_COMPATIBLE;
	}
	if ( ( nativeFlags & SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG ) != 0 ) {
		flags |= GLIMP_CONTEXT_ROBUST_ACCESS;
	}
	if ( ( nativeFlags & SDL_GL_CONTEXT_RESET_ISOLATION_FLAG ) != 0 ) {
		flags |= GLIMP_CONTEXT_RESET_ISOLATION;
	}
	return flags;
}

bool GLimp_SetAttribute( const SDL_GLAttr attribute, const int value ) noexcept {
	if ( SDL_GL_SetAttribute( attribute, value ) ) {
		return true;
	}

	Sys_DebugPrintf(
		"GLimp: SDL rejected context attribute %d=%d: %s.\n",
		static_cast<int>( attribute ),
		value,
		SDL_GetError() );
	return false;
}

bool GLimp_GetAttribute( const SDL_GLAttr attribute, int &valueOut ) noexcept {
	valueOut = 0;
	if ( SDL_GL_GetAttribute( attribute, &valueOut ) ) {
		return true;
	}

	Sys_DebugPrintf(
		"GLimp: SDL could not query context attribute %d: %s.\n",
		static_cast<int>( attribute ),
		SDL_GetError() );
	return false;
}

void GLimp_GetOptionalAttribute( const SDL_GLAttr attribute, int &valueOut ) noexcept {
	valueOut = 0;
	if ( !SDL_GL_GetAttribute( attribute, &valueOut ) ) {
		// Some valid platform contexts cannot report every SDL capability hint.
		// The public context record uses conservative defaults for those fields.
		(void)SDL_ClearError();
	}
}

bool GLimp_StoreByte( const int value, common::u8 &valueOut ) noexcept {
	if ( value < 0 || value > static_cast<int>( std::numeric_limits<common::u8>::max() ) ) {
		return false;
	}

	valueOut = static_cast<common::u8>( value );
	return true;
}

} // namespace

/*
================
GLimp_ContextDescIsValid

Rejects programmer errors before SDL initializes video or mutates process-wide
OpenGL attributes. Driver support is still decided during native creation.
================
*/
bool GLimp_ContextDescIsValid( const gl_context_desc_t &description ) noexcept {
	if ( description.majorVersion == 0u || description.profile <= gl_profile_t::NONE || description.profile >= gl_profile_t::COUNT || ( description.flags & ~GLIMP_CONTEXT_FLAG_MASK ) != 0u ) {
		return false;
	}

	// Desktop profile masks were introduced with OpenGL 3.2. ES contexts use
	// their own version line and therefore do not use this restriction.
	if ( description.profile != gl_profile_t::ES && ( description.majorVersion < 3u || ( description.majorVersion == 3u && description.minorVersion < 2u ) ) ) {
		return false;
	}

	if ( description.redBits == 0u || description.greenBits == 0u || description.blueBits == 0u ) {
		return false;
	}

	// A single sample is equivalent to no multisampling and is represented by
	// zero. Real MSAA requests stay power-of-two so configuration is unambiguous.
	if ( description.sampleCount == 1u || ( description.sampleCount != 0u && ( description.sampleCount & ( description.sampleCount - 1u ) ) != 0u ) ) {
		return false;
	}

	return true;
}

/*
================
GLimp_ApplyWindowAttributes

Called by Sys_CreateWindow after SDL video initialization and before native
window creation. SDL retains these values for the subsequent context creation.
================
*/
sys_error_t GLimp_ApplyWindowAttributes( const gl_context_desc_t &description ) noexcept {
	if ( !GLimp_ContextDescIsValid( description ) ) {
		return sys_error_t::ERR_INVALID_ARGUMENT;
	}

	SDL_GL_ResetAttributes();

	const int profile = GLimp_ProfileToSDL( description.profile );
	const int contextFlags = GLimp_ContextFlagsToSDL( description.flags );
	const int multisampleBuffers = description.sampleCount == 0u ? 0 : 1;

	const bool attributesAccepted =
		GLimp_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, description.majorVersion ) && GLimp_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, description.minorVersion ) && GLimp_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, profile ) && GLimp_SetAttribute( SDL_GL_CONTEXT_FLAGS, contextFlags ) && GLimp_SetAttribute( SDL_GL_RED_SIZE, description.redBits ) && GLimp_SetAttribute( SDL_GL_GREEN_SIZE, description.greenBits ) && GLimp_SetAttribute( SDL_GL_BLUE_SIZE, description.blueBits ) && GLimp_SetAttribute( SDL_GL_ALPHA_SIZE, description.alphaBits ) && GLimp_SetAttribute( SDL_GL_DEPTH_SIZE, description.depthBits ) && GLimp_SetAttribute( SDL_GL_STENCIL_SIZE, description.stencilBits ) && GLimp_SetAttribute( SDL_GL_DOUBLEBUFFER, description.doubleBuffered ? 1 : 0 ) && GLimp_SetAttribute( SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, description.sRGBFramebuffer ? 1 : 0 ) && GLimp_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, multisampleBuffers ) && GLimp_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, description.sampleCount ) && ( !description.requireAcceleration || GLimp_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 ) );

	if ( !attributesAccepted ) {
		SDL_GL_ResetAttributes();
		return sys_error_t::ERR_GRAPHICS_CONFIG_UNSUPPORTED;
	}

	return sys_error_t::OK;
}

void GLimp_ResetWindowAttributes() noexcept {
	SDL_GL_ResetAttributes();
}

bool GLimp_HasActiveContext() noexcept {
	return glimpState.context != nullptr;
}

bool GLimp_WindowOwnsActiveContext( const sys_window_id_t windowId ) noexcept {
	return windowId != SYS_INVALID_WINDOW_ID && glimpState.context != nullptr && glimpState.windowId == windowId;
}

bool GLimp_IsContextValid( const gl_context_t &context ) noexcept {
	return context.nativeContext != nullptr && GLimp_GetNativeContext( context ) == glimpState.context;
}

sys_error_t GLimp_CreateContext( window_t &window, gl_context_t &contextOut ) noexcept {
	if ( !Sys_IsInitialized() ) {
		return sys_error_t::ERR_NOT_INIT;
	}
	if ( !Sys_WindowIsValid( window ) || window.graphicsApi != window_graphics_api_t::OPENGL ) {
		return sys_error_t::ERR_INVALID_ARGUMENT;
	}
	if ( contextOut.nativeContext != nullptr ) {
		return sys_error_t::ERR_IS_INIT;
	}
	if ( glimpState.context != nullptr ) {
		return sys_error_t::ERR_RESOURCE_BUSY;
	}

	SDL_Window *nativeWindow = GLimp_GetNativeWindow( window );
	SDL_GLContext nativeContext = SDL_GL_CreateContext( nativeWindow );
	if ( nativeContext == nullptr ) {
		Sys_DebugPrintf( "GLimp_CreateContext: %s.\n", SDL_GetError() );
		return sys_error_t::ERR_GRAPHICS_CONTEXT_FAILED;
	}

	// SDL_GL_CreateContext makes the new context current. Verify this instead
	// of assuming a driver honored the operation incompletely.
	if ( SDL_GL_GetCurrentContext() != nativeContext || SDL_GL_GetCurrentWindow() != nativeWindow ) {
		(void)SDL_GL_DestroyContext( nativeContext );
		Sys_DebugPrintf( "GLimp_CreateContext: native context was not made current.\n" );
		return sys_error_t::ERR_GRAPHICS_CONTEXT_FAILED;
	}

	glimpState.context = nativeContext;
	glimpState.window = nativeWindow;
	glimpState.windowId = window.id;
	contextOut.nativeContext = nativeContext;
	return sys_error_t::OK;
}

sys_error_t GLimp_MakeCurrent( window_t &window, const gl_context_t &context ) noexcept {
	if ( !Sys_IsInitialized() ) {
		return sys_error_t::ERR_NOT_INIT;
	}
	if ( !Sys_WindowIsValid( window ) || !GLimp_IsContextValid( context ) || GLimp_GetNativeWindow( window ) != glimpState.window || window.id != glimpState.windowId ) {
		return sys_error_t::ERR_INVALID_ARGUMENT;
	}

	if ( !SDL_GL_MakeCurrent( glimpState.window, glimpState.context ) ) {
		Sys_DebugPrintf( "GLimp_MakeCurrent: %s.\n", SDL_GetError() );
		return sys_error_t::ERR_GRAPHICS_CONTEXT_FAILED;
	}

	return sys_error_t::OK;
}

sys_error_t GLimp_DestroyContext( gl_context_t &context ) noexcept {
	if ( !Sys_IsInitialized() ) {
		return sys_error_t::ERR_NOT_INIT;
	}
	if ( context.nativeContext == nullptr ) {
		return sys_error_t::ERR_NOT_INIT;
	}
	if ( !GLimp_IsContextValid( context ) ) {
		return sys_error_t::ERR_INVALID_ARGUMENT;
	}

	if ( !SDL_GL_DestroyContext( glimpState.context ) ) {
		Sys_DebugPrintf( "GLimp_DestroyContext: %s.\n", SDL_GetError() );
		return sys_error_t::ERR_GRAPHICS_OPERATION_FAILED;
	}

	context = {};
	glimpState = {};
	SDL_GL_ResetAttributes();
	return sys_error_t::OK;
}

gl_proc_t GLimp_GetProcAddress( const char *procedureName ) noexcept {
	if ( !Sys_IsInitialized() || procedureName == nullptr || procedureName[0] == '\0' || !GLimp_IsCurrent() ) {
		return nullptr;
	}

	return reinterpret_cast<gl_proc_t>( SDL_GL_GetProcAddress( procedureName ) );
}

sys_error_t GLimp_QueryContextInfo( gl_context_info_t &infoOut ) noexcept {
	infoOut = {};
	if ( !Sys_IsInitialized() || !GLimp_IsCurrent() ) {
		return sys_error_t::ERR_NOT_INIT;
	}

	int majorVersion = 0;
	int minorVersion = 0;
	int profile = 0;
	int contextFlags = 0;
	int redBits = 0;
	int greenBits = 0;
	int blueBits = 0;
	int alphaBits = 0;
	int depthBits = 0;
	int stencilBits = 0;
	int multisampleBuffers = 0;
	int sampleCount = 0;
	int doubleBuffered = 0;
	int sRGBFramebuffer = 0;
	int accelerated = 0;

	const bool requiredQueriesSucceeded =
		GLimp_GetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, majorVersion ) && GLimp_GetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, minorVersion ) && GLimp_GetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, profile ) && GLimp_GetAttribute( SDL_GL_CONTEXT_FLAGS, contextFlags ) && GLimp_GetAttribute( SDL_GL_RED_SIZE, redBits ) && GLimp_GetAttribute( SDL_GL_GREEN_SIZE, greenBits ) && GLimp_GetAttribute( SDL_GL_BLUE_SIZE, blueBits ) && GLimp_GetAttribute( SDL_GL_ALPHA_SIZE, alphaBits ) && GLimp_GetAttribute( SDL_GL_DEPTH_SIZE, depthBits ) && GLimp_GetAttribute( SDL_GL_STENCIL_SIZE, stencilBits ) && GLimp_GetAttribute( SDL_GL_DOUBLEBUFFER, doubleBuffered );

	if ( !requiredQueriesSucceeded ) {
		return sys_error_t::ERR_GRAPHICS_OPERATION_FAILED;
	}

	// MSAA, sRGB, and acceleration are capabilities rather than context
	// identity. SDL cannot query all of them on every valid WGL/GLX/CGL
	// context, so an unavailable report maps to the conservative zero value.
	GLimp_GetOptionalAttribute( SDL_GL_MULTISAMPLEBUFFERS, multisampleBuffers );
	if ( multisampleBuffers != 0 ) {
		GLimp_GetOptionalAttribute( SDL_GL_MULTISAMPLESAMPLES, sampleCount );
	}
	GLimp_GetOptionalAttribute( SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, sRGBFramebuffer );
	GLimp_GetOptionalAttribute( SDL_GL_ACCELERATED_VISUAL, accelerated );

	gl_context_info_t actual{};
	actual.profile = GLimp_ProfileFromSDL( profile );
	actual.flags = GLimp_ContextFlagsFromSDL( contextFlags );
	actual.doubleBuffered = doubleBuffered != 0;
	actual.sRGBFramebuffer = sRGBFramebuffer != 0;
	actual.accelerated = accelerated != 0;

	const bool valuesFit =
		actual.profile != gl_profile_t::NONE &&
		GLimp_StoreByte( majorVersion, actual.majorVersion ) &&
		GLimp_StoreByte( minorVersion, actual.minorVersion ) &&
		GLimp_StoreByte( redBits, actual.redBits ) &&
		GLimp_StoreByte( greenBits, actual.greenBits ) &&
		GLimp_StoreByte( blueBits, actual.blueBits ) &&
		GLimp_StoreByte( alphaBits, actual.alphaBits ) &&
		GLimp_StoreByte( depthBits, actual.depthBits ) &&
		GLimp_StoreByte( stencilBits, actual.stencilBits );

	if ( !valuesFit ) {
		return sys_error_t::ERR_GRAPHICS_OPERATION_FAILED;
	}

	const int effectiveSampleCount =
		multisampleBuffers != 0 && sampleCount > 0 ? sampleCount : 0;
	if ( !GLimp_StoreByte( effectiveSampleCount, actual.sampleCount ) ) {
		actual.sampleCount = 0u;
	}

	infoOut = actual;
	return sys_error_t::OK;
}

sys_error_t GLimp_SetSwapInterval( const gl_swap_interval_t interval ) noexcept {
	if ( !Sys_IsInitialized() || !GLimp_IsCurrent() ) {
		return sys_error_t::ERR_NOT_INIT;
	}

	const int nativeInterval = static_cast<int>( interval );
	if ( nativeInterval < -1 || nativeInterval > 1 ) {
		return sys_error_t::ERR_INVALID_ARGUMENT;
	}

	if ( !SDL_GL_SetSwapInterval( nativeInterval ) ) {
		Sys_DebugPrintf( "GLimp_SetSwapInterval: %s.\n", SDL_GetError() );
		return sys_error_t::ERR_GRAPHICS_OPERATION_FAILED;
	}

	return sys_error_t::OK;
}

sys_error_t GLimp_GetSwapInterval( gl_swap_interval_t &intervalOut ) noexcept {
	intervalOut = gl_swap_interval_t::IMMEDIATE;
	if ( !Sys_IsInitialized() || !GLimp_IsCurrent() ) {
		return sys_error_t::ERR_NOT_INIT;
	}

	int nativeInterval = 0;
	if ( !SDL_GL_GetSwapInterval( &nativeInterval ) || nativeInterval < -1 || nativeInterval > 1 ) {
		Sys_DebugPrintf( "GLimp_GetSwapInterval: %s.\n", SDL_GetError() );
		return sys_error_t::ERR_GRAPHICS_OPERATION_FAILED;
	}

	intervalOut = static_cast<gl_swap_interval_t>( nativeInterval );
	return sys_error_t::OK;
}

sys_error_t GLimp_SwapWindow( window_t &window ) noexcept {
	if ( !Sys_IsInitialized() || !GLimp_IsCurrent() ) {
		return sys_error_t::ERR_NOT_INIT;
	}
	if ( !Sys_WindowIsValid( window ) || window.graphicsApi != window_graphics_api_t::OPENGL || window.id != glimpState.windowId || GLimp_GetNativeWindow( window ) != glimpState.window ) {
		return sys_error_t::ERR_INVALID_ARGUMENT;
	}

	if ( !SDL_GL_SwapWindow( glimpState.window ) ) {
		Sys_DebugPrintf( "GLimp_SwapWindow: %s.\n", SDL_GetError() );
		return sys_error_t::ERR_GRAPHICS_OPERATION_FAILED;
	}

	return sys_error_t::OK;
}

} // namespace cypher::engine::sys
