//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL_Buffer.cpp
//  Purpose: Implements native OpenGL renderer-buffer operations.
//  Details: Generic backend tokens carry OpenGL names while public handles and
//           mapping policy remain owned by the renderer frontend.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_OpenGL_Buffer.h"

#include "CypherRender_OpenGL_Local.h"

#include <glad/gl.h>

#include <limits>

namespace cypher::engine::render
{

namespace
{

render_error_t GL_ValidateBufferCall(
    const backend_buffer_t buffer,
    const void *backendState ) noexcept
{
    if ( backendState != &glState ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !glState.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( !R_IsBackendBufferValid( buffer ) ||
         buffer.value > std::numeric_limits<GLuint>::max() ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    return render_error_t::OK;
}

bool GL_BufferRangeFitsNative(
    const ::cypher::common::u64 offset,
    const ::cypher::common::u64 byteSize ) noexcept
{
    const auto nativeMaximum = static_cast<::cypher::common::u64>(
        std::numeric_limits<GLsizeiptr>::max() );
    return offset <= nativeMaximum && byteSize <= nativeMaximum;
}

GLuint GL_BufferName( const backend_buffer_t buffer ) noexcept
{
    return static_cast<GLuint>( buffer.value );
}

GLenum GL_BufferUsageHint( const render_buffer_desc_t &description ) noexcept
{
    const bool readback = description.memory == render_buffer_memory_t::READBACK;
    switch ( description.updatePolicy ) {
        case render_buffer_update_t::IMMUTABLE:
            return readback ? GL_STATIC_READ : GL_STATIC_DRAW;
        case render_buffer_update_t::DYNAMIC:
            return readback ? GL_DYNAMIC_READ : GL_DYNAMIC_DRAW;
        case render_buffer_update_t::STREAM:
            return readback ? GL_STREAM_READ : GL_STREAM_DRAW;
        case render_buffer_update_t::COUNT:
        default:
            return 0u;
    }
}

render_error_t GL_RestoreBufferBinding(
    const GLint previousBinding,
    const render_validation_t validation ) noexcept
{
    glBindBuffer(
        GL_COPY_WRITE_BUFFER,
        static_cast<GLuint>( previousBinding ) );
    return GL_CheckErrors( validation );
}

} // namespace

render_error_t GL_CreateBuffer(
    const render_buffer_desc_t &description,
    const render_buffer_data_t *initialData,
    backend_buffer_t &bufferOut,
    void *backendState ) noexcept
{
    bufferOut = R_INVALID_BACKEND_BUFFER;
    if ( backendState != &glState ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !glState.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( !GL_BufferRangeFitsNative( 0u, description.byteSize ) ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }

    const GLenum usageHint = GL_BufferUsageHint( description );
    if ( usageHint == 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    GL_ClearErrors();
    GLint previousBinding = 0;
    // The generated GL 4.1 header aliases the binding query to the target token.
    glGetIntegerv( GL_COPY_WRITE_BUFFER, &previousBinding );

    GLuint nativeBuffer = 0u;
    glGenBuffers( 1, &nativeBuffer );
    glBindBuffer( GL_COPY_WRITE_BUFFER, nativeBuffer );
    glBufferData(
        GL_COPY_WRITE_BUFFER,
        static_cast<GLsizeiptr>( description.byteSize ),
        initialData != nullptr ? initialData->bytes : nullptr,
        usageHint );

    const render_error_t operationResult = GL_CheckErrors(
        glState.config.validation );
    const render_error_t restoreResult = GL_RestoreBufferBinding(
        previousBinding,
        glState.config.validation );
    if ( operationResult != render_error_t::OK ||
         restoreResult != render_error_t::OK || nativeBuffer == 0u ) {
        if ( nativeBuffer != 0u ) {
            glDeleteBuffers( 1, &nativeBuffer );
        }
        return operationResult != render_error_t::OK
            ? operationResult
            : restoreResult != render_error_t::OK
                ? restoreResult
                : render_error_t::ERR_RESOURCE_CREATE_FAILED;
    }

    bufferOut.value = nativeBuffer;
    return render_error_t::OK;
}

render_error_t GL_UpdateBuffer(
    const backend_buffer_t buffer,
    const ::cypher::common::u64 destinationOffset,
    const render_buffer_data_t &sourceData,
    void *backendState ) noexcept
{
    const render_error_t validationResult = GL_ValidateBufferCall(
        buffer,
        backendState );
    if ( validationResult != render_error_t::OK ) {
        return validationResult;
    }
    if ( sourceData.bytes == nullptr || sourceData.byteSize == 0u ||
         !GL_BufferRangeFitsNative( destinationOffset, sourceData.byteSize ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    GL_ClearErrors();
    GLint previousBinding = 0;
    glGetIntegerv( GL_COPY_WRITE_BUFFER, &previousBinding );
    glBindBuffer( GL_COPY_WRITE_BUFFER, GL_BufferName( buffer ) );
    glBufferSubData(
        GL_COPY_WRITE_BUFFER,
        static_cast<GLintptr>( destinationOffset ),
        static_cast<GLsizeiptr>( sourceData.byteSize ),
        sourceData.bytes );

    const render_error_t operationResult = GL_CheckErrors(
        glState.config.validation );
    const render_error_t restoreResult = GL_RestoreBufferBinding(
        previousBinding,
        glState.config.validation );
    return operationResult != render_error_t::OK
        ? render_error_t::ERR_RESOURCE_TRANSFER_FAILED
        : restoreResult;
}

render_error_t GL_MapBuffer(
    const backend_buffer_t buffer,
    const render_buffer_range_t &range,
    const render_buffer_map_flags_t access,
    render_buffer_mapping_t &mappingOut,
    void *backendState ) noexcept
{
    mappingOut = {};
    const render_error_t validationResult = GL_ValidateBufferCall(
        buffer,
        backendState );
    if ( validationResult != render_error_t::OK ) {
        return validationResult;
    }
    if ( range.byteSize == 0u ||
         !GL_BufferRangeFitsNative( range.offset, range.byteSize ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    GLbitfield nativeAccess = 0u;
    if ( ( access & R_BUFFER_MAP_READ ) != 0u ) nativeAccess |= GL_MAP_READ_BIT;
    if ( ( access & R_BUFFER_MAP_WRITE ) != 0u ) nativeAccess |= GL_MAP_WRITE_BIT;
    if ( nativeAccess == 0u || ( access & ~R_BUFFER_MAP_KNOWN_MASK ) != 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    GL_ClearErrors();
    GLint previousBinding = 0;
    glGetIntegerv( GL_COPY_WRITE_BUFFER, &previousBinding );
    glBindBuffer( GL_COPY_WRITE_BUFFER, GL_BufferName( buffer ) );
    void *mappedBytes = glMapBufferRange(
        GL_COPY_WRITE_BUFFER,
        static_cast<GLintptr>( range.offset ),
        static_cast<GLsizeiptr>( range.byteSize ),
        nativeAccess );

    const render_error_t operationResult = GL_CheckErrors(
        glState.config.validation );
    const render_error_t restoreResult = GL_RestoreBufferBinding(
        previousBinding,
        glState.config.validation );
    if ( operationResult != render_error_t::OK ||
         restoreResult != render_error_t::OK || mappedBytes == nullptr ) {
        return render_error_t::ERR_RESOURCE_TRANSFER_FAILED;
    }

    mappingOut.bytes = mappedBytes;
    mappingOut.byteSize = range.byteSize;
    return render_error_t::OK;
}

render_error_t GL_FlushMappedBuffer(
    const backend_buffer_t buffer,
    const render_buffer_range_t &range,
    void *backendState ) noexcept
{
    const render_error_t validationResult = GL_ValidateBufferCall(
        buffer,
        backendState );
    if ( validationResult != render_error_t::OK ) {
        return validationResult;
    }
    if ( range.byteSize == 0u ||
         !GL_BufferRangeFitsNative( range.offset, range.byteSize ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    // OpenGL 4.1 mappings in this backend are non-persistent and do not use
    // GL_MAP_FLUSH_EXPLICIT_BIT. Unmap publishes all CPU writes atomically.
    return render_error_t::OK;
}

render_error_t GL_InvalidateMappedBuffer(
    const backend_buffer_t buffer,
    const render_buffer_range_t &range,
    void *backendState ) noexcept
{
    const render_error_t validationResult = GL_ValidateBufferCall(
        buffer,
        backendState );
    if ( validationResult != render_error_t::OK ) {
        return validationResult;
    }
    if ( range.byteSize == 0u ||
         !GL_BufferRangeFitsNative( range.offset, range.byteSize ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    // A non-persistent OpenGL read mapping synchronizes when it is created.
    // There is no separate cache invalidation operation in this GL contract.
    return render_error_t::OK;
}

render_error_t GL_UnmapBuffer(
    const backend_buffer_t buffer,
    void *backendState ) noexcept
{
    const render_error_t validationResult = GL_ValidateBufferCall(
        buffer,
        backendState );
    if ( validationResult != render_error_t::OK ) {
        return validationResult;
    }

    GL_ClearErrors();
    GLint previousBinding = 0;
    glGetIntegerv( GL_COPY_WRITE_BUFFER, &previousBinding );
    glBindBuffer( GL_COPY_WRITE_BUFFER, GL_BufferName( buffer ) );
    const GLboolean contentsValid = glUnmapBuffer( GL_COPY_WRITE_BUFFER );

    const render_error_t operationResult = GL_CheckErrors(
        glState.config.validation );
    const render_error_t restoreResult = GL_RestoreBufferBinding(
        previousBinding,
        glState.config.validation );
    if ( operationResult != render_error_t::OK || contentsValid != GL_TRUE ) {
        return render_error_t::ERR_RESOURCE_TRANSFER_FAILED;
    }
    return restoreResult;
}

render_error_t GL_DestroyBuffer(
    const backend_buffer_t buffer,
    void *backendState ) noexcept
{
    const render_error_t validationResult = GL_ValidateBufferCall(
        buffer,
        backendState );
    if ( validationResult != render_error_t::OK ) {
        return validationResult;
    }

    const GLuint nativeBuffer = GL_BufferName( buffer );
    GL_ClearErrors();
    glDeleteBuffers( 1, &nativeBuffer );
    const render_error_t result = GL_CheckErrors( glState.config.validation );
    return result == render_error_t::OK
        ? render_error_t::OK
        : render_error_t::ERR_RESOURCE_TRANSFER_FAILED;
}

} // namespace cypher::engine::render
