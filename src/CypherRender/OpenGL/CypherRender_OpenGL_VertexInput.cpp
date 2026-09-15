//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL_VertexInput.cpp
//  Purpose: Implements OpenGL vertex-array object creation and destruction.
//  Details: OpenGL 4.1 captures attribute formats, source buffer names, strides,
//           divisors, and the optional element buffer in one VAO.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_OpenGL_VertexInput.h"

#include "CypherRender_OpenGL_Local.h"

#include <glad/gl.h>

#include <cstdint>
#include <limits>

namespace cypher::engine::render
{

namespace
{

struct gl_vertex_format_t {
    GLint componentCount{ 0 };
    GLenum scalarType{ 0u };
    GLboolean normalized{ GL_FALSE };
    bool integerInput{ false };
};

GLuint GL_VertexBufferName( const backend_buffer_t buffer ) noexcept
{
    return static_cast<GLuint>( buffer.value );
}

GLuint GL_VertexArrayName( const backend_vertex_input_t vertexInput ) noexcept
{
    return static_cast<GLuint>( vertexInput.value );
}

const backend_vertex_buffer_binding_t *GL_FindVertexBinding(
    const backend_vertex_input_desc_t &description,
    const ::cypher::common::u8 binding ) noexcept
{
    for ( ::cypher::common::u32 i = 0u;
          i < description.vertexBufferCount;
          ++i ) {
        if ( description.vertexBuffers[i].binding == binding ) {
            return &description.vertexBuffers[i];
        }
    }
    return nullptr;
}

bool GL_TranslateVertexFormat(
    const render_format_t format,
    gl_vertex_format_t &nativeOut ) noexcept
{
    nativeOut = {};
    render_vertex_format_info_t info{};
    if ( !R_GetVertexFormatInfo( format, &info ) ) return false;

    nativeOut.componentCount = static_cast<GLint>( info.componentCount );
    nativeOut.normalized = info.normalized ? GL_TRUE : GL_FALSE;
    nativeOut.integerInput = info.valueType != render_vertex_value_t::FLOAT;

    switch ( format ) {
        case render_format_t::R8_UNORM:
        case render_format_t::RG8_UNORM:
        case render_format_t::RGB8_UNORM:
        case render_format_t::RGBA8_UNORM:
        case render_format_t::R8_UINT:
        case render_format_t::RG8_UINT:
        case render_format_t::RGB8_UINT:
        case render_format_t::RGBA8_UINT:
            nativeOut.scalarType = GL_UNSIGNED_BYTE;
            break;

        case render_format_t::R8_SNORM:
        case render_format_t::RG8_SNORM:
        case render_format_t::RGB8_SNORM:
        case render_format_t::RGBA8_SNORM:
        case render_format_t::R8_SINT:
        case render_format_t::RG8_SINT:
        case render_format_t::RGB8_SINT:
        case render_format_t::RGBA8_SINT:
            nativeOut.scalarType = GL_BYTE;
            break;

        case render_format_t::R16_UNORM:
        case render_format_t::RG16_UNORM:
        case render_format_t::RGB16_UNORM:
        case render_format_t::RGBA16_UNORM:
        case render_format_t::R16_UINT:
        case render_format_t::RG16_UINT:
        case render_format_t::RGB16_UINT:
        case render_format_t::RGBA16_UINT:
            nativeOut.scalarType = GL_UNSIGNED_SHORT;
            break;

        case render_format_t::R16_SNORM:
        case render_format_t::RG16_SNORM:
        case render_format_t::RGB16_SNORM:
        case render_format_t::RGBA16_SNORM:
        case render_format_t::R16_SINT:
        case render_format_t::RG16_SINT:
        case render_format_t::RGB16_SINT:
        case render_format_t::RGBA16_SINT:
            nativeOut.scalarType = GL_SHORT;
            break;

        case render_format_t::R16_FLOAT:
        case render_format_t::RG16_FLOAT:
        case render_format_t::RGB16_FLOAT:
        case render_format_t::RGBA16_FLOAT:
            nativeOut.scalarType = GL_HALF_FLOAT;
            break;

        case render_format_t::R32_UINT:
        case render_format_t::RG32_UINT:
        case render_format_t::RGB32_UINT:
        case render_format_t::RGBA32_UINT:
            nativeOut.scalarType = GL_UNSIGNED_INT;
            break;

        case render_format_t::R32_SINT:
        case render_format_t::RG32_SINT:
        case render_format_t::RGB32_SINT:
        case render_format_t::RGBA32_SINT:
            nativeOut.scalarType = GL_INT;
            break;

        case render_format_t::R32_FLOAT:
        case render_format_t::RG32_FLOAT:
        case render_format_t::RGB32_FLOAT:
        case render_format_t::RGBA32_FLOAT:
            nativeOut.scalarType = GL_FLOAT;
            break;

        case render_format_t::RGB10A2_UNORM:
            nativeOut.scalarType = GL_UNSIGNED_INT_2_10_10_10_REV;
            nativeOut.integerInput = false;
            break;

        default:
            return false;
    }
    return true;
}

bool GL_VertexInputDescriptionFitsNative(
    const backend_vertex_input_desc_t &description ) noexcept
{
    if ( description.vertexBufferCount == 0u ||
         description.vertexBufferCount > R_MAX_VERTEX_BINDINGS ||
         R_ValidateVertexLayout( description.layout ) != render_error_t::OK ) {
        return false;
    }

    for ( ::cypher::common::u32 i = 0u;
          i < description.vertexBufferCount;
          ++i ) {
        const backend_vertex_buffer_binding_t &binding =
            description.vertexBuffers[i];
        if ( !R_IsBackendBufferValid( binding.buffer ) ||
             binding.buffer.value > std::numeric_limits<GLuint>::max() ||
             binding.offset > std::numeric_limits<std::uintptr_t>::max() ) {
            return false;
        }
    }

    return !R_IsBackendBufferValid( description.indexBuffer.buffer ) ||
        description.indexBuffer.buffer.value <=
            std::numeric_limits<GLuint>::max();
}

} // namespace

render_error_t GL_CreateVertexInput(
    const backend_vertex_input_desc_t &description,
    backend_vertex_input_t &vertexInputOut,
    void *backendState ) noexcept
{
    vertexInputOut = R_INVALID_BACKEND_VERTEX_INPUT;
    if ( backendState != &glState ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !glState.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( !GL_VertexInputDescriptionFitsNative( description ) ) {
        return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
    }

    GL_ClearErrors();
    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &previousVertexArray );
    glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer );

    GLuint vertexArray = 0u;
    glGenVertexArrays( 1, &vertexArray );
    glBindVertexArray( vertexArray );

    bool translationFailed = false;
    for ( ::cypher::common::u32 i = 0u;
          i < description.layout.attributeCount;
          ++i ) {
        const render_vertex_attribute_desc_t &attribute =
            description.layout.attributes[i];
        const render_vertex_binding_desc_t *layoutBinding = nullptr;
        for ( ::cypher::common::u32 bindingIndex = 0u;
              bindingIndex < description.layout.bindingCount;
              ++bindingIndex ) {
            if ( description.layout.bindings[bindingIndex].binding ==
                 attribute.binding ) {
                layoutBinding = &description.layout.bindings[bindingIndex];
                break;
            }
        }

        const backend_vertex_buffer_binding_t *bufferBinding =
            GL_FindVertexBinding( description, attribute.binding );
        gl_vertex_format_t nativeFormat{};
        if ( layoutBinding == nullptr || bufferBinding == nullptr ||
             !GL_TranslateVertexFormat( attribute.format, nativeFormat ) ||
             attribute.offset > ::cypher::common::CY_U64_MAX -
                bufferBinding->offset ) {
            translationFailed = true;
            break;
        }

        const ::cypher::common::u64 absoluteOffset =
            bufferBinding->offset + attribute.offset;
        if ( absoluteOffset > std::numeric_limits<std::uintptr_t>::max() ) {
            translationFailed = true;
            break;
        }

        glBindBuffer(
            GL_ARRAY_BUFFER,
            GL_VertexBufferName( bufferBinding->buffer ) );
        const void *attributeAddress = reinterpret_cast<const void *>(
            static_cast<std::uintptr_t>( absoluteOffset ) );
        if ( nativeFormat.integerInput ) {
            glVertexAttribIPointer(
                attribute.location,
                nativeFormat.componentCount,
                nativeFormat.scalarType,
                static_cast<GLsizei>( layoutBinding->stride ),
                attributeAddress );
        } else {
            glVertexAttribPointer(
                attribute.location,
                nativeFormat.componentCount,
                nativeFormat.scalarType,
                nativeFormat.normalized,
                static_cast<GLsizei>( layoutBinding->stride ),
                attributeAddress );
        }
        glEnableVertexAttribArray( attribute.location );
        glVertexAttribDivisor(
            attribute.location,
            layoutBinding->inputRate == render_vertex_input_rate_t::PER_INSTANCE
                ? layoutBinding->instanceDivisor
                : 0u );
    }

    if ( !translationFailed &&
         R_IsBackendBufferValid( description.indexBuffer.buffer ) ) {
        // The element-buffer name is VAO state. Its byte offset and index type
        // are supplied later by the indexed draw command.
        glBindBuffer(
            GL_ELEMENT_ARRAY_BUFFER,
            GL_VertexBufferName( description.indexBuffer.buffer ) );
    }

    const render_error_t operationResult = translationFailed
        ? render_error_t::ERR_VERTEX_LAYOUT_INVALID
        : GL_CheckErrors( glState.config.validation );

    glBindVertexArray( static_cast<GLuint>( previousVertexArray ) );
    glBindBuffer(
        GL_ARRAY_BUFFER,
        static_cast<GLuint>( previousArrayBuffer ) );
    const render_error_t restoreResult = GL_CheckErrors(
        glState.config.validation );

    if ( operationResult != render_error_t::OK ||
         restoreResult != render_error_t::OK || vertexArray == 0u ) {
        if ( vertexArray != 0u ) {
            glDeleteVertexArrays( 1, &vertexArray );
        }
        if ( operationResult != render_error_t::OK ) return operationResult;
        return restoreResult != render_error_t::OK
            ? restoreResult
            : render_error_t::ERR_RESOURCE_CREATE_FAILED;
    }

    vertexInputOut.value = vertexArray;
    return render_error_t::OK;
}

render_error_t GL_DestroyVertexInput(
    const backend_vertex_input_t vertexInput,
    void *backendState ) noexcept
{
    if ( backendState != &glState ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !glState.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( !R_IsBackendVertexInputValid( vertexInput ) ||
         vertexInput.value > std::numeric_limits<GLuint>::max() ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }

    const GLuint vertexArray = GL_VertexArrayName( vertexInput );
    GL_ClearErrors();
    glDeleteVertexArrays( 1, &vertexArray );
    const render_error_t result = GL_CheckErrors( glState.config.validation );
    return result == render_error_t::OK
        ? render_error_t::OK
        : render_error_t::ERR_RESOURCE_TRANSFER_FAILED;
}

} // namespace cypher::engine::render
