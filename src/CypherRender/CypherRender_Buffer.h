//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Buffer.h
//  Purpose: Declares backend-neutral renderer buffer types and operations.
//  Details: Renderer buffers contain vertex, index, uniform, and transfer data.
//           Native OpenGL buffer names remain private to the OpenGL backend.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_BUFFER_H
#define CYPHER_ENGINE_RENDER_BUFFER_H
#pragma once

#include "CypherRender_Error.h"
#include "CypherRender_Types.h"

namespace cypher::engine::render
{

/*
===============================================================================

    Renderer buffers

Buffers are untyped byte-storage objects. Their actual interpretation comes
from usage flags, vertex layouts, binding descriptions, and draw commands.

The public renderer owns render_buffer_handle_t values. Graphics backends own
the corresponding native objects, such as an OpenGL buffer name or VkBuffer.

All buffer operations must execute on the renderer thread unless a later
upload-queue API explicitly documents otherwise.

===============================================================================
*/

using render_buffer_handle_t = render_handle_t;

inline constexpr render_buffer_handle_t R_INVALID_BUFFER{};

/*
================
Buffer usage flags

Multiple usages may be combined. These values describe legal operations and
must never be translated by callers into native OpenGL or Vulkan constants.
================
*/
using render_buffer_usage_flags_t = ::cypher::common::flags32_t;

enum render_buffer_usage_flag_t : render_buffer_usage_flags_t {
    R_BUFFER_USAGE_NONE                 = 0u,
    R_BUFFER_USAGE_VERTEX               = CYPHER_BIT32( 0 ), // Supplies vertex attributes.
    R_BUFFER_USAGE_INDEX                = CYPHER_BIT32( 1 ), // Supplies unsigned vertex indices.
    R_BUFFER_USAGE_UNIFORM              = CYPHER_BIT32( 2 ), // Supplies shader constant blocks.
    R_BUFFER_USAGE_STORAGE              = CYPHER_BIT32( 3 ), // General shader-readable/writable storage.
    R_BUFFER_USAGE_INDIRECT             = CYPHER_BIT32( 4 ), // Contains indirect draw arguments.
    R_BUFFER_USAGE_TRANSFER_SOURCE      = CYPHER_BIT32( 5 ), // May be copied from by GPU commands.
    R_BUFFER_USAGE_TRANSFER_DESTINATION = CYPHER_BIT32( 6 ), // May be copied into by GPU commands.
    R_BUFFER_USAGE_TEXEL                = CYPHER_BIT32( 7 )  // May be viewed as formatted texel data.
};

inline constexpr render_buffer_usage_flags_t R_BUFFER_USAGE_KNOWN_MASK =
    R_BUFFER_USAGE_VERTEX |
    R_BUFFER_USAGE_INDEX |
    R_BUFFER_USAGE_UNIFORM |
    R_BUFFER_USAGE_STORAGE |
    R_BUFFER_USAGE_INDIRECT |
    R_BUFFER_USAGE_TRANSFER_SOURCE |
    R_BUFFER_USAGE_TRANSFER_DESTINATION |
    R_BUFFER_USAGE_TEXEL;

/*
================
Buffer update policy

This describes expected update frequency. It is a performance hint and does
not determine whether the allocation is CPU-visible.
================
*/
enum class render_buffer_update_t : ::cypher::common::u8 {
    IMMUTABLE = 0u, // CPU updates are forbidden after creation; device writes remain legal.
    DYNAMIC,        // Updated occasionally during normal execution.
    STREAM,         // Updated every frame or many times during a frame.
    COUNT           // Number of policies; never legal in a descriptor.
};

/*
================
Buffer memory domain

The memory domain describes the intended direction of data movement. Backends
translate this policy into native heaps, memory properties, or buffer modes.
================
*/
enum class render_buffer_memory_t : ::cypher::common::u8 {
    DEVICE_LOCAL = 0u, // GPU-optimized memory; direct CPU mapping is forbidden.
    UPLOAD,            // CPU-writable memory used to provide data to the GPU.
    READBACK,          // CPU-readable memory used to retrieve GPU results.
    COUNT              // Number of domains; never legal in a descriptor.
};

/*
================
Buffer mapping flags

READ and WRITE are independent because a backend may support read/write mapped
memory. Additional mapping policies can be added only when a real use requires
them; persistent mapping is deliberately absent from the OpenGL 4.1 contract.
================
*/
using render_buffer_map_flags_t = ::cypher::common::flags32_t;

enum render_buffer_map_flag_t : render_buffer_map_flags_t {
    R_BUFFER_MAP_NONE  = 0u,
    R_BUFFER_MAP_READ  = CYPHER_BIT32( 0 ), // CPU intends to read mapped bytes.
    R_BUFFER_MAP_WRITE = CYPHER_BIT32( 1 )  // CPU intends to modify mapped bytes.
};

inline constexpr render_buffer_map_flags_t R_BUFFER_MAP_KNOWN_MASK =
    R_BUFFER_MAP_READ | R_BUFFER_MAP_WRITE;

/*
================
Buffer descriptor

The descriptor contains only backend-neutral policy. A backend may add native
usage bits internally when required to implement the requested behavior.
================
*/
struct render_buffer_desc_t {
    ::cypher::common::u64 byteSize{ 0u }; // Exact logical storage size.

    render_buffer_usage_flags_t usage{
        R_BUFFER_USAGE_NONE }; // Every operation for which the buffer may be used.

    render_buffer_update_t updatePolicy{
        render_buffer_update_t::IMMUTABLE }; // Expected content replacement rate.

    render_buffer_memory_t memory{
        render_buffer_memory_t::DEVICE_LOCAL }; // Preferred memory placement.

    const char *debugName{
        nullptr }; // Optional borrowed UTF-8 label, consumed during creation.
};

/*
================
Buffer data

Source bytes remain owned by the caller. R_CreateBuffer and R_UpdateBuffer must
copy or consume the data before returning and must never retain this pointer.
================
*/
struct render_buffer_data_t {
    const void *bytes{ nullptr };          // First readable source byte.
    ::cypher::common::u64 byteSize{ 0u };  // Number of readable source bytes.
};

/*
================
Buffer range

Offsets are measured from the beginning of the buffer. Zero-sized ranges are
invalid; every operation verifies that offset + byteSize does not overflow and
does not exceed the buffer allocation.
================
*/
struct render_buffer_range_t {
    ::cypher::common::u64 offset{ 0u };    // First participating buffer byte.
    ::cypher::common::u64 byteSize{ 0u };  // Number of participating bytes.
};

/*
================
Mapped buffer memory

The returned address remains valid only until R_UnmapBuffer. It points to the
first byte of the requested range rather than the beginning of the buffer.
================
*/
struct render_buffer_mapping_t {
    void *bytes{ nullptr };                // CPU-visible address of mapped data.
    ::cypher::common::u64 byteSize{ 0u };  // Number of accessible mapped bytes.
};

/*
================
Buffer information

This is a backend-neutral snapshot maintained by the renderer frontend. It
never exposes GLuint, VkBuffer, native pointers, or backend allocation objects.
================
*/
struct render_buffer_info_t {
    ::cypher::common::u64 byteSize{ 0u };
    render_buffer_usage_flags_t usage{ R_BUFFER_USAGE_NONE };
    render_buffer_update_t updatePolicy{ render_buffer_update_t::IMMUTABLE };
    render_buffer_memory_t memory{ render_buffer_memory_t::DEVICE_LOCAL };

    render_buffer_map_flags_t mapAccess{ R_BUFFER_MAP_NONE };
    render_buffer_range_t mappedRange{};
    bool mapped{ false };
};

/*
===============================================================================

    Public buffer operations

===============================================================================
*/

// Validates backend-independent descriptor and initial-data invariants.
CYPHER_NODISCARD render_error_t R_ValidateBufferDesc(
    const render_buffer_desc_t &description,
    const render_buffer_data_t *initialData ) noexcept;

// Creates frontend metadata and one native object in the selected backend.
CYPHER_NODISCARD render_error_t R_CreateBuffer(
    const render_buffer_desc_t &description,
    const render_buffer_data_t *initialData,
    CY_OUT render_buffer_handle_t *bufferOut ) noexcept;

// Copies CPU data into an existing mutable buffer at destinationOffset.
CYPHER_NODISCARD render_error_t R_UpdateBuffer(
    render_buffer_handle_t buffer,
    ::cypher::common::u64 destinationOffset,
    const render_buffer_data_t &sourceData ) noexcept;

// Maps one CPU-visible range. Only one active mapping is allowed per buffer.
CYPHER_NODISCARD render_error_t R_MapBuffer(
    render_buffer_handle_t buffer,
    const render_buffer_range_t &range,
    render_buffer_map_flags_t access,
    CY_OUT render_buffer_mapping_t *mappingOut ) noexcept;

// Publishes CPU writes for an absolute range contained by the active mapping.
CYPHER_NODISCARD render_error_t R_FlushMappedBuffer(
    render_buffer_handle_t buffer,
    const render_buffer_range_t &range ) noexcept;

// Refreshes CPU reads for an absolute range contained by the active mapping.
CYPHER_NODISCARD render_error_t R_InvalidateMappedBuffer(
    render_buffer_handle_t buffer,
    const render_buffer_range_t &range ) noexcept;

// Ends the active mapping and invalidates its returned CPU address.
CYPHER_NODISCARD render_error_t R_UnmapBuffer(
    render_buffer_handle_t buffer ) noexcept;

// Invalidates the public handle; native release may be deferred until GPU-safe.
CYPHER_NODISCARD render_error_t R_DestroyBuffer(
    render_buffer_handle_t buffer ) noexcept;

// Checks the frontend slot, generation, object type, and live state.
CYPHER_NODISCARD bool R_IsBufferValid(
    render_buffer_handle_t buffer ) noexcept;

// Returns backend-neutral metadata for a live buffer.
CYPHER_NODISCARD render_error_t R_GetBufferInfo(
    render_buffer_handle_t buffer,
    CY_OUT render_buffer_info_t *infoOut ) noexcept;

}       // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_BUFFER_H
