//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Owns immutable RGBA8 two-dimensional sampled images.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_TEXTURE_H
#define CYPHER_ENGINE_RENDER_TEXTURE_H
#pragma once

#include "CypherRender_Error.h"
#include "CypherRender_Types.h"

namespace cypher::engine::render
{
using render_texture_handle_t = render_handle_t;
inline constexpr render_texture_handle_t R_INVALID_TEXTURE{};

// First sampled-image contract: tightly packed RGBA8, one layer, one face.
// Filtering is linear (trilinear with mips). Addressing is repeat or edge clamp.
// These immutable sampling defaults can later be replaced by explicit samplers.
struct render_texture_desc_t {
    ::cypher::common::u32 width{ 0u };
    ::cypher::common::u32 height{ 0u };
    bool sRGB{ true };
    bool generateMips{ true };
    bool repeat{ true };
    const char *debugName{ nullptr }; // Borrowed only during creation.
};
struct render_texture_data_t {
    const void *bytes{ nullptr };
    ::cypher::common::u64 byteSize{ 0u }; // Exactly width * height * four bytes.
};
struct render_texture_info_t {
    ::cypher::common::u32 width{ 0u };
    ::cypher::common::u32 height{ 0u };
    bool sRGB{ true };
    bool generateMips{ true };
    bool repeat{ true };
    ::cypher::common::u32 mipLevels{ 0u };
};

// Calls run on the renderer thread. Creation consumes all pixel bytes before
// returning; no source pointer is retained. Draw bindings are immediate.
CYPHER_NODISCARD render_error_t R_CreateTexture2D(
    const render_texture_desc_t &description,
    const render_texture_data_t &data,
    CY_OUT render_texture_handle_t *textureOut ) noexcept;
CYPHER_NODISCARD render_error_t R_DestroyTexture( render_texture_handle_t texture ) noexcept;
CYPHER_NODISCARD bool R_IsTextureValid( render_texture_handle_t texture ) noexcept;
CYPHER_NODISCARD render_error_t R_GetTextureInfo(
    render_texture_handle_t texture, CY_OUT render_texture_info_t *infoOut ) noexcept;
} // namespace cypher::engine::render
#endif // CYPHER_ENGINE_RENDER_TEXTURE_H
