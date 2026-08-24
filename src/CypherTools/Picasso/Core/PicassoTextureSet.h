//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoTextureSet.h
//  Purpose: Declares ownership of a related set of material texture channels.
//  Details: A set gives every semantic constant-time storage while allocating
//           pixel memory only for channels that an authored document enables.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_TEXTURESET_H
#define CYPHER_TOOLS_PICASSO_TEXTURESET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "PicassoChannel.h"
#include "CypherCommon_ImageSurface.h"

namespace cypher::tools::picasso
{

inline constexpr u32 PICASSO_TEXTURE_MAX_DIMENSION = 16384u;
inline constexpr usize PICASSO_TEXTURE_ROW_ALIGNMENT = 64u;

enum class picasso_texture_set_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    NOT_INITIALIZED,
    ALREADY_CREATED,
    NO_TEXTURE_SET,
    INVALID_EXTENT,
    INVALID_CHANNEL_DESC,
    CHANNEL_EXISTS,
    CHANNEL_NOT_FOUND,
    EXTENT_MISMATCH,
    ALLOCATION_FAILED,
    PROCESSING_FAILED,
    INVALID_STATE
};

// Surfaces are indexed directly by semantic. The active mask determines which
// entries own pixels; inactive entries must remain canonical empty surfaces.
struct picasso_texture_set_t {
    picasso_texture_set_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( picasso_texture_set_t );

    const allocator_t *pAllocator{ nullptr };
    image_extent_t extent{};
    image_surface_t channels[PICASSO_CHANNEL_COUNT]{};
    u64 channelRevisions[PICASSO_CHANNEL_COUNT]{};
    picasso_channel_mask_t activeChannels{ 0u };
    u64 nRevision{ 0u };
};

CYPHER_NODISCARD picasso_texture_set_status_t PicassoTextureSet_Init(
    picasso_texture_set_t *pTextureSet,
    const allocator_t *pAllocator ) noexcept;

void PicassoTextureSet_Shutdown(
    picasso_texture_set_t *pTextureSet ) noexcept;

// Defines the common 2D extent without allocating channel pixels. Channels may
// then be added lazily as the artist enables material properties.
CYPHER_NODISCARD picasso_texture_set_status_t PicassoTextureSet_Create(
    picasso_texture_set_t *pTextureSet,
    u32 nWidth,
    u32 nHeight ) noexcept;

// Releases every channel while retaining the allocator for another document.
void PicassoTextureSet_Clear(
    picasso_texture_set_t *pTextureSet ) noexcept;

// Adds a channel filled with one value. Failure leaves the set unchanged.
CYPHER_NODISCARD picasso_texture_set_status_t PicassoTextureSet_AddChannel(
    picasso_texture_set_t *pTextureSet,
    const picasso_channel_desc_t &desc,
    colorf_t initialValue ) noexcept;

CYPHER_NODISCARD picasso_texture_set_status_t
PicassoTextureSet_AddDefaultChannel(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept;

// Copies or adopts an existing surface transactionally. Adopt transfers pixel
// ownership only after all semantic and extent checks succeed.
CYPHER_NODISCARD picasso_texture_set_status_t
PicassoTextureSet_SetChannelFromView(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic,
    const const_image_view_t &source ) noexcept;

CYPHER_NODISCARD picasso_texture_set_status_t
PicassoTextureSet_AdoptChannelSurface(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic,
    image_surface_t *pSource ) noexcept;

CYPHER_NODISCARD picasso_texture_set_status_t PicassoTextureSet_RemoveChannel(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD bool_t PicassoTextureSet_IsCreated(
    const picasso_texture_set_t *pTextureSet ) noexcept;

CYPHER_NODISCARD bool_t PicassoTextureSet_IsValid(
    const picasso_texture_set_t *pTextureSet ) noexcept;

CYPHER_NODISCARD bool_t PicassoTextureSet_HasChannel(
    const picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD usize PicassoTextureSet_ChannelCount(
    const picasso_texture_set_t *pTextureSet ) noexcept;

CYPHER_NODISCARD usize PicassoTextureSet_ByteSize(
    const picasso_texture_set_t *pTextureSet ) noexcept;

CYPHER_NODISCARD image_surface_t *PicassoTextureSet_GetChannel(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept;

CYPHER_NODISCARD const image_surface_t *PicassoTextureSet_GetChannel(
    const picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept;

// Call after direct pixel mutation so compositors and previews can invalidate
// only consumers of the modified channel.
CYPHER_NODISCARD picasso_texture_set_status_t
PicassoTextureSet_MarkChannelChanged(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept;

// Exchanges complete ownership without copying pixel payloads.
CYPHER_NODISCARD bool_t PicassoTextureSet_Swap(
    picasso_texture_set_t *pLeft,
    picasso_texture_set_t *pRight ) noexcept;

CYPHER_NODISCARD const char *PicassoTextureSet_StatusName(
    picasso_texture_set_status_t status ) noexcept;

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_TEXTURESET_H
