//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL_Buffer.h
//  Purpose: Declares private OpenGL implementations of renderer buffer calls.
//  Details: Native OpenGL names remain behind the generic backend buffer token.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_OPENGL_BUFFER_H
#define CYPHER_ENGINE_RENDER_OPENGL_BUFFER_H
#pragma once

#include "CypherRender/CypherRender_Backend.h"

namespace cypher::engine::render
{

CYPHER_NODISCARD render_error_t GL_CreateBuffer(
    const render_buffer_desc_t &description,
    const render_buffer_data_t *initialData,
    backend_buffer_t &bufferOut,
    void *backendState ) noexcept;

CYPHER_NODISCARD render_error_t GL_UpdateBuffer(
    backend_buffer_t buffer,
    ::cypher::common::u64 destinationOffset,
    const render_buffer_data_t &sourceData,
    void *backendState ) noexcept;

CYPHER_NODISCARD render_error_t GL_MapBuffer(
    backend_buffer_t buffer,
    const render_buffer_range_t &range,
    render_buffer_map_flags_t access,
    render_buffer_mapping_t &mappingOut,
    void *backendState ) noexcept;

CYPHER_NODISCARD render_error_t GL_FlushMappedBuffer(
    backend_buffer_t buffer,
    const render_buffer_range_t &range,
    void *backendState ) noexcept;

CYPHER_NODISCARD render_error_t GL_InvalidateMappedBuffer(
    backend_buffer_t buffer,
    const render_buffer_range_t &range,
    void *backendState ) noexcept;

CYPHER_NODISCARD render_error_t GL_UnmapBuffer(
    backend_buffer_t buffer,
    void *backendState ) noexcept;

CYPHER_NODISCARD render_error_t GL_DestroyBuffer(
    backend_buffer_t buffer,
    void *backendState ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_OPENGL_BUFFER_H
