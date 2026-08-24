//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoTextureSet.cpp
//  Purpose: Implements allocator-backed Picasso texture-channel sets.
//  Details: Channel publication is transactional and revisions provide cheap
//           invalidation keys for future compositing and preview caches.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoTextureSet.h"

#include "CypherCommon_ImageProcess.h"

namespace cypher::tools::picasso
{

namespace
{

usize PicassoTextureSet_ChannelIndex(
    picasso_channel_semantic_t semantic ) noexcept
{
    return static_cast<usize>( semantic );
}

bool_t PicassoTextureSet_IsInitialized(
    const picasso_texture_set_t *pTextureSet ) noexcept
{
    return pTextureSet != nullptr &&
           Allocator_IsValid( pTextureSet->pAllocator );
}

bool_t PicassoTextureSet_IsExtentValid(
    const image_extent_t &extent ) noexcept
{
    return extent.nWidth > 0u && extent.nHeight > 0u &&
           extent.nDepth == 1u &&
           extent.nWidth <= PICASSO_TEXTURE_MAX_DIMENSION &&
           extent.nHeight <= PICASSO_TEXTURE_MAX_DIMENSION;
}

bool_t PicassoTextureSet_ExtentsEqual(
    const image_extent_t &left,
    const image_extent_t &right ) noexcept
{
    return left.nWidth == right.nWidth &&
           left.nHeight == right.nHeight &&
           left.nDepth == right.nDepth;
}

u64 PicassoTextureSet_AdvanceRevision(
    picasso_texture_set_t &textureSet ) noexcept
{
    textureSet.nRevision = textureSet.nRevision == CY_U64_MAX
        ? 1u
        : textureSet.nRevision + 1u;
    if ( textureSet.nRevision == 0u ) {
        textureSet.nRevision = 1u;
    }
    return textureSet.nRevision;
}

picasso_channel_desc_t PicassoTextureSet_DescFromSurface(
    picasso_channel_semantic_t semantic,
    const image_surface_t &surface ) noexcept
{
    return {
        semantic,
        surface.desc.pixelFormat,
        surface.desc.colorSpace,
        surface.desc.alphaMode
    };
}

picasso_texture_set_status_t PicassoTextureSet_ValidateIncomingSurface(
    const picasso_texture_set_t &textureSet,
    picasso_channel_semantic_t semantic,
    const image_surface_t &surface ) noexcept
{
    if ( !PicassoChannel_IsSemanticValid( semantic ) ||
         !ImageSurface_IsValid( &surface ) ) {
        return picasso_texture_set_status_t::INVALID_ARGUMENT;
    }
    if ( !PicassoTextureSet_ExtentsEqual(
             textureSet.extent,
             surface.desc.extent ) ) {
        return picasso_texture_set_status_t::EXTENT_MISMATCH;
    }
    return PicassoChannel_ValidateDesc(
               PicassoTextureSet_DescFromSurface( semantic, surface ) ) ==
            picasso_channel_status_t::OK
        ? picasso_texture_set_status_t::OK
        : picasso_texture_set_status_t::INVALID_CHANNEL_DESC;
}

void PicassoTextureSet_PublishChannel(
    picasso_texture_set_t &textureSet,
    picasso_channel_semantic_t semantic,
    image_surface_t &pending ) noexcept
{
    const usize iChannel = PicassoTextureSet_ChannelIndex( semantic );
    ImageSurface_Swap( &textureSet.channels[iChannel], &pending );
    textureSet.activeChannels |= PicassoChannel_Bit( semantic );
    textureSet.channelRevisions[iChannel] =
        PicassoTextureSet_AdvanceRevision( textureSet );
}

} // namespace

picasso_texture_set_status_t PicassoTextureSet_Init(
    picasso_texture_set_t *pTextureSet,
    const allocator_t *pAllocator ) noexcept
{
    if ( pTextureSet == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return picasso_texture_set_status_t::INVALID_ARGUMENT;
    }
    if ( pTextureSet->pAllocator != nullptr ||
         pTextureSet->activeChannels != 0u ||
         PicassoTextureSet_IsExtentValid( pTextureSet->extent ) ) {
        return picasso_texture_set_status_t::INVALID_STATE;
    }
    pTextureSet->pAllocator = pAllocator;
    return picasso_texture_set_status_t::OK;
}

void PicassoTextureSet_Shutdown(
    picasso_texture_set_t *pTextureSet ) noexcept
{
    if ( pTextureSet == nullptr ) {
        return;
    }
    PicassoTextureSet_Clear( pTextureSet );
    pTextureSet->pAllocator = nullptr;
}

picasso_texture_set_status_t PicassoTextureSet_Create(
    picasso_texture_set_t *pTextureSet,
    u32 nWidth,
    u32 nHeight ) noexcept
{
    if ( !PicassoTextureSet_IsInitialized( pTextureSet ) ) {
        return pTextureSet == nullptr
            ? picasso_texture_set_status_t::INVALID_ARGUMENT
            : picasso_texture_set_status_t::NOT_INITIALIZED;
    }
    if ( PicassoTextureSet_IsCreated( pTextureSet ) ) {
        return picasso_texture_set_status_t::ALREADY_CREATED;
    }

    const image_extent_t extent{ nWidth, nHeight, 1u };
    if ( !PicassoTextureSet_IsExtentValid( extent ) ) {
        return picasso_texture_set_status_t::INVALID_EXTENT;
    }

    pTextureSet->extent = extent;
    PicassoTextureSet_AdvanceRevision( *pTextureSet );
    return picasso_texture_set_status_t::OK;
}

void PicassoTextureSet_Clear(
    picasso_texture_set_t *pTextureSet ) noexcept
{
    if ( pTextureSet == nullptr ) {
        return;
    }
    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        ImageSurface_Destroy( &pTextureSet->channels[iChannel] );
        pTextureSet->channelRevisions[iChannel] = 0u;
    }
    pTextureSet->extent = {};
    pTextureSet->activeChannels = 0u;
    pTextureSet->nRevision = 0u;
}

picasso_texture_set_status_t PicassoTextureSet_AddChannel(
    picasso_texture_set_t *pTextureSet,
    const picasso_channel_desc_t &desc,
    colorf_t initialValue ) noexcept
{
    if ( !PicassoTextureSet_IsInitialized( pTextureSet ) ) {
        return pTextureSet == nullptr
            ? picasso_texture_set_status_t::INVALID_ARGUMENT
            : picasso_texture_set_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureSet_IsCreated( pTextureSet ) ) {
        return picasso_texture_set_status_t::NO_TEXTURE_SET;
    }
    if ( PicassoChannel_ValidateDesc( desc ) !=
         picasso_channel_status_t::OK ) {
        return picasso_texture_set_status_t::INVALID_CHANNEL_DESC;
    }
    if ( PicassoTextureSet_HasChannel( pTextureSet, desc.semantic ) ) {
        return picasso_texture_set_status_t::CHANNEL_EXISTS;
    }

    const image_desc_t imageDesc{
        pTextureSet->extent,
        desc.pixelFormat,
        desc.colorSpace,
        desc.alphaMode
    };
    image_surface_t pending{};
    if ( ImageSurface_Create(
             &pending,
             pTextureSet->pAllocator,
             imageDesc,
             image_surface_init_t::UNINITIALIZED,
             PICASSO_TEXTURE_ROW_ALIGNMENT ) !=
         image_surface_status_t::OK ) {
        return picasso_texture_set_status_t::ALLOCATION_FAILED;
    }

    byte encodedPixel[16]{};
    const image_format_info_t *pFormat =
        ImageFormat_GetInfo( desc.pixelFormat );
    if ( PicassoChannel_EncodePixel(
             desc,
             initialValue,
             { encodedPixel, sizeof( encodedPixel ) } ) !=
             picasso_channel_status_t::OK ||
         ImageProcess_Fill(
             ImageSurface_GetView( &pending ),
             { encodedPixel, pFormat->cbPixel } ) !=
             image_process_status_t::OK ) {
        return picasso_texture_set_status_t::PROCESSING_FAILED;
    }

    PicassoTextureSet_PublishChannel( *pTextureSet, desc.semantic, pending );
    return picasso_texture_set_status_t::OK;
}

picasso_texture_set_status_t PicassoTextureSet_AddDefaultChannel(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept
{
    return PicassoTextureSet_AddChannel(
        pTextureSet,
        PicassoChannel_DefaultDesc( semantic ),
        PicassoChannel_DefaultValue( semantic ) );
}

picasso_texture_set_status_t PicassoTextureSet_SetChannelFromView(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic,
    const const_image_view_t &source ) noexcept
{
    if ( !PicassoTextureSet_IsInitialized( pTextureSet ) ||
         !PicassoChannel_IsSemanticValid( semantic ) ) {
        return pTextureSet == nullptr
            ? picasso_texture_set_status_t::INVALID_ARGUMENT
            : picasso_texture_set_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureSet_IsCreated( pTextureSet ) ) {
        return picasso_texture_set_status_t::NO_TEXTURE_SET;
    }
    if ( !PicassoTextureSet_ExtentsEqual(
             pTextureSet->extent,
             source.desc.extent ) ) {
        return picasso_texture_set_status_t::EXTENT_MISMATCH;
    }

    const picasso_channel_desc_t desc{
        semantic,
        source.desc.pixelFormat,
        source.desc.colorSpace,
        source.desc.alphaMode
    };
    if ( PicassoChannel_ValidateDesc( desc ) !=
         picasso_channel_status_t::OK ) {
        return picasso_texture_set_status_t::INVALID_CHANNEL_DESC;
    }

    image_surface_t pending{};
    if ( ImageSurface_CreateFromView(
             &pending,
             pTextureSet->pAllocator,
             source,
             PICASSO_TEXTURE_ROW_ALIGNMENT ) !=
         image_surface_status_t::OK ) {
        return picasso_texture_set_status_t::ALLOCATION_FAILED;
    }
    PicassoTextureSet_PublishChannel( *pTextureSet, semantic, pending );
    return picasso_texture_set_status_t::OK;
}

picasso_texture_set_status_t PicassoTextureSet_AdoptChannelSurface(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic,
    image_surface_t *pSource ) noexcept
{
    if ( !PicassoTextureSet_IsInitialized( pTextureSet ) ||
         pSource == nullptr ) {
        return pTextureSet == nullptr || pSource == nullptr
            ? picasso_texture_set_status_t::INVALID_ARGUMENT
            : picasso_texture_set_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureSet_IsCreated( pTextureSet ) ) {
        return picasso_texture_set_status_t::NO_TEXTURE_SET;
    }

    const picasso_texture_set_status_t validation =
        PicassoTextureSet_ValidateIncomingSurface(
            *pTextureSet,
            semantic,
            *pSource );
    if ( validation != picasso_texture_set_status_t::OK ) {
        return validation;
    }

    image_surface_t pending{};
    if ( !ImageSurface_Move( &pending, pSource ) ) {
        return picasso_texture_set_status_t::INVALID_STATE;
    }
    PicassoTextureSet_PublishChannel( *pTextureSet, semantic, pending );
    return picasso_texture_set_status_t::OK;
}

picasso_texture_set_status_t PicassoTextureSet_RemoveChannel(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept
{
    if ( !PicassoTextureSet_IsInitialized( pTextureSet ) ||
         !PicassoChannel_IsSemanticValid( semantic ) ) {
        return pTextureSet == nullptr
            ? picasso_texture_set_status_t::INVALID_ARGUMENT
            : picasso_texture_set_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureSet_HasChannel( pTextureSet, semantic ) ) {
        return picasso_texture_set_status_t::CHANNEL_NOT_FOUND;
    }

    const usize iChannel = PicassoTextureSet_ChannelIndex( semantic );
    ImageSurface_Destroy( &pTextureSet->channels[iChannel] );
    pTextureSet->activeChannels &= ~PicassoChannel_Bit( semantic );
    pTextureSet->channelRevisions[iChannel] = 0u;
    PicassoTextureSet_AdvanceRevision( *pTextureSet );
    return picasso_texture_set_status_t::OK;
}

bool_t PicassoTextureSet_IsCreated(
    const picasso_texture_set_t *pTextureSet ) noexcept
{
    return PicassoTextureSet_IsInitialized( pTextureSet ) &&
           PicassoTextureSet_IsExtentValid( pTextureSet->extent );
}

bool_t PicassoTextureSet_IsValid(
    const picasso_texture_set_t *pTextureSet ) noexcept
{
    if ( !PicassoTextureSet_IsCreated( pTextureSet ) ) {
        return CY_FALSE;
    }

    const picasso_channel_mask_t validMask =
        PICASSO_CHANNEL_COUNT == 32u
        ? CY_U32_MAX
        : ( CYPHER_BIT32( static_cast<u32>( PICASSO_CHANNEL_COUNT ) ) - 1u );
    if ( ( pTextureSet->activeChannels & ~validMask ) != 0u ) {
        return CY_FALSE;
    }

    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        const auto semantic =
            static_cast<picasso_channel_semantic_t>( iChannel );
        const bool_t bActive =
            ( pTextureSet->activeChannels & PicassoChannel_Bit( semantic ) ) != 0u;
        const image_surface_t &surface = pTextureSet->channels[iChannel];
        if ( !bActive ) {
            if ( !ImageSurface_IsEmpty( &surface ) ||
                 pTextureSet->channelRevisions[iChannel] != 0u ) {
                return CY_FALSE;
            }
            continue;
        }
        if ( PicassoTextureSet_ValidateIncomingSurface(
                 *pTextureSet,
                 semantic,
                 surface ) != picasso_texture_set_status_t::OK ||
             pTextureSet->channelRevisions[iChannel] == 0u ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t PicassoTextureSet_HasChannel(
    const picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept
{
    return pTextureSet != nullptr &&
           PicassoChannel_IsSemanticValid( semantic ) &&
           ( pTextureSet->activeChannels & PicassoChannel_Bit( semantic ) ) != 0u;
}

usize PicassoTextureSet_ChannelCount(
    const picasso_texture_set_t *pTextureSet ) noexcept
{
    if ( pTextureSet == nullptr ) {
        return 0u;
    }
    usize nChannels = 0u;
    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        const auto semantic =
            static_cast<picasso_channel_semantic_t>( iChannel );
        nChannels += PicassoTextureSet_HasChannel( pTextureSet, semantic )
            ? 1u
            : 0u;
    }
    return nChannels;
}

usize PicassoTextureSet_ByteSize(
    const picasso_texture_set_t *pTextureSet ) noexcept
{
    if ( pTextureSet == nullptr ) {
        return 0u;
    }
    usize cbTotal = 0u;
    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        const usize cbChannel =
            ImageSurface_GetByteSize( &pTextureSet->channels[iChannel] );
        if ( cbChannel > CY_USIZE_MAX - cbTotal ) {
            return CY_USIZE_MAX;
        }
        cbTotal += cbChannel;
    }
    return cbTotal;
}

image_surface_t *PicassoTextureSet_GetChannel(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept
{
    return PicassoTextureSet_HasChannel( pTextureSet, semantic )
        ? &pTextureSet->channels[PicassoTextureSet_ChannelIndex( semantic )]
        : nullptr;
}

const image_surface_t *PicassoTextureSet_GetChannel(
    const picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept
{
    return PicassoTextureSet_HasChannel( pTextureSet, semantic )
        ? &pTextureSet->channels[PicassoTextureSet_ChannelIndex( semantic )]
        : nullptr;
}

picasso_texture_set_status_t PicassoTextureSet_MarkChannelChanged(
    picasso_texture_set_t *pTextureSet,
    picasso_channel_semantic_t semantic ) noexcept
{
    if ( !PicassoTextureSet_IsInitialized( pTextureSet ) ) {
        return pTextureSet == nullptr
            ? picasso_texture_set_status_t::INVALID_ARGUMENT
            : picasso_texture_set_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureSet_HasChannel( pTextureSet, semantic ) ) {
        return picasso_texture_set_status_t::CHANNEL_NOT_FOUND;
    }
    const usize iChannel = PicassoTextureSet_ChannelIndex( semantic );
    pTextureSet->channelRevisions[iChannel] =
        PicassoTextureSet_AdvanceRevision( *pTextureSet );
    return picasso_texture_set_status_t::OK;
}

bool_t PicassoTextureSet_Swap(
    picasso_texture_set_t *pLeft,
    picasso_texture_set_t *pRight ) noexcept
{
    if ( pLeft == nullptr || pRight == nullptr || pLeft == pRight ) {
        return pLeft != nullptr && pLeft == pRight;
    }

    const allocator_t *pAllocator = pLeft->pAllocator;
    pLeft->pAllocator = pRight->pAllocator;
    pRight->pAllocator = pAllocator;

    const image_extent_t extent = pLeft->extent;
    pLeft->extent = pRight->extent;
    pRight->extent = extent;

    const picasso_channel_mask_t mask = pLeft->activeChannels;
    pLeft->activeChannels = pRight->activeChannels;
    pRight->activeChannels = mask;

    const u64 revision = pLeft->nRevision;
    pLeft->nRevision = pRight->nRevision;
    pRight->nRevision = revision;

    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        ImageSurface_Swap(
            &pLeft->channels[iChannel],
            &pRight->channels[iChannel] );
        const u64 channelRevision = pLeft->channelRevisions[iChannel];
        pLeft->channelRevisions[iChannel] =
            pRight->channelRevisions[iChannel];
        pRight->channelRevisions[iChannel] = channelRevision;
    }
    return CY_TRUE;
}

const char *PicassoTextureSet_StatusName(
    picasso_texture_set_status_t status ) noexcept
{
    switch ( status ) {
        case picasso_texture_set_status_t::OK:                  return "OK";
        case picasso_texture_set_status_t::INVALID_ARGUMENT:    return "INVALID_ARGUMENT";
        case picasso_texture_set_status_t::NOT_INITIALIZED:     return "NOT_INITIALIZED";
        case picasso_texture_set_status_t::ALREADY_CREATED:     return "ALREADY_CREATED";
        case picasso_texture_set_status_t::NO_TEXTURE_SET:      return "NO_TEXTURE_SET";
        case picasso_texture_set_status_t::INVALID_EXTENT:      return "INVALID_EXTENT";
        case picasso_texture_set_status_t::INVALID_CHANNEL_DESC:return "INVALID_CHANNEL_DESC";
        case picasso_texture_set_status_t::CHANNEL_EXISTS:      return "CHANNEL_EXISTS";
        case picasso_texture_set_status_t::CHANNEL_NOT_FOUND:   return "CHANNEL_NOT_FOUND";
        case picasso_texture_set_status_t::EXTENT_MISMATCH:     return "EXTENT_MISMATCH";
        case picasso_texture_set_status_t::ALLOCATION_FAILED:   return "ALLOCATION_FAILED";
        case picasso_texture_set_status_t::PROCESSING_FAILED:   return "PROCESSING_FAILED";
        case picasso_texture_set_status_t::INVALID_STATE:       return "INVALID_STATE";
        default:                                                 return "UNKNOWN_TEXTURE_SET_STATUS";
    }
}

} // namespace cypher::tools::picasso
