//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoChannel.cpp
//  Purpose: Implements semantic texture-channel policy for Picasso.
//  Details: Descriptor validation rejects physically valid image formats that
//           would carry the wrong meaning, such as sRGB roughness or a scalar
//           base-color channel.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoChannel.h"

#include "CypherCommon_ImageFormat.h"

namespace cypher::tools::picasso
{

namespace
{

bool_t PicassoChannel_HasExpectedComponentCount(
    picasso_channel_semantic_t semantic,
    u8 cChannels ) noexcept
{
    if ( PicassoChannel_IsColor( semantic ) ) {
        return cChannels == 4u;
    }
    if ( semantic == picasso_channel_semantic_t::NORMAL ) {
        // Two-channel normals reconstruct Z; four-channel normals remain useful
        // for interchange and authoring tools that preserve an auxiliary alpha.
        return cChannels == 2u || cChannels == 4u;
    }
    return PicassoChannel_IsScalar( semantic ) && cChannels == 1u;
}

} // namespace

bool_t PicassoChannel_IsSemanticValid(
    picasso_channel_semantic_t semantic ) noexcept
{
    return static_cast<usize>( semantic ) < PICASSO_CHANNEL_COUNT;
}

picasso_channel_mask_t PicassoChannel_Bit(
    picasso_channel_semantic_t semantic ) noexcept
{
    return PicassoChannel_IsSemanticValid( semantic )
        ? CYPHER_BIT32( static_cast<u32>( semantic ) )
        : 0u;
}

bool_t PicassoChannel_IsColor(
    picasso_channel_semantic_t semantic ) noexcept
{
    return semantic == picasso_channel_semantic_t::BASE_COLOR ||
           semantic == picasso_channel_semantic_t::EMISSIVE;
}

bool_t PicassoChannel_IsScalar(
    picasso_channel_semantic_t semantic ) noexcept
{
    return semantic == picasso_channel_semantic_t::ROUGHNESS ||
           semantic == picasso_channel_semantic_t::METALNESS ||
           semantic == picasso_channel_semantic_t::AMBIENT_OCCLUSION ||
           semantic == picasso_channel_semantic_t::HEIGHT ||
           semantic == picasso_channel_semantic_t::OPACITY;
}

picasso_channel_desc_t PicassoChannel_DefaultDesc(
    picasso_channel_semantic_t semantic ) noexcept
{
    switch ( semantic ) {
        case picasso_channel_semantic_t::BASE_COLOR:
            return {
                semantic,
                image_pixel_format_t::RGBA8_UNORM,
                image_color_space_t::SRGB,
                image_alpha_mode_t::STRAIGHT
            };
        case picasso_channel_semantic_t::NORMAL:
            return {
                semantic,
                image_pixel_format_t::RGBA8_UNORM,
                image_color_space_t::LINEAR,
                image_alpha_mode_t::NONE
            };
        case picasso_channel_semantic_t::EMISSIVE:
            return {
                semantic,
                image_pixel_format_t::RGBA8_UNORM,
                image_color_space_t::SRGB,
                image_alpha_mode_t::NONE
            };
        case picasso_channel_semantic_t::HEIGHT:
            return {
                semantic,
                image_pixel_format_t::R16_UNORM,
                image_color_space_t::LINEAR,
                image_alpha_mode_t::NONE
            };
        case picasso_channel_semantic_t::ROUGHNESS:
        case picasso_channel_semantic_t::METALNESS:
        case picasso_channel_semantic_t::AMBIENT_OCCLUSION:
        case picasso_channel_semantic_t::OPACITY:
            return {
                semantic,
                image_pixel_format_t::R8_UNORM,
                image_color_space_t::LINEAR,
                image_alpha_mode_t::NONE
            };
        default:
            return {};
    }
}

colorf_t PicassoChannel_DefaultValue(
    picasso_channel_semantic_t semantic ) noexcept
{
    switch ( semantic ) {
        case picasso_channel_semantic_t::BASE_COLOR:
            return { 0.0f, 0.0f, 0.0f, 0.0f };
        case picasso_channel_semantic_t::NORMAL:
            return { 0.5f, 0.5f, 1.0f, 1.0f };
        case picasso_channel_semantic_t::ROUGHNESS:
        case picasso_channel_semantic_t::AMBIENT_OCCLUSION:
        case picasso_channel_semantic_t::OPACITY:
            return { 1.0f, 0.0f, 0.0f, 1.0f };
        case picasso_channel_semantic_t::HEIGHT:
            return { 0.5f, 0.0f, 0.0f, 1.0f };
        case picasso_channel_semantic_t::METALNESS:
        case picasso_channel_semantic_t::EMISSIVE:
        default:
            return { 0.0f, 0.0f, 0.0f, 1.0f };
    }
}

picasso_channel_status_t PicassoChannel_ValidateDesc(
    const picasso_channel_desc_t &desc ) noexcept
{
    if ( !PicassoChannel_IsSemanticValid( desc.semantic ) ) {
        return picasso_channel_status_t::INVALID_SEMANTIC;
    }

    const image_format_info_t *pFormat =
        ImageFormat_GetInfo( desc.pixelFormat );
    if ( pFormat == nullptr ||
         !PicassoChannel_HasExpectedComponentCount(
             desc.semantic,
             pFormat->cChannels ) ) {
        return picasso_channel_status_t::INVALID_PIXEL_FORMAT;
    }

    if ( desc.colorSpace != image_color_space_t::LINEAR &&
         desc.colorSpace != image_color_space_t::SRGB ) {
        return picasso_channel_status_t::INVALID_COLOR_SPACE;
    }
    if ( !PicassoChannel_IsColor( desc.semantic ) &&
         desc.colorSpace != image_color_space_t::LINEAR ) {
        return picasso_channel_status_t::INVALID_COLOR_SPACE;
    }

    if ( desc.semantic == picasso_channel_semantic_t::BASE_COLOR ) {
        if ( desc.alphaMode != image_alpha_mode_t::STRAIGHT ) {
            return picasso_channel_status_t::INVALID_ALPHA_MODE;
        }
    } else if ( desc.alphaMode != image_alpha_mode_t::NONE ) {
        return picasso_channel_status_t::INVALID_ALPHA_MODE;
    }

    return picasso_channel_status_t::OK;
}

picasso_channel_status_t PicassoChannel_EncodePixel(
    const picasso_channel_desc_t &desc,
    colorf_t value,
    byte_span_t destination ) noexcept
{
    const picasso_channel_status_t validation =
        PicassoChannel_ValidateDesc( desc );
    if ( validation != picasso_channel_status_t::OK ) {
        return validation;
    }
    if ( destination.pData == nullptr ) {
        return picasso_channel_status_t::INVALID_ARGUMENT;
    }

    const image_format_info_t *pFormat =
        ImageFormat_GetInfo( desc.pixelFormat );
    if ( destination.nCount < pFormat->cbPixel ) {
        return picasso_channel_status_t::OUTPUT_TOO_SMALL;
    }

    const image_desc_t sourceDesc{
        { 1u, 1u, 1u },
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT
    };
    const const_image_view_t source{
        sourceDesc,
        {
            reinterpret_cast<const byte *>( &value ),
            sizeof( value )
        },
        sizeof( value ),
        sizeof( value )
    };

    const image_desc_t destinationDesc{
        { 1u, 1u, 1u },
        desc.pixelFormat,
        desc.colorSpace,
        desc.alphaMode
    };
    const image_view_t destinationView{
        destinationDesc,
        { destination.pData, pFormat->cbPixel },
        pFormat->cbPixel,
        pFormat->cbPixel
    };

    return ImageConvert( destinationView, source ) ==
            image_convert_status_t::OK
        ? picasso_channel_status_t::OK
        : picasso_channel_status_t::ENCODE_FAILED;
}

const char *PicassoChannel_Name(
    picasso_channel_semantic_t semantic ) noexcept
{
    switch ( semantic ) {
        case picasso_channel_semantic_t::BASE_COLOR:        return "Base Color";
        case picasso_channel_semantic_t::NORMAL:            return "Normal";
        case picasso_channel_semantic_t::ROUGHNESS:         return "Roughness";
        case picasso_channel_semantic_t::METALNESS:         return "Metalness";
        case picasso_channel_semantic_t::AMBIENT_OCCLUSION: return "Ambient Occlusion";
        case picasso_channel_semantic_t::EMISSIVE:          return "Emissive";
        case picasso_channel_semantic_t::HEIGHT:            return "Height";
        case picasso_channel_semantic_t::OPACITY:           return "Opacity";
        default:                                             return "Unknown Channel";
    }
}

const char *PicassoChannel_StatusName(
    picasso_channel_status_t status ) noexcept
{
    switch ( status ) {
        case picasso_channel_status_t::OK:                  return "OK";
        case picasso_channel_status_t::INVALID_ARGUMENT:    return "INVALID_ARGUMENT";
        case picasso_channel_status_t::INVALID_SEMANTIC:    return "INVALID_SEMANTIC";
        case picasso_channel_status_t::INVALID_PIXEL_FORMAT:return "INVALID_PIXEL_FORMAT";
        case picasso_channel_status_t::INVALID_COLOR_SPACE: return "INVALID_COLOR_SPACE";
        case picasso_channel_status_t::INVALID_ALPHA_MODE:  return "INVALID_ALPHA_MODE";
        case picasso_channel_status_t::OUTPUT_TOO_SMALL:    return "OUTPUT_TOO_SMALL";
        case picasso_channel_status_t::ENCODE_FAILED:       return "ENCODE_FAILED";
        default:                                             return "UNKNOWN_CHANNEL_STATUS";
    }
}

} // namespace cypher::tools::picasso
