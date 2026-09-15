//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_VertexLayout.h
//  Purpose: Declares backend-neutral vertex and index input contracts.
//  Details: Vertex bindings describe byte streams; attributes describe how a
//           pipeline interprets those streams. Native OpenGL VAOs and enums
//           remain private to the OpenGL backend.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_VERTEX_LAYOUT_H
#define CYPHER_ENGINE_RENDER_VERTEX_LAYOUT_H
#pragma once

#include "CypherRender_Error.h"
#include "CypherCommon/Formats/CypherCommon_RenderFormat.h"

namespace cypher::engine::render
{

/*
===============================================================================

    Vertex input limits

These are portable contract limits, not a claim about the active device. The
renderer may expose larger device limits, but authored layouts stay inside this
small fixed vocabulary so descriptors remain allocation-free and deterministic.

===============================================================================
*/
inline constexpr ::cypher::common::u32 R_MAX_VERTEX_BINDINGS = 16u;
inline constexpr ::cypher::common::u32 R_MAX_VERTEX_ATTRIBUTES = 16u;
inline constexpr ::cypher::common::u32 R_MAX_VERTEX_STRIDE = 2048u;

using render_format_t = ::cypher::common::render_format_t;

/*
================
Vertex value class

FLOAT means the shader receives floating-point values. Normalized integer
storage also belongs to FLOAT because conversion occurs during vertex fetch.
SINT and UINT preserve integer values and require an integer shader input.
================
*/
enum class render_vertex_value_t : ::cypher::common::u8 {
    FLOAT = 0u,
    SINT,
    UINT,
    COUNT
};

struct render_vertex_format_info_t {
    render_format_t format{ render_format_t::UNKNOWN }; // Canonical serialized format identity.
    render_vertex_value_t valueType{ render_vertex_value_t::FLOAT }; // Shader-visible value class.
    ::cypher::common::u8 componentCount{ 0u }; // Scalar components delivered to one shader location.
    ::cypher::common::u8 byteSize{ 0u };       // Bytes occupied by one complete attribute value.
    ::cypher::common::u8 alignment{ 0u };      // Required byte alignment of the attribute address.
    bool normalized{ false };                  // Integer storage converts into a normalized float range.
    bool packed{ false };                      // Components share one packed integer storage word.
};

/*
================
Vertex binding rate

A binding advances once per vertex or once per group of instances. Per-vertex
bindings use a divisor of zero. Per-instance bindings use a nonzero divisor;
one is the normal instancing case.
================
*/
enum class render_vertex_input_rate_t : ::cypher::common::u8 {
    PER_VERTEX = 0u,
    PER_INSTANCE,
    COUNT
};

struct render_vertex_binding_desc_t {
    ::cypher::common::u32 stride{ 0u }; // Bytes from one element in this stream to the next.
    ::cypher::common::u32 instanceDivisor{ 0u }; // Instances sharing one element; zero for vertices.
    ::cypher::common::u8 binding{ 0u }; // Stable stream slot referenced by attributes and buffers.
    render_vertex_input_rate_t inputRate{
        render_vertex_input_rate_t::PER_VERTEX }; // Stream advancement policy.
};

struct render_vertex_attribute_desc_t {
    render_format_t format{ render_format_t::UNKNOWN }; // Storage and shader conversion rules.
    ::cypher::common::u32 offset{ 0u }; // Byte offset inside one element of the selected binding.
    ::cypher::common::u8 location{ 0u }; // Explicit shader input location.
    ::cypher::common::u8 binding{ 0u };  // Vertex stream supplying this attribute.
};

/*
================
Vertex layout

Bindings and attributes are compact arrays with explicit active-prefix counts.
Array order has no semantic meaning; binding and location fields carry identity.
The descriptor contains no buffers and may therefore be shared by many meshes.
================
*/
struct render_vertex_layout_t {
    render_vertex_binding_desc_t bindings[R_MAX_VERTEX_BINDINGS]{};
    render_vertex_attribute_desc_t attributes[R_MAX_VERTEX_ATTRIBUTES]{};
    ::cypher::common::u32 bindingCount{ 0u };
    ::cypher::common::u32 attributeCount{ 0u };
};

/*
================
Index type

Eight-bit indices are intentionally absent because they are not a portable core
feature across the planned backends. Index byte offsets must align to this size.
================
*/
enum class render_index_type_t : ::cypher::common::u8 {
    UINT16 = 0u,
    UINT32,
    COUNT
};

// Returns metadata only for formats legal as portable vertex attributes.
CYPHER_NODISCARD bool R_GetVertexFormatInfo(
    render_format_t format,
    CY_OUT render_vertex_format_info_t *infoOut ) noexcept;

// Returns zero for an invalid index type.
CYPHER_NODISCARD ::cypher::common::u32 R_IndexTypeSize(
    render_index_type_t type ) noexcept;

// Validates format support, identities, rates, alignment, and stride bounds.
CYPHER_NODISCARD render_error_t R_ValidateVertexLayout(
    const render_vertex_layout_t &layout ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_VERTEX_LAYOUT_H
