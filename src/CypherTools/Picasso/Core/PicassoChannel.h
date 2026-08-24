//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoChannel.h
//  Purpose: Declares semantic texture channels used by Picasso documents.
//  Details: Channel semantics remain separate from physical pixel storage so
//           tools can reason about material meaning without depending on one
//           precision, color space, or future rendering backend.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_CHANNEL_H
#define CYPHER_TOOLS_PICASSO_CHANNEL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Color.h"
#include "CypherCommon_ImageConvert.h"

namespace cypher::tools::picasso
{

using namespace cypher::common;

// These are authoring meanings, not shader binding names. A shader may map a
// channel to any binding, while Picasso keeps painting, defaults, and previews
// consistent across materials.
enum class picasso_channel_semantic_t : u8 {
    BASE_COLOR = 0u,
    NORMAL,
    ROUGHNESS,
    METALNESS,
    AMBIENT_OCCLUSION,
    EMISSIVE,
    HEIGHT,
    OPACITY,
    COUNT
};

using picasso_channel_mask_t = flags32_t;

inline constexpr usize PICASSO_CHANNEL_COUNT =
    static_cast<usize>( picasso_channel_semantic_t::COUNT );

static_assert(
    PICASSO_CHANNEL_COUNT <= 32u,
    "Picasso channel masks require at most 32 semantic channels." );

// Storage policy is explicit per channel. This permits an R8 roughness map and
// an RGBA16F base-color map to coexist in the same texture set.
struct picasso_channel_desc_t {
    picasso_channel_semantic_t semantic{
        picasso_channel_semantic_t::BASE_COLOR
    };
    image_pixel_format_t pixelFormat{ image_pixel_format_t::UNKNOWN };
    image_color_space_t colorSpace{ image_color_space_t::UNKNOWN };
    image_alpha_mode_t alphaMode{ image_alpha_mode_t::NONE };
};

enum class picasso_channel_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_SEMANTIC,
    INVALID_PIXEL_FORMAT,
    INVALID_COLOR_SPACE,
    INVALID_ALPHA_MODE,
    OUTPUT_TOO_SMALL,
    ENCODE_FAILED
};

CYPHER_NODISCARD bool_t PicassoChannel_IsSemanticValid(
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD picasso_channel_mask_t PicassoChannel_Bit(
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD bool_t PicassoChannel_IsColor(
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD bool_t PicassoChannel_IsScalar(
    picasso_channel_semantic_t semantic ) noexcept;

// Returns the preferred 1.0 working format for one semantic. Callers may select
// another validated precision when creating high-precision documents.
CYPHER_NODISCARD picasso_channel_desc_t PicassoChannel_DefaultDesc(
    picasso_channel_semantic_t semantic ) noexcept;

// Returns a neutral authoring value in linear floating-point form. Color
// channels are converted to their storage color space when encoded.
CYPHER_NODISCARD colorf_t PicassoChannel_DefaultValue(
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD picasso_channel_status_t PicassoChannel_ValidateDesc(
    const picasso_channel_desc_t &desc ) noexcept;

// Encodes one linear floating-point value into the channel's physical pixel
// representation. Brush and fill code can use this without duplicating UNORM,
// float, sRGB, and alpha conversion rules.
CYPHER_NODISCARD picasso_channel_status_t PicassoChannel_EncodePixel(
    const picasso_channel_desc_t &desc,
    colorf_t value,
    byte_span_t destination ) noexcept;

CYPHER_NODISCARD const char *PicassoChannel_Name(
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD const char *PicassoChannel_StatusName(
    picasso_channel_status_t status ) noexcept;

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_CHANNEL_H
