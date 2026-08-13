//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp
//  Purpose: Implements typed decoding for renderer-facing source assets.
//  Details: Generic schema validation establishes shape and bounds first. These
//           decoders then enforce resource extensions, canonical paths, identifier
//           grammar, duplicate policy, and cross-field texture semantics.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_RenderAsset.h"

#include "CypherCommon_DataValidation.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t AssetText(
    const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

CYPHER_NODISCARD bool_t ReadRequiredString(
    const key_value_t *pObject,
    string_view_t name,
    string_view_t &valueOut ) noexcept
{
    return KeyValue_GetString( KeyValue_Find( pObject, name ), &valueOut );
}

CYPHER_NODISCARD bool_t ReadOptionalString(
    const key_value_t *pObject,
    string_view_t name,
    string_view_t &valueOut,
    bool_t &bFoundOut ) noexcept
{
    const key_value_t *pValue = KeyValue_Find( pObject, name );
    bFoundOut = pValue != nullptr;
    return pValue == nullptr || KeyValue_GetString( pValue, &valueOut );
}

CYPHER_NODISCARD bool_t ReadOptionalBool(
    const key_value_t *pObject,
    string_view_t name,
    bool_t &valueOut ) noexcept
{
    const key_value_t *pValue = KeyValue_Find( pObject, name );
    return pValue == nullptr || KeyValue_GetBool( pValue, &valueOut );
}

CYPHER_NODISCARD bool_t ReadNumberAsF64(
    const key_value_t *pValue,
    f64 &valueOut ) noexcept
{
    switch ( KeyValue_Type( pValue ) ) {
        case key_value_type_t::I64: {
            i64 value = 0;
            if ( !KeyValue_GetI64( pValue, &value ) ) {
                return CY_FALSE;
            }
            valueOut = static_cast<f64>( value );
            return CY_TRUE;
        }
        case key_value_type_t::U64: {
            u64 value = 0u;
            if ( !KeyValue_GetU64( pValue, &value ) ) {
                return CY_FALSE;
            }
            valueOut = static_cast<f64>( value );
            return CY_TRUE;
        }
        case key_value_type_t::F64:
            return KeyValue_GetF64( pValue, &valueOut );
        default:
            return CY_FALSE;
    }
}

CYPHER_NODISCARD bool_t IsCanonicalPathWithAnyExtension(
    string_view_t path,
    const string_view_t *pExtensions,
    usize nExtensions ) noexcept
{
    if ( !DataValidation_Succeeded(
             DataValidation_CheckCanonicalVirtualPath(
                 path,
                 CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
        return CY_FALSE;
    }
    for ( usize iExtension = 0u;
          iExtension < nExtensions;
          ++iExtension ) {
        if ( StringPath_HasExtension(
                 path,
                 pExtensions[iExtension],
                 CY_FALSE ) ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ParseTextureUsage(
    string_view_t value,
    render_texture_usage_t &usageOut ) noexcept
{
    if ( StringView_Equals( value, AssetText( "color" ) ) ) {
        usageOut = render_texture_usage_t::COLOR;
        return CY_TRUE;
    }
    if ( StringView_Equals( value, AssetText( "normal" ) ) ) {
        usageOut = render_texture_usage_t::NORMAL;
        return CY_TRUE;
    }
    if ( StringView_Equals( value, AssetText( "data" ) ) ) {
        usageOut = render_texture_usage_t::DATA;
        return CY_TRUE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ParseTextureColorSpace(
    string_view_t value,
    render_texture_color_space_t &colorSpaceOut ) noexcept
{
    if ( StringView_Equals( value, AssetText( "srgb" ) ) ) {
        colorSpaceOut = render_texture_color_space_t::SRGB;
        return CY_TRUE;
    }
    if ( StringView_Equals( value, AssetText( "linear" ) ) ) {
        colorSpaceOut = render_texture_color_space_t::LINEAR;
        return CY_TRUE;
    }
    return CY_FALSE;
}

void SetSemanticFailure(
    render_asset_decode_result_t &result,
    render_asset_decode_status_t status,
    string_view_t field,
    usize iElement = CY_INVALID_SIZE ) noexcept
{
    result.status = status;
    result.field = field;
    result.iElement = iElement;
}

} // namespace

render_asset_decode_result_t RenderShaderSource_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_shader_source_view_t *pShaderOut ) noexcept
{
    render_asset_decode_result_t result{};
    if ( pDocument == nullptr || pShaderOut == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ) {
        result.status = render_asset_decode_status_t::INVALID_ARGUMENT;
        result.validation.status = schema_validation_status_t::INVALID_ARGUMENT;
        return result;
    }

    result.validation = Schema_ValidateDocument(
        RenderShaderSchema_V1(),
        pDocument,
        options,
        pDiagnostics,
        nDiagnosticCapacity );
    if ( !Schema_ValidationSucceeded( result.validation ) ) {
        result.status = render_asset_decode_status_t::INVALID_DOCUMENT;
        return result;
    }

    const key_value_t *pRoot = KeyValue_Root( pDocument );
    render_shader_source_view_t shader{};
    string_view_t language{};
    if ( !ReadRequiredString( pRoot, AssetText( "language" ), language ) ||
         !ReadRequiredString( pRoot, AssetText( "vertex" ), shader.vertexSource ) ||
         !ReadRequiredString(
             pRoot,
             AssetText( "fragment" ),
             shader.fragmentSource ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }
    if ( !StringView_Equals( language, AssetText( "glsl" ) ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }

    constexpr string_view_t vertexExtensions[]{
        AssetText( ".vert" ),
        AssetText( ".glsl" )
    };
    constexpr string_view_t fragmentExtensions[]{
        AssetText( ".frag" ),
        AssetText( ".glsl" )
    };
    if ( !IsCanonicalPathWithAnyExtension(
             shader.vertexSource,
             vertexExtensions,
             sizeof( vertexExtensions ) / sizeof( vertexExtensions[0] ) ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_RESOURCE_PATH,
            AssetText( "vertex" ) );
        return result;
    }
    if ( !IsCanonicalPathWithAnyExtension(
             shader.fragmentSource,
             fragmentExtensions,
             sizeof( fragmentExtensions ) / sizeof( fragmentExtensions[0] ) ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_RESOURCE_PATH,
            AssetText( "fragment" ) );
        return result;
    }

    const key_value_t *pDefines = KeyValue_Find(
        pRoot,
        AssetText( "defines" ) );
    if ( pDefines != nullptr ) {
        shader.nDefines = KeyValue_ChildCount( pDefines );
        if ( shader.nDefines > CY_RENDER_SHADER_MAX_DEFINES ) {
            result.status = render_asset_decode_status_t::INTERNAL_ERROR;
            return result;
        }
        for ( usize iDefine = 0u;
              iDefine < shader.nDefines;
              ++iDefine ) {
            if ( !KeyValue_GetString(
                     KeyValue_ChildAt( pDefines, iDefine ),
                     &shader.defines[iDefine] ) ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            if ( !DataValidation_Succeeded(
                     DataValidation_CheckAsciiIdentifier(
                         shader.defines[iDefine],
                         CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "defines" ),
                    iDefine );
                return result;
            }

            for ( usize iPrevious = 0u;
                  iPrevious < iDefine;
                  ++iPrevious ) {
                if ( StringView_Equals(
                         shader.defines[iPrevious],
                         shader.defines[iDefine] ) ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::DUPLICATE_VALUE,
                        AssetText( "defines" ),
                        iDefine );
                    return result;
                }
            }
        }
    }

    *pShaderOut = shader;
    return result;
}

render_asset_decode_result_t RenderTextureSource_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_texture_source_view_t *pTextureOut ) noexcept
{
    render_asset_decode_result_t result{};
    if ( pDocument == nullptr || pTextureOut == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ) {
        result.status = render_asset_decode_status_t::INVALID_ARGUMENT;
        result.validation.status = schema_validation_status_t::INVALID_ARGUMENT;
        return result;
    }

    result.validation = Schema_ValidateDocument(
        RenderTextureSchema_V1(),
        pDocument,
        options,
        pDiagnostics,
        nDiagnosticCapacity );
    if ( !Schema_ValidationSucceeded( result.validation ) ) {
        result.status = render_asset_decode_status_t::INVALID_DOCUMENT;
        return result;
    }

    const key_value_t *pRoot = KeyValue_Root( pDocument );
    render_texture_source_view_t texture{};
    if ( !ReadRequiredString( pRoot, AssetText( "source" ), texture.source ) ||
         !ReadOptionalBool(
             pRoot,
             AssetText( "generate_mips" ),
             texture.bGenerateMips ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }

    constexpr string_view_t sourceExtensions[]{
        AssetText( ".png" ),
        AssetText( ".jpg" ),
        AssetText( ".jpeg" ),
        AssetText( ".exr" ),
        AssetText( ".ktx" ),
        AssetText( ".ktx2" )
    };
    if ( !IsCanonicalPathWithAnyExtension(
             texture.source,
             sourceExtensions,
             sizeof( sourceExtensions ) / sizeof( sourceExtensions[0] ) ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_RESOURCE_PATH,
            AssetText( "source" ) );
        return result;
    }

    string_view_t usageText{};
    bool_t bHasUsage = CY_FALSE;
    if ( !ReadOptionalString(
             pRoot,
             AssetText( "usage" ),
             usageText,
             bHasUsage ) ||
         ( bHasUsage && !ParseTextureUsage( usageText, texture.usage ) ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }

    string_view_t colorSpaceText{};
    bool_t bHasColorSpace = CY_FALSE;
    if ( !ReadOptionalString(
             pRoot,
             AssetText( "color_space" ),
             colorSpaceText,
             bHasColorSpace ) ||
         ( bHasColorSpace &&
           !ParseTextureColorSpace( colorSpaceText, texture.colorSpace ) ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }

    if ( !bHasColorSpace && texture.usage != render_texture_usage_t::COLOR ) {
        texture.colorSpace = render_texture_color_space_t::LINEAR;
    }
    if ( texture.usage != render_texture_usage_t::COLOR &&
         texture.colorSpace != render_texture_color_space_t::LINEAR ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "color_space" ) );
        return result;
    }

    *pTextureOut = texture;
    return result;
}

render_asset_decode_result_t RenderMaterialSource_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_material_source_view_t *pMaterialOut ) noexcept
{
    render_asset_decode_result_t result{};
    if ( pDocument == nullptr || pMaterialOut == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ) {
        result.status = render_asset_decode_status_t::INVALID_ARGUMENT;
        result.validation.status = schema_validation_status_t::INVALID_ARGUMENT;
        return result;
    }

    result.validation = Schema_ValidateDocument(
        RenderMaterialSchema_V1(),
        pDocument,
        options,
        pDiagnostics,
        nDiagnosticCapacity );
    if ( !Schema_ValidationSucceeded( result.validation ) ) {
        result.status = render_asset_decode_status_t::INVALID_DOCUMENT;
        return result;
    }

    const key_value_t *pRoot = KeyValue_Root( pDocument );
    render_material_source_view_t material{};
    if ( !ReadRequiredString( pRoot, AssetText( "shader" ), material.shader ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }
    if ( !DataValidation_Succeeded(
             DataValidation_CheckResourcePath(
                 material.shader,
                 AssetText( ".cyshader" ),
                 CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_RESOURCE_PATH,
            AssetText( "shader" ) );
        return result;
    }

    const key_value_t *pTextures = KeyValue_Find(
        pRoot,
        AssetText( "textures" ) );
    if ( pTextures != nullptr ) {
        material.nTextures = KeyValue_ChildCount( pTextures );
        if ( material.nTextures > CY_RENDER_MATERIAL_MAX_TEXTURES ) {
            result.status = render_asset_decode_status_t::INTERNAL_ERROR;
            return result;
        }
        for ( usize iTexture = 0u;
              iTexture < material.nTextures;
              ++iTexture ) {
            const key_value_t *pTexture = KeyValue_ChildAt(
                pTextures,
                iTexture );
            render_material_texture_view_t &binding =
                material.textures[iTexture];
            binding.binding = KeyValue_Name( pTexture );
            if ( !KeyValue_GetString( pTexture, &binding.texture ) ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            if ( !DataValidation_Succeeded(
                     DataValidation_CheckAsciiIdentifier(
                         binding.binding,
                         CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "textures" ),
                    iTexture );
                return result;
            }
            if ( !DataValidation_Succeeded(
                     DataValidation_CheckResourcePath(
                         binding.texture,
                         AssetText( ".cytex" ),
                         CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_RESOURCE_PATH,
                    AssetText( "textures" ),
                    iTexture );
                return result;
            }
        }
    }

    const key_value_t *pParameters = KeyValue_Find(
        pRoot,
        AssetText( "parameters" ) );
    if ( pParameters != nullptr ) {
        material.nParameters = KeyValue_ChildCount( pParameters );
        if ( material.nParameters > CY_RENDER_MATERIAL_MAX_PARAMETERS ) {
            result.status = render_asset_decode_status_t::INTERNAL_ERROR;
            return result;
        }
        for ( usize iParameter = 0u;
              iParameter < material.nParameters;
              ++iParameter ) {
            const key_value_t *pValue = KeyValue_ChildAt(
                pParameters,
                iParameter );
            render_material_parameter_view_t &parameter =
                material.parameters[iParameter];
            parameter.name = KeyValue_Name( pValue );
            if ( !DataValidation_Succeeded(
                     DataValidation_CheckAsciiIdentifier(
                         parameter.name,
                         CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "parameters" ),
                    iParameter );
                return result;
            }

            switch ( KeyValue_Type( pValue ) ) {
                case key_value_type_t::BOOL:
                    parameter.type = render_material_parameter_type_t::BOOL;
                    if ( !KeyValue_GetBool( pValue, &parameter.bValue ) ) {
                        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                        return result;
                    }
                    break;
                case key_value_type_t::I64:
                case key_value_type_t::U64:
                case key_value_type_t::F64:
                    parameter.type = render_material_parameter_type_t::SCALAR;
                    parameter.nComponents = 1u;
                    if ( !ReadNumberAsF64( pValue, parameter.values[0] ) ) {
                        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                        return result;
                    }
                    break;
                case key_value_type_t::ARRAY:
                    parameter.type = render_material_parameter_type_t::VECTOR;
                    parameter.nComponents = KeyValue_ChildCount( pValue );
                    if ( parameter.nComponents >
                         CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS ) {
                        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                        return result;
                    }
                    for ( usize iComponent = 0u;
                          iComponent < parameter.nComponents;
                          ++iComponent ) {
                        if ( !ReadNumberAsF64(
                                 KeyValue_ChildAt( pValue, iComponent ),
                                 parameter.values[iComponent] ) ) {
                            result.status =
                                render_asset_decode_status_t::INTERNAL_ERROR;
                            return result;
                        }
                    }
                    break;
                default:
                    result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                    return result;
            }
        }
    }

    *pMaterialOut = material;
    return result;
}

bool_t RenderAsset_DecodeSucceeded(
    const render_asset_decode_result_t &result ) noexcept
{
    return result.status == render_asset_decode_status_t::OK;
}

const char *RenderAsset_DecodeStatusName(
    render_asset_decode_status_t status ) noexcept
{
    switch ( status ) {
        case render_asset_decode_status_t::OK: return "OK";
        case render_asset_decode_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case render_asset_decode_status_t::INVALID_DOCUMENT: return "INVALID_DOCUMENT";
        case render_asset_decode_status_t::INVALID_IDENTIFIER: return "INVALID_IDENTIFIER";
        case render_asset_decode_status_t::INVALID_RESOURCE_PATH: return "INVALID_RESOURCE_PATH";
        case render_asset_decode_status_t::DUPLICATE_VALUE: return "DUPLICATE_VALUE";
        case render_asset_decode_status_t::INVALID_COMBINATION: return "INVALID_COMBINATION";
        case render_asset_decode_status_t::INTERNAL_ERROR: return "INTERNAL_ERROR";
    }
    return "UNKNOWN";
}

const char *RenderTextureUsage_Name(
    render_texture_usage_t usage ) noexcept
{
    switch ( usage ) {
        case render_texture_usage_t::COLOR: return "color";
        case render_texture_usage_t::NORMAL: return "normal";
        case render_texture_usage_t::DATA: return "data";
    }
    return "unknown";
}

const char *RenderTextureColorSpace_Name(
    render_texture_color_space_t colorSpace ) noexcept
{
    switch ( colorSpace ) {
        case render_texture_color_space_t::SRGB: return "srgb";
        case render_texture_color_space_t::LINEAR: return "linear";
    }
    return "unknown";
}

} // namespace cypher::common
