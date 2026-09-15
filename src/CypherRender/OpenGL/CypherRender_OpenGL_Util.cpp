//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL_Util.cpp
//  Purpose: Implements diagnostics shared by private OpenGL modules.
//  Details: Native error values are translated into the stable renderer domain.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_OpenGL_Local.h"

#include <glad/gl.h>

namespace cypher::engine::render
{

void GL_ClearErrors() noexcept
{
    while ( glGetError() != GL_NO_ERROR ) {
        // Establish a clean diagnostic boundary for the next checked block.
    }
}

render_error_t GL_CheckErrors( const render_validation_t validation ) noexcept
{
    // Even disabled validation must observe errors that determine whether a
    // native operation succeeded. The policy controls extra diagnostics, not
    // whether allocation and transfer failures are silently accepted.
    (void)validation;

    render_error_t result = render_error_t::OK;
    GLenum nativeError = GL_NO_ERROR;
    while ( ( nativeError = glGetError() ) != GL_NO_ERROR ) {
        if ( nativeError == GL_OUT_OF_MEMORY ) {
            result = render_error_t::ERR_OUT_OF_MEMORY;
        } else if ( result == render_error_t::OK ) {
            result = render_error_t::ERR_FAILED;
        }
    }
    return result;
}

} // namespace cypher::engine::render
