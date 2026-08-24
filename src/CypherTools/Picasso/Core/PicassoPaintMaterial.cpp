//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoPaintMaterial.cpp
//  Purpose: Implements Picasso paint-material state and validation.
//  Details: Mutations validate complete replacement state before publication,
//           keeping material browsers and future brush tools free from partial
//           paths, non-finite values, and collapsed texture mappings.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoPaintMaterial.h"

#include "CypherCommon_DataValidation.h"
#include "CypherCommon_Unicode.h"

#include <cmath>

namespace cypher::tools::picasso
{

namespace
{

constexpr string_view_t PICASSO_TEXTURE_EXTENSION{ ".cytex", 6u };
constexpr string_view_t PICASSO_SHADER_EXTENSION{ ".cyshader", 9u };

usize PicassoPaintMaterial_ChannelIndex(
    picasso_channel_semantic_t semantic ) noexcept
{
    return static_cast<usize>( semantic );
}

bool_t PicassoPaintMaterial_IsFinite( colorf_t value ) noexcept
{
    return std::isfinite( value.r ) && std::isfinite( value.g ) &&
           std::isfinite( value.b ) && std::isfinite( value.a );
}

bool_t PicassoPaintMaterial_IsStrengthValid( f32 strength ) noexcept
{
    return std::isfinite( strength ) &&
           strength >= 0.0f && strength <= 1.0f;
}

bool_t PicassoPaintMaterial_IsMappingValid(
    const picasso_material_mapping_t &mapping ) noexcept
{
    return std::isfinite( mapping.scaleU ) &&
           std::isfinite( mapping.scaleV ) &&
           std::isfinite( mapping.offsetU ) &&
           std::isfinite( mapping.offsetV ) &&
           std::isfinite( mapping.rotationDegrees ) &&
           mapping.scaleU != 0.0f && mapping.scaleV != 0.0f;
}

bool_t PicassoPaintMaterial_IsNameValid( string_view_t name ) noexcept
{
    if ( name.pData == nullptr || name.cchLength == 0u ||
         name.cchLength > PICASSO_MATERIAL_NAME_CAPACITY ||
         Unicode_ValidateUtf8( name ).status != unicode_status_t::OK ) {
        return CY_FALSE;
    }

    // Embedded nulls would make UI and serialization views disagree about the
    // owned name even though the surrounding UTF-8 byte sequence is valid.
    for ( usize iByte = 0u; iByte < name.cchLength; ++iByte ) {
        if ( name.pData[iByte] == '\0' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t PicassoPaintMaterial_IsTexturePathValid(
    string_view_t path ) noexcept
{
    return DataValidation_Succeeded(
        DataValidation_CheckResourcePath(
            path,
            PICASSO_TEXTURE_EXTENSION,
            PICASSO_MATERIAL_PATH_CAPACITY ) );
}

bool_t PicassoPaintMaterial_IsShaderPathValid(
    string_view_t path ) noexcept
{
    return DataValidation_Succeeded(
        DataValidation_CheckResourcePath(
            path,
            PICASSO_SHADER_EXTENSION,
            PICASSO_MATERIAL_PATH_CAPACITY ) );
}

u64 PicassoPaintMaterial_AdvanceRevision(
    picasso_paint_material_t &material ) noexcept
{
    material.nRevision = material.nRevision == CY_U64_MAX
        ? 1u
        : material.nRevision + 1u;
    if ( material.nRevision == 0u ) {
        material.nRevision = 1u;
    }
    return material.nRevision;
}

picasso_paint_material_status_t PicassoPaintMaterial_ValidateChannel(
    const picasso_material_channel_t &channel ) noexcept
{
    if ( channel.kind != picasso_material_source_kind_t::CONSTANT &&
         channel.kind != picasso_material_source_kind_t::TEXTURE_RESOURCE ) {
        return picasso_paint_material_status_t::INVALID_SOURCE_KIND;
    }
    if ( !PicassoPaintMaterial_IsFinite( channel.constant ) ) {
        return picasso_paint_material_status_t::INVALID_VALUE;
    }
    if ( !PicassoPaintMaterial_IsStrengthValid( channel.strength ) ) {
        return picasso_paint_material_status_t::INVALID_STRENGTH;
    }
    if ( !PicassoPaintMaterial_IsMappingValid( channel.mapping ) ) {
        return picasso_paint_material_status_t::INVALID_MAPPING;
    }
    if ( channel.kind == picasso_material_source_kind_t::TEXTURE_RESOURCE &&
         !PicassoPaintMaterial_IsTexturePathValid(
             FixedString_View( channel.texture ) ) ) {
        return picasso_paint_material_status_t::INVALID_RESOURCE_PATH;
    }
    if ( channel.kind == picasso_material_source_kind_t::CONSTANT &&
         !FixedString_IsEmpty( channel.texture ) ) {
        return picasso_paint_material_status_t::INVALID_STATE;
    }
    return picasso_paint_material_status_t::OK;
}

} // namespace

picasso_paint_material_status_t PicassoPaintMaterial_Init(
    picasso_paint_material_t *pMaterial,
    string_view_t name ) noexcept
{
    if ( pMaterial == nullptr ) {
        return picasso_paint_material_status_t::INVALID_ARGUMENT;
    }
    if ( pMaterial->nRevision != 0u || pMaterial->activeChannels != 0u ||
         !FixedString_IsEmpty( pMaterial->name ) ) {
        return picasso_paint_material_status_t::INVALID_STATE;
    }
    if ( !PicassoPaintMaterial_IsNameValid( name ) ) {
        return picasso_paint_material_status_t::INVALID_NAME;
    }

    (void) FixedString_Assign( &pMaterial->name, name );
    PicassoPaintMaterial_AdvanceRevision( *pMaterial );
    return picasso_paint_material_status_t::OK;
}

void PicassoPaintMaterial_Reset(
    picasso_paint_material_t *pMaterial ) noexcept
{
    if ( pMaterial == nullptr ) {
        return;
    }
    *pMaterial = {};
}

picasso_paint_material_status_t PicassoPaintMaterial_SetName(
    picasso_paint_material_t *pMaterial,
    string_view_t name ) noexcept
{
    if ( pMaterial == nullptr ) {
        return picasso_paint_material_status_t::INVALID_ARGUMENT;
    }
    if ( pMaterial->nRevision == 0u ) {
        return picasso_paint_material_status_t::INVALID_STATE;
    }
    if ( !PicassoPaintMaterial_IsNameValid( name ) ) {
        return picasso_paint_material_status_t::INVALID_NAME;
    }

    (void) FixedString_Assign( &pMaterial->name, name );
    PicassoPaintMaterial_AdvanceRevision( *pMaterial );
    return picasso_paint_material_status_t::OK;
}

picasso_paint_material_status_t PicassoPaintMaterial_SetShader(
    picasso_paint_material_t *pMaterial,
    string_view_t shaderPath ) noexcept
{
    if ( pMaterial == nullptr ) {
        return picasso_paint_material_status_t::INVALID_ARGUMENT;
    }
    if ( pMaterial->nRevision == 0u ) {
        return picasso_paint_material_status_t::INVALID_STATE;
    }
    if ( shaderPath.cchLength != 0u &&
         !PicassoPaintMaterial_IsShaderPathValid( shaderPath ) ) {
        return picasso_paint_material_status_t::INVALID_RESOURCE_PATH;
    }

    FixedString_Clear( &pMaterial->shader );
    if ( shaderPath.cchLength != 0u ) {
        (void) FixedString_Assign( &pMaterial->shader, shaderPath );
    }
    PicassoPaintMaterial_AdvanceRevision( *pMaterial );
    return picasso_paint_material_status_t::OK;
}

picasso_paint_material_status_t PicassoPaintMaterial_SetConstant(
    picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic,
    colorf_t value,
    f32 strength ) noexcept
{
    if ( pMaterial == nullptr ) {
        return picasso_paint_material_status_t::INVALID_ARGUMENT;
    }
    if ( pMaterial->nRevision == 0u ) {
        return picasso_paint_material_status_t::INVALID_STATE;
    }
    if ( !PicassoChannel_IsSemanticValid( semantic ) ) {
        return picasso_paint_material_status_t::INVALID_SEMANTIC;
    }
    if ( !PicassoPaintMaterial_IsFinite( value ) ) {
        return picasso_paint_material_status_t::INVALID_VALUE;
    }
    if ( !PicassoPaintMaterial_IsStrengthValid( strength ) ) {
        return picasso_paint_material_status_t::INVALID_STRENGTH;
    }

    picasso_material_channel_t pending{};
    pending.kind = picasso_material_source_kind_t::CONSTANT;
    pending.constant = value;
    pending.strength = strength;

    const usize iChannel = PicassoPaintMaterial_ChannelIndex( semantic );
    pMaterial->channels[iChannel] = pending;
    pMaterial->activeChannels |= PicassoChannel_Bit( semantic );
    PicassoPaintMaterial_AdvanceRevision( *pMaterial );
    return picasso_paint_material_status_t::OK;
}

picasso_paint_material_status_t PicassoPaintMaterial_SetTexture(
    picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic,
    string_view_t texturePath,
    f32 strength ) noexcept
{
    if ( pMaterial == nullptr ) {
        return picasso_paint_material_status_t::INVALID_ARGUMENT;
    }
    if ( pMaterial->nRevision == 0u ) {
        return picasso_paint_material_status_t::INVALID_STATE;
    }
    if ( !PicassoChannel_IsSemanticValid( semantic ) ) {
        return picasso_paint_material_status_t::INVALID_SEMANTIC;
    }
    if ( !PicassoPaintMaterial_IsTexturePathValid( texturePath ) ) {
        return picasso_paint_material_status_t::INVALID_RESOURCE_PATH;
    }
    if ( !PicassoPaintMaterial_IsStrengthValid( strength ) ) {
        return picasso_paint_material_status_t::INVALID_STRENGTH;
    }

    picasso_material_channel_t pending{};
    pending.kind = picasso_material_source_kind_t::TEXTURE_RESOURCE;
    pending.constant = PicassoChannel_DefaultValue( semantic );
    pending.strength = strength;
    (void) FixedString_Assign( &pending.texture, texturePath );

    const usize iChannel = PicassoPaintMaterial_ChannelIndex( semantic );
    pMaterial->channels[iChannel] = pending;
    pMaterial->activeChannels |= PicassoChannel_Bit( semantic );
    PicassoPaintMaterial_AdvanceRevision( *pMaterial );
    return picasso_paint_material_status_t::OK;
}

picasso_paint_material_status_t PicassoPaintMaterial_SetMapping(
    picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic,
    const picasso_material_mapping_t &mapping ) noexcept
{
    if ( pMaterial == nullptr ) {
        return picasso_paint_material_status_t::INVALID_ARGUMENT;
    }
    if ( !PicassoChannel_IsSemanticValid( semantic ) ) {
        return picasso_paint_material_status_t::INVALID_SEMANTIC;
    }
    if ( !PicassoPaintMaterial_IsMappingValid( mapping ) ) {
        return picasso_paint_material_status_t::INVALID_MAPPING;
    }

    picasso_material_channel_t *pChannel =
        PicassoPaintMaterial_HasChannel( pMaterial, semantic )
        ? &pMaterial->channels[PicassoPaintMaterial_ChannelIndex( semantic )]
        : nullptr;
    if ( pChannel == nullptr ) {
        return picasso_paint_material_status_t::CHANNEL_DISABLED;
    }

    pChannel->mapping = mapping;
    PicassoPaintMaterial_AdvanceRevision( *pMaterial );
    return picasso_paint_material_status_t::OK;
}

picasso_paint_material_status_t PicassoPaintMaterial_SetStrength(
    picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic,
    f32 strength ) noexcept
{
    if ( pMaterial == nullptr ) {
        return picasso_paint_material_status_t::INVALID_ARGUMENT;
    }
    if ( !PicassoChannel_IsSemanticValid( semantic ) ) {
        return picasso_paint_material_status_t::INVALID_SEMANTIC;
    }
    if ( !PicassoPaintMaterial_IsStrengthValid( strength ) ) {
        return picasso_paint_material_status_t::INVALID_STRENGTH;
    }

    picasso_material_channel_t *pChannel =
        PicassoPaintMaterial_HasChannel( pMaterial, semantic )
        ? &pMaterial->channels[PicassoPaintMaterial_ChannelIndex( semantic )]
        : nullptr;
    if ( pChannel == nullptr ) {
        return picasso_paint_material_status_t::CHANNEL_DISABLED;
    }

    pChannel->strength = strength;
    PicassoPaintMaterial_AdvanceRevision( *pMaterial );
    return picasso_paint_material_status_t::OK;
}

picasso_paint_material_status_t PicassoPaintMaterial_DisableChannel(
    picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic ) noexcept
{
    if ( pMaterial == nullptr ) {
        return picasso_paint_material_status_t::INVALID_ARGUMENT;
    }
    if ( !PicassoChannel_IsSemanticValid( semantic ) ) {
        return picasso_paint_material_status_t::INVALID_SEMANTIC;
    }
    if ( !PicassoPaintMaterial_HasChannel( pMaterial, semantic ) ) {
        return picasso_paint_material_status_t::CHANNEL_DISABLED;
    }

    const usize iChannel = PicassoPaintMaterial_ChannelIndex( semantic );
    pMaterial->channels[iChannel] = {};
    pMaterial->activeChannels &= ~PicassoChannel_Bit( semantic );
    PicassoPaintMaterial_AdvanceRevision( *pMaterial );
    return picasso_paint_material_status_t::OK;
}

bool_t PicassoPaintMaterial_IsValid(
    const picasso_paint_material_t *pMaterial ) noexcept
{
    if ( pMaterial == nullptr || pMaterial->nRevision == 0u ||
         !PicassoPaintMaterial_IsNameValid(
             FixedString_View( pMaterial->name ) ) ) {
        return CY_FALSE;
    }
    if ( !FixedString_IsEmpty( pMaterial->shader ) &&
         !PicassoPaintMaterial_IsShaderPathValid(
             FixedString_View( pMaterial->shader ) ) ) {
        return CY_FALSE;
    }

    const picasso_channel_mask_t validMask =
        CYPHER_BIT32( static_cast<u32>( PICASSO_CHANNEL_COUNT ) ) - 1u;
    if ( ( pMaterial->activeChannels & ~validMask ) != 0u ) {
        return CY_FALSE;
    }

    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        const auto semantic =
            static_cast<picasso_channel_semantic_t>( iChannel );
        const bool_t bActive =
            ( pMaterial->activeChannels & PicassoChannel_Bit( semantic ) ) != 0u;
        const picasso_material_channel_t &channel =
            pMaterial->channels[iChannel];
        if ( bActive ) {
            if ( PicassoPaintMaterial_ValidateChannel( channel ) !=
                 picasso_paint_material_status_t::OK ) {
                return CY_FALSE;
            }
        } else if ( channel.kind !=
                   picasso_material_source_kind_t::DISABLED ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t PicassoPaintMaterial_HasChannel(
    const picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic ) noexcept
{
    return pMaterial != nullptr &&
           PicassoChannel_IsSemanticValid( semantic ) &&
           ( pMaterial->activeChannels & PicassoChannel_Bit( semantic ) ) != 0u;
}

usize PicassoPaintMaterial_ChannelCount(
    const picasso_paint_material_t *pMaterial ) noexcept
{
    if ( pMaterial == nullptr ) {
        return 0u;
    }
    usize nChannels = 0u;
    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        nChannels += PicassoPaintMaterial_HasChannel(
            pMaterial,
            static_cast<picasso_channel_semantic_t>( iChannel ) )
            ? 1u
            : 0u;
    }
    return nChannels;
}

const picasso_material_channel_t *PicassoPaintMaterial_GetChannel(
    const picasso_paint_material_t *pMaterial,
    picasso_channel_semantic_t semantic ) noexcept
{
    return PicassoPaintMaterial_HasChannel( pMaterial, semantic )
        ? &pMaterial->channels[PicassoPaintMaterial_ChannelIndex( semantic )]
        : nullptr;
}

const char *PicassoPaintMaterial_StatusName(
    picasso_paint_material_status_t status ) noexcept
{
    switch ( status ) {
        case picasso_paint_material_status_t::OK:                   return "OK";
        case picasso_paint_material_status_t::INVALID_ARGUMENT:     return "INVALID_ARGUMENT";
        case picasso_paint_material_status_t::INVALID_NAME:         return "INVALID_NAME";
        case picasso_paint_material_status_t::INVALID_SEMANTIC:     return "INVALID_SEMANTIC";
        case picasso_paint_material_status_t::INVALID_SOURCE_KIND:  return "INVALID_SOURCE_KIND";
        case picasso_paint_material_status_t::INVALID_VALUE:        return "INVALID_VALUE";
        case picasso_paint_material_status_t::INVALID_STRENGTH:     return "INVALID_STRENGTH";
        case picasso_paint_material_status_t::INVALID_MAPPING:      return "INVALID_MAPPING";
        case picasso_paint_material_status_t::INVALID_RESOURCE_PATH:return "INVALID_RESOURCE_PATH";
        case picasso_paint_material_status_t::CHANNEL_DISABLED:     return "CHANNEL_DISABLED";
        case picasso_paint_material_status_t::INVALID_STATE:        return "INVALID_STATE";
        default:                                                     return "UNKNOWN_PAINT_MATERIAL_STATUS";
    }
}

} // namespace cypher::tools::picasso
