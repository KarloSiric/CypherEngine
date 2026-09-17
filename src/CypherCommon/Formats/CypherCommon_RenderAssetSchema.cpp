//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp
//  Purpose: Implements CYKV schemas for renderer-facing source assets.
//  Details: Structural rules remain declarative. Canonical resource paths,
//           extensions, identifiers, and duplicate define policy are enforced by
//           the matching typed decoders after schema validation succeeds.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_RenderAssetSchema.h"

namespace cypher::common
{

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t SchemaText(
    const char ( &text )[nExtent] ) noexcept
{
    // Schema literals are static borrowed views without their trailing NUL.
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

CYPHER_NODISCARD constexpr schema_rule_t BoolRule() noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_BOOL;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t NumberRule() noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_NUMBER;
    // Material integers are decoded into f64. Keep their accepted range exactly
    // representable so authoring does not silently lose integer precision.
    constexpr i64 nExactIntegerLimit = 9007199254740992ll;
    rule.signedInteger.nMin = -nExactIntegerLimit;
    rule.signedInteger.nMax = nExactIntegerLimit;
    rule.unsignedInteger.nMax = static_cast<u64>( nExactIntegerLimit );
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t NumberRangeRule(
    f64 minimum,
    f64 maximum ) noexcept
{
    schema_rule_t rule = NumberRule();
    rule.signedInteger.nMin = static_cast<i64>( minimum );
    rule.signedInteger.nMax = static_cast<i64>( maximum );
    rule.unsignedInteger.nMin = minimum > 0.0
        ? static_cast<u64>( minimum )
        : 0u;
    rule.unsignedInteger.nMax = static_cast<u64>( maximum );
    rule.floatingPoint.flMin = minimum;
    rule.floatingPoint.flMax = maximum;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t UnsignedRangeRule(
    u64 minimum,
    u64 maximum ) noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_I64 | SCHEMA_TYPE_U64;
    rule.signedInteger.nMin = static_cast<i64>( minimum );
    rule.signedInteger.nMax = static_cast<i64>( maximum );
    rule.unsignedInteger.nMin = minimum;
    rule.unsignedInteger.nMax = maximum;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t StringRule(
    usize cbMinLength,
    usize cbMaxLength ) noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_STRING;
    rule.string.cbMinLength = cbMinLength;
    rule.string.cbMaxLength = cbMaxLength;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t StringValuesRule(
    const string_view_t *pAllowedValues,
    usize nAllowedValues ) noexcept
{
    schema_rule_t rule = StringRule( 1u, 32u );
    rule.string.pAllowedValues = pAllowedValues;
    rule.string.nAllowedValues = nAllowedValues;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t ArrayRule(
    const schema_rule_t *pElementRule,
    usize nMinElements,
    usize nMaxElements ) noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_ARRAY;
    rule.array.pElementRule = pElementRule;
    rule.array.nMinElements = nMinElements;
    rule.array.nMaxElements = nMaxElements;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t ClosedObjectRule(
    const schema_member_t *pMembers,
    usize nMembers ) noexcept
{
    // Authored resource schemas reject misspelled or unsupported fields.
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_OBJECT;
    rule.object.pMembers = pMembers;
    rule.object.nMembers = nMembers;
    rule.object.flags = SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t DynamicObjectRule(
    const schema_rule_t *pMemberRule,
    usize nMaxMembers ) noexcept
{
    // Dynamic object member names become shader binding or parameter identifiers.
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_OBJECT;
    rule.object.pAdditionalMemberRule = pMemberRule;
    rule.object.nMinMembers = 1u;
    rule.object.nMaxMembers = nMaxMembers;
    return rule;
}

// Shared leaf rules feed three immutable process-lifetime schema graphs.
inline constexpr string_view_t g_shaderLanguages[]{
    SchemaText( "glsl" )
};
inline constexpr string_view_t g_textureUsages[]{
    SchemaText( "color" ),
    SchemaText( "normal" ),
    SchemaText( "data" )
};
inline constexpr string_view_t g_textureColorSpaces[]{
    SchemaText( "srgb" ),
    SchemaText( "linear" )
};

inline constexpr schema_rule_t g_pathRule = StringRule(
    1u,
    CY_RENDER_ASSET_PATH_MAX_LENGTH );
inline constexpr schema_rule_t g_identifierRule = StringRule(
    1u,
    CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH );
inline constexpr schema_rule_t g_boolRule = BoolRule();
inline constexpr schema_rule_t g_numberRule = NumberRule();

// cypher.shader: one GLSL graphics program plus an optional define set.
inline constexpr schema_rule_t g_shaderLanguageRule = StringValuesRule(
    g_shaderLanguages,
    sizeof( g_shaderLanguages ) / sizeof( g_shaderLanguages[0] ) );
inline constexpr schema_rule_t g_shaderDefinesRule = ArrayRule(
    &g_identifierRule,
    1u,
    CY_RENDER_SHADER_MAX_DEFINES );
inline constexpr schema_member_t g_shaderMembers[]{
    { SchemaText( "language" ), &g_shaderLanguageRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "vertex" ), &g_pathRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "fragment" ), &g_pathRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "defines" ), &g_shaderDefinesRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_shaderRootRule = ClosedObjectRule(
    g_shaderMembers,
    sizeof( g_shaderMembers ) / sizeof( g_shaderMembers[0] ) );
inline constexpr schema_descriptor_t g_shaderSchema{
    SchemaText( "cypher.shader" ),
    CY_RENDER_ASSET_SCHEMA_VERSION,
    &g_shaderRootRule
};

// cypher.texture: one imported image with explicit usage and color policy.
inline constexpr schema_rule_t g_textureUsageRule = StringValuesRule(
    g_textureUsages,
    sizeof( g_textureUsages ) / sizeof( g_textureUsages[0] ) );
inline constexpr schema_rule_t g_textureColorSpaceRule = StringValuesRule(
    g_textureColorSpaces,
    sizeof( g_textureColorSpaces ) / sizeof( g_textureColorSpaces[0] ) );
inline constexpr schema_member_t g_textureMembers[]{
    { SchemaText( "source" ), &g_pathRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "usage" ), &g_textureUsageRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "color_space" ), &g_textureColorSpaceRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "generate_mips" ), &g_boolRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_textureRootRule = ClosedObjectRule(
    g_textureMembers,
    sizeof( g_textureMembers ) / sizeof( g_textureMembers[0] ) );
inline constexpr schema_descriptor_t g_textureSchema{
    SchemaText( "cypher.texture" ),
    CY_RENDER_ASSET_SCHEMA_VERSION,
    &g_textureRootRule
};

// cypher.material: dynamic binding maps validated semantically by the decoder.
inline constexpr schema_rule_t g_materialTextureMapRule = DynamicObjectRule(
    &g_pathRule,
    CY_RENDER_MATERIAL_MAX_TEXTURES );
inline constexpr schema_rule_t g_materialParameterRule{
    SCHEMA_TYPE_BOOL | SCHEMA_TYPE_NUMBER | SCHEMA_TYPE_ARRAY,
    {},
    { &g_numberRule,
      CY_RENDER_MATERIAL_VECTOR_MIN_COMPONENTS,
      CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS }
};
inline constexpr schema_rule_t g_materialParameterMapRule = DynamicObjectRule(
    &g_materialParameterRule,
    CY_RENDER_MATERIAL_MAX_PARAMETERS );
inline constexpr schema_member_t g_materialMembers[]{
    { SchemaText( "shader" ), &g_pathRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "textures" ), &g_materialTextureMapRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "parameters" ), &g_materialParameterMapRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_materialRootRule = ClosedObjectRule(
    g_materialMembers,
    sizeof( g_materialMembers ) / sizeof( g_materialMembers[0] ) );
inline constexpr schema_descriptor_t g_materialSchema{
    SchemaText( "cypher.material" ),
    CY_RENDER_ASSET_SCHEMA_VERSION,
    &g_materialRootRule
};

// cypher.shader V2: stage objects, a typed public interface, and bounded
// material-selectable features. Physical backend bindings are intentionally not
// authorable here; those are derived by compilation and reflection.
inline constexpr string_view_t g_shaderTextureTypesV2[]{
    SchemaText( "texture2d" ),
    SchemaText( "texture_cube" ),
    SchemaText( "texture2d_array" ),
    SchemaText( "texture3d" )
};
inline constexpr string_view_t g_shaderSamplerTypesV2[]{
    SchemaText( "filtering" ),
    SchemaText( "comparison" )
};
inline constexpr string_view_t g_shaderParameterTypesV2[]{
    SchemaText( "bool" ),
    SchemaText( "i32" ),
    SchemaText( "u32" ),
    SchemaText( "f32" ),
    SchemaText( "f32x2" ),
    SchemaText( "f32x3" ),
    SchemaText( "f32x4" ),
    SchemaText( "color3" ),
    SchemaText( "color4" ),
    SchemaText( "mat3" ),
    SchemaText( "mat4" )
};
inline constexpr string_view_t g_shaderFeatureTypesV2[]{
    SchemaText( "bool" ),
    SchemaText( "enum" )
};
inline constexpr string_view_t g_shaderFeatureModesV2[]{
    SchemaText( "static" ),
    SchemaText( "dynamic" )
};
inline constexpr schema_rule_t g_shaderTextureTypeRuleV2 = StringValuesRule(
    g_shaderTextureTypesV2,
    sizeof( g_shaderTextureTypesV2 ) / sizeof( g_shaderTextureTypesV2[0] ) );
inline constexpr schema_rule_t g_shaderSamplerTypeRuleV2 = StringValuesRule(
    g_shaderSamplerTypesV2,
    sizeof( g_shaderSamplerTypesV2 ) / sizeof( g_shaderSamplerTypesV2[0] ) );
inline constexpr schema_rule_t g_shaderParameterTypeRuleV2 = StringValuesRule(
    g_shaderParameterTypesV2,
    sizeof( g_shaderParameterTypesV2 ) /
        sizeof( g_shaderParameterTypesV2[0] ) );
inline constexpr schema_rule_t g_shaderFeatureTypeRuleV2 = StringValuesRule(
    g_shaderFeatureTypesV2,
    sizeof( g_shaderFeatureTypesV2 ) / sizeof( g_shaderFeatureTypesV2[0] ) );
inline constexpr schema_rule_t g_shaderFeatureModeRuleV2 = StringValuesRule(
    g_shaderFeatureModesV2,
    sizeof( g_shaderFeatureModesV2 ) / sizeof( g_shaderFeatureModesV2[0] ) );
inline constexpr schema_member_t g_shaderStageMembersV2[]{
    { SchemaText( "source" ), &g_pathRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "entry" ), &g_identifierRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_shaderStageRuleV2 = ClosedObjectRule(
    g_shaderStageMembersV2,
    sizeof( g_shaderStageMembersV2 ) / sizeof( g_shaderStageMembersV2[0] ) );
inline constexpr schema_member_t g_shaderStagesMembersV2[]{
    { SchemaText( "vertex" ), &g_shaderStageRuleV2, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "fragment" ), &g_shaderStageRuleV2, SCHEMA_MEMBER_REQUIRED }
};
inline constexpr schema_rule_t g_shaderStagesRuleV2 = ClosedObjectRule(
    g_shaderStagesMembersV2,
    sizeof( g_shaderStagesMembersV2 ) / sizeof( g_shaderStagesMembersV2[0] ) );

inline constexpr schema_member_t g_shaderTextureMembersV2[]{
    { SchemaText( "type" ), &g_shaderTextureTypeRuleV2, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "usage" ), &g_textureUsageRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "color_space" ), &g_textureColorSpaceRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "required" ), &g_boolRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_shaderTextureRuleV2 = ClosedObjectRule(
    g_shaderTextureMembersV2,
    sizeof( g_shaderTextureMembersV2 ) /
        sizeof( g_shaderTextureMembersV2[0] ) );
inline constexpr schema_rule_t g_shaderTextureMapRuleV2 = DynamicObjectRule(
    &g_shaderTextureRuleV2,
    CY_RENDER_SHADER_MAX_INTERFACE_TEXTURES );

inline constexpr schema_member_t g_shaderSamplerMembersV2[]{
    { SchemaText( "type" ), &g_shaderSamplerTypeRuleV2, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "default" ), &g_identifierRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "required" ), &g_boolRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_shaderSamplerRuleV2 = ClosedObjectRule(
    g_shaderSamplerMembersV2,
    sizeof( g_shaderSamplerMembersV2 ) /
        sizeof( g_shaderSamplerMembersV2[0] ) );
inline constexpr schema_rule_t g_shaderSamplerMapRuleV2 = DynamicObjectRule(
    &g_shaderSamplerRuleV2,
    CY_RENDER_SHADER_MAX_INTERFACE_SAMPLERS );

inline constexpr schema_rule_t g_shaderParameterDefaultRuleV2{
    SCHEMA_TYPE_BOOL | SCHEMA_TYPE_NUMBER | SCHEMA_TYPE_ARRAY,
    {},
    { &g_numberRule,
      CY_RENDER_MATERIAL_VECTOR_MIN_COMPONENTS,
      CY_RENDER_MATERIAL_VALUE_MAX_COMPONENTS }
};
inline constexpr schema_member_t g_shaderParameterMembersV2[]{
    { SchemaText( "type" ), &g_shaderParameterTypeRuleV2, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "default" ), &g_shaderParameterDefaultRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "minimum" ), &g_numberRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "maximum" ), &g_numberRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "required" ), &g_boolRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_shaderParameterRuleV2 = ClosedObjectRule(
    g_shaderParameterMembersV2,
    sizeof( g_shaderParameterMembersV2 ) /
        sizeof( g_shaderParameterMembersV2[0] ) );
inline constexpr schema_rule_t g_shaderParameterMapRuleV2 = DynamicObjectRule(
    &g_shaderParameterRuleV2,
    CY_RENDER_SHADER_MAX_INTERFACE_PARAMETERS );

inline constexpr schema_member_t g_shaderInterfaceMembersV2[]{
    { SchemaText( "textures" ), &g_shaderTextureMapRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "samplers" ), &g_shaderSamplerMapRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "parameters" ), &g_shaderParameterMapRuleV2, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_shaderInterfaceRuleV2{
    SCHEMA_TYPE_OBJECT,
    { g_shaderInterfaceMembersV2,
      sizeof( g_shaderInterfaceMembersV2 ) /
          sizeof( g_shaderInterfaceMembersV2[0] ),
      nullptr,
      1u,
      3u,
      SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS }
};

inline constexpr schema_rule_t g_shaderFeatureValuesRuleV2 = ArrayRule(
    &g_identifierRule,
    2u,
    CY_RENDER_SHADER_MAX_ENUM_VALUES );
inline constexpr schema_rule_t g_shaderFeatureDefaultRuleV2{
    SCHEMA_TYPE_BOOL | SCHEMA_TYPE_STRING,
    {},
    {},
    { 1u, CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH }
};
inline constexpr schema_member_t g_shaderFeatureMembersV2[]{
    { SchemaText( "type" ), &g_shaderFeatureTypeRuleV2, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "mode" ), &g_shaderFeatureModeRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "default" ), &g_shaderFeatureDefaultRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "values" ), &g_shaderFeatureValuesRuleV2, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_shaderFeatureRuleV2 = ClosedObjectRule(
    g_shaderFeatureMembersV2,
    sizeof( g_shaderFeatureMembersV2 ) /
        sizeof( g_shaderFeatureMembersV2[0] ) );
inline constexpr schema_rule_t g_shaderFeatureMapRuleV2 = DynamicObjectRule(
    &g_shaderFeatureRuleV2,
    CY_RENDER_SHADER_MAX_FEATURES );
inline constexpr schema_rule_t g_shaderVariantBudgetRuleV2 = UnsignedRangeRule(
    1u,
    CY_RENDER_SHADER_MAX_VARIANT_BUDGET );
inline constexpr schema_member_t g_shaderMembersV2[]{
    { SchemaText( "language" ), &g_shaderLanguageRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "stages" ), &g_shaderStagesRuleV2, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "defines" ), &g_shaderDefinesRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "interface" ), &g_shaderInterfaceRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "features" ), &g_shaderFeatureMapRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "variant_budget" ), &g_shaderVariantBudgetRuleV2, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_shaderRootRuleV2 = ClosedObjectRule(
    g_shaderMembersV2,
    sizeof( g_shaderMembersV2 ) / sizeof( g_shaderMembersV2[0] ) );
inline constexpr schema_descriptor_t g_shaderSchemaV2{
    SchemaText( "cypher.shader" ),
    CY_RENDER_ASSET_SCHEMA_VERSION_V2,
    &g_shaderRootRuleV2
};

// cypher.texture V2: a single-source image recipe with semantic alpha, mip,
// output, and residency policies. More complex source graphs remain a later schema.
inline constexpr string_view_t g_textureTypesV2[]{ SchemaText( "2d" ) };
inline constexpr string_view_t g_textureAlphaModesV2[]{
    SchemaText( "none" ),
    SchemaText( "straight" ),
    SchemaText( "premultiplied" ),
    SchemaText( "mask" ),
    SchemaText( "data" )
};
inline constexpr string_view_t g_textureMipModesV2[]{
    SchemaText( "generate" ),
    SchemaText( "preserve" ),
    SchemaText( "none" )
};
inline constexpr string_view_t g_textureMipFiltersV2[]{
    SchemaText( "box" ),
    SchemaText( "kaiser" ),
    SchemaText( "lanczos" )
};
inline constexpr string_view_t g_textureEdgeModesV2[]{
    SchemaText( "clamp" ),
    SchemaText( "repeat" ),
    SchemaText( "mirror" )
};
inline constexpr string_view_t g_textureOutputFormatsV2[]{
    SchemaText( "auto" ),
    SchemaText( "uncompressed" )
};
inline constexpr string_view_t g_textureOutputQualitiesV2[]{
    SchemaText( "fast" ),
    SchemaText( "balanced" ),
    SchemaText( "production" )
};
inline constexpr schema_rule_t g_textureTypeRuleV2 = StringValuesRule(
    g_textureTypesV2,
    sizeof( g_textureTypesV2 ) / sizeof( g_textureTypesV2[0] ) );
inline constexpr schema_rule_t g_textureAlphaModeRuleV2 = StringValuesRule(
    g_textureAlphaModesV2,
    sizeof( g_textureAlphaModesV2 ) / sizeof( g_textureAlphaModesV2[0] ) );
inline constexpr schema_rule_t g_textureMipModeRuleV2 = StringValuesRule(
    g_textureMipModesV2,
    sizeof( g_textureMipModesV2 ) / sizeof( g_textureMipModesV2[0] ) );
inline constexpr schema_rule_t g_textureMipFilterRuleV2 = StringValuesRule(
    g_textureMipFiltersV2,
    sizeof( g_textureMipFiltersV2 ) / sizeof( g_textureMipFiltersV2[0] ) );
inline constexpr schema_rule_t g_textureEdgeModeRuleV2 = StringValuesRule(
    g_textureEdgeModesV2,
    sizeof( g_textureEdgeModesV2 ) / sizeof( g_textureEdgeModesV2[0] ) );
inline constexpr schema_rule_t g_textureOutputFormatRuleV2 = StringValuesRule(
    g_textureOutputFormatsV2,
    sizeof( g_textureOutputFormatsV2 ) /
        sizeof( g_textureOutputFormatsV2[0] ) );
inline constexpr schema_rule_t g_textureOutputQualityRuleV2 = StringValuesRule(
    g_textureOutputQualitiesV2,
    sizeof( g_textureOutputQualitiesV2 ) /
        sizeof( g_textureOutputQualitiesV2[0] ) );
inline constexpr schema_rule_t g_unitNumberRuleV2 = NumberRangeRule( 0.0, 1.0 );
inline constexpr schema_member_t g_textureAlphaMembersV2[]{
    { SchemaText( "mode" ), &g_textureAlphaModeRuleV2, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "cutoff" ), &g_unitNumberRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "dilate_rgb" ), &g_boolRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_textureAlphaRuleV2 = ClosedObjectRule(
    g_textureAlphaMembersV2,
    sizeof( g_textureAlphaMembersV2 ) / sizeof( g_textureAlphaMembersV2[0] ) );
inline constexpr schema_member_t g_textureMipMembersV2[]{
    { SchemaText( "mode" ), &g_textureMipModeRuleV2, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "filter" ), &g_textureMipFilterRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "edge" ), &g_textureEdgeModeRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "preserve_alpha_coverage" ), &g_boolRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_textureMipRuleV2 = ClosedObjectRule(
    g_textureMipMembersV2,
    sizeof( g_textureMipMembersV2 ) / sizeof( g_textureMipMembersV2[0] ) );
inline constexpr schema_member_t g_textureOutputMembersV2[]{
    { SchemaText( "format" ), &g_textureOutputFormatRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "quality" ), &g_textureOutputQualityRuleV2, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_textureOutputRuleV2{
    SCHEMA_TYPE_OBJECT,
    { g_textureOutputMembersV2,
      sizeof( g_textureOutputMembersV2 ) /
          sizeof( g_textureOutputMembersV2[0] ),
      nullptr,
      1u,
      2u,
      SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS }
};
inline constexpr schema_rule_t g_textureResidentMipRuleV2 = UnsignedRangeRule(
    1u,
    CY_RENDER_TEXTURE_MAX_RESIDENT_MIP_COUNT );
inline constexpr schema_member_t g_textureStreamingMembersV2[]{
    { SchemaText( "class" ), &g_identifierRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "resident_mips" ), &g_textureResidentMipRuleV2, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_textureStreamingRuleV2 = ClosedObjectRule(
    g_textureStreamingMembersV2,
    sizeof( g_textureStreamingMembersV2 ) /
        sizeof( g_textureStreamingMembersV2[0] ) );
inline constexpr schema_member_t g_textureMembersV2[]{
    { SchemaText( "source" ), &g_pathRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "type" ), &g_textureTypeRuleV2, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "usage" ), &g_textureUsageRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "color_space" ), &g_textureColorSpaceRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "alpha" ), &g_textureAlphaRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "mips" ), &g_textureMipRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "output" ), &g_textureOutputRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "streaming" ), &g_textureStreamingRuleV2, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_textureRootRuleV2 = ClosedObjectRule(
    g_textureMembersV2,
    sizeof( g_textureMembersV2 ) / sizeof( g_textureMembersV2[0] ) );
inline constexpr schema_descriptor_t g_textureSchemaV2{
    SchemaText( "cypher.texture" ),
    CY_RENDER_ASSET_SCHEMA_VERSION_V2,
    &g_textureRootRuleV2
};

// cypher.material V2: partial overrides are legal when a base material is named.
// Null dynamic-map values explicitly remove inherited entries before resolution.
inline constexpr string_view_t g_materialDomainsV2[]{
    SchemaText( "surface" ),
    SchemaText( "decal" ),
    SchemaText( "ui" ),
    SchemaText( "postprocess" ),
    SchemaText( "particle" )
};
inline constexpr string_view_t g_materialAlphaModesV2[]{
    SchemaText( "opaque" ),
    SchemaText( "mask" ),
    SchemaText( "blend" ),
    SchemaText( "additive" )
};
inline constexpr schema_rule_t g_materialDomainRuleV2 = StringValuesRule(
    g_materialDomainsV2,
    sizeof( g_materialDomainsV2 ) / sizeof( g_materialDomainsV2[0] ) );
inline constexpr schema_rule_t g_materialAlphaModeRuleV2 = StringValuesRule(
    g_materialAlphaModesV2,
    sizeof( g_materialAlphaModesV2 ) /
        sizeof( g_materialAlphaModesV2[0] ) );
inline constexpr schema_rule_t g_materialFeatureValueRuleV2{
    SCHEMA_TYPE_NULL | SCHEMA_TYPE_BOOL | SCHEMA_TYPE_STRING,
    {},
    {},
    { 1u, CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH }
};
inline constexpr schema_rule_t g_materialFeatureMapRuleV2 = DynamicObjectRule(
    &g_materialFeatureValueRuleV2,
    CY_RENDER_MATERIAL_MAX_FEATURES );
inline constexpr schema_member_t g_materialStateMembersV2[]{
    { SchemaText( "alpha_mode" ), &g_materialAlphaModeRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "alpha_cutoff" ), &g_unitNumberRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "two_sided" ), &g_boolRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "casts_shadows" ), &g_boolRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "receives_shadows" ), &g_boolRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_materialStateRuleV2{
    SCHEMA_TYPE_OBJECT,
    { g_materialStateMembersV2,
      sizeof( g_materialStateMembersV2 ) /
          sizeof( g_materialStateMembersV2[0] ),
      nullptr,
      1u,
      5u,
      SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS }
};
inline constexpr schema_rule_t g_materialUvSetRuleV2 = UnsignedRangeRule(
    0u,
    CY_RENDER_MATERIAL_MAX_UV_SET );
inline constexpr schema_rule_t g_vec2RuleV2 = ArrayRule( &g_numberRule, 2u, 2u );
inline constexpr schema_member_t g_materialUvMembersV2[]{
    { SchemaText( "set" ), &g_materialUvSetRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "scale" ), &g_vec2RuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "offset" ), &g_vec2RuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "rotation" ), &g_numberRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_materialUvRuleV2{
    SCHEMA_TYPE_OBJECT,
    { g_materialUvMembersV2,
      sizeof( g_materialUvMembersV2 ) / sizeof( g_materialUvMembersV2[0] ),
      nullptr,
      1u,
      4u,
      SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS }
};
inline constexpr schema_member_t g_materialTextureMembersV2[]{
    { SchemaText( "resource" ), &g_pathRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "sampler" ), &g_identifierRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "uv" ), &g_materialUvRuleV2, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_materialTextureObjectRuleV2{
    SCHEMA_TYPE_OBJECT | SCHEMA_TYPE_NULL,
    { g_materialTextureMembersV2,
      sizeof( g_materialTextureMembersV2 ) /
          sizeof( g_materialTextureMembersV2[0] ),
      nullptr,
      1u,
      3u,
      SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS }
};
inline constexpr schema_rule_t g_materialTextureMapRuleV2 = DynamicObjectRule(
    &g_materialTextureObjectRuleV2,
    CY_RENDER_MATERIAL_MAX_TEXTURES );
inline constexpr schema_rule_t g_materialParameterValueRuleV2{
    SCHEMA_TYPE_NULL | SCHEMA_TYPE_BOOL | SCHEMA_TYPE_NUMBER | SCHEMA_TYPE_ARRAY,
    {},
    { &g_numberRule,
      CY_RENDER_MATERIAL_VECTOR_MIN_COMPONENTS,
      CY_RENDER_MATERIAL_VALUE_MAX_COMPONENTS }
};
inline constexpr schema_rule_t g_materialParameterMapRuleV2 = DynamicObjectRule(
    &g_materialParameterValueRuleV2,
    CY_RENDER_MATERIAL_MAX_PARAMETERS );
inline constexpr schema_member_t g_materialMembersV2[]{
    { SchemaText( "base" ), &g_pathRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "shader" ), &g_pathRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "domain" ), &g_materialDomainRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "features" ), &g_materialFeatureMapRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "state" ), &g_materialStateRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "surface" ), &g_pathRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "textures" ), &g_materialTextureMapRuleV2, SCHEMA_MEMBER_NONE },
    { SchemaText( "parameters" ), &g_materialParameterMapRuleV2, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_materialRootRuleV2 = ClosedObjectRule(
    g_materialMembersV2,
    sizeof( g_materialMembersV2 ) / sizeof( g_materialMembersV2[0] ) );
inline constexpr schema_descriptor_t g_materialSchemaV2{
    SchemaText( "cypher.material" ),
    CY_RENDER_ASSET_SCHEMA_VERSION_V2,
    &g_materialRootRuleV2
};

} // namespace

const schema_descriptor_t *RenderShaderSchema_V1() noexcept
{
    return &g_shaderSchema;
}

const schema_descriptor_t *RenderTextureSchema_V1() noexcept
{
    return &g_textureSchema;
}

const schema_descriptor_t *RenderMaterialSchema_V1() noexcept
{
    return &g_materialSchema;
}

const schema_descriptor_t *RenderShaderSchema_V2() noexcept
{
    return &g_shaderSchemaV2;
}

const schema_descriptor_t *RenderTextureSchema_V2() noexcept
{
    return &g_textureSchemaV2;
}

const schema_descriptor_t *RenderMaterialSchema_V2() noexcept
{
    return &g_materialSchemaV2;
}

} // namespace cypher::common
