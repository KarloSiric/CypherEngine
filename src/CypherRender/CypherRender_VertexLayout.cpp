//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_VertexLayout.cpp
//  Purpose: Implements portable vertex-format metadata and layout validation.
//  Details: Backends receive only layouts that have passed these checks, which
//           keeps native API translation focused on execution rather than policy.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_VertexLayout.h"

#include <array>

namespace cypher::engine::render
{

namespace
{

using format_info_array_t = std::array<render_vertex_format_info_t, 49u>;

constexpr format_info_array_t R_VERTEX_FORMATS{{
    { render_format_t::R8_UNORM,    render_vertex_value_t::FLOAT, 1u, 1u, 1u, true,  false },
    { render_format_t::R8_SNORM,    render_vertex_value_t::FLOAT, 1u, 1u, 1u, true,  false },
    { render_format_t::R8_UINT,     render_vertex_value_t::UINT,  1u, 1u, 1u, false, false },
    { render_format_t::R8_SINT,     render_vertex_value_t::SINT,  1u, 1u, 1u, false, false },
    { render_format_t::RG8_UNORM,   render_vertex_value_t::FLOAT, 2u, 2u, 1u, true,  false },
    { render_format_t::RG8_SNORM,   render_vertex_value_t::FLOAT, 2u, 2u, 1u, true,  false },
    { render_format_t::RG8_UINT,    render_vertex_value_t::UINT,  2u, 2u, 1u, false, false },
    { render_format_t::RG8_SINT,    render_vertex_value_t::SINT,  2u, 2u, 1u, false, false },
    { render_format_t::RGB8_UNORM,  render_vertex_value_t::FLOAT, 3u, 3u, 1u, true,  false },
    { render_format_t::RGB8_SNORM,  render_vertex_value_t::FLOAT, 3u, 3u, 1u, true,  false },
    { render_format_t::RGB8_UINT,   render_vertex_value_t::UINT,  3u, 3u, 1u, false, false },
    { render_format_t::RGB8_SINT,   render_vertex_value_t::SINT,  3u, 3u, 1u, false, false },
    { render_format_t::RGBA8_UNORM, render_vertex_value_t::FLOAT, 4u, 4u, 1u, true,  false },
    { render_format_t::RGBA8_SNORM, render_vertex_value_t::FLOAT, 4u, 4u, 1u, true,  false },
    { render_format_t::RGBA8_UINT,  render_vertex_value_t::UINT,  4u, 4u, 1u, false, false },
    { render_format_t::RGBA8_SINT,  render_vertex_value_t::SINT,  4u, 4u, 1u, false, false },

    { render_format_t::R16_UNORM,    render_vertex_value_t::FLOAT, 1u, 2u, 2u, true,  false },
    { render_format_t::R16_SNORM,    render_vertex_value_t::FLOAT, 1u, 2u, 2u, true,  false },
    { render_format_t::R16_UINT,     render_vertex_value_t::UINT,  1u, 2u, 2u, false, false },
    { render_format_t::R16_SINT,     render_vertex_value_t::SINT,  1u, 2u, 2u, false, false },
    { render_format_t::R16_FLOAT,    render_vertex_value_t::FLOAT, 1u, 2u, 2u, false, false },
    { render_format_t::RG16_UNORM,   render_vertex_value_t::FLOAT, 2u, 4u, 2u, true,  false },
    { render_format_t::RG16_SNORM,   render_vertex_value_t::FLOAT, 2u, 4u, 2u, true,  false },
    { render_format_t::RG16_UINT,    render_vertex_value_t::UINT,  2u, 4u, 2u, false, false },
    { render_format_t::RG16_SINT,    render_vertex_value_t::SINT,  2u, 4u, 2u, false, false },
    { render_format_t::RG16_FLOAT,   render_vertex_value_t::FLOAT, 2u, 4u, 2u, false, false },
    { render_format_t::RGB16_UNORM,  render_vertex_value_t::FLOAT, 3u, 6u, 2u, true,  false },
    { render_format_t::RGB16_SNORM,  render_vertex_value_t::FLOAT, 3u, 6u, 2u, true,  false },
    { render_format_t::RGB16_UINT,   render_vertex_value_t::UINT,  3u, 6u, 2u, false, false },
    { render_format_t::RGB16_SINT,   render_vertex_value_t::SINT,  3u, 6u, 2u, false, false },
    { render_format_t::RGB16_FLOAT,  render_vertex_value_t::FLOAT, 3u, 6u, 2u, false, false },
    { render_format_t::RGBA16_UNORM, render_vertex_value_t::FLOAT, 4u, 8u, 2u, true,  false },
    { render_format_t::RGBA16_SNORM, render_vertex_value_t::FLOAT, 4u, 8u, 2u, true,  false },
    { render_format_t::RGBA16_UINT,  render_vertex_value_t::UINT,  4u, 8u, 2u, false, false },
    { render_format_t::RGBA16_SINT,  render_vertex_value_t::SINT,  4u, 8u, 2u, false, false },
    { render_format_t::RGBA16_FLOAT, render_vertex_value_t::FLOAT, 4u, 8u, 2u, false, false },

    { render_format_t::R32_UINT,    render_vertex_value_t::UINT,  1u, 4u, 4u, false, false },
    { render_format_t::R32_SINT,    render_vertex_value_t::SINT,  1u, 4u, 4u, false, false },
    { render_format_t::R32_FLOAT,   render_vertex_value_t::FLOAT, 1u, 4u, 4u, false, false },
    { render_format_t::RG32_UINT,   render_vertex_value_t::UINT,  2u, 8u, 4u, false, false },
    { render_format_t::RG32_SINT,   render_vertex_value_t::SINT,  2u, 8u, 4u, false, false },
    { render_format_t::RG32_FLOAT,  render_vertex_value_t::FLOAT, 2u, 8u, 4u, false, false },
    { render_format_t::RGB32_UINT,  render_vertex_value_t::UINT,  3u, 12u, 4u, false, false },
    { render_format_t::RGB32_SINT,  render_vertex_value_t::SINT,  3u, 12u, 4u, false, false },
    { render_format_t::RGB32_FLOAT, render_vertex_value_t::FLOAT, 3u, 12u, 4u, false, false },
    { render_format_t::RGBA32_UINT, render_vertex_value_t::UINT,  4u, 16u, 4u, false, false },
    { render_format_t::RGBA32_SINT, render_vertex_value_t::SINT,  4u, 16u, 4u, false, false },
    { render_format_t::RGBA32_FLOAT,render_vertex_value_t::FLOAT, 4u, 16u, 4u, false, false },

    { render_format_t::RGB10A2_UNORM, render_vertex_value_t::FLOAT, 4u, 4u, 4u, true, true }
}};

const render_vertex_binding_desc_t *R_FindBinding(
    const render_vertex_layout_t &layout,
    const ::cypher::common::u8 binding ) noexcept
{
    for ( ::cypher::common::u32 i = 0u; i < layout.bindingCount; ++i ) {
        if ( layout.bindings[i].binding == binding ) {
            return &layout.bindings[i];
        }
    }
    return nullptr;
}

} // namespace

bool R_GetVertexFormatInfo(
    const render_format_t format,
    render_vertex_format_info_t *infoOut ) noexcept
{
    if ( infoOut == nullptr ) {
        return false;
    }
    *infoOut = {};

    for ( const render_vertex_format_info_t &info : R_VERTEX_FORMATS ) {
        if ( info.format == format ) {
            *infoOut = info;
            return true;
        }
    }
    return false;
}

::cypher::common::u32 R_IndexTypeSize(
    const render_index_type_t type ) noexcept
{
    switch ( type ) {
        case render_index_type_t::UINT16: return 2u;
        case render_index_type_t::UINT32: return 4u;
        case render_index_type_t::COUNT:
        default: return 0u;
    }
}

render_error_t R_ValidateVertexLayout(
    const render_vertex_layout_t &layout ) noexcept
{
    if ( layout.bindingCount == 0u ||
         layout.bindingCount > R_MAX_VERTEX_BINDINGS ||
         layout.attributeCount == 0u ||
         layout.attributeCount > R_MAX_VERTEX_ATTRIBUTES ) {
        return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
    }

    bool bindingSeen[R_MAX_VERTEX_BINDINGS]{};
    bool bindingUsed[R_MAX_VERTEX_BINDINGS]{};
    bool locationSeen[R_MAX_VERTEX_ATTRIBUTES]{};

    for ( ::cypher::common::u32 i = 0u; i < layout.bindingCount; ++i ) {
        const render_vertex_binding_desc_t &binding = layout.bindings[i];
        if ( binding.binding >= R_MAX_VERTEX_BINDINGS ||
             bindingSeen[binding.binding] ||
             binding.stride == 0u || binding.stride > R_MAX_VERTEX_STRIDE ||
             binding.inputRate >= render_vertex_input_rate_t::COUNT ) {
            return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
        }
        if ( ( binding.inputRate == render_vertex_input_rate_t::PER_VERTEX &&
               binding.instanceDivisor != 0u ) ||
             ( binding.inputRate == render_vertex_input_rate_t::PER_INSTANCE &&
               binding.instanceDivisor == 0u ) ) {
            return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
        }
        bindingSeen[binding.binding] = true;
    }
    
    for ( ::cypher::common::u32 i = 0u; i < layout.attributeCount; ++i ) {
        const render_vertex_attribute_desc_t &attribute = layout.attributes[i];
        render_vertex_format_info_t formatInfo{};
        const render_vertex_binding_desc_t *binding = R_FindBinding(
            layout,
            attribute.binding );

        if ( attribute.location >= R_MAX_VERTEX_ATTRIBUTES ||
             locationSeen[attribute.location] || binding == nullptr ||
             !R_GetVertexFormatInfo( attribute.format, &formatInfo ) ||
             formatInfo.byteSize == 0u || formatInfo.alignment == 0u ||
             ( attribute.offset % formatInfo.alignment ) != 0u ||
             attribute.offset > binding->stride ||
             formatInfo.byteSize > binding->stride - attribute.offset ) {
            return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
        }

        locationSeen[attribute.location] = true;
        bindingUsed[attribute.binding] = true;
    }

    for ( ::cypher::common::u32 i = 0u; i < layout.bindingCount; ++i ) {
        if ( !bindingUsed[layout.bindings[i].binding] ) {
            return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
        }
    }
    return render_error_t::OK;
}

} // namespace cypher::engine::render
