//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Uploads one validated static indexed mesh to renderer objects.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_MESH_BRIDGE_H
#define CYPHER_ENGINE_RENDER_MESH_BRIDGE_H
#pragma once

#include "CypherRender/CypherRender_Draw.h"

namespace cypher::engine::render
{

// A single interleaved, per-vertex stream at binding zero. Bytes remain owned
// by the caller and need only survive the synchronous create call. Indices are
// native-endian UINT16 or UINT32 values; every value is checked before upload.
struct render_static_mesh_source_t {
    render_vertex_layout_t vertexLayout{};
    const void *vertexBytes{ nullptr };
    ::cypher::common::u64 vertexByteSize{ 0u };
    ::cypher::common::u32 vertexCount{ 0u };
    const void *indexBytes{ nullptr };
    ::cypher::common::u64 indexByteSize{ 0u };
    ::cypher::common::u32 indexCount{ 0u };
    render_index_type_t indexType{ render_index_type_t::UINT16 };
    const char *debugName{ nullptr }; // Borrowed only during creation.
};

// Caller-owned renderer handles. This is not a cooked-mesh file format, cache,
// or new backend object type. The vertex input retains both buffers while live.
struct render_static_mesh_bundle_t {
    render_buffer_handle_t vertexBuffer{};
    render_buffer_handle_t indexBuffer{};
    render_vertex_input_handle_t vertexInput{};
    render_vertex_layout_t vertexLayout{};
    ::cypher::common::u32 vertexCount{ 0u };
    ::cypher::common::u32 indexCount{ 0u };
    render_index_type_t indexType{ render_index_type_t::UINT16 };
};

enum class render_static_mesh_bridge_status_t : ::cypher::common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INDEX_DATA_INVALID,
    RENDER_ERROR,
    CLEANUP_FAILED
};

struct render_static_mesh_bridge_result_t {
    render_static_mesh_bridge_status_t status{ render_static_mesh_bridge_status_t::OK };
    render_error_t renderError{ render_error_t::OK }; // Original validation/create error.
    render_error_t cleanupRenderError{ render_error_t::OK };
};

// Validates layout, exact byte extents, and every index before making any GPU
// allocation. bundleOut must have no live handles; an already-owned bundle is
// rejected without changing it. On success it owns three renderer handles.
// Ordinary failure leaves it empty. If rollback destruction fails, CLEANUP_FAILED keeps
// still-live handles in bundleOut for a later destroy retry.
CYPHER_NODISCARD render_static_mesh_bridge_result_t R_CreateStaticMeshBundle(
    const render_static_mesh_source_t &source,
    CY_OUT render_static_mesh_bundle_t *bundleOut ) noexcept;

// Destroys vertex input before index and vertex buffers. A failed destroy
// retains its handle and any dependent handles for a later retry.
CYPHER_NODISCARD render_error_t R_DestroyStaticMeshBundle(
    render_static_mesh_bundle_t *bundle ) noexcept;

// Copies only geometry fields into draw, preserving pipeline, material,
// uniform buffers, samplers, scissor, viewport, and instance count. A failed
// validation leaves draw unchanged.
CYPHER_NODISCARD render_error_t R_ApplyStaticMeshBundleToDraw(
    const render_static_mesh_bundle_t &bundle,
    render_draw_indexed_desc_t *draw ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_MESH_BRIDGE_H
