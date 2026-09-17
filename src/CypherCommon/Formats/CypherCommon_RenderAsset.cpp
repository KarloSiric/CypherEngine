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
    // Compile-time literals become views without carrying their trailing NUL.
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

CYPHER_NODISCARD bool_t ReadOptionalNumber(
    const key_value_t *pObject,
    string_view_t name,
    f64 &valueOut,
    bool_t &bFoundOut ) noexcept;

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

CYPHER_NODISCARD bool_t ReadOptionalNumber(
    const key_value_t *pObject,
    string_view_t name,
    f64 &valueOut,
    bool_t &bFoundOut ) noexcept
{
    const key_value_t *pValue = KeyValue_Find( pObject, name );
    bFoundOut = pValue != nullptr;
    return pValue == nullptr || ReadNumberAsF64( pValue, valueOut );
}

CYPHER_NODISCARD bool_t ReadUnsignedInteger(
    const key_value_t *pValue,
    u64 &valueOut ) noexcept
{
    if ( KeyValue_Type( pValue ) == key_value_type_t::U64 ) {
        return KeyValue_GetU64( pValue, &valueOut );
    }
    if ( KeyValue_Type( pValue ) == key_value_type_t::I64 ) {
        i64 value = 0;
        if ( !KeyValue_GetI64( pValue, &value ) || value < 0 ) {
            return CY_FALSE;
        }
        valueOut = static_cast<u64>( value );
        return CY_TRUE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ReadAssetValue(
    const key_value_t *pValue,
    render_asset_value_view_t &valueOut ) noexcept
{
    render_asset_value_view_t value{};
    switch ( KeyValue_Type( pValue ) ) {
        case key_value_type_t::BOOL:
            value.type = render_asset_value_type_t::BOOL;
            if ( !KeyValue_GetBool( pValue, &value.bValue ) ) {
                return CY_FALSE;
            }
            break;
        case key_value_type_t::I64:
            value.type = render_asset_value_type_t::I64;
            if ( !KeyValue_GetI64( pValue, &value.iValue ) ) {
                return CY_FALSE;
            }
            break;
        case key_value_type_t::U64:
            value.type = render_asset_value_type_t::U64;
            if ( !KeyValue_GetU64( pValue, &value.uValue ) ) {
                return CY_FALSE;
            }
            break;
        case key_value_type_t::F64:
            value.type = render_asset_value_type_t::F64;
            value.nComponents = 1u;
            if ( !KeyValue_GetF64( pValue, &value.values[0] ) ) {
                return CY_FALSE;
            }
            break;
        case key_value_type_t::ARRAY:
            value.type = render_asset_value_type_t::F64_ARRAY;
            value.nComponents = KeyValue_ChildCount( pValue );
            if ( value.nComponents > CY_RENDER_MATERIAL_VALUE_MAX_COMPONENTS ) {
                return CY_FALSE;
            }
            for ( usize iComponent = 0u;
                  iComponent < value.nComponents;
                  ++iComponent ) {
                if ( !ReadNumberAsF64(
                         KeyValue_ChildAt( pValue, iComponent ),
                         value.values[iComponent] ) ) {
                    return CY_FALSE;
                }
            }
            break;
        default:
            return CY_FALSE;
    }
    valueOut = value;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t IsIdentifier( string_view_t value ) noexcept
{
    return DataValidation_Succeeded(
        DataValidation_CheckAsciiIdentifier(
            value,
            CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) );
}

CYPHER_NODISCARD bool_t IsStableIdentifier( string_view_t value ) noexcept
{
    return DataValidation_Succeeded(
        DataValidation_CheckStableIdentifier(
            value,
            CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) );
}

CYPHER_NODISCARD bool_t HasDuplicateMemberName(
    const key_value_t *pObject,
    usize iMember ) noexcept
{
    const string_view_t name = KeyValue_Name(
        KeyValue_ChildAt( pObject, iMember ) );
    for ( usize iPrevious = 0u; iPrevious < iMember; ++iPrevious ) {
        if ( StringView_Equals(
                 name,
                 KeyValue_Name( KeyValue_ChildAt( pObject, iPrevious ) ) ) ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ReadVec2(
    const key_value_t *pValue,
    f64 ( &valuesOut )[2] ) noexcept
{
    if ( pValue == nullptr || KeyValue_Type( pValue ) != key_value_type_t::ARRAY ||
         KeyValue_ChildCount( pValue ) != 2u ) {
        return CY_FALSE;
    }
    return ReadNumberAsF64( KeyValue_ChildAt( pValue, 0u ), valuesOut[0] ) &&
           ReadNumberAsF64( KeyValue_ChildAt( pValue, 1u ), valuesOut[1] );
}

CYPHER_NODISCARD bool_t IsCanonicalPathWithAnyExtension(
    string_view_t path,
    const string_view_t *pExtensions,
    usize nExtensions ) noexcept
{
    // Path normalization is a source-authoring rule; decoders never repair input.
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

CYPHER_NODISCARD bool_t ParseShaderTextureType(
    string_view_t value,
    render_shader_texture_type_t &typeOut ) noexcept
{
    if ( StringView_Equals( value, AssetText( "texture2d" ) ) ) {
        typeOut = render_shader_texture_type_t::TEXTURE_2D;
    } else if ( StringView_Equals( value, AssetText( "texture_cube" ) ) ) {
        typeOut = render_shader_texture_type_t::TEXTURE_CUBE;
    } else if ( StringView_Equals( value, AssetText( "texture2d_array" ) ) ) {
        typeOut = render_shader_texture_type_t::TEXTURE_2D_ARRAY;
    } else if ( StringView_Equals( value, AssetText( "texture3d" ) ) ) {
        typeOut = render_shader_texture_type_t::TEXTURE_3D;
    } else {
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseShaderSamplerType(
    string_view_t value,
    render_shader_sampler_type_t &typeOut ) noexcept
{
    if ( StringView_Equals( value, AssetText( "filtering" ) ) ) {
        typeOut = render_shader_sampler_type_t::FILTERING;
    } else if ( StringView_Equals( value, AssetText( "comparison" ) ) ) {
        typeOut = render_shader_sampler_type_t::COMPARISON;
    } else {
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseShaderParameterType(
    string_view_t value,
    render_shader_parameter_type_t &typeOut ) noexcept
{
    struct parameter_type_name_t {
        string_view_t name;
        render_shader_parameter_type_t type;
    };
    constexpr parameter_type_name_t names[]{
        { AssetText( "bool" ), render_shader_parameter_type_t::BOOL },
        { AssetText( "i32" ), render_shader_parameter_type_t::I32 },
        { AssetText( "u32" ), render_shader_parameter_type_t::U32 },
        { AssetText( "f32" ), render_shader_parameter_type_t::F32 },
        { AssetText( "f32x2" ), render_shader_parameter_type_t::F32X2 },
        { AssetText( "f32x3" ), render_shader_parameter_type_t::F32X3 },
        { AssetText( "f32x4" ), render_shader_parameter_type_t::F32X4 },
        { AssetText( "color3" ), render_shader_parameter_type_t::COLOR3 },
        { AssetText( "color4" ), render_shader_parameter_type_t::COLOR4 },
        { AssetText( "mat3" ), render_shader_parameter_type_t::MAT3 },
        { AssetText( "mat4" ), render_shader_parameter_type_t::MAT4 }
    };
    for ( const parameter_type_name_t &entry : names ) {
        if ( StringView_Equals( value, entry.name ) ) {
            typeOut = entry.type;
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD usize ShaderParameterComponentCount(
    render_shader_parameter_type_t type ) noexcept
{
    switch ( type ) {
        case render_shader_parameter_type_t::BOOL:
        case render_shader_parameter_type_t::I32:
        case render_shader_parameter_type_t::U32:
        case render_shader_parameter_type_t::F32: return 1u;
        case render_shader_parameter_type_t::F32X2: return 2u;
        case render_shader_parameter_type_t::F32X3:
        case render_shader_parameter_type_t::COLOR3: return 3u;
        case render_shader_parameter_type_t::F32X4:
        case render_shader_parameter_type_t::COLOR4: return 4u;
        case render_shader_parameter_type_t::MAT3: return 9u;
        case render_shader_parameter_type_t::MAT4: return 16u;
    }
    return 0u;
}

CYPHER_NODISCARD bool_t NormalizeShaderParameterValue(
    const key_value_t *pValue,
    render_shader_parameter_type_t type,
    render_asset_value_view_t &valueOut ) noexcept
{
    render_asset_value_view_t value{};
    if ( type == render_shader_parameter_type_t::BOOL ) {
        value.type = render_asset_value_type_t::BOOL;
        if ( !KeyValue_GetBool( pValue, &value.bValue ) ) {
            return CY_FALSE;
        }
        valueOut = value;
        return CY_TRUE;
    }
    if ( type == render_shader_parameter_type_t::I32 ) {
        i64 signedValue = 0;
        if ( KeyValue_Type( pValue ) == key_value_type_t::I64 ) {
            if ( !KeyValue_GetI64( pValue, &signedValue ) ) {
                return CY_FALSE;
            }
        } else if ( KeyValue_Type( pValue ) == key_value_type_t::U64 ) {
            u64 unsignedValue = 0u;
            if ( !KeyValue_GetU64( pValue, &unsignedValue ) ||
                 unsignedValue > static_cast<u64>( CY_I32_MAX ) ) {
                return CY_FALSE;
            }
            signedValue = static_cast<i64>( unsignedValue );
        } else {
            return CY_FALSE;
        }
        if ( signedValue < CY_I32_MIN || signedValue > CY_I32_MAX ) {
            return CY_FALSE;
        }
        value.type = render_asset_value_type_t::I64;
        value.iValue = signedValue;
        valueOut = value;
        return CY_TRUE;
    }
    if ( type == render_shader_parameter_type_t::U32 ) {
        u64 unsignedValue = 0u;
        if ( !ReadUnsignedInteger( pValue, unsignedValue ) ||
             unsignedValue > CY_U32_MAX ) {
            return CY_FALSE;
        }
        value.type = render_asset_value_type_t::U64;
        value.uValue = unsignedValue;
        valueOut = value;
        return CY_TRUE;
    }

    const usize nExpectedComponents = ShaderParameterComponentCount( type );
    if ( nExpectedComponents == 1u ) {
        value.type = render_asset_value_type_t::F64;
        value.nComponents = 1u;
        if ( !ReadNumberAsF64( pValue, value.values[0] ) ||
             value.values[0] < -static_cast<f64>( CY_F32_MAX ) ||
             value.values[0] > static_cast<f64>( CY_F32_MAX ) ) {
            return CY_FALSE;
        }
    } else {
        if ( KeyValue_Type( pValue ) != key_value_type_t::ARRAY ||
             KeyValue_ChildCount( pValue ) != nExpectedComponents ) {
            return CY_FALSE;
        }
        value.type = render_asset_value_type_t::F64_ARRAY;
        value.nComponents = nExpectedComponents;
        for ( usize iComponent = 0u;
              iComponent < nExpectedComponents;
              ++iComponent ) {
            if ( !ReadNumberAsF64(
                     KeyValue_ChildAt( pValue, iComponent ),
                     value.values[iComponent] ) ||
                 value.values[iComponent] < -static_cast<f64>( CY_F32_MAX ) ||
                 value.values[iComponent] > static_cast<f64>( CY_F32_MAX ) ) {
                return CY_FALSE;
            }
        }
    }
    valueOut = value;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseTextureAlphaMode(
    string_view_t value,
    render_texture_alpha_mode_t &modeOut ) noexcept
{
    if ( StringView_Equals( value, AssetText( "none" ) ) ) {
        modeOut = render_texture_alpha_mode_t::NONE;
    } else if ( StringView_Equals( value, AssetText( "straight" ) ) ) {
        modeOut = render_texture_alpha_mode_t::STRAIGHT;
    } else if ( StringView_Equals( value, AssetText( "premultiplied" ) ) ) {
        modeOut = render_texture_alpha_mode_t::PREMULTIPLIED;
    } else if ( StringView_Equals( value, AssetText( "mask" ) ) ) {
        modeOut = render_texture_alpha_mode_t::MASK;
    } else if ( StringView_Equals( value, AssetText( "data" ) ) ) {
        modeOut = render_texture_alpha_mode_t::DATA;
    } else {
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseTextureMipMode(
    string_view_t value,
    render_texture_mip_mode_t &modeOut ) noexcept
{
    if ( StringView_Equals( value, AssetText( "generate" ) ) ) {
        modeOut = render_texture_mip_mode_t::GENERATE;
    } else if ( StringView_Equals( value, AssetText( "preserve" ) ) ) {
        modeOut = render_texture_mip_mode_t::PRESERVE;
    } else if ( StringView_Equals( value, AssetText( "none" ) ) ) {
        modeOut = render_texture_mip_mode_t::NONE;
    } else {
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseTextureMipFilter(
    string_view_t value,
    render_texture_mip_filter_t &filterOut ) noexcept
{
    if ( StringView_Equals( value, AssetText( "box" ) ) ) {
        filterOut = render_texture_mip_filter_t::BOX;
    } else if ( StringView_Equals( value, AssetText( "kaiser" ) ) ) {
        filterOut = render_texture_mip_filter_t::KAISER;
    } else if ( StringView_Equals( value, AssetText( "lanczos" ) ) ) {
        filterOut = render_texture_mip_filter_t::LANCZOS;
    } else {
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseTextureEdgeMode(
    string_view_t value,
    render_texture_edge_mode_t &modeOut ) noexcept
{
    if ( StringView_Equals( value, AssetText( "clamp" ) ) ) {
        modeOut = render_texture_edge_mode_t::CLAMP;
    } else if ( StringView_Equals( value, AssetText( "repeat" ) ) ) {
        modeOut = render_texture_edge_mode_t::REPEAT;
    } else if ( StringView_Equals( value, AssetText( "mirror" ) ) ) {
        modeOut = render_texture_edge_mode_t::MIRROR;
    } else {
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseMaterialDomain(
    string_view_t value,
    render_material_domain_t &domainOut ) noexcept
{
    if ( StringView_Equals( value, AssetText( "surface" ) ) ) {
        domainOut = render_material_domain_t::SURFACE;
    } else if ( StringView_Equals( value, AssetText( "decal" ) ) ) {
        domainOut = render_material_domain_t::DECAL;
    } else if ( StringView_Equals( value, AssetText( "ui" ) ) ) {
        domainOut = render_material_domain_t::UI;
    } else if ( StringView_Equals( value, AssetText( "postprocess" ) ) ) {
        domainOut = render_material_domain_t::POSTPROCESS;
    } else if ( StringView_Equals( value, AssetText( "particle" ) ) ) {
        domainOut = render_material_domain_t::PARTICLE;
    } else {
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseMaterialAlphaMode(
    string_view_t value,
    render_material_alpha_mode_t &modeOut ) noexcept
{
    if ( StringView_Equals( value, AssetText( "opaque" ) ) ) {
        modeOut = render_material_alpha_mode_t::OPAQUE;
    } else if ( StringView_Equals( value, AssetText( "mask" ) ) ) {
        modeOut = render_material_alpha_mode_t::MASK;
    } else if ( StringView_Equals( value, AssetText( "blend" ) ) ) {
        modeOut = render_material_alpha_mode_t::BLEND;
    } else if ( StringView_Equals( value, AssetText( "additive" ) ) ) {
        modeOut = render_material_alpha_mode_t::ADDITIVE;
    } else {
        return CY_FALSE;
    }
    return CY_TRUE;
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

    // Structural schema validation precedes resource-specific semantic checks.
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

    // Generic .glsl is accepted alongside stage-specific conventional suffixes.
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

    // Defines preserve authoring order but behave as a unique set.
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

    // Schema handles shape and bounds; this pass handles path and color semantics.
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
        AssetText( ".tga" ),
        AssetText( ".exr" )
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

    // HDR and non-color inputs default to linear and may never be tagged sRGB.
    const bool_t bExrSource = StringPath_HasExtension(
        texture.source,
        AssetText( ".exr" ),
        CY_FALSE );
    if ( !bHasColorSpace &&
         ( texture.usage != render_texture_usage_t::COLOR || bExrSource ) ) {
        texture.colorSpace = render_texture_color_space_t::LINEAR;
    }
    if ( ( texture.usage != render_texture_usage_t::COLOR || bExrSource ) &&
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

    // Decode into a local view so failure never publishes a partially filled result.
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

    // Object member names become shader bindings; values are cooked texture paths.
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

    // CYKV value kinds select the compact material parameter representation.
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

    // Publish only after every borrowed field has passed semantic validation.
    *pMaterialOut = material;
    return result;
}

render_asset_decode_result_t RenderShaderSourceV2_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_shader_source_v2_view_t *pShaderOut ) noexcept
{
    render_asset_decode_result_t result{};
    if ( pDocument == nullptr || pShaderOut == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ) {
        result.status = render_asset_decode_status_t::INVALID_ARGUMENT;
        result.validation.status = schema_validation_status_t::INVALID_ARGUMENT;
        return result;
    }

    result.validation = Schema_ValidateDocument(
        RenderShaderSchema_V2(),
        pDocument,
        options,
        pDiagnostics,
        nDiagnosticCapacity );
    if ( !Schema_ValidationSucceeded( result.validation ) ) {
        result.status = render_asset_decode_status_t::INVALID_DOCUMENT;
        return result;
    }

    const key_value_t *pRoot = KeyValue_Root( pDocument );
    render_shader_source_v2_view_t shader{};
    string_view_t language{};
    if ( !ReadRequiredString( pRoot, AssetText( "language" ), language ) ||
         !StringView_Equals( language, AssetText( "glsl" ) ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }

    const key_value_t *pStages = KeyValue_Find( pRoot, AssetText( "stages" ) );
    const key_value_t *pVertex = KeyValue_Find(
        pStages,
        AssetText( "vertex" ) );
    const key_value_t *pFragment = KeyValue_Find(
        pStages,
        AssetText( "fragment" ) );
    bool_t bHasEntry = CY_FALSE;
    shader.vertex.entry = AssetText( "main" );
    shader.fragment.entry = AssetText( "main" );
    if ( !ReadRequiredString(
             pVertex,
             AssetText( "source" ),
             shader.vertex.source ) ||
         !ReadOptionalString(
             pVertex,
             AssetText( "entry" ),
             shader.vertex.entry,
             bHasEntry ) ||
         !ReadRequiredString(
             pFragment,
             AssetText( "source" ),
             shader.fragment.source ) ||
         !ReadOptionalString(
             pFragment,
             AssetText( "entry" ),
             shader.fragment.entry,
             bHasEntry ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }
    if ( !IsIdentifier( shader.vertex.entry ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_IDENTIFIER,
            AssetText( "stages.vertex.entry" ) );
        return result;
    }
    if ( !IsIdentifier( shader.fragment.entry ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_IDENTIFIER,
            AssetText( "stages.fragment.entry" ) );
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
             shader.vertex.source,
             vertexExtensions,
             sizeof( vertexExtensions ) / sizeof( vertexExtensions[0] ) ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_RESOURCE_PATH,
            AssetText( "stages.vertex.source" ) );
        return result;
    }
    if ( !IsCanonicalPathWithAnyExtension(
             shader.fragment.source,
             fragmentExtensions,
             sizeof( fragmentExtensions ) / sizeof( fragmentExtensions[0] ) ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_RESOURCE_PATH,
            AssetText( "stages.fragment.source" ) );
        return result;
    }

    const key_value_t *pDefines = KeyValue_Find( pRoot, AssetText( "defines" ) );
    if ( pDefines != nullptr ) {
        shader.nDefines = KeyValue_ChildCount( pDefines );
        for ( usize iDefine = 0u; iDefine < shader.nDefines; ++iDefine ) {
            if ( !KeyValue_GetString(
                     KeyValue_ChildAt( pDefines, iDefine ),
                     &shader.defines[iDefine] ) ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            if ( !IsIdentifier( shader.defines[iDefine] ) ) {
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

    const key_value_t *pVariantBudget = KeyValue_Find(
        pRoot,
        AssetText( "variant_budget" ) );
    if ( pVariantBudget != nullptr ) {
        u64 nBudget = 0u;
        if ( !ReadUnsignedInteger( pVariantBudget, nBudget ) ||
             nBudget > CY_RENDER_SHADER_MAX_VARIANT_BUDGET ) {
            result.status = render_asset_decode_status_t::INTERNAL_ERROR;
            return result;
        }
        shader.nVariantBudget = static_cast<u32>( nBudget );
    }

    const key_value_t *pInterface = KeyValue_Find(
        pRoot,
        AssetText( "interface" ) );
    const key_value_t *pTextureMap = pInterface != nullptr
        ? KeyValue_Find( pInterface, AssetText( "textures" ) )
        : nullptr;
    if ( pTextureMap != nullptr ) {
        shader.nTextures = KeyValue_ChildCount( pTextureMap );
        for ( usize iTexture = 0u;
              iTexture < shader.nTextures;
              ++iTexture ) {
            const key_value_t *pSource = KeyValue_ChildAt(
                pTextureMap,
                iTexture );
            render_shader_texture_interface_v2_view_t &texture =
                shader.textures[iTexture];
            texture.name = KeyValue_Name( pSource );
            if ( !IsIdentifier( texture.name ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "interface.textures" ),
                    iTexture );
                return result;
            }
            if ( HasDuplicateMemberName( pTextureMap, iTexture ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::DUPLICATE_VALUE,
                    AssetText( "interface.textures" ),
                    iTexture );
                return result;
            }

            string_view_t typeText{};
            string_view_t usageText{};
            if ( !ReadRequiredString(
                     pSource,
                     AssetText( "type" ),
                     typeText ) ||
                 !ParseShaderTextureType( typeText, texture.type ) ||
                 !ReadRequiredString(
                     pSource,
                     AssetText( "usage" ),
                     usageText ) ||
                 !ParseTextureUsage( usageText, texture.usage ) ||
                 !ReadOptionalBool(
                     pSource,
                     AssetText( "required" ),
                     texture.bRequired ) ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            string_view_t colorSpaceText{};
            bool_t bHasColorSpace = CY_FALSE;
            if ( !ReadOptionalString(
                     pSource,
                     AssetText( "color_space" ),
                     colorSpaceText,
                     bHasColorSpace ) ||
                 ( bHasColorSpace &&
                   !ParseTextureColorSpace(
                       colorSpaceText,
                       texture.colorSpace ) ) ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            if ( !bHasColorSpace &&
                 texture.usage != render_texture_usage_t::COLOR ) {
                texture.colorSpace = render_texture_color_space_t::LINEAR;
            }
            if ( texture.usage != render_texture_usage_t::COLOR &&
                 texture.colorSpace != render_texture_color_space_t::LINEAR ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_COMBINATION,
                    AssetText( "interface.textures.color_space" ),
                    iTexture );
                return result;
            }
        }
    }

    const key_value_t *pSamplerMap = pInterface != nullptr
        ? KeyValue_Find( pInterface, AssetText( "samplers" ) )
        : nullptr;
    if ( pSamplerMap != nullptr ) {
        shader.nSamplers = KeyValue_ChildCount( pSamplerMap );
        for ( usize iSampler = 0u;
              iSampler < shader.nSamplers;
              ++iSampler ) {
            const key_value_t *pSource = KeyValue_ChildAt(
                pSamplerMap,
                iSampler );
            render_shader_sampler_interface_v2_view_t &sampler =
                shader.samplers[iSampler];
            sampler.name = KeyValue_Name( pSource );
            if ( !IsIdentifier( sampler.name ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "interface.samplers" ),
                    iSampler );
                return result;
            }
            if ( HasDuplicateMemberName( pSamplerMap, iSampler ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::DUPLICATE_VALUE,
                    AssetText( "interface.samplers" ),
                    iSampler );
                return result;
            }
            for ( usize iTexture = 0u;
                  iTexture < shader.nTextures;
                  ++iTexture ) {
                if ( StringView_Equals(
                         sampler.name,
                         shader.textures[iTexture].name ) ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::DUPLICATE_VALUE,
                        AssetText( "interface.samplers" ),
                        iSampler );
                    return result;
                }
            }
            string_view_t typeText{};
            if ( !ReadRequiredString(
                     pSource,
                     AssetText( "type" ),
                     typeText ) ||
                 !ParseShaderSamplerType( typeText, sampler.type ) ||
                 !ReadOptionalString(
                     pSource,
                     AssetText( "default" ),
                     sampler.defaultPreset,
                     sampler.bHasDefaultPreset ) ||
                 !ReadOptionalBool(
                     pSource,
                     AssetText( "required" ),
                     sampler.bRequired ) ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            if ( sampler.bHasDefaultPreset &&
                 !IsStableIdentifier( sampler.defaultPreset ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "interface.samplers.default" ),
                    iSampler );
                return result;
            }
        }
    }

    const key_value_t *pParameterMap = pInterface != nullptr
        ? KeyValue_Find( pInterface, AssetText( "parameters" ) )
        : nullptr;
    if ( pParameterMap != nullptr ) {
        shader.nParameters = KeyValue_ChildCount( pParameterMap );
        for ( usize iParameter = 0u;
              iParameter < shader.nParameters;
              ++iParameter ) {
            const key_value_t *pSource = KeyValue_ChildAt(
                pParameterMap,
                iParameter );
            render_shader_parameter_interface_v2_view_t &parameter =
                shader.parameters[iParameter];
            parameter.name = KeyValue_Name( pSource );
            if ( !IsIdentifier( parameter.name ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "interface.parameters" ),
                    iParameter );
                return result;
            }
            if ( HasDuplicateMemberName( pParameterMap, iParameter ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::DUPLICATE_VALUE,
                    AssetText( "interface.parameters" ),
                    iParameter );
                return result;
            }
            for ( usize iTexture = 0u;
                  iTexture < shader.nTextures;
                  ++iTexture ) {
                if ( StringView_Equals(
                         parameter.name,
                         shader.textures[iTexture].name ) ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::DUPLICATE_VALUE,
                        AssetText( "interface.parameters" ),
                        iParameter );
                    return result;
                }
            }
            for ( usize iSampler = 0u;
                  iSampler < shader.nSamplers;
                  ++iSampler ) {
                if ( StringView_Equals(
                         parameter.name,
                         shader.samplers[iSampler].name ) ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::DUPLICATE_VALUE,
                        AssetText( "interface.parameters" ),
                        iParameter );
                    return result;
                }
            }

            string_view_t typeText{};
            if ( !ReadRequiredString(
                     pSource,
                     AssetText( "type" ),
                     typeText ) ||
                 !ParseShaderParameterType( typeText, parameter.type ) ||
                 !ReadOptionalBool(
                     pSource,
                     AssetText( "required" ),
                     parameter.bRequired ) ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            const key_value_t *pDefault = KeyValue_Find(
                pSource,
                AssetText( "default" ) );
            parameter.bHasDefault = pDefault != nullptr;
            if ( pDefault != nullptr &&
                 !NormalizeShaderParameterValue(
                     pDefault,
                     parameter.type,
                     parameter.defaultValue ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_COMBINATION,
                    AssetText( "interface.parameters.default" ),
                    iParameter );
                return result;
            }
            if ( !ReadOptionalNumber(
                     pSource,
                     AssetText( "minimum" ),
                     parameter.minimum,
                     parameter.bHasMinimum ) ||
                 !ReadOptionalNumber(
                     pSource,
                     AssetText( "maximum" ),
                     parameter.maximum,
                     parameter.bHasMaximum ) ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            const bool_t bScalarNumeric =
                parameter.type == render_shader_parameter_type_t::I32 ||
                parameter.type == render_shader_parameter_type_t::U32 ||
                parameter.type == render_shader_parameter_type_t::F32;
            if ( ( parameter.bHasMinimum || parameter.bHasMaximum ) &&
                 !bScalarNumeric ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_COMBINATION,
                    AssetText( "interface.parameters.minimum" ),
                    iParameter );
                return result;
            }
            if ( parameter.bHasMinimum && parameter.bHasMaximum &&
                 parameter.minimum > parameter.maximum ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_COMBINATION,
                    AssetText( "interface.parameters.maximum" ),
                    iParameter );
                return result;
            }
            if ( bScalarNumeric ) {
                const f64 typeMinimum =
                    parameter.type == render_shader_parameter_type_t::I32
                        ? static_cast<f64>( CY_I32_MIN )
                        : parameter.type == render_shader_parameter_type_t::U32
                            ? 0.0
                            : -static_cast<f64>( CY_F32_MAX );
                const f64 typeMaximum =
                    parameter.type == render_shader_parameter_type_t::I32
                        ? static_cast<f64>( CY_I32_MAX )
                        : parameter.type == render_shader_parameter_type_t::U32
                            ? static_cast<f64>( CY_U32_MAX )
                            : static_cast<f64>( CY_F32_MAX );
                if ( ( parameter.bHasMinimum &&
                       ( parameter.minimum < typeMinimum ||
                         parameter.minimum > typeMaximum ) ) ||
                     ( parameter.bHasMaximum &&
                       ( parameter.maximum < typeMinimum ||
                         parameter.maximum > typeMaximum ) ) ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::INVALID_COMBINATION,
                        AssetText( "interface.parameters.minimum" ),
                        iParameter );
                    return result;
                }
                if ( parameter.type != render_shader_parameter_type_t::F32 &&
                     ( ( parameter.bHasMinimum &&
                         static_cast<i64>( parameter.minimum ) !=
                             parameter.minimum ) ||
                       ( parameter.bHasMaximum &&
                         static_cast<i64>( parameter.maximum ) !=
                             parameter.maximum ) ) ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::INVALID_COMBINATION,
                        AssetText( "interface.parameters.minimum" ),
                        iParameter );
                    return result;
                }
                if ( parameter.bHasDefault ) {
                    f64 defaultValue = 0.0;
                    switch ( parameter.defaultValue.type ) {
                        case render_asset_value_type_t::I64:
                            defaultValue = static_cast<f64>(
                                parameter.defaultValue.iValue );
                            break;
                        case render_asset_value_type_t::U64:
                            defaultValue = static_cast<f64>(
                                parameter.defaultValue.uValue );
                            break;
                        case render_asset_value_type_t::F64:
                            defaultValue = parameter.defaultValue.values[0];
                            break;
                        default:
                            result.status =
                                render_asset_decode_status_t::INTERNAL_ERROR;
                            return result;
                    }
                    if ( ( parameter.bHasMinimum &&
                           defaultValue < parameter.minimum ) ||
                         ( parameter.bHasMaximum &&
                           defaultValue > parameter.maximum ) ) {
                        SetSemanticFailure(
                            result,
                            render_asset_decode_status_t::INVALID_COMBINATION,
                            AssetText( "interface.parameters.default" ),
                            iParameter );
                        return result;
                    }
                }
            }
        }
    }

    const key_value_t *pFeatureMap = KeyValue_Find(
        pRoot,
        AssetText( "features" ) );
    u64 nStaticVariants = 1u;
    if ( pFeatureMap != nullptr ) {
        shader.nFeatures = KeyValue_ChildCount( pFeatureMap );
        for ( usize iFeature = 0u;
              iFeature < shader.nFeatures;
              ++iFeature ) {
            const key_value_t *pSource = KeyValue_ChildAt(
                pFeatureMap,
                iFeature );
            render_shader_feature_v2_view_t &feature =
                shader.features[iFeature];
            feature.name = KeyValue_Name( pSource );
            if ( !IsIdentifier( feature.name ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "features" ),
                    iFeature );
                return result;
            }
            if ( HasDuplicateMemberName( pFeatureMap, iFeature ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::DUPLICATE_VALUE,
                    AssetText( "features" ),
                    iFeature );
                return result;
            }

            string_view_t typeText{};
            string_view_t modeText{};
            bool_t bHasMode = CY_FALSE;
            if ( !ReadRequiredString(
                     pSource,
                     AssetText( "type" ),
                     typeText ) ||
                 !ReadOptionalString(
                     pSource,
                     AssetText( "mode" ),
                     modeText,
                     bHasMode ) ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            if ( StringView_Equals( typeText, AssetText( "bool" ) ) ) {
                feature.type = render_shader_feature_type_t::BOOL;
            } else if ( StringView_Equals( typeText, AssetText( "enum" ) ) ) {
                feature.type = render_shader_feature_type_t::ENUM;
            } else {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            if ( bHasMode ) {
                if ( StringView_Equals( modeText, AssetText( "static" ) ) ) {
                    feature.mode = render_shader_feature_mode_t::STATIC;
                } else if ( StringView_Equals(
                                modeText,
                                AssetText( "dynamic" ) ) ) {
                    feature.mode = render_shader_feature_mode_t::DYNAMIC;
                } else {
                    result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                    return result;
                }
            }

            const key_value_t *pDefault = KeyValue_Find(
                pSource,
                AssetText( "default" ) );
            const key_value_t *pValues = KeyValue_Find(
                pSource,
                AssetText( "values" ) );
            u64 nFeatureVariants = 2u;
            if ( feature.type == render_shader_feature_type_t::BOOL ) {
                if ( pValues != nullptr ||
                     ( pDefault != nullptr &&
                       !KeyValue_GetBool( pDefault, &feature.bDefault ) ) ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::INVALID_COMBINATION,
                        AssetText( "features.values" ),
                        iFeature );
                    return result;
                }
            } else {
                if ( pValues == nullptr || pDefault == nullptr ||
                     !KeyValue_GetString( pDefault, &feature.defaultEnum ) ||
                     !IsStableIdentifier( feature.defaultEnum ) ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::INVALID_COMBINATION,
                        AssetText( "features.default" ),
                        iFeature );
                    return result;
                }
                feature.nEnumValues = KeyValue_ChildCount( pValues );
                bool_t bFoundDefault = CY_FALSE;
                for ( usize iValue = 0u;
                      iValue < feature.nEnumValues;
                      ++iValue ) {
                    if ( !KeyValue_GetString(
                             KeyValue_ChildAt( pValues, iValue ),
                             &feature.enumValues[iValue] ) ||
                         !IsStableIdentifier( feature.enumValues[iValue] ) ) {
                        SetSemanticFailure(
                            result,
                            render_asset_decode_status_t::INVALID_IDENTIFIER,
                            AssetText( "features.values" ),
                            iFeature );
                        return result;
                    }
                    for ( usize iPrevious = 0u;
                          iPrevious < iValue;
                          ++iPrevious ) {
                        if ( StringView_Equals(
                                 feature.enumValues[iPrevious],
                                 feature.enumValues[iValue] ) ) {
                            SetSemanticFailure(
                                result,
                                render_asset_decode_status_t::DUPLICATE_VALUE,
                                AssetText( "features.values" ),
                                iFeature );
                            return result;
                        }
                    }
                    bFoundDefault = bFoundDefault || StringView_Equals(
                        feature.defaultEnum,
                        feature.enumValues[iValue] );
                }
                if ( !bFoundDefault ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::INVALID_COMBINATION,
                        AssetText( "features.default" ),
                        iFeature );
                    return result;
                }
                nFeatureVariants = feature.nEnumValues;
            }

            if ( feature.mode == render_shader_feature_mode_t::STATIC ) {
                if ( nFeatureVariants > shader.nVariantBudget ||
                     nStaticVariants >
                         shader.nVariantBudget / nFeatureVariants ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::INVALID_COMBINATION,
                        AssetText( "variant_budget" ),
                        iFeature );
                    return result;
                }
                nStaticVariants *= nFeatureVariants;
            }
        }
    }
    shader.nStaticVariantCount = static_cast<u32>( nStaticVariants );

    *pShaderOut = shader;
    return result;
}

render_asset_decode_result_t RenderTextureSourceV2_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_texture_source_v2_view_t *pTextureOut ) noexcept
{
    render_asset_decode_result_t result{};
    if ( pDocument == nullptr || pTextureOut == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ) {
        result.status = render_asset_decode_status_t::INVALID_ARGUMENT;
        result.validation.status = schema_validation_status_t::INVALID_ARGUMENT;
        return result;
    }

    result.validation = Schema_ValidateDocument(
        RenderTextureSchema_V2(),
        pDocument,
        options,
        pDiagnostics,
        nDiagnosticCapacity );
    if ( !Schema_ValidationSucceeded( result.validation ) ) {
        result.status = render_asset_decode_status_t::INVALID_DOCUMENT;
        return result;
    }

    const key_value_t *pRoot = KeyValue_Root( pDocument );
    render_texture_source_v2_view_t texture{};
    string_view_t typeText{};
    string_view_t usageText{};
    string_view_t colorSpaceText{};
    if ( !ReadRequiredString( pRoot, AssetText( "source" ), texture.source ) ||
         !ReadRequiredString( pRoot, AssetText( "type" ), typeText ) ||
         !ReadRequiredString( pRoot, AssetText( "usage" ), usageText ) ||
         !ReadRequiredString(
             pRoot,
             AssetText( "color_space" ),
             colorSpaceText ) ||
         !StringView_Equals( typeText, AssetText( "2d" ) ) ||
         !ParseTextureUsage( usageText, texture.usage ) ||
         !ParseTextureColorSpace( colorSpaceText, texture.colorSpace ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }

    constexpr string_view_t sourceExtensions[]{
        AssetText( ".png" ),
        AssetText( ".jpg" ),
        AssetText( ".jpeg" ),
        AssetText( ".tga" ),
        AssetText( ".exr" ),
        AssetText( ".dds" ),
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

    const bool_t bExrSource = StringPath_HasExtension(
        texture.source,
        AssetText( ".exr" ),
        CY_FALSE );
    if ( ( texture.usage != render_texture_usage_t::COLOR || bExrSource ) &&
         texture.colorSpace != render_texture_color_space_t::LINEAR ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "color_space" ) );
        return result;
    }

    const key_value_t *pAlpha = KeyValue_Find( pRoot, AssetText( "alpha" ) );
    bool_t bHasAlphaCutoff = CY_FALSE;
    if ( pAlpha != nullptr ) {
        string_view_t modeText{};
        if ( !ReadRequiredString( pAlpha, AssetText( "mode" ), modeText ) ||
             !ParseTextureAlphaMode( modeText, texture.alpha.mode ) ||
             !ReadOptionalNumber(
                 pAlpha,
                 AssetText( "cutoff" ),
                 texture.alpha.cutoff,
                 bHasAlphaCutoff ) ||
             !ReadOptionalBool(
                 pAlpha,
                 AssetText( "dilate_rgb" ),
                 texture.alpha.bDilateRgb ) ) {
            result.status = render_asset_decode_status_t::INTERNAL_ERROR;
            return result;
        }
    }
    if ( bHasAlphaCutoff &&
         texture.alpha.mode != render_texture_alpha_mode_t::MASK ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "alpha.cutoff" ) );
        return result;
    }
    if ( texture.alpha.bDilateRgb &&
         texture.alpha.mode != render_texture_alpha_mode_t::STRAIGHT &&
         texture.alpha.mode != render_texture_alpha_mode_t::MASK ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "alpha.dilate_rgb" ) );
        return result;
    }
    if ( texture.usage != render_texture_usage_t::COLOR &&
         texture.alpha.mode != render_texture_alpha_mode_t::NONE &&
         texture.alpha.mode != render_texture_alpha_mode_t::DATA ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "alpha.mode" ) );
        return result;
    }

    const key_value_t *pMips = KeyValue_Find( pRoot, AssetText( "mips" ) );
    bool_t bHasMipFilter = CY_FALSE;
    bool_t bHasMipEdge = CY_FALSE;
    bool_t bHasCoverageField = CY_FALSE;
    if ( pMips != nullptr ) {
        string_view_t modeText{};
        string_view_t filterText{};
        string_view_t edgeText{};
        if ( !ReadRequiredString( pMips, AssetText( "mode" ), modeText ) ||
             !ParseTextureMipMode( modeText, texture.mips.mode ) ||
             !ReadOptionalString(
                 pMips,
                 AssetText( "filter" ),
                 filterText,
                 bHasMipFilter ) ||
             ( bHasMipFilter &&
               !ParseTextureMipFilter( filterText, texture.mips.filter ) ) ||
             !ReadOptionalString(
                 pMips,
                 AssetText( "edge" ),
                 edgeText,
                 bHasMipEdge ) ||
             ( bHasMipEdge &&
               !ParseTextureEdgeMode( edgeText, texture.mips.edge ) ) ) {
            result.status = render_asset_decode_status_t::INTERNAL_ERROR;
            return result;
        }
        const key_value_t *pCoverage = KeyValue_Find(
            pMips,
            AssetText( "preserve_alpha_coverage" ) );
        bHasCoverageField = pCoverage != nullptr;
        if ( pCoverage != nullptr &&
             !KeyValue_GetBool(
                 pCoverage,
                 &texture.mips.bPreserveAlphaCoverage ) ) {
            result.status = render_asset_decode_status_t::INTERNAL_ERROR;
            return result;
        }
    }
    if ( texture.mips.mode != render_texture_mip_mode_t::GENERATE &&
         ( bHasMipFilter || bHasMipEdge || bHasCoverageField ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "mips.mode" ) );
        return result;
    }
    if ( texture.mips.bPreserveAlphaCoverage &&
         texture.alpha.mode != render_texture_alpha_mode_t::MASK ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "mips.preserve_alpha_coverage" ) );
        return result;
    }
    if ( texture.mips.mode == render_texture_mip_mode_t::PRESERVE &&
         !StringPath_HasExtension(
             texture.source,
             AssetText( ".dds" ),
             CY_FALSE ) &&
         !StringPath_HasExtension(
             texture.source,
             AssetText( ".ktx2" ),
             CY_FALSE ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "mips.mode" ) );
        return result;
    }

    const key_value_t *pOutput = KeyValue_Find( pRoot, AssetText( "output" ) );
    if ( pOutput != nullptr ) {
        string_view_t formatText{};
        string_view_t qualityText{};
        bool_t bHasFormat = CY_FALSE;
        bool_t bHasQuality = CY_FALSE;
        if ( !ReadOptionalString(
                 pOutput,
                 AssetText( "format" ),
                 formatText,
                 bHasFormat ) ||
             !ReadOptionalString(
                 pOutput,
                 AssetText( "quality" ),
                 qualityText,
                 bHasQuality ) ) {
            result.status = render_asset_decode_status_t::INTERNAL_ERROR;
            return result;
        }
        if ( bHasFormat ) {
            if ( StringView_Equals( formatText, AssetText( "auto" ) ) ) {
                texture.output.format = render_texture_output_format_t::AUTO;
            } else if ( StringView_Equals(
                            formatText,
                            AssetText( "uncompressed" ) ) ) {
                texture.output.format =
                    render_texture_output_format_t::UNCOMPRESSED;
            } else {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
        }
        if ( bHasQuality ) {
            if ( StringView_Equals( qualityText, AssetText( "fast" ) ) ) {
                texture.output.quality = render_texture_output_quality_t::FAST;
            } else if ( StringView_Equals(
                            qualityText,
                            AssetText( "balanced" ) ) ) {
                texture.output.quality =
                    render_texture_output_quality_t::BALANCED;
            } else if ( StringView_Equals(
                            qualityText,
                            AssetText( "production" ) ) ) {
                texture.output.quality =
                    render_texture_output_quality_t::PRODUCTION;
            } else {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
        }
    }

    const key_value_t *pStreaming = KeyValue_Find(
        pRoot,
        AssetText( "streaming" ) );
    if ( pStreaming != nullptr ) {
        texture.streaming.bEnabled = CY_TRUE;
        if ( !ReadRequiredString(
                 pStreaming,
                 AssetText( "class" ),
                 texture.streaming.resourceClass ) ||
             !IsStableIdentifier( texture.streaming.resourceClass ) ) {
            SetSemanticFailure(
                result,
                render_asset_decode_status_t::INVALID_IDENTIFIER,
                AssetText( "streaming.class" ) );
            return result;
        }
        const key_value_t *pResidentMips = KeyValue_Find(
            pStreaming,
            AssetText( "resident_mips" ) );
        if ( pResidentMips != nullptr ) {
            u64 nResidentMips = 0u;
            if ( !ReadUnsignedInteger( pResidentMips, nResidentMips ) ||
                 nResidentMips >
                     CY_RENDER_TEXTURE_MAX_RESIDENT_MIP_COUNT ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            texture.streaming.nResidentMipCount = static_cast<usize>(
                nResidentMips );
        }
        if ( texture.mips.mode == render_texture_mip_mode_t::NONE &&
             texture.streaming.nResidentMipCount != 1u ) {
            SetSemanticFailure(
                result,
                render_asset_decode_status_t::INVALID_COMBINATION,
                AssetText( "streaming.resident_mips" ) );
            return result;
        }
    }

    *pTextureOut = texture;
    return result;
}

render_asset_decode_result_t RenderMaterialSourceV2_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_material_source_v2_view_t *pMaterialOut ) noexcept
{
    render_asset_decode_result_t result{};
    if ( pDocument == nullptr || pMaterialOut == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ) {
        result.status = render_asset_decode_status_t::INVALID_ARGUMENT;
        result.validation.status = schema_validation_status_t::INVALID_ARGUMENT;
        return result;
    }

    result.validation = Schema_ValidateDocument(
        RenderMaterialSchema_V2(),
        pDocument,
        options,
        pDiagnostics,
        nDiagnosticCapacity );
    if ( !Schema_ValidationSucceeded( result.validation ) ) {
        result.status = render_asset_decode_status_t::INVALID_DOCUMENT;
        return result;
    }

    const key_value_t *pRoot = KeyValue_Root( pDocument );
    render_material_source_v2_view_t material{};
    if ( !ReadOptionalString(
             pRoot,
             AssetText( "base" ),
             material.base,
             material.bHasBase ) ||
         !ReadOptionalString(
             pRoot,
             AssetText( "shader" ),
             material.shader,
             material.bHasShader ) ||
         !ReadOptionalString(
             pRoot,
             AssetText( "surface" ),
             material.surface,
             material.bHasSurface ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }
    if ( !material.bHasBase && !material.bHasShader ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "shader" ) );
        return result;
    }
    if ( material.bHasBase &&
         !DataValidation_Succeeded(
             DataValidation_CheckResourcePath(
                 material.base,
                 AssetText( ".cymat" ),
                 CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_RESOURCE_PATH,
            AssetText( "base" ) );
        return result;
    }
    if ( material.bHasShader &&
         !DataValidation_Succeeded(
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
    if ( material.bHasSurface &&
         !DataValidation_Succeeded(
             DataValidation_CheckResourcePath(
                 material.surface,
                 AssetText( ".cysurface" ),
                 CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_RESOURCE_PATH,
            AssetText( "surface" ) );
        return result;
    }

    string_view_t domainText{};
    if ( !ReadOptionalString(
             pRoot,
             AssetText( "domain" ),
             domainText,
             material.bHasDomain ) ||
         ( material.bHasDomain &&
           !ParseMaterialDomain( domainText, material.domain ) ) ) {
        result.status = render_asset_decode_status_t::INTERNAL_ERROR;
        return result;
    }
    if ( material.bHasSurface && material.bHasDomain &&
         material.domain != render_material_domain_t::SURFACE &&
         material.domain != render_material_domain_t::DECAL ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "surface" ) );
        return result;
    }

    const key_value_t *pState = KeyValue_Find( pRoot, AssetText( "state" ) );
    if ( pState != nullptr ) {
        string_view_t alphaModeText{};
        if ( !ReadOptionalString(
                 pState,
                 AssetText( "alpha_mode" ),
                 alphaModeText,
                 material.state.bHasAlphaMode ) ||
             ( material.state.bHasAlphaMode &&
               !ParseMaterialAlphaMode(
                   alphaModeText,
                   material.state.alphaMode ) ) ||
             !ReadOptionalNumber(
                 pState,
                 AssetText( "alpha_cutoff" ),
                 material.state.alphaCutoff,
                 material.state.bHasAlphaCutoff ) ) {
            result.status = render_asset_decode_status_t::INTERNAL_ERROR;
            return result;
        }

        const key_value_t *pTwoSided = KeyValue_Find(
            pState,
            AssetText( "two_sided" ) );
        material.state.bHasTwoSided = pTwoSided != nullptr;
        const key_value_t *pCastsShadows = KeyValue_Find(
            pState,
            AssetText( "casts_shadows" ) );
        material.state.bHasCastsShadows = pCastsShadows != nullptr;
        const key_value_t *pReceivesShadows = KeyValue_Find(
            pState,
            AssetText( "receives_shadows" ) );
        material.state.bHasReceivesShadows = pReceivesShadows != nullptr;
        if ( ( pTwoSided != nullptr &&
               !KeyValue_GetBool(
                   pTwoSided,
                   &material.state.bTwoSided ) ) ||
             ( pCastsShadows != nullptr &&
               !KeyValue_GetBool(
                   pCastsShadows,
                   &material.state.bCastsShadows ) ) ||
             ( pReceivesShadows != nullptr &&
               !KeyValue_GetBool(
                   pReceivesShadows,
                   &material.state.bReceivesShadows ) ) ) {
            result.status = render_asset_decode_status_t::INTERNAL_ERROR;
            return result;
        }
    }
    if ( material.state.bHasAlphaCutoff &&
         ( ( material.state.bHasAlphaMode &&
             material.state.alphaMode != render_material_alpha_mode_t::MASK ) ||
           ( !material.state.bHasAlphaMode && !material.bHasBase ) ) ) {
        SetSemanticFailure(
            result,
            render_asset_decode_status_t::INVALID_COMBINATION,
            AssetText( "state.alpha_cutoff" ) );
        return result;
    }

    const key_value_t *pFeatureMap = KeyValue_Find(
        pRoot,
        AssetText( "features" ) );
    if ( pFeatureMap != nullptr ) {
        material.nFeatures = KeyValue_ChildCount( pFeatureMap );
        for ( usize iFeature = 0u;
              iFeature < material.nFeatures;
              ++iFeature ) {
            const key_value_t *pValue = KeyValue_ChildAt(
                pFeatureMap,
                iFeature );
            render_material_feature_v2_view_t &feature =
                material.features[iFeature];
            feature.name = KeyValue_Name( pValue );
            if ( !IsIdentifier( feature.name ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "features" ),
                    iFeature );
                return result;
            }
            if ( HasDuplicateMemberName( pFeatureMap, iFeature ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::DUPLICATE_VALUE,
                    AssetText( "features" ),
                    iFeature );
                return result;
            }
            if ( KeyValue_Type( pValue ) == key_value_type_t::NULL_VALUE ) {
                if ( !material.bHasBase ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::INVALID_COMBINATION,
                        AssetText( "features" ),
                        iFeature );
                    return result;
                }
                feature.bRemove = CY_TRUE;
            } else if ( KeyValue_Type( pValue ) == key_value_type_t::BOOL ) {
                feature.type = render_material_feature_value_type_t::BOOL;
                if ( !KeyValue_GetBool( pValue, &feature.bValue ) ) {
                    result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                    return result;
                }
            } else {
                feature.type = render_material_feature_value_type_t::ENUM;
                if ( !KeyValue_GetString( pValue, &feature.enumValue ) ||
                     !IsStableIdentifier( feature.enumValue ) ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::INVALID_IDENTIFIER,
                        AssetText( "features" ),
                        iFeature );
                    return result;
                }
            }
        }
    }

    const key_value_t *pTextureMap = KeyValue_Find(
        pRoot,
        AssetText( "textures" ) );
    if ( pTextureMap != nullptr ) {
        material.nTextures = KeyValue_ChildCount( pTextureMap );
        for ( usize iTexture = 0u;
              iTexture < material.nTextures;
              ++iTexture ) {
            const key_value_t *pSource = KeyValue_ChildAt(
                pTextureMap,
                iTexture );
            render_material_texture_v2_view_t &texture =
                material.textures[iTexture];
            texture.binding = KeyValue_Name( pSource );
            if ( !IsIdentifier( texture.binding ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "textures" ),
                    iTexture );
                return result;
            }
            if ( HasDuplicateMemberName( pTextureMap, iTexture ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::DUPLICATE_VALUE,
                    AssetText( "textures" ),
                    iTexture );
                return result;
            }
            if ( KeyValue_Type( pSource ) == key_value_type_t::NULL_VALUE ) {
                if ( !material.bHasBase ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::INVALID_COMBINATION,
                        AssetText( "textures" ),
                        iTexture );
                    return result;
                }
                texture.bRemove = CY_TRUE;
                continue;
            }

            if ( !ReadOptionalString(
                     pSource,
                     AssetText( "resource" ),
                     texture.resource,
                     texture.bHasResource ) ||
                 !ReadOptionalString(
                     pSource,
                     AssetText( "sampler" ),
                     texture.sampler,
                     texture.bHasSampler ) ) {
                result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                return result;
            }
            if ( !material.bHasBase && !texture.bHasResource ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_COMBINATION,
                    AssetText( "textures.resource" ),
                    iTexture );
                return result;
            }
            if ( texture.bHasResource &&
                 !DataValidation_Succeeded(
                     DataValidation_CheckResourcePath(
                         texture.resource,
                         AssetText( ".cytex" ),
                         CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_RESOURCE_PATH,
                    AssetText( "textures.resource" ),
                    iTexture );
                return result;
            }
            if ( texture.bHasSampler &&
                 !IsStableIdentifier( texture.sampler ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "textures.sampler" ),
                    iTexture );
                return result;
            }

            const key_value_t *pUv = KeyValue_Find(
                pSource,
                AssetText( "uv" ) );
            texture.uv.bPresent = pUv != nullptr;
            if ( pUv != nullptr ) {
                const key_value_t *pSet = KeyValue_Find(
                    pUv,
                    AssetText( "set" ) );
                if ( pSet != nullptr ) {
                    u64 nSet = 0u;
                    if ( !ReadUnsignedInteger( pSet, nSet ) ||
                         nSet > CY_RENDER_MATERIAL_MAX_UV_SET ) {
                        result.status =
                            render_asset_decode_status_t::INTERNAL_ERROR;
                        return result;
                    }
                    texture.uv.nSet = static_cast<u32>( nSet );
                }
                const key_value_t *pScale = KeyValue_Find(
                    pUv,
                    AssetText( "scale" ) );
                const key_value_t *pOffset = KeyValue_Find(
                    pUv,
                    AssetText( "offset" ) );
                if ( ( pScale != nullptr &&
                       !ReadVec2( pScale, texture.uv.scale ) ) ||
                     ( pOffset != nullptr &&
                       !ReadVec2( pOffset, texture.uv.offset ) ) ) {
                    result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                    return result;
                }
                bool_t bHasRotation = CY_FALSE;
                if ( !ReadOptionalNumber(
                         pUv,
                         AssetText( "rotation" ),
                         texture.uv.rotation,
                         bHasRotation ) ) {
                    result.status = render_asset_decode_status_t::INTERNAL_ERROR;
                    return result;
                }
            }
        }
    }

    const key_value_t *pParameterMap = KeyValue_Find(
        pRoot,
        AssetText( "parameters" ) );
    if ( pParameterMap != nullptr ) {
        material.nParameters = KeyValue_ChildCount( pParameterMap );
        for ( usize iParameter = 0u;
              iParameter < material.nParameters;
              ++iParameter ) {
            const key_value_t *pValue = KeyValue_ChildAt(
                pParameterMap,
                iParameter );
            render_material_parameter_v2_view_t &parameter =
                material.parameters[iParameter];
            parameter.name = KeyValue_Name( pValue );
            if ( !IsIdentifier( parameter.name ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::INVALID_IDENTIFIER,
                    AssetText( "parameters" ),
                    iParameter );
                return result;
            }
            if ( HasDuplicateMemberName( pParameterMap, iParameter ) ) {
                SetSemanticFailure(
                    result,
                    render_asset_decode_status_t::DUPLICATE_VALUE,
                    AssetText( "parameters" ),
                    iParameter );
                return result;
            }
            if ( KeyValue_Type( pValue ) == key_value_type_t::NULL_VALUE ) {
                if ( !material.bHasBase ) {
                    SetSemanticFailure(
                        result,
                        render_asset_decode_status_t::INVALID_COMBINATION,
                        AssetText( "parameters" ),
                        iParameter );
                    return result;
                }
                parameter.bRemove = CY_TRUE;
            } else if ( !ReadAssetValue( pValue, parameter.value ) ) {
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
