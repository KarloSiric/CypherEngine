//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Backend.h
//  Purpose: Declares the private renderer frontend/backend dispatch contract.
//  Details: The frontend calls one stable table while each compiled backend
//           supplies its own functions and opaque runtime state.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_BACKEND_H
#define CYPHER_ENGINE_RENDER_BACKEND_H

#pragma once

#include "CypherRender_Buffer.h"
#include "CypherRender_Texture.h"
#include "CypherRender_HostSurface.h"
#include "CypherRender_Shader.h"
#include "CypherRender_Pipeline.h"
#include "CypherRender_VertexInput.h"
#include "CypherSystem/CypherSystem_Window.h"

namespace cypher::engine::render
{

/*
===============================================================================

    Private renderer backend ABI

The frontend owns call ordering and validation. A backend owns native graphics
objects and receives its opaque state pointer on every call. Keeping that state
outside the frontend lets OpenGL, software, and Vulkan use unrelated internal
layouts without exposing them to Host.

Version six adds immutable 2D textures and one sampled binding per draw. Linked
shaders, immutable graphics pipelines, immediate indexed draws, and all native
object tokens remain behind this private boundary.

===============================================================================
*/
inline constexpr ::cypher::common::u32 R_BACKEND_API_VERSION = 6u;

/*
================
Backend buffer token

The frontend owns public generational handles. This token identifies the
corresponding native object inside the active backend. Zero remains invalid for
OpenGL names, software allocation records, and future Vulkan object records.
================
*/
struct backend_buffer_t {
    ::cypher::common::u64 value{ 0u };
};

inline constexpr backend_buffer_t R_INVALID_BACKEND_BUFFER{};

CYPHER_NODISCARD constexpr bool R_IsBackendBufferValid(
    const backend_buffer_t buffer ) noexcept
{
    return buffer.value != R_INVALID_BACKEND_BUFFER.value;
}

/*
================
Backend vertex-input token and descriptor

The frontend translates public buffer handles into native backend tokens before
dispatch. No backend is permitted to retain pointers into the public descriptor.
================
*/
struct backend_vertex_input_t {
    ::cypher::common::u64 value{ 0u };
};

inline constexpr backend_vertex_input_t R_INVALID_BACKEND_VERTEX_INPUT{};

CYPHER_NODISCARD constexpr bool R_IsBackendVertexInputValid(
    const backend_vertex_input_t vertexInput ) noexcept
{
    return vertexInput.value != R_INVALID_BACKEND_VERTEX_INPUT.value;
}

struct backend_vertex_buffer_binding_t {
    backend_buffer_t buffer{};
    ::cypher::common::u64 offset{ 0u };
    ::cypher::common::u8 binding{ 0u };
};

struct backend_index_buffer_binding_t {
    backend_buffer_t buffer{};
    ::cypher::common::u64 offset{ 0u };
    render_index_type_t type{ render_index_type_t::UINT16 };
};

struct backend_vertex_input_desc_t {
    render_vertex_layout_t layout{};
    backend_vertex_buffer_binding_t vertexBuffers[R_MAX_VERTEX_BINDINGS]{};
    ::cypher::common::u32 vertexBufferCount{ 0u };
    backend_index_buffer_binding_t indexBuffer{};
    const char *debugName{ nullptr };
};

/*
================
Backend shader token

Identifies one complete backend-owned shader program. The frontend stores this
token in its shader record and exposes a separate generational public handle.

For OpenGL, the token identifies the linked program. Temporary stage objects
remain private to shader creation.

Zero is reserved for an invalid token. The helper below checks only that
sentinel; frontend ownership and handle validation establish object lifetime.
================
*/
struct backend_shader_t {
    ::cypher::common::u64 value{ 0u };
};

inline constexpr backend_shader_t R_INVALID_BACKEND_SHADER{};

CYPHER_NODISCARD constexpr bool R_IsBackendShaderValid(
    const backend_shader_t shader ) noexcept
{
    return shader.value != R_INVALID_BACKEND_SHADER.value;
}

struct backend_texture_t {
    ::cypher::common::u64 value{ 0u };
};
inline constexpr backend_texture_t R_INVALID_BACKEND_TEXTURE{};

struct backend_pipeline_t {
    ::cypher::common::u64 value{ 0u };
};

inline constexpr backend_pipeline_t R_INVALID_BACKEND_PIPELINE{};

CYPHER_NODISCARD constexpr bool R_IsBackendPipelineValid(
    const backend_pipeline_t pipeline ) noexcept
{
    return pipeline.value != R_INVALID_BACKEND_PIPELINE.value;
}

// Names are borrowed for the duration of creation; no pointer is retained.
struct backend_pipeline_desc_t {
    backend_shader_t shader{};
    render_vertex_layout_t vertexLayout{};
    bool depthTest{ true };
    bool depthWrite{ true };
    bool cullBackFaces{ true };
    bool frontCounterClockwise{ true };
    bool alphaBlend{ false };
    const char *uniformBlockName{ nullptr };
    ::cypher::common::u32 uniformBlockBytes{ 0u };
    const char *debugName{ nullptr };
    const char *sampledTextureName{ nullptr }; // Optional sampler2D at unit zero.
};

struct backend_draw_indexed_desc_t {
    backend_pipeline_t pipeline{};
    backend_vertex_input_t vertexInput{};
    backend_buffer_t uniformBuffer{};
    backend_texture_t sampledTexture{};
    render_index_type_t indexType{ render_index_type_t::UINT16 };
    ::cypher::common::u64 indexByteOffset{ 0u };
    ::cypher::common::u32 indexCount{ 0u };
    ::cypher::common::u32 vertexCount{ 0u };
};

// Selects graphics-related native-window attributes before Sys_CreateWindow.
using backend_configure_window_fn_t = render_error_t (*)(
    const render_config_t &config,
    ::cypher::engine::sys::window_desc_t &windowDescription,
    void *backendState ) noexcept;

// Creates the native device/context and returns a stable capability snapshot.
using backend_init_fn_t = render_error_t (*)(
    ::cypher::engine::sys::window_t &window,
    const render_config_t &config,
    render_info_t &infoOut,
    void *backendState ) noexcept;

// Initializes against a host-owned graphics context and presentation surface.
using backend_init_host_surface_fn_t = render_error_t (*)(
    const render_host_surface_desc_t &surface,
    const render_config_t &config,
    render_info_t &infoOut,
    void *backendState ) noexcept;

// Destroys every backend-owned object while the Host-owned window still lives.
using backend_shutdown_fn_t =
    render_error_t (*)( void *backendState ) noexcept;

// Activates the frame and applies default-target viewport and clear state.
using backend_begin_frame_fn_t = render_error_t (*)(
    const render_frame_info_t &frameInfo,
    void *backendState ) noexcept;

// Updates default presentation dimensions outside an active frame.
using backend_resize_fn_t = render_error_t (*)(
    const render_extent_t &drawableExtent,
    void *backendState ) noexcept;

// Completes and presents the active frame; the frame is consumed on failure.
using backend_end_frame_fn_t =
    render_error_t (*)( void *backendState ) noexcept;

// Applies a presentation policy and reports the actual fallback selected.
using backend_set_present_mode_fn_t = render_error_t (*)(
    render_present_mode_t presentMode,
    render_present_mode_t &actualModeOut,
    void *backendState ) noexcept;

// Blocks until previously submitted native graphics work is complete.
using backend_wait_idle_fn_t =
    render_error_t (*)( void *backendState ) noexcept;

// Allocates native storage and optionally consumes complete initial contents.
using backend_create_buffer_fn_t = render_error_t (*)(
    const render_buffer_desc_t &description,
    const render_buffer_data_t *initialData,
    backend_buffer_t &bufferOut,
    void *backendState ) noexcept;

// Replaces one byte range in an existing native buffer.
using backend_update_buffer_fn_t = render_error_t (*)(
    backend_buffer_t buffer,
    ::cypher::common::u64 destinationOffset,
    const render_buffer_data_t &sourceData,
    void *backendState ) noexcept;

// Exposes one native byte range to the CPU.
using backend_map_buffer_fn_t = render_error_t (*)(
    backend_buffer_t buffer,
    const render_buffer_range_t &range,
    render_buffer_map_flags_t access,
    render_buffer_mapping_t &mappingOut,
    void *backendState ) noexcept;

// Publishes CPU writes made through a mapped absolute buffer range.
using backend_flush_mapped_buffer_fn_t = render_error_t (*)(
    backend_buffer_t buffer,
    const render_buffer_range_t &range,
    void *backendState ) noexcept;

// Makes native-device writes observable through mapped CPU memory.
using backend_invalidate_mapped_buffer_fn_t = render_error_t (*)(
    backend_buffer_t buffer,
    const render_buffer_range_t &range,
    void *backendState ) noexcept;

// Ends the active native mapping and invalidates its returned address.
using backend_unmap_buffer_fn_t = render_error_t (*)(
    backend_buffer_t buffer,
    void *backendState ) noexcept;

// Releases the native object represented by the backend token.
using backend_destroy_buffer_fn_t = render_error_t (*)(
    backend_buffer_t buffer,
    void *backendState ) noexcept;

// Materializes one validated layout and native buffer-binding package.
using backend_create_vertex_input_fn_t = render_error_t (*)(
    const backend_vertex_input_desc_t &description,
    backend_vertex_input_t &vertexInputOut,
    void *backendState ) noexcept;

// Releases the backend-specific input object, such as an OpenGL VAO.
using backend_destroy_vertex_input_fn_t = render_error_t (*)(
    backend_vertex_input_t vertexInput,
    void *backendState ) noexcept;

/*
================
Backend shader creation

Consumes the cooked view, its referenced stage bytes, and the optional debug
name synchronously. Neither the descriptor nor borrowed pointers may be
retained after the call returns. Cooked stage sizes include one trailing NUL;
native source upload uses an explicit length excluding that terminator.

Success publishes one complete backend-owned program through shaderOut.
Failure leaves shaderOut invalid and releases objects created by the attempt.
The frontend owns destruction if later registration of that program fails.
================
*/
using backend_create_shader_fn_t = render_error_t (*)(
    const render_shader_desc_t &description,
    backend_shader_t &shaderOut,
    void *backendState ) noexcept;

/*
================
Backend shader destruction

Releases the program represented by the token. The frontend must first ensure
that no live renderer object retains a dependency on this shader.
================
*/
using backend_destroy_shader_fn_t = render_error_t (*)(
    backend_shader_t shader,
    void *backendState ) noexcept;

using backend_create_graphics_pipeline_fn_t = render_error_t (*)(
    const backend_pipeline_desc_t &description,
    backend_pipeline_t &pipelineOut,
    void *backendState ) noexcept;

using backend_destroy_graphics_pipeline_fn_t = render_error_t (*)(
    backend_pipeline_t pipeline,
    void *backendState ) noexcept;

using backend_draw_indexed_fn_t = render_error_t (*)(
    const backend_draw_indexed_desc_t &description,
    void *backendState ) noexcept;

using backend_create_texture_2d_fn_t = render_error_t (*)(
    const render_texture_desc_t &, const render_texture_data_t &,
    backend_texture_t &, void * ) noexcept;
using backend_destroy_texture_fn_t = render_error_t (*)(
    backend_texture_t, void * ) noexcept;

struct backend_api_t {
    ::cypher::common::u32 apiVersion{ 0u };              // Must equal R_BACKEND_API_VERSION.
    ::cypher::common::u32 structSize{ 0u };              // Exact table size rejects ABI layout mismatches.
    render_backend_t backend{ render_backend_t::AUTO };  // Concrete backend; AUTO is invalid in a table.
    const char *name{ nullptr };                         // Static diagnostic name owned by the backend.

    backend_configure_window_fn_t ConfigureWindow{ nullptr }; // Pre-window native attribute selection.
    backend_init_fn_t Init{ nullptr };                         // Device/context creation and discovery.
    backend_init_host_surface_fn_t InitHostSurface{ nullptr }; // Borrows an editor-owned device/context.
    backend_shutdown_fn_t Shutdown{ nullptr };                 // Final backend-owned object destruction.
    backend_begin_frame_fn_t BeginFrame{ nullptr };             // Opens one frame and clears attachments.
    backend_resize_fn_t Resize{ nullptr };                      // Applies a drawable-size transition.
    backend_end_frame_fn_t EndFrame{ nullptr };                 // Ends and presents one open frame.
    backend_set_present_mode_fn_t SetPresentMode{ nullptr };    // Changes swap/presentation policy.
    backend_wait_idle_fn_t WaitIdle{ nullptr };                 // Drains outstanding native work.

    backend_create_buffer_fn_t CreateBuffer{ nullptr };         // Allocates native byte storage.
    backend_update_buffer_fn_t UpdateBuffer{ nullptr };         // Uploads one byte range.
    backend_map_buffer_fn_t MapBuffer{ nullptr };               // Maps one CPU-visible range.
    backend_flush_mapped_buffer_fn_t FlushMappedBuffer{ nullptr }; // Publishes mapped CPU writes.
    backend_invalidate_mapped_buffer_fn_t InvalidateMappedBuffer{ nullptr }; // Refreshes mapped CPU reads.
    backend_unmap_buffer_fn_t UnmapBuffer{ nullptr };           // Ends one active mapping.
    backend_destroy_buffer_fn_t DestroyBuffer{ nullptr };       // Releases native storage.

    backend_create_vertex_input_fn_t CreateVertexInput{ nullptr }; // Creates native vertex-input state.
    backend_destroy_vertex_input_fn_t DestroyVertexInput{ nullptr }; // Releases native vertex-input state.

    backend_create_shader_fn_t CreateShader{ nullptr };
    backend_destroy_shader_fn_t DestroyShader{ nullptr };
    backend_create_graphics_pipeline_fn_t CreateGraphicsPipeline{ nullptr };
    backend_destroy_graphics_pipeline_fn_t DestroyGraphicsPipeline{ nullptr };
    backend_draw_indexed_fn_t DrawIndexed{ nullptr };

    // Both callbacks may be absent for a backend without sampled images.
    backend_create_texture_2d_fn_t CreateTexture2D{ nullptr };
    backend_destroy_texture_fn_t DestroyTexture{ nullptr };

    void *state{ nullptr }; // Backend-owned runtime passed back unchanged to every callback.
};

inline constexpr ::cypher::common::u32 R_BACKEND_API_SIZE =
    static_cast<::cypher::common::u32>( sizeof( backend_api_t ) );

CYPHER_NODISCARD bool R_IsBackendValid(
    const backend_api_t *backend ) noexcept;

CYPHER_NODISCARD render_error_t R_SelectBackend(
    const render_config_t &config,
    const backend_api_t **backendOut ) noexcept;

}           // namespace cypher::engine::render

#endif      //  CYPHER_ENGINE_RENDER_BACKEND_H
