//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Shared cooked-material decoding and GPU ownership for editor previews.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_TOOLS_TILEEDITOR_MATERIALPREVIEW_H
#define CYPHER_TOOLS_TILEEDITOR_MATERIALPREVIEW_H
#pragma once

#include "CypherTileMapDocument.h"
#include "CypherRender/CypherRender_Texture.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cypher::tools::tile_editor
{

inline constexpr usize TILE_MATERIAL_PREVIEW_MAX_TEXTURE_BYTES = 64u * CY_MIB;
inline constexpr usize TILE_MATERIAL_PREVIEW_MAX_SET_BYTES = 512u * CY_MIB;

// CPU pixels own the base mip and use the cooked texture's RGBA8 encoding.
// This type can be used without initializing a renderer (for asset thumbnails).
struct tile_material_preview_source_t {
    u32 width{ 0u };
    u32 height{ 0u };
    bool sRGB{ true };
    bool generateMips{ false };
    std::vector<byte> pixels{};
    f32 tint[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    f32 uvScale[2]{ 1.0f, 1.0f };
};

struct tile_material_preview_record_t {
    u16 nSlot{ 0u };
    ::cypher::engine::render::render_texture_handle_t texture{};
    f32 tint[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    f32 uvScale[2]{ 1.0f, 1.0f };
};

// Explicit GPU lifetime: Shutdown must run with the owning renderer/context
// current, outside a frame, before R_Shutdown. Default construction is empty.
struct tile_material_preview_set_t {
    tile_material_preview_set_t() = default;
    CYPHER_NO_COPY_MOVE( tile_material_preview_set_t );
    std::vector<tile_material_preview_record_t> records{};
};

// materialPath is a canonical .cymat reference relative to the cooked root.
// Reads materialPath + "_c", then its base_color .cytex_c dependency. Supports
// tile_surface.cyshader with optional tint(vec4) and uv_scale(vec2) parameters.
// UV scales must be finite and nonzero; negative scales intentionally mirror.
// Failure leaves sourceOut unchanged and provides an actionable diagnostic.
bool CypherTileMaterialPreview_Read(
    const std::filesystem::path &cookedRoot,
    std::string_view materialPath,
    tile_material_preview_source_t &sourceOut,
    std::string &error ) noexcept;

// Atomically replaces all bound slots. Any failed decode/upload destroys only
// pending textures, preserving the previous set. Unbound slots use blockout colors.
bool CypherTileMaterialPreview_Reload(
    tile_material_preview_set_t &set,
    const tile_map_document_t &document,
    const std::filesystem::path &cookedRoot,
    std::string &error ) noexcept;

void CypherTileMaterialPreview_Shutdown(
    tile_material_preview_set_t &set ) noexcept;

const tile_material_preview_record_t *CypherTileMaterialPreview_Find(
    const tile_material_preview_set_t &set,
    u16 nSlot ) noexcept;

} // namespace cypher::tools::tile_editor
#endif
