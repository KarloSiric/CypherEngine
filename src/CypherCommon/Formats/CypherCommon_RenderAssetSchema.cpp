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
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_OBJECT;
    rule.object.pAdditionalMemberRule = pMemberRule;
    rule.object.nMinMembers = 1u;
    rule.object.nMaxMembers = nMaxMembers;
    return rule;
}

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

} // namespace cypher::common
