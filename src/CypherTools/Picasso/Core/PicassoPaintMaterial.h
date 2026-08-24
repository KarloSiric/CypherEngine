//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoPaintMaterial.h
//  Purpose: Declares renderer-neutral material sources used by paint tools.
//  Details: A paint material supplies constants or `.cytex` resources to several
//           semantic channels at once. It is authoring state, not a runtime GPU
//           material and not a replacement for the `.cymat` format.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_PAINTMATERIAL_H
#define CYPHER_TOOLS_PICASSO_PAINTMATERIAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "PicassoChannel.h"
#include "CypherCommon_FixedString.h"

namespace cypher::tools::picasso
{

inline constexpr usize PICASSO_MATERIAL_NAME_CAPACITY = 95u;
inline constexpr usize PICASSO_MATERIAL_PATH_CAPACITY = 259u;

enum class picasso_material_source_kind_t : u8 {
    DISABLED = 0u,
    CONSTANT,
    TEXTURE_RESOURCE
};

// Texture mapping belongs to each source because artists may tile a detail map
// independently from base color. Negative scale permits intentional mirroring;
// zero scale is rejected because it collapses an axis to one sample.
struct picasso_material_mapping_t {
    f32 scaleU{ 1.0f };
    f32 scaleV{ 1.0f };
    f32 offsetU{ 0.0f };
    f32 offsetV{ 0.0f };
    f32 rotationDegrees{ 0.0f };
};

// Entries are stored at the index of their semantic. A disabled entry owns no
// resource path; constants use linear values and scalar channels consume red.
struct picasso_material_channel_t {
    picasso_material_source_kind_t kind{
        picasso_material_source_kind_t::DISABLED
    };
    colorf_t constant{ 0.0f, 0.0f, 0.0f, 1.0f };
    fixed_string_t<PICASSO_MATERIAL_PATH_CAPACITY> texture{};
    picasso_material_mapping_t mapping{};
    f32 strength{ 1.0f };
};

struct picasso_paint_material_t {
    fixed_string_t<PICASSO_MATERIAL_NAME_CAPACITY> name{};
    // Imported `.cymat` assets retain their shader contract. Procedural paint
    // presets may leave this empty until they are saved as runtime materials.
    fixed_string_t<PICASSO_MATERIAL_PATH_CAPACITY> shader{};
    picasso_material_channel_t channels[PICASSO_CHANNEL_COUNT]{};
    picasso_channel_mask_t activeChannels{ 0u };
    u64 nRevision{ 0u };
};

enum class picasso_paint_material_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_NAME,
    INVALID_SEMANTIC,
    INVALID_SOURCE_KIND,
    INVALID_VALUE,
    INVALID_STRENGTH,
    INVALID_MAPPING,
    INVALID_RESOURCE_PATH,
    CHANNEL_DISABLED,
    INVALID_STATE
};

// Initializes a canonical material. The name is owned and UTF-8 validated.
CYPHER_NODISCARD picasso_paint_material_status_t PicassoPaintMaterial_Init(
    picasso_paint_material_t *pMaterial,
    string_view_t name ) noexcept;

void PicassoPaintMaterial_Reset(
    picasso_paint_material_t *pMaterial ) noexcept;

CYPHER_NODISCARD picasso_paint_material_status_t PicassoPaintMaterial_SetName(
    picasso_paint_material_t *pMaterial,
    string_view_t name ) noexcept;

// Assigns a canonical `.cyshader` VFS path. An empty path clears the optional
// shader association without changing any authoring channels.
CYPHER_NODISCARD picasso_paint_material_status_t
PicassoPaintMaterial_SetShader(
    picasso_paint_material_t *pMaterial,
    string_view_t shaderPath ) noexcept;

CYPHER_NODISCARD picasso_paint_material_status_t
PicassoPaintMaterial_SetConstant(
    picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic,
    colorf_t value,
    f32 strength = 1.0f ) noexcept;

// Texture references use canonical VFS paths ending in `.cytex`. Source images
// are imported into texture documents or recipes before becoming material input.
CYPHER_NODISCARD picasso_paint_material_status_t
PicassoPaintMaterial_SetTexture(
    picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic,
    string_view_t texturePath,
    f32 strength = 1.0f ) noexcept;

CYPHER_NODISCARD picasso_paint_material_status_t
PicassoPaintMaterial_SetMapping(
    picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic,
    const picasso_material_mapping_t &mapping ) noexcept;

CYPHER_NODISCARD picasso_paint_material_status_t
PicassoPaintMaterial_SetStrength(
    picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic,
    f32 strength ) noexcept;

CYPHER_NODISCARD picasso_paint_material_status_t
PicassoPaintMaterial_DisableChannel(
    picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD bool_t PicassoPaintMaterial_IsValid(
    const picasso_paint_material_t *pMaterial ) noexcept;

CYPHER_NODISCARD bool_t PicassoPaintMaterial_HasChannel(
    const picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD usize PicassoPaintMaterial_ChannelCount(
    const picasso_paint_material_t *pMaterial ) noexcept;

CYPHER_NODISCARD const picasso_material_channel_t *
PicassoPaintMaterial_GetChannel(
    const picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD const char *PicassoPaintMaterial_StatusName(
    picasso_paint_material_status_t status ) noexcept;

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_PAINTMATERIAL_H
